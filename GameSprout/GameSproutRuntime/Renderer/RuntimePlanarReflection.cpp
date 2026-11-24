#include "RuntimeRenderer.h"
#include "RuntimeRenderResourceCache.h"
#include "RuntimeMath.h"
#include "RuntimeEnvironment.h"
#include <algorithm>
#include <cmath>

namespace GameSproutRuntime {
namespace {
bool MakeReflectedView(const RenderView& source,const PlanarReflectionSettings& settings,float aspect,
        RenderView& reflected,Mat4& viewProjection) {
    constexpr float clipBias=.001f;
    if(!(source.Camera.Position.Y>settings.Height+clipBias))return false;
    reflected=source;
    auto& camera=reflected.Camera;
    camera.Position.Y=settings.Height*2-camera.Position.Y;
    Mat4 mirror,flipX;
    mirror(1,1)=-1;mirror(1,3)=2*settings.Height;
    // A second flip preserves the original front-face winding. The shader
    // undoes this horizontal image flip when sampling the reflected camera.
    flipX(0,0)=-1;
    camera.View=flipX*camera.View*mirror;
    camera.Projection.CullingMask&=settings.LayerMask;
    camera.Projection.FarPlane=std::min(settings.MaxDistance,camera.Projection.FarPlane);
    const auto& lens=camera.Projection;
    const bool perspective=lens.Projection==CameraProjection::Perspective;
    Mat4 projection,viewToWorld;
    if(!RuntimeMath::TryInverseAffine(camera.View,viewToWorld)||
       !(perspective?TryMakePerspective(lens.VerticalFovRadians,aspect,lens.NearPlane,lens.FarPlane,projection):
         TryMakeOrthographic(lens.OrthographicHeight,aspect,lens.NearPlane,lens.FarPlane,projection)))return false;

    // View-space plane with positive half-space above water. Replace the
    // canonical [0,1] projection's near plane (z=0) with that plane. Scaling
    // by the far clip corner preserves valid depth for the opposite corner.
    const Vector4 plane{viewToWorld(1,0),viewToWorld(1,1),viewToWorld(1,2),
        viewToWorld(1,3)-settings.Height-clipBias};
    const auto sign=[](float component){return component>=0?1.0f:-1.0f;};
    const double x=sign(plane.X)/projection(0,0),y=sign(plane.Y)/projection(1,1);
    const double z=perspective?-1.0:lens.FarPlane*-1.0;
    const double w=perspective?1.0/lens.FarPlane:1.0;
    const double scale=plane.X*x+plane.Y*y+plane.Z*z+plane.W*w;
    if(!std::isfinite(scale)||scale<=1e-6)return false;
    projection(2,0)=float(plane.X/scale);
    projection(2,1)=float(plane.Y/scale);
    projection(2,2)=float(plane.Z/scale);
    projection(2,3)=float(plane.W/scale);
    viewProjection=projection*camera.View;
    reflected.EnablePlanarReflections=false;
    return RuntimeMath::IsFinite(camera.Position)&&RuntimeMath::IsFinite(viewProjection);
}

struct CapturePassScope {
    GRHI::ICommandBuffer& Command;
    GRHI::IRenderPass* Pass;
    ~CapturePassScope(){if(Pass)Command.EndRenderPass(Pass);}
};
}

bool Renderer::IsPlanarReceiver(const PreparedDraw& draw,const PlanarReflectionSettings& settings,Vector3 eye) const {
    if(draw.Shader||draw.Material->ShadingModel!=MaterialShadingModel::Lit||!draw.Material->PlanarReflection)return false;
    const auto bounds=LocalLightObjectBounds(*draw.Object,*draw.Mesh);
    if(!bounds.Valid)return false;
    const double camera[]={eye.X,eye.Y,eye.Z};
    double distanceSquared=0;
    for(std::size_t i=0;i<3;++i) {
        const double extent=std::abs(bounds.Axis[0][i])+std::abs(bounds.Axis[1][i])+std::abs(bounds.Axis[2][i]);
        if(i==1&&std::abs(settings.Height-bounds.Center[i])>extent+.05)return false;
        const double gap=std::max(0.0,std::abs(camera[i]-bounds.Center[i])-extent);
        distanceSquared+=gap*gap;
    }
    return distanceSquared<=double(settings.MaxDistance)*settings.MaxDistance;
}

bool Renderer::UsesPlanarReflection(const PreparedDraw& draw) const {
    return _PlanarParameters.X>0&&draw.Material->PlanarReflection;
}

void Renderer::BindPlanarReflection(GRHI::IRenderPass& pass,const PreparedDraw& draw,LitUniform& uniform) {
    const bool enabled=UsesPlanarReflection(draw);
    if(enabled)uniform.PlanarReflection=_PlanarParameters;
    pass.SetFragmentTextureSampler(7,enabled?_PlanarColor.get():_White,_DefaultSampler);
}

bool Renderer::RenderPlanarReflection(GRHI::ICommandBuffer& command,const RenderView& view,
        std::uint32_t width,std::uint32_t height,const RenderEnvironment::Uniform& environment,
        const EnvironmentGPU* sky,const EnvironmentGPU* reflections) {
    const auto& settings=view.Scene->GetEnvironmentLighting();
    const auto& plane=settings.PlanarReflection;
    if(!view.EnablePlanarReflections||!plane.Enabled||plane.Strength==0||plane.LayerMask==0) {
        _PlanarCapture.reset();
        _PlanarColor.reset();
        return true;
    }
    if(!std::any_of(_PreparedDraws.begin(),_PreparedDraws.end(),[&](const PreparedDraw& draw) {
        return IsPlanarReceiver(draw,plane,view.Camera.Position);
    }))return true;

    const float aspect=float(width)/float(height);
    RenderView reflected;
    Mat4 reflectedViewProjection;
    if(!MakeReflectedView(view,plane,aspect,reflected,reflectedViewProjection))return true;

    const auto scale=std::min(double(plane.ResolutionScale),double(plane.MaxResolution)/std::max(width,height));
    const auto desiredWidth=std::max(1u,static_cast<std::uint32_t>(std::lround(width*scale)));
    const auto desiredHeight=std::max(1u,static_cast<std::uint32_t>(std::lround(height*scale)));
    constexpr auto format=GRHI::TextureFormat::RGBA16_Float;
    if(!_PlanarColor||_PlanarColor->GetWidth()!=desiredWidth||_PlanarColor->GetHeight()!=desiredHeight) {
        GRHI::TextureDesc description;
        description.Width=desiredWidth;description.Height=desiredHeight;
        description.Format=format;description.Usage=GRHI::TextureUsage::ColorTarget|GRHI::TextureUsage::Sampled;
        auto* color=_Device.CreateTexture(description);
        if(!color) {
            Diagnostic(DeviceError("Allocate planar reflection color target"));
            return true;
        }
        _PlanarColor.reset(color);
    }
    if(!_PlanarCapture) {
        _PlanarCapture=std::make_unique<Renderer>(_Runtime,_Device,_Resources);
        _PlanarCapture->_PlanarShadowSource=this;
    }
    auto& capture=*_PlanarCapture;
    ++capture._Frame;
    capture._Error.clear();capture._Statistics={};
    // A recorded buffer update belongs to this same command. Use the parent's
    // submission history before trusting updates from a previous mirrored view.
    if(!capture._InstanceFrameSubmitted)capture.InvalidateInstanceUploads();
    capture._InstanceFrameSubmitted=false;
    capture.SyncRevision();
    if(capture._TexturePrefetchEnabled!=_TexturePrefetchEnabled||
        capture._SeenTexturePrefetchRevision!=_SeenTexturePrefetchRevision)capture._PreparedKnown=false;
    capture._TexturePrefetchEnabled=_TexturePrefetchEnabled;
    capture._SeenTexturePrefetchRevision=_SeenTexturePrefetchRevision;

    auto* depth=capture.DepthTarget(desiredWidth,desiredHeight);
    if(!depth)return Fail("Planar reflection: "+capture._Error);
    if(!capture.Extract(reflected,capture._SceneObjects)||
       !capture.ExtractLights(reflected.Scene,capture._SceneLights))return Fail("Planar reflection: "+capture._Error);
    capture._ShaderFrame=_ShaderFrame;
    capture._ShaderFrame.CameraPosition={reflected.Camera.Position.X,reflected.Camera.Position.Y,reflected.Camera.Position.Z,1};
    capture.PrepareDraws(reflected,reflectedViewProjection);
    capture.PrepareLocalLights();
    capture.PrepareReflectionProbes(reflected,settings);
    if(!capture.PrepareColorInstances(command,reflected))return Fail("Planar reflection: "+capture._Error);

    GRHI::RenderPassDesc pass;
    pass.ColorTarget=_PlanarColor->GetColorTargetView();
    pass.ClearColor={view.ClearColor.R,view.ClearColor.G,view.ClearColor.B,view.ClearColor.A};
    pass.DepthTarget=depth->GetDepthTargetView();pass.DepthLoad=GRHI::LoadOp::Clear;pass.ClearDepth=1;
    CapturePassScope scope{command,command.BeginRenderPass(pass)};
    if(!scope.Pass)return Fail(DeviceError("Begin planar reflection pass"));
    if(!capture.DrawSky(*scope.Pass,reflected,format,aspect,capture._SceneLights,sky)||
       !capture.DrawPrepared(*scope.Pass,capture._PreparedDraws,reflected,reflectedViewProjection,format,
            capture._SceneLights,environment,reflections))return Fail("Planar reflection: "+capture._Error);
    _Statistics.PlanarReflectionDrawCalls=capture._Statistics.DrawCalls;
    _Statistics.PlanarReflectionWidth=desiredWidth;_Statistics.PlanarReflectionHeight=desiredHeight;
    _Statistics.RequiresContinuousRendering|=capture._Statistics.RequiresContinuousRendering;
    _PlanarParameters={plane.Strength,1.0f/width,1.0f/height,plane.Height};
    return true;
}
}
