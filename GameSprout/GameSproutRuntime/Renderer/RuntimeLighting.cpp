#include "RuntimeRenderer.h"
#include "RuntimeMath.h"
#include "Scene.h"
#include "GameSproutRuntime.h"
#include "Components/TransformComponent.h"
#include <algorithm>
#include <cmath>

namespace GameSproutRuntime {
namespace {
double Luminance(Color color) {return double(color.R)*.2126+double(color.G)*.7152+double(color.B)*.0722;}
Vector3 LightPosition(const IGameObject& object) {
    const auto world=object.GetTransform()->GetWorldMatrix();
    return {world(0,3),world(1,3),world(2,3)};
}
}
bool Renderer::ExtractLights(IScene* scene,std::vector<DirectionalLight>& lights) {
    const auto sceneID=scene->GetInstanceID();
    const auto& runtimeScene=static_cast<const Scene&>(*scene);
    const auto revision=runtimeScene.GetRenderRevision();
    if(_LightSnapshotKnown&&_LightSceneID==sceneID) {
        std::vector<std::uint64_t> changes;
        if(_LightSceneRevision==revision||(runtimeScene.GetRenderTransformChanges(_LightSceneRevision,changes)&&
            std::none_of(changes.begin(),changes.end(),[this](auto id){return _LightTransformSources.contains(id);}))) {
            _LightSceneRevision=revision;
            Diagnostic(_LightDiagnostic);return true;
        }
    }
    _LightSnapshotKnown=false;lights.clear();_LocalLights.clear();
    ++_LocalLightGeneration;
    _LightTransformSources.clear();
    const auto diagnosticBefore=_Error.size();
    ++_Statistics.LightSnapshotRebuilds;
    std::vector<IGameObject*> pending{scene};
    while(!pending.empty()) {
        auto* object=pending.back();pending.pop_back();
        for(std::size_t index=object->GetChildCount();index>0;--index)pending.push_back(object->GetChild(index-1));
        auto* light=object->GetComponent<ILightComponent>();
        if(!light||!object->IsActiveInHierarchy())continue;
        const auto& settings=light->GetSettings();
        if(!settings.Enabled||settings.Intensity==0||!settings.CullingMask)continue;
        // Unity light rays follow local +Z; surface-to-light is local -Z.
        // Compose authored rotations only: ancestor scale, including reflections
        // and zero scales, must never change the orientation of an emitter.
        Vector3 direction{0,0,1};
        for(auto* node=object;node;node=node->Parent()) {
            _LightTransformSources.insert(node->GetInstanceID());
            if(settings.Type==LightType::Point)continue;
            auto* transform=node->GetTransform();
            Quaternion orientation;
            if(const auto* current=dynamic_cast<const TransformComponent*>(transform)) {
                if(!current->GetCurrentLocalRotation(orientation))return Fail("Invalid animated light orientation");
            } else orientation=transform->GetLocalRotation();
            const Mat4 rotation=RuntimeMath::ComposeTRS({},orientation,{1,1,1});
            const auto rotated=rotation*Vector4{direction.X,direction.Y,direction.Z,0};
            direction={rotated.X,rotated.Y,rotated.Z};
        }
        Color radiance{settings.Color.R*settings.Intensity,settings.Color.G*settings.Intensity,
            settings.Color.B*settings.Intensity,1};
        if(!direction.TryNormalize(direction)||!RuntimeMath::IsFinite(radiance)) {
            Diagnostic("Skipped nonfinite light radiance/orientation: "+object->GetName());continue;
        }
        if(settings.Type==LightType::Directional) {
            lights.push_back({direction*-1,radiance,settings.CullingMask,settings.Shadows,object->GetInstanceID()});
        }else {
            const auto position=LightPosition(*object);
            if(!RuntimeMath::IsFinite(position)){Diagnostic("Skipped nonfinite light position: "+object->GetName());continue;}
            const auto outer=DegreesToRadians(settings.OuterSpotAngleDegrees);
            const auto inner=DegreesToRadians(settings.InnerSpotAngleDegrees);
            _LocalLights.push_back({position,direction,radiance,settings.Type,settings.CullingMask,settings.Range,
                std::cos(inner*.5f),settings.Type==LightType::Spot?std::cos(outer*.5f):-1,outer,Luminance(radiance),
                settings.Shadows,object->GetInstanceID(),light->GetComponentInstanceID()});
        }
    }
    // Highest linear luminance first; hierarchy order resolves exact ties.
    std::stable_sort(lights.begin(),lights.end(),[](const DirectionalLight& left,const DirectionalLight& right){
        return Luminance(left.Radiance)>Luminance(right.Radiance);
    });
    _LightSceneID=sceneID;_LightSceneRevision=revision;
    _LightDiagnostic=_Error.substr(diagnosticBefore);
    if(!_LightDiagnostic.empty()&&_LightDiagnostic.front()=='\n')_LightDiagnostic.erase(0,1);
    _LightSnapshotKnown=true;
    return true;
}

RenderFrustum::Box Renderer::LocalLightObjectBounds(const DrawObject& object,const MeshGPU& mesh) const {
    if(object.Skinned) {
        const auto* node=_Runtime.QueryGameObject(object.ObjectID);
        const auto* component=node?node->GetComponent<IMeshComponent>():nullptr;
        if(!component)return {};
        const auto bounds=component->GetLocalBounds();
        return {bounds.Center-bounds.Extents,bounds.Center+bounds.Extents,object.Model};
    }
    if(object.CullBoxKnown)return object.CullBox;
    if(mesh.CullBoundsKnown)return {mesh.CullMinimum,mesh.CullMaximum,object.Model};
    const auto& bounds=mesh.Data.LocalBounds;
    return {bounds.Center-bounds.Extents,bounds.Center+bounds.Extents,object.Model};
}

void Renderer::PrepareLocalLights() {
    if(_LocalLights.empty()&&!_HadLocalLighting)return;
    _HadLocalLighting=!_LocalLights.empty();
    _VisibleLocalShadowReceivers.assign(_LocalLights.size(),false);
    for(auto& draw:_PreparedDraws) {
        if(draw.Shader||draw.Material->ShadingModel!=MaterialShadingModel::Lit)continue;
        auto& object=_SceneObjects[draw.Object->InstanceIndex];
        auto& indices=object.LocalLightIndices;
        if(object.LocalLightGeneration!=_LocalLightGeneration||(object.Skinned&&object.LocalLightFrame!=_Frame)) {
            indices={};
            std::array<double,MaxLocalLights> scores{};
            const auto bounds=LocalLightObjectBounds(object,*draw.Mesh);
            const auto layer=std::uint32_t{1}<<object.Layer;
            for(std::size_t index=0;index<_LocalLights.size();++index) {
                const auto& light=_LocalLights[index];
                if(!(light.CullingMask&layer))continue;
                const auto rangeSquared=double(light.Range)*light.Range;
                double distanceSquared=0,coneSupport=0;
                if(bounds.Valid) {
                    const float position[]{light.Position.X,light.Position.Y,light.Position.Z};
                    const float forward[]{light.Forward.X,light.Forward.Y,light.Forward.Z};
                    for(std::size_t axis=0;axis<3;++axis) {
                        const double centre=bounds.Center[axis]-position[axis];
                        const double extent=std::abs(bounds.Axis[0][axis])+std::abs(bounds.Axis[1][axis])+std::abs(bounds.Axis[2][axis]);
                        const double gap=std::max(0.0,std::abs(centre)-extent);
                        distanceSquared+=gap*gap;
                        coneSupport+=centre*forward[axis]+extent*std::abs(forward[axis]);
                    }
                    if(distanceSquared>=rangeSquared||
                        (light.Type==LightType::Spot&&(coneSupport<0||coneSupport*coneSupport<
                            double(light.OuterCosine)*light.OuterCosine*distanceSquared)))continue;
                }
                const double fraction=distanceSquared/rangeSquared;
                const double falloff=1-fraction*fraction;
                const double score=light.Luminance*falloff*falloff/std::max(.01,distanceSquared);
                for(std::size_t slot=0;slot<indices.size();++slot) {
                    if(score<=scores[slot])continue;
                    for(std::size_t tail=indices.size()-1;tail>slot;--tail) {
                        indices[tail]=indices[tail-1];scores[tail]=scores[tail-1];
                    }
                    indices[slot]=static_cast<std::uint32_t>(index+1);scores[slot]=score;break;
                }
            }
            object.LocalLightGeneration=_LocalLightGeneration;
            object.LocalLightFrame=_Frame;
        }
        if(object.ReceiveShadows)for(const auto index:indices) {
            if(!index)break;
            _VisibleLocalShadowReceivers[index-1]=true;
        }
        if(draw.LocalLightIndices!=indices){draw.LocalLightIndices=indices;_ColorInstanceBatchesKnown=false;}
    }
}

Renderer::LitUniform Renderer::MakeLitUniform(const RenderView& view,const PreparedDraw& draw,
    const std::vector<DirectionalLight>& lights) {
    const auto& material=*draw.Material;
    const auto& object=*draw.Object;
    LitUniform result{};
    result.CameraPosition={view.Camera.Position.X,view.Camera.Position.Y,view.Camera.Position.Z,material.SpecularHighlights?1.0f:0.0f};
    result.Parameters={material.Metallic,material.Roughness,material.NormalScale,draw.NormalTexture?1.0f:0.0f};
    result.Emission=material.EmissionColor;result.NormalTextureST=material.NormalTextureST;
    result.StyleParameters={material.Stylization,material.LightThreshold,material.LightSoftness,material.ColorSaturation};
    result.StylizedShadowTint=material.StylizedShadowTint;
    const auto& shadowSource=_PlanarShadowSource?*_PlanarShadowSource:*this;
    std::size_t count=0;
    for(const auto& light:lights) {
        const auto lightIndex=static_cast<std::size_t>(&light-lights.data());
        if(!(light.CullingMask&(std::uint32_t{1}<<object.Layer)))continue;
        if(count==MaxDirectionalLights){Diagnostic("Lit uses the four brightest matching directional lights per object");break;}
        const bool sampleShadows=object.ReceiveShadows&&lightIndex==shadowSource._ShadowLightIndex&&shadowSource._ShadowUniform.Info.X>0;
        result.Directions[count]={light.TowardsLight.X,light.TowardsLight.Y,light.TowardsLight.Z,sampleShadows?2.0f:1.0f};
        result.Radiances[count]=light.Radiance;++count;
    }
    for(std::size_t slot=0;slot<draw.LocalLightIndices.size();++slot) {
        const auto index=draw.LocalLightIndices[slot];if(!index)break;
        const auto& light=_LocalLights[index-1];
        float shadowSlot=0;
        if(object.ReceiveShadows)for(std::size_t i=0;i<shadowSource._SpotShadowCount;++i)
            if(shadowSource._SpotShadowIndices[i]==index){shadowSlot=static_cast<float>(i+1);break;}
        result.LocalPositions[slot]={light.Position.X,light.Position.Y,light.Position.Z,light.Range};
        result.LocalDirections[slot]={light.Forward.X,light.Forward.Y,light.Forward.Z,light.OuterCosine};
        result.LocalParameters[slot]={light.InnerCosine,shadowSlot,0,0};
        result.LocalRadiances[slot]=light.Radiance;
        result.LocalInfo.X=static_cast<float>(slot+1);
    }
    return result;
}
}
