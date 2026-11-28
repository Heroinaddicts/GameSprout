#include "RuntimeRenderer.h"
#include "RuntimeRenderResourceCache.h"
#include "RuntimeEnvironment.h"
#include "RuntimeMath.h"
#include <algorithm>
#include <cmath>

namespace GameSproutRuntime {
namespace {
float WeatherNoise(int x,int y,int period,std::uint32_t seed) {
    std::uint32_t hash=std::uint32_t(x%period)*0x9e3779b9u^std::uint32_t(y%period)*0x85ebca6bu^seed;
    hash=(hash^(hash>>16))*0x7feb352du;
    return float((hash^(hash>>15))&65535u)/65535.0f;
}
float CloudNoise(float u,float v,int period,std::uint32_t seed) {
    const float x=u*period,y=v*period;
    const auto i=int(x),j=int(y);
    const float fx=x-i,fy=y-j;
    const float sx=fx*fx*(3-2*fx),sy=fy*fy*(3-2*fy);
    return std::lerp(std::lerp(WeatherNoise(i,j,period,seed),WeatherNoise(i+1,j,period,seed),sx),
        std::lerp(WeatherNoise(i,j+1,period,seed),WeatherNoise(i+1,j+1,period,seed),sx),sy);
}

constexpr unsigned LightningSegments=12;
struct LightningVertices {
    Mat4 ViewProjection;
    Vector4 CameraPositionAndWidth;
    Vector4 CameraRight;
    std::array<Vector4,LightningSegments+1> Points;
};
struct WeatherSkyParameters {
    Color Zenith,Horizon,Tint;
    Vector4 SunDirection;
    Vector4 SunRadiance;
    Vector4 SkyCube; // Yaw XY and HDRI intensity.
    Vector4 Stars;   // Yaw XY, intensity, density.
    Vector4 TimeAndFlash; // Daylight, direct-sun attenuation, flash intensity.
    Vector4 LightningTint;
    std::array<Vector4,2> Clouds;       // Coverage, density, scale.
    Vector4 CloudOffsets;
};
}
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

GRHI::ITexture* Renderer::WeatherCloudTexture() {
    auto& texture=_ResourceCache.WeatherCloudTexture;
    if(texture)return texture.get();
    auto& sampler=_ResourceCache.WeatherCloudSampler;
    if(!sampler) {
        GRHI::SamplerDesc description;
        description.AddressU=description.AddressV=GRHI::AddressMode::Repeat;
        description.MipFilter=GRHI::FilterMode::Linear;
        sampler.reset(_Device.CreateSampler(description));
        if(!sampler)return nullptr;
    }
    constexpr std::uint32_t size=128;
    GRHI::TextureDesc description;
    description.Width=description.Height=size;
    description.MipLevels=8;
    description.Format=GRHI::TextureFormat::RGBA8_UNorm;
    description.Usage=GRHI::TextureUsage::Sampled;
    std::unique_ptr<GRHI::ITexture> noise(_Device.CreateTexture(description));
    if(!noise)return nullptr;
    std::vector<std::uint8_t> pixels(size*size*4,255);
    for(unsigned channel=0;channel<2;++channel) {
        std::array<unsigned,256> distribution{};
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
            float value=0,weight=.54f;
            for(int octave=0;octave<5;++octave,weight*=.48f)
                value+=weight*CloudNoise(float(x)/size,float(y)/size,3<<octave,
                    0xb5297a4du*(channel+2)+0x68e31da4u*std::uint32_t(octave));
            const auto value8=static_cast<std::uint8_t>(std::clamp(value,0.0f,1.0f)*255);
            pixels[(y*size+x)*4+channel]=value8;
            ++distribution[value8];
        }
        // Coverage is a percentile so 50% cloud stays useful for both patterns.
        unsigned seen=0;
        for(auto& count:distribution) {
            const auto next=seen+count;
            count=(seen+next)*255/(2*size*size);
            seen=next;
        }
        for(std::size_t offset=channel;offset<pixels.size();offset+=4)
            pixels[offset]=static_cast<std::uint8_t>(distribution[pixels[offset]]);
    }
    for(std::uint32_t level=0,width=size;level<description.MipLevels;++level,width/=2) {
        if(!_Device.UploadTextureSubresourceRows(noise.get(),level,0,0,width,
            {pixels.data(),std::size_t(width)*width*4,std::size_t(width)*4}))return nullptr;
        for(unsigned y=0;y<width/2;++y)for(unsigned x=0;x<width/2;++x)
            for(unsigned channel=0;channel<2;++channel) {
                const auto source=((y*2)*width+x*2)*4+channel;
                pixels[(y*(width/2)+x)*4+channel]=static_cast<std::uint8_t>(
                    (unsigned(pixels[source])+pixels[source+4]+pixels[source+width*4]+pixels[source+width*4+4]+2)/4);
            }
    }
    texture=std::move(noise);
    return texture.get();
}

void Renderer::PrepareWeatherEnvironment(RenderEnvironment::Uniform& environment) const {
    const auto& preset=_Weather.Preset;
    const float ambient=std::lerp(.14f,1.0f,_Weather.Daylight)*preset.AmbientMultiplier;
    environment.WeatherLighting={ambient,ambient,0,0};
    const auto& strike=_Weather.Lightning;
    const float flash=_Weather.LightningFlash*strike.Intensity*strike.Tint.A;
    const float fogDaylight=std::lerp(.24f,1.0f,_Weather.Daylight);
    environment.AtmosphereParameters.W=std::min(1.0f,environment.AtmosphereParameters.W*preset.FogStrength);
    environment.AtmosphereFogColor=environment.AtmosphereFogColor*fogDaylight
        +Vector4{strike.Tint.R,strike.Tint.G,strike.Tint.B,0}*(flash*.04f);
    if(flash>0) {
        environment.LightningPosition={strike.Position.X,strike.Position.Y+strike.Height*.38f,strike.Position.Z,strike.Radius};
        environment.LightningRadiance={strike.Tint.R*flash,strike.Tint.G*flash,strike.Tint.B*flash,1};
    }
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

const Renderer::EnvironmentGPU* Renderer::Environment(const ResourceReference& reference,bool loadSky) {
    if(reference.Guid.empty()||!reference.LocalID)return nullptr;
    auto [found,inserted]=_ResourceCache.Environments.try_emplace(ResourceKey(reference));
    auto& environment=found->second;
    if(inserted||(loadSky&&!environment.SkyAttempted)) {
        environment.SkyAttempted|=loadSky;
        environment.ResourceRevision=_Resources.GetResourceRevision(reference);
        EnvironmentRenderData data;
        if(_Resources.LoadEnvironment(reference,data,environment.Error)) {
            if((loadSky&&data.Sky.Dimension!=TextureDimension::Cube)||
               data.Specular.Dimension!=TextureDimension::Cube||!data.Specular.MipLevels) {
                environment.Error="Prepared environment requires a reflection cube and a sky cube when drawn as sky";
            } else if(!EnvironmentSampler()) {
                environment.Error=DeviceError("Create environment sampler");
            } else {
                if(!environment.Specular) {
                    environment.Specular.reset(UploadPreparedTexture(data.Specular,environment.Error));
                    environment.SpecularMaxLOD=float(data.Specular.MipLevels-1);
                }
                if(loadSky&&environment.Specular)
                    environment.Sky.reset(UploadPreparedTexture(data.Sky,environment.Error));
                environment.DiffuseSH=data.DiffuseSH;
            }
        }
    }
    if(environment.Specular&&(!loadSky||environment.Sky))return &environment;
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
    if(!settings.Atmosphere.DrawSky||(!settings.Atmosphere.Enabled&&!_Weather.Enabled))return true;
    if(!IsValidEnvironmentLighting(settings))return true;
    const auto fragmentKind=_Weather.Enabled
        ?(environment?PreviewShader::WeatherCubemapSkyFragment:PreviewShader::WeatherSkyFragment)
        :(environment?PreviewShader::CubemapSkyFragment:PreviewShader::SkyFragment);
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
    if(_Weather.Enabled) {
        auto* cloudNoise=WeatherCloudTexture();
        if(!cloudNoise)return Fail(DeviceError("Create Runtime weather cloud texture"));
        const auto& weather=_Weather.Preset;
        const auto& lightning=_Weather.Lightning;
        const auto yaw=RenderEnvironment::SkyYaw(settings.SkyRotationDegrees);
        const auto starYaw=RenderEnvironment::SkyYaw(weather.StarRotationDegrees);
        WeatherSkyParameters fragment{};
        fragment.Zenith=settings.Atmosphere.ZenithColor;fragment.Horizon=settings.Atmosphere.HorizonColor;
        fragment.Tint=weather.CloudColor;
        for(const auto& light:lights)if(light.CullingMask&camera.CullingMask) {
            fragment.SunDirection={light.TowardsLight.X,light.TowardsLight.Y,light.TowardsLight.Z,1};
            fragment.SunRadiance={light.Radiance.R,light.Radiance.G,light.Radiance.B,1};
            break;
        }
        fragment.SkyCube={yaw.X,yaw.Y,settings.SkyIntensity,0};
        fragment.Stars={starYaw.X,starYaw.Y,weather.StarIntensity,weather.StarDensity};
        fragment.TimeAndFlash={_Weather.Daylight,weather.DaylightMultiplier,
            _Weather.LightningFlash*lightning.Intensity*lightning.Tint.A,0};
        fragment.LightningTint={lightning.Tint.R,lightning.Tint.G,lightning.Tint.B,1};
        for(std::size_t index=0;index<fragment.Clouds.size();++index) {
            const auto& cloud=weather.Clouds[index];
            fragment.Clouds[index]={cloud.Coverage,cloud.Density,cloud.Scale,0};
        }
        fragment.CloudOffsets={_Weather.CloudOffsets[0].X,_Weather.CloudOffsets[0].Y,
            _Weather.CloudOffsets[1].X,_Weather.CloudOffsets[1].Y};
        pass.SetFragmentUniform(0,&fragment,sizeof(fragment));
        pass.SetFragmentTextureSampler(0,cloudNoise,_ResourceCache.WeatherCloudSampler.get());
        if(environment)pass.SetFragmentTextureSampler(1,environment->Sky.get(),EnvironmentSampler());
    } else if(environment) {
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

bool Renderer::DrawWeatherLightning(GRHI::IRenderPass& pass,const RenderView& view,
    const Mat4& viewProjection,GRHI::TextureFormat format) {
    const auto& strike=_Weather.Lightning;
    const float flash=_Weather.LightningFlash*strike.Intensity*strike.Tint.A;
    if(!_Weather.Enabled||flash<=0)return true;
    auto& pipeline=_ResourceCache.WeatherBoltPipelines[format];
    if(!pipeline) {
        auto*& vertex=_Shaders[static_cast<unsigned>(PreviewShader::WeatherLightningVertex)];
        auto*& fragment=_Shaders[static_cast<unsigned>(PreviewShader::WeatherLightningFragment)];
        if(!vertex)vertex=CreatePreviewShader(_Device,PreviewShader::WeatherLightningVertex);
        if(!fragment)fragment=CreatePreviewShader(_Device,PreviewShader::WeatherLightningFragment);
        if(!vertex||!fragment)return Fail(DeviceError("Create Runtime lightning shaders"));
        GRHI::GraphicsPipelineDesc description;
        description.VertexShader=vertex;description.FragmentShader=fragment;description.ColorFormat=format;
        description.DepthFormat=GRHI::TextureFormat::D32_Float;description.DepthTest=true;
        description.Blend=GRHI::BlendMode::PremultipliedAlpha;
        pipeline.reset(_Device.CreateGraphicsPipeline(description));
        if(!pipeline)return Fail(DeviceError("Create Runtime lightning pipeline"));
    }
    const float width=std::clamp(strike.Height*.010f,.12f,40.0f);
    LightningVertices uniform;
    uniform.ViewProjection=viewProjection;
    const auto& camera=view.Camera.Position;
    uniform.CameraPositionAndWidth={camera.X,camera.Y,camera.Z,width};
    uniform.CameraRight={view.Camera.View(0,0),view.Camera.View(0,1),view.Camera.View(0,2),0};
    for(unsigned index=0;index<=LightningSegments;++index) {
        const float height=float(index)/LightningSegments;
        const float stride=strike.Height*.20f*height;
        const float x=WeatherNoise(index,2,64,strike.Seed)-.5f;
        const float z=WeatherNoise(index,3,64,strike.Seed)-.5f;
        uniform.Points[index]={strike.Position.X+x*stride,strike.Position.Y+strike.Height*height,
            strike.Position.Z+z*stride,1};
    }
    const Vector4 radiance{strike.Tint.R*flash,strike.Tint.G*flash,strike.Tint.B*flash,1};
    pass.SetPipeline(pipeline.get());
    pass.SetVertexUniform(0,&uniform,sizeof(uniform));
    pass.SetFragmentUniform(0,&radiance,sizeof(radiance));
    pass.Draw(LightningSegments*6);
    ++_Statistics.DrawCalls;_Statistics.Triangles+=LightningSegments*2;
    return true;
}
}
