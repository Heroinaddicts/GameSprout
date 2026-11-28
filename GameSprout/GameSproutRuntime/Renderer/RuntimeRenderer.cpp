#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Components/MeshComponent.h"
#include "RuntimeMath.h"
#include "RuntimeFrustum.h"
#include "RuntimeEnvironment.h"
#include "../../Tools/FrameTiming.h"
#include <exception>
#include <algorithm>

namespace {
struct CommandScope {
    GRHI::IDevice& Device;
    GRHI::ICommandBuffer* Command=nullptr;
    GRHI::IRenderPass* Pass=nullptr;
    bool AcquiredFrame=false;
    ~CommandScope() {
        if(!Command) return;
        try {
            if(Pass) Command->EndRenderPass(Pass);
            if(AcquiredFrame) Device.Submit(Command);else Device.Cancel(Command);
        } catch(...) {}
    }
};
struct RenderingScope {
    bool& Value;
    explicit RenderingScope(bool& value):Value(value) {Value=true;}
    ~RenderingScope() {Value=false;}
};
struct DebugBufferScope {
    std::vector<GRHI::IBuffer*> Buffers;
    ~DebugBufferScope() {for(auto* buffer:Buffers) delete buffer;}
};
}
namespace GameSproutRuntime {
bool Renderer::Render(const RenderView& view) {
    GAMESPROUT_FRAME_SCOPE(Runtime, "render/view");
    _HasRenderedCamera=false;_RenderedCamera={};_LinearCaptureReady=false;
    if(_Rendering || _Runtime.IsExecuting() || _Runtime.IsShuttingDown() || _Runtime.IsConstructing()) return Fail("Runtime Renderer cannot run inside a Runtime callback, construction or another Render");
    _Error.clear();_Statistics={};_ShadowRecorded=false;_SpotShadowRecorded=false;_PlanarParameters={};
    if(!_Surface&&!_ColorTarget){_Statistics={};return true;}
    if(view.CaptureLinearColor&&(_Surface||!view.DrawScene))return Fail("Linear capture requires an offscreen 3D render");
    if(!IsValidStaticFlags(view.RequiredStaticFlags))return Fail("RenderView mesh static flag filter is invalid");
    if(_Surface&&_ColorTarget)return Fail("Viewport has conflicting render targets");
    if(view.DrawScene||view.DrawUI) {
        auto* scene=_Runtime.Find(view.Scene);
        if(!_Runtime.IsUsable(scene) || scene->SceneOwner!=view.Scene) return Fail("RenderView scene is not a live scene owned by this Runtime");
        if(view.DrawScene&&(!RuntimeMath::IsFinite(view.Camera.View) || !RuntimeMath::IsFinite(view.Camera.Position)))
            return Fail("RenderView camera is nonfinite");
    }
    if(!RuntimeMath::IsFinite(view.ClearColor)) return Fail("RenderView clear color is nonfinite");
    RenderingScope rendering(_Rendering);
    RuntimeOperation operation(_Runtime);
    try {
        if(view.DrawScene||view.DrawUI) SyncRevision();
        if(view.DrawScene&&!Initialize()) return false;
        // 缓冲在Command提交/取消后释放；异常退出也保持这一顺序。
        DebugBufferScope debugBuffers;
        CommandScope command{_Device};command.Command=_Device.AcquireCommandBuffer();
        if(!command.Command) return Fail(DeviceError("Acquire Runtime render command"));
        GRHI::ColorTargetView target;
        GRHI::TextureFormat format=GRHI::TextureFormat::Unknown;
        std::uint32_t width=0,height=0;
        if(_Surface) {
            GRHI::SurfaceFrame frame;
            const auto result=view.WaitForSurface ? _Surface->AcquireFrame(command.Command,frame)
                : _Surface->TryAcquireFrame(command.Command,frame);
            if(result==GRHI::AcquireFrameResult::Unavailable) {
                const bool cancelled=_Device.Cancel(command.Command);
                if(cancelled) command.Command=nullptr;
                _Statistics={};
                return cancelled?true:Fail(DeviceError("Cancel unavailable Runtime view"));
            }
            if(result!=GRHI::AcquireFrameResult::Ready) return Fail(DeviceError("Acquire Runtime surface frame"));
            command.AcquiredFrame=true;target=frame.ColorTarget;width=frame.Width;height=frame.Height;format=frame.Format;
        } else {
            target=_ColorTarget->GetColorTargetView();width=_ColorTarget->GetWidth();height=_ColorTarget->GetHeight();format=_ColorTarget->GetFormat();
        }
        if(!width || !height || !target.Token) return Fail("Runtime render target has no valid color view or pixel extent");
        ++_Frame;
        if(!_InstanceFrameSubmitted)InvalidateInstanceUploads();
        _InstanceFrameSubmitted=false;
        Mat4 viewProjection;
        GRHI::ITexture* depth=nullptr;
        if(view.DrawScene) {
            Mat4 projection;
            const float aspect=float(width)/float(height);
            bool valid=false;
            if(view.Camera.Projection.Projection==CameraProjection::Perspective) valid=TryMakePerspective(view.Camera.Projection.VerticalFovRadians,aspect,
                view.Camera.Projection.NearPlane,view.Camera.Projection.FarPlane,projection);
            else if(view.Camera.Projection.Projection==CameraProjection::Orthographic) valid=TryMakeOrthographic(view.Camera.Projection.OrthographicHeight,aspect,
                view.Camera.Projection.NearPlane,view.Camera.Projection.FarPlane,projection);
            viewProjection=projection*view.Camera.View;
            if(!valid || !RuntimeMath::IsFinite(viewProjection)) return Fail("Invalid explicit RenderView camera projection");
            depth=DepthTarget(width,height);if(!depth) return false;
        }
        std::vector<DebugBatch> debugBatches;
        if(view.DrawScene && !PrepareDebug(view.DebugDraw,debugBatches)) return false;
        if(view.DrawScene) {
            GAMESPROUT_FRAME_SCOPE(Runtime, "render/extract_scene");
            if(!Extract(view,_SceneObjects))return false;
        }
        auto& lights=_SceneLights;
        if(view.DrawScene&&!ExtractLights(view.Scene,lights))return false;
        _ShaderFrame={};
        _ShaderFrame.Time=_Runtime.GetShaderTime();
        _ShaderFrame.CameraPosition={view.Camera.Position.X,view.Camera.Position.Y,view.Camera.Position.Z,1};
        RenderEnvironment::Uniform environment{};
        const auto* settings=view.DrawScene?&view.Scene->GetEnvironmentLighting():nullptr;
        const auto* sky=settings?Environment(settings->SkyEnvironment):nullptr;
        const EnvironmentGPU* reflections=nullptr;
        _Weather.Enabled=false;
        if(settings) {
            if(!RenderEnvironment::Prepare(*settings,environment,sky?&sky->DiffuseSH:nullptr,sky?sky->SpecularMaxLOD:0))
                Diagnostic("Scene environment radiance is invalid or exceeds finite shader precision; environment lighting disabled for this frame");
            else if(sky&&settings->ReflectionIntensity>0)reflections=sky;
            _Runtime.GetWeatherState(*view.Scene,_Weather);
            if(_Weather.Enabled)PrepareWeatherEnvironment(environment);
        }
        if(view.DrawScene) {
            GAMESPROUT_FRAME_SCOPE(Runtime, "render/prepare_scene");
            _IndirectLightingAtlas=nullptr;
            if(view.EnableBakedIndirectLighting)PrepareIndirectLighting(*settings,environment);
            BeginTexturePrefetchFrame(view.EnableTexturePrefetch);
            {
                GAMESPROUT_FRAME_SCOPE(Runtime, "render/prepare_draws");
                PrepareDraws(view,viewProjection);
            }
            PrepareLocalLights();
            PrepareReflectionProbes(view,*settings);
            {
                GAMESPROUT_FRAME_SCOPE(Runtime, "render/prepare_instances");
                if(!PrepareColorInstances(*command.Command,view))return false;
            }
            {
                GAMESPROUT_FRAME_SCOPE(Runtime, "render/record_shadows");
                if(!RenderShadows(*command.Command,view,float(width)/height))return false;
                if(!RenderSpotShadows(*command.Command,view,viewProjection))return false;
            }
            {
                GAMESPROUT_FRAME_SCOPE(Runtime, "render/record_planar");
                if(!RenderPlanarReflection(*command.Command,view,width,height,environment,sky,reflections))return false;
            }
            EndTexturePrefetchFrame();
            for(auto* buffer:{_InstanceTransformsBuffer,_SingleInstanceIndicesBuffer,_SkinPaletteOffsetsBuffer,_SkinPaletteBuffer,
                _ColorInstances.Buffer,_StaticShadowInstances.Buffer,_DynamicShadowInstances.Buffer,_SpotShadowInstances.Buffer})
                if(buffer)_Statistics.ResidentInstanceBytes+=buffer->GetSize();
        }
        if(view.DrawUI&&!PrepareUI(*view.Scene,width,height,view.UIPreviewObjectID,view.UIScreenPreviewLayoutSize))return false;
        GRHI::RenderPassDesc pass;
        const bool hdr=settings&&(settings->HDREnabled||view.CaptureLinearColor);
        auto sceneFormat=format;
        pass.ColorTarget=target;
        if(hdr) {
            auto* hdrTarget=HDRTarget(width,height);
            if(!hdrTarget)return false;
            sceneFormat=hdrTarget->GetFormat();pass.ColorTarget=hdrTarget->GetColorTargetView();
            if(!debugBatches.empty())pass.DepthStore=GRHI::StoreOp::Store;
        } else if(settings)_HDRTarget.reset();
        pass.ClearColor={view.ClearColor.R,view.ClearColor.G,view.ClearColor.B,view.ClearColor.A};
        if(depth){pass.DepthTarget=depth->GetDepthTargetView();pass.DepthLoad=GRHI::LoadOp::Clear;pass.ClearDepth=1.0f;}
        command.Pass=command.Command->BeginRenderPass(pass);
        if(!command.Pass) return Fail(DeviceError("Begin Runtime depth/color pass"));
        _Statistics.Width=width;_Statistics.Height=height;
        if(view.DrawScene&&!DrawSky(*command.Pass,view,sceneFormat,float(width)/float(height),lights,sky))return false;
        if(view.DrawScene && view.DrawGrid && !DrawGrid(*command.Pass,viewProjection,sceneFormat)) return false;
        if(view.DrawScene) {
            GAMESPROUT_FRAME_SCOPE(Runtime, "render/record_scene_draws");
            if(!DrawPrepared(*command.Pass,_PreparedDraws,view,viewProjection,sceneFormat,lights,environment,reflections))return false;
            if(!DrawWeatherLightning(*command.Pass,view,viewProjection,sceneFormat))return false;
        }
        const auto depthFormat=depth?depth->GetFormat():GRHI::TextureFormat::Unknown;
        if(hdr) {
            command.Command->EndRenderPass(command.Pass);command.Pass=nullptr;
            GRHI::RenderPassDesc display;
            display.ColorTarget=target;display.Load=GRHI::LoadOp::DontCare;
            display.DepthTarget=pass.DepthTarget;
            display.DepthLoad=debugBatches.empty()?GRHI::LoadOp::DontCare:GRHI::LoadOp::Load;
            command.Pass=command.Command->BeginRenderPass(display);
            if(!command.Pass)return Fail(DeviceError("Begin Runtime HDR resolve pass"));
            if(!ResolveHDR(*command.Pass,format,depthFormat,*settings))return false;
        }
        if(view.DrawScene && !DrawDebug(*command.Pass,viewProjection,format,debugBatches,debugBuffers.Buffers)) return false;
        if(view.DrawUI&&!DrawUI(*command.Pass,format,depthFormat)) return false;
        if(view.UIScreenDebugDraw&&!DrawUIScreenDebug(*command.Pass,format,depthFormat,{float(width),float(height)},
            *view.UIScreenDebugDraw,debugBuffers.Buffers)) return false;
        command.Command->EndRenderPass(command.Pass);command.Pass=nullptr;
        auto* submitted=command.Command;command.Command=nullptr;
        if(!_Device.Submit(submitted)) return Fail(DeviceError("Submit Runtime view"));
        GAMESPROUT_FRAME_COUNT(Runtime, "render/width_px", width);
        GAMESPROUT_FRAME_COUNT(Runtime, "render/height_px", height);
        GAMESPROUT_FRAME_COUNT(Runtime, "render/draw_calls", _Statistics.DrawCalls);
        GAMESPROUT_FRAME_COUNT(Runtime, "render/triangles", _Statistics.Triangles);
        GAMESPROUT_FRAME_COUNT(Runtime, "render/skin_upload_bytes", _Statistics.InstanceSkinPaletteUploadBytes);
        GAMESPROUT_FRAME_COUNT(Runtime, "render/transform_upload_bytes", _Statistics.InstanceTransformUploadBytes);
        GAMESPROUT_FRAME_COUNT(Runtime, "render/planar_draw_calls", _Statistics.PlanarReflectionDrawCalls);
        _InstanceFrameSubmitted=true;
        if(_PlanarCapture&&_PlanarParameters.X>0)_PlanarCapture->_InstanceFrameSubmitted=true;
        _LinearCaptureReady=view.CaptureLinearColor;
        // A recorded atlas is reusable only after the whole command is submitted.
        // A later color-pass error/cancel must not cache an unexecuted depth pass.
        if(_ShadowRecorded)_ShadowValid=true;
        if(_SpotShadowRecorded)_SpotShadowValid=true;
        if(view.DrawScene){_RenderedCamera={view.Camera,viewProjection,width,height,view.Scene->GetInstanceID()};_HasRenderedCamera=true;}
        return true;
    } catch(const std::exception& exception) {return Fail(std::string("Runtime Renderer: ")+exception.what());}
    catch(...) {return Fail("Unexpected Runtime Renderer failure");}
}
bool Renderer::GetRenderedCamera(RenderedCamera& camera) const {
    camera=_HasRenderedCamera?_RenderedCamera:RenderedCamera{};return _HasRenderedCamera;
}
}
