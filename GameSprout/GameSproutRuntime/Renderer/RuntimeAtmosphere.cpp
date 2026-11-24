#include "RuntimeRenderer.h"
#include "RuntimeRenderResourceCache.h"
#include "RuntimeEnvironment.h"
#include "RuntimeMath.h"
#include <cmath>

namespace GameSproutRuntime {
GRHI::IGraphicsPipeline* Renderer::FullscreenPipeline(PreviewShader vertex,PreviewShader fragment,
    GRHI::TextureFormat color,GRHI::TextureFormat depth) {
    auto& pipelines=_ResourceCache.FullscreenPipelines;
    const auto key=std::make_tuple(fragment,color,depth);
    const auto found=pipelines.find(key);
    if(found!=pipelines.end())return found->second;
    for(const auto kind:{vertex,fragment}) {
        auto*& shader=_Shaders[static_cast<unsigned>(kind)];
        if(!shader)shader=CreatePreviewShader(_Device,kind);
        if(!shader) {
            Fail(DeviceError("Create Runtime full-screen shader"));
            return nullptr;
        }
    }
    GRHI::GraphicsPipelineDesc description;
    description.VertexShader=_Shaders[static_cast<unsigned>(vertex)];
    description.FragmentShader=_Shaders[static_cast<unsigned>(fragment)];
    description.ColorFormat=color;description.DepthFormat=depth;
    description.Cull=GRHI::CullMode::None;description.DepthTest=false;description.DepthWrite=false;
    auto pipeline=std::unique_ptr<GRHI::IGraphicsPipeline>(_Device.CreateGraphicsPipeline(description));
    if(!pipeline) {
        Fail(DeviceError("Create Runtime full-screen pipeline"));
        return nullptr;
    }
    pipelines.emplace(key,pipeline.get());
    return pipeline.release();
}

GRHI::ISampler* Renderer::EnvironmentSampler() {
    auto& sampler=_ResourceCache.EnvironmentSampler;
    if(!sampler) {
        GRHI::SamplerDesc description;
        description.MipFilter=GRHI::FilterMode::Linear;
        sampler.reset(_Device.CreateSampler(description));
    }
    return sampler.get();
}

const Renderer::EnvironmentGPU* Renderer::Environment(const ResourceReference& reference) {
    if(reference.Guid.empty()||!reference.LocalID)return nullptr;
    auto [found,inserted]=_ResourceCache.Environments.try_emplace(ResourceKey(reference));
    auto& environment=found->second;
    if(inserted) {
        environment.ResourceRevision=_Resources.GetResourceRevision(reference);
        EnvironmentRenderData data;
        if(_Resources.LoadEnvironment(reference,data,environment.Error)) {
            if(data.Sky.Dimension!=TextureDimension::Cube||data.Specular.Dimension!=TextureDimension::Cube||!data.Specular.MipLevels) {
                environment.Error="Prepared environment requires two cubemaps and at least one reflection mip";
            } else if(!EnvironmentSampler()) {
                environment.Error=DeviceError("Create environment sampler");
            } else {
                auto sky=std::unique_ptr<GRHI::ITexture>(UploadPreparedTexture(data.Sky,environment.Error));
                if(sky) {
                    auto specular=std::unique_ptr<GRHI::ITexture>(UploadPreparedTexture(data.Specular,environment.Error));
                    if(specular) {
                        environment.Sky=std::move(sky);
                        environment.Specular=std::move(specular);
                        environment.DiffuseSH=data.DiffuseSH;
                        environment.SpecularMaxLOD=float(data.Specular.MipLevels-1);
                    }
                }
            }
        }
    }
    if(environment.Sky)return &environment;
    Diagnostic("Environment "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+
        (environment.Error.empty()?"Prepared asset is unavailable":environment.Error));
    return nullptr;
}

GRHI::ITexture* Renderer::HDRTarget(std::uint32_t width,std::uint32_t height) {
    if(_HDRTarget&&_HDRTarget->GetWidth()==width&&_HDRTarget->GetHeight()==height)return _HDRTarget.get();
    GRHI::TextureDesc description;
    description.Width=width;description.Height=height;
    description.Format=GRHI::TextureFormat::RGBA16_Float;
    description.Usage=GRHI::TextureUsage::ColorTarget|GRHI::TextureUsage::Sampled;
    auto* target=_Device.CreateTexture(description);
    if(!target) {
        Fail(DeviceError("Allocate Runtime HDR scene color"));
        return nullptr;
    }
    _HDRTarget.reset(target);
    return target;
}

bool Renderer::ResolveHDR(GRHI::IRenderPass& pass,GRHI::TextureFormat color,GRHI::TextureFormat depth,
    const EnvironmentLightingSettings& settings) {
    auto* pipeline=FullscreenPipeline(PreviewShader::HDRResolveVertex,PreviewShader::HDRResolveFragment,color,depth);
    if(!pipeline)return false;
    const Vector4 parameters{std::exp2(settings.ExposureEV),static_cast<float>(settings.Tonemap),
        1.0f/_HDRTarget->GetWidth(),1.0f/_HDRTarget->GetHeight()};
    pass.SetPipeline(pipeline);
    pass.SetFragmentUniform(0,&parameters,sizeof(parameters));
    pass.SetFragmentTextureSampler(0,_HDRTarget.get(),_DefaultSampler);
    pass.Draw(3);
    ++_Statistics.DrawCalls;++_Statistics.Triangles;
    return true;
}

bool Renderer::DrawSky(GRHI::IRenderPass& pass,const RenderView& view,GRHI::TextureFormat format,
    float aspect,const std::vector<DirectionalLight>& lights,const EnvironmentGPU* environment) {
    const auto& settings=view.Scene->GetEnvironmentLighting();
    if(!settings.Atmosphere.Enabled||!settings.Atmosphere.DrawSky)return true;
    if(!IsValidEnvironmentLighting(settings))return true;
    const auto fragmentKind=environment?PreviewShader::CubemapSkyFragment:PreviewShader::SkyFragment;
    auto* pipeline=FullscreenPipeline(PreviewShader::SkyVertex,fragmentKind,format,GRHI::TextureFormat::D32_Float);
    if(!pipeline)return false;
    struct VertexUniform {Vector4 Right,Up,Forward,Projection;} vertex{};
    Mat4 inverseView;
    // GPU cameras use an RH view basis internally even though authored world
    // coordinates use Unity's +Z-forward convention. Use the actual matrices.
    const auto& camera=view.Camera.Projection;
    if(!RuntimeMath::TryInverseAffine(view.Camera.View,inverseView))return Fail("Cannot reconstruct Runtime sky camera rays");
    vertex.Right=inverseView*Vector4{1,0,0,0};vertex.Up=inverseView*Vector4{0,1,0,0};
    vertex.Forward=inverseView*Vector4{0,0,-1,0};
    if(camera.Projection==CameraProjection::Perspective) {
        const float halfHeight=std::tan(camera.VerticalFovRadians*.5f);
        vertex.Projection={halfHeight*aspect,halfHeight,0,0};
    }
    pass.SetPipeline(pipeline);pass.SetVertexUniform(0,&vertex,sizeof(vertex));
    if(environment) {
        const auto yaw=RenderEnvironment::SkyYaw(settings.SkyRotationDegrees);
        const Vector4 parameters{yaw.X,yaw.Y,settings.SkyIntensity,0};
        pass.SetFragmentUniform(0,&parameters,sizeof(parameters));
        pass.SetFragmentTextureSampler(0,environment->Sky.get(),EnvironmentSampler());
    } else {
        struct FragmentUniform {Color Zenith,Horizon;Vector4 SunDirection;Color SunRadiance;} fragment{};
        fragment.Zenith=settings.Atmosphere.ZenithColor;fragment.Horizon=settings.Atmosphere.HorizonColor;
        for(const auto& light:lights)if(light.CullingMask&camera.CullingMask) {
            fragment.SunDirection={light.TowardsLight.X,light.TowardsLight.Y,light.TowardsLight.Z,1};
            fragment.SunRadiance=light.Radiance;break;
        }
        pass.SetFragmentUniform(0,&fragment,sizeof(fragment));
    }
    pass.Draw(3);
    ++_Statistics.DrawCalls;++_Statistics.Triangles;
    return true;
}
}
