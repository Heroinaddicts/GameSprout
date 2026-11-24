#include "RuntimeRenderer.h"
#include "RuntimeEnvironment.h"
#include "RuntimeRenderResourceCache.h"
#include <cmath>

namespace GameSproutRuntime {
void Renderer::PrepareReflectionProbes(const RenderView& view,const EnvironmentLightingSettings& settings) {
    const auto& probes=settings.ReflectionProbes;
    const auto count=view.EnableReflectionProbes?probes.size():0;
    if(count==0&&_ReflectionProbeSettings.empty())return;
    if((_ReflectionProbeSettings.size()!=count||(count&&_ReflectionProbeSettings!=probes))||
       _ReflectionProbeEnvironmentGeneration!=_ResourceCache.EnvironmentGeneration) {
        if(count)_ReflectionProbeSettings=probes;
        else _ReflectionProbeSettings.clear();
        _ReflectionProbeBindings.assign(count,{});
        _ReflectionProbeEnvironmentGeneration=_ResourceCache.EnvironmentGeneration;
        if(++_ReflectionProbeGeneration==0)++_ReflectionProbeGeneration;
    }
    for(auto& draw:_PreparedDraws) {
        std::size_t index=0;
        if(count&&!draw.Shader&&draw.Material->ShadingModel==MaterialShadingModel::Lit&&draw.Material->EnvironmentReflections) {
            auto& object=_SceneObjects[draw.Object->InstanceIndex];
            if(object.ReflectionProbeGeneration!=_ReflectionProbeGeneration||
               (object.Skinned&&object.ReflectionProbeFrame!=_Frame)) {
                object.ReflectionProbeGeneration=_ReflectionProbeGeneration;
                object.ReflectionProbeFrame=_Frame;
                object.ReflectionProbeIndex=0;
                const auto bounds=LocalLightObjectBounds(object,*draw.Mesh);
                const auto layer=std::uint32_t{1}<<object.Layer;
                const ReflectionProbeSettings* selected=nullptr;
                bool selectedContainsCenter=false;
                double selectedDistance=0;
                for(std::size_t offset=0;offset<count;++offset) {
                    const auto& probe=probes[offset];
                    if(!probe.Enabled||probe.Environment.Guid.empty()||probe.Intensity<=0||!(layer&probe.CullingMask)||
                       (selected&&probe.Priority<selected->Priority))continue;
                    const double center[]{probe.Center.X,probe.Center.Y,probe.Center.Z};
                    const double half[]{probe.Size.X*.5,probe.Size.Y*.5,probe.Size.Z*.5};
                    double distance=0;
                    bool containsCenter=true,overlaps=true;
                    for(std::size_t axis=0;axis<3;++axis) {
                        const double coordinate=bounds.Valid?bounds.Center[axis]:object.Model(axis,3);
                        const double extent=bounds.Valid?std::fabs(bounds.Axis[0][axis])+std::fabs(bounds.Axis[1][axis])+
                            std::fabs(bounds.Axis[2][axis]):0;
                        const double delta=std::fabs(coordinate-center[axis]);
                        if(delta>half[axis]+extent){overlaps=false;break;}
                        containsCenter&=delta<=half[axis];
                        const double normalized=delta/half[axis];
                        distance+=normalized*normalized;
                    }
                    if(!overlaps)continue;
                    if(selected&&probe.Priority==selected->Priority) {
                        if(selectedContainsCenter!=containsCenter) {
                            if(selectedContainsCenter)continue;
                        } else if(distance>=selectedDistance)continue;
                    }
                    auto& binding=_ReflectionProbeBindings[offset];
                    if(!binding.Resolved) {
                        binding.Resource=Environment(probe.Environment,false);
                        binding.Resolved=true;
                    }
                    if(!binding.Resource)continue;
                    selected=&probe;
                    selectedContainsCenter=containsCenter;
                    selectedDistance=distance;
                    object.ReflectionProbeIndex=offset+1;
                }
            }
            index=object.ReflectionProbeIndex;
        }
        if(draw.ReflectionProbeIndex!=index) {
            draw.ReflectionProbeIndex=index;
            _ColorInstanceBatchesKnown=false;
        }
    }
}

GRHI::ITexture* Renderer::BlackEnvironmentCube() {
    auto& black=_ResourceCache.BlackEnvironment;
    if(black)return black.get();
    constexpr std::uint8_t texel[]{0,0,0,255};
    GRHI::TextureDesc description;
    description.Width=description.Height=1;
    description.Dimension=GRHI::TextureDimension::Cube;
    description.Format=GRHI::TextureFormat::RGBA8_UNorm;
    description.Usage=GRHI::TextureUsage::Sampled;
    std::unique_ptr<GRHI::ITexture> texture(_Device.CreateTexture(description));
    if(!texture)return nullptr;
    for(std::uint32_t face=0;face<6;++face)
        if(!_Device.UploadTextureSubresourceRows(texture.get(),0,face,0,1,{texel,sizeof(texel),sizeof(texel)}))
            return nullptr;
    black=std::move(texture);
    return black.get();
}

bool Renderer::BindSpatialEnvironment(GRHI::IRenderPass& pass,const PreparedDraw& draw,
    const EnvironmentLightingSettings& settings,const EnvironmentGPU* sky,RenderEnvironment::Uniform& environment) {
    auto* globalCube=sky?sky->Specular.get():nullptr;
    if(!sky)environment.SkyTransform.W=0;
    auto* localCube=static_cast<GRHI::ITexture*>(nullptr);
    if(draw.ReflectionProbeIndex) {
        const auto offset=draw.ReflectionProbeIndex-1;
        const auto& source=settings.ReflectionProbes[offset];
        const auto* resource=_ReflectionProbeBindings[offset].Resource;
        if(resource) {
            localCube=resource->Specular.get();
            environment.ProbeCenter={source.Center.X,source.Center.Y,source.Center.Z,source.BlendDistance};
            environment.ProbeExtent={source.Size.X*.5f,source.Size.Y*.5f,source.Size.Z*.5f,source.Intensity};
            const auto levels=resource->SpecularMaxLOD+1;
            environment.ProbePosition={source.CapturePosition.X,source.CapturePosition.Y,source.CapturePosition.Z,
                source.BoxProjection?levels:-levels};
        }
    }
    if(!globalCube&&!localCube)globalCube=BlackEnvironmentCube();
    if(!globalCube)globalCube=localCube;
    if(!localCube)localCube=globalCube;
    auto* sampler=EnvironmentSampler();
    if(!globalCube||!localCube||!sampler)return Fail(DeviceError("Create spatial lighting cubemap fallback"));
    pass.SetFragmentTextureSampler(5,globalCube,sampler);
    pass.SetFragmentTextureSampler(6,localCube,sampler);
    return true;
}
}
