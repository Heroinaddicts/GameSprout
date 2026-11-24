#include "Scene.h"
#include <algorithm>

namespace GameSproutRuntime {
void Scene::RecordAuthoringChange(std::uint64_t objectID,SceneNodeEventType type,const char* component,std::uint64_t batch) {
    if(type==SceneNodeEventType::OperationFailed)return;
    ++_AuthoringRevision;
    try {
        _AuthoringChanges.push_back({{objectID,type,component?component:""},batch});
        constexpr std::size_t historyWindow=2048;
        if(_AuthoringChanges.size()<=historyWindow)return;
        // Keep the entire structural edit that overlaps the recent history.
        // Deleting or restoring thousands of nodes must not evict its own changes.
        const auto oldestRetainedBatch=_AuthoringChanges[_AuthoringChanges.size()-historyWindow].Batch;
        while(_AuthoringChanges.size()>historyWindow&&
              (!oldestRetainedBatch||_AuthoringChanges.front().Batch!=oldestRetainedBatch))
            _AuthoringChanges.pop_front();
    } catch(...) {_AuthoringChanges.clear();}
}

void Scene::InvalidateRenderSnapshot(std::uint64_t objectID,RenderObjectChangeKind kind) {
    ++_RenderRevision;
    _VisualPending=true;
    if(!objectID) {
        _RenderStructureRevision=_RenderRevision;
        _RenderObjectChanges.clear();
        return;
    }
    if(kind==RenderObjectChangeKind::Mesh)_RenderMeshRevision=_RenderRevision;
    // Position and rotation setters often touch the same object consecutively.
    // Readers see the final values and can catch up from any earlier revision.
    if(!_RenderObjectChanges.empty()&&_RenderObjectChanges.back().ObjectID==objectID&&
       _RenderObjectChanges.back().Kind==kind) {
        _RenderObjectChanges.back().Revision=_RenderRevision;
        return;
    }
    try {
        constexpr std::size_t historyWindow=4096;
        _RenderObjectChanges.push_back({objectID,_RenderRevision,kind});
        if(_RenderObjectChanges.size()>historyWindow) {
            _DiscardedRenderObjectRevision=_RenderObjectChanges.front().Revision;
            _RenderObjectChanges.pop_front();
        }
    } catch(...) {
        _DiscardedRenderObjectRevision=_RenderRevision;
        _RenderObjectChanges.clear();
    }
}
bool Scene::GetRenderObjectChanges(std::uint64_t after,std::vector<RenderObjectChange>& changes) const {
    if(after>_RenderRevision||after<_RenderStructureRevision||after<_DiscardedRenderObjectRevision)return false;
    const auto start=std::upper_bound(_RenderObjectChanges.begin(),_RenderObjectChanges.end(),after,
        [](auto revision,const auto& change){return revision<change.Revision;});
    changes.assign(start,_RenderObjectChanges.end());
    return true;
}
bool Scene::SetEnvironmentLighting(const EnvironmentLightingSettings& settings) {
    if(!IsValidEnvironmentLighting(settings))return false;
    const auto sameColor=[](Color a,Color b){return a.R==b.R&&a.G==b.G&&a.B==b.B&&a.A==b.A;};
    bool same=settings.Mode==_EnvironmentLighting.Mode&&settings.Intensity==_EnvironmentLighting.Intensity&&
        settings.Atmosphere==_EnvironmentLighting.Atmosphere&&
        settings.SkyEnvironment==_EnvironmentLighting.SkyEnvironment&&
        settings.SkyRotationDegrees==_EnvironmentLighting.SkyRotationDegrees&&settings.SkyIntensity==_EnvironmentLighting.SkyIntensity&&
        settings.ReflectionIntensity==_EnvironmentLighting.ReflectionIntensity&&settings.HDREnabled==_EnvironmentLighting.HDREnabled&&
        settings.ExposureEV==_EnvironmentLighting.ExposureEV&&settings.Tonemap==_EnvironmentLighting.Tonemap&&
        settings.SpotShadows==_EnvironmentLighting.SpotShadows&&settings.ReflectionProbes==_EnvironmentLighting.ReflectionProbes&&
        settings.PlanarReflection==_EnvironmentLighting.PlanarReflection&&
        settings.IndirectLightVolumes==_EnvironmentLighting.IndirectLightVolumes&&
        sameColor(settings.AmbientColor,_EnvironmentLighting.AmbientColor)&&sameColor(settings.SkyColor,_EnvironmentLighting.SkyColor)&&
        sameColor(settings.EquatorColor,_EnvironmentLighting.EquatorColor)&&sameColor(settings.GroundColor,_EnvironmentLighting.GroundColor);
    for(std::size_t index=0;same&&index<settings.SHCoefficients.size();++index) {
        const auto a=settings.SHCoefficients[index],b=_EnvironmentLighting.SHCoefficients[index];
        same=a.X==b.X&&a.Y==b.Y&&a.Z==b.Z;
    }
    if(same)return true;
    _EnvironmentLighting=settings;
    State().Owner.Notify(State(),SceneNodeEventType::EnvironmentLightingChanged);
    return true;
}
IGameObject* Scene::CreateGameObject(const std::string& name, IGameObject* parent) {
    return State().Owner.CreateObject(this,name,parent ? parent : this);
}
bool Scene::DestroyGameObject(IGameObject* object) {
    RuntimeOperation operation(State().Owner);
    GameObjectState* node = State().Owner.Find(object);
    return node && node->SceneOwner == this && object != this && State().Owner.RequestDestroy(node,false);
}
void Scene::SetSimulationEnabled(bool enabled) {
    if(_SimulationEnabled==enabled)return;
    _SimulationEnabled=enabled;
    if(!enabled)State().Owner.ResetPhysicsPresentation(GetInstanceID());
}
bool Scene::IsSimulationEnabled() const { return _SimulationEnabled; }
bool Scene::SetMainCamera(ICameraComponent* camera) {
    std::uint64_t id=0;
    if(camera) {
        GameObjectState* node=State().Owner.Find(camera->GetGameObject());
            if(!State().Owner.IsInScene(node) || node->SceneOwner!=this ||
           node->GetComponent(typeid(ICameraComponent))!=camera) return false;
        id=node->ID;
    }
    if(_MainCameraID==id) return true;
    _MainCameraID=id;
    State().Owner.Notify(State(),SceneNodeEventType::MainCameraChanged);
    return true;
}
ICameraComponent* Scene::GetMainCamera() const {
    GameObjectState* node=State().Owner.Find(_MainCameraID);
    return State().Owner.IsInScene(node) && node->SceneOwner==this ? static_cast<ICameraComponent*>(node->GetComponent(typeid(ICameraComponent))) : nullptr;
}
void Scene::ClearMainCameraForNode(std::uint64_t id) {
    if(_MainCameraID==id) SetMainCamera(nullptr);
}
}
