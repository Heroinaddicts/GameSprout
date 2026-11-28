#pragma once
#include "GameObject.h"
#include <deque>
#include <optional>
#include <unordered_map>

namespace GameSproutRuntime {
enum class RenderObjectChangeKind { Transform, Mesh, Materials };
struct RenderObjectChange {
    std::uint64_t ObjectID;
    std::uint64_t Revision;
    RenderObjectChangeKind Kind;
};
struct RenderObjectTransform {
    Mat4 Model;
    Mat4 Normal;
    bool Valid=false;
    bool Negative=false;

    static RenderObjectTransform FromModel(const Mat4& model);
    static RenderObjectTransform FromModel(const Mat4& model,const Mat4& previousModel,const Mat4& previousNormal,
        bool previousNegative);
};
struct CachedRenderObjectTransform {
    RenderObjectTransform Value;
    std::uint64_t Revision=0;
};

class Scene final : public GameObjectBase<IScene>, public IWeatherScene {
public:
    Scene(Runtime& runtime, const std::string& name, std::uint64_t id)
        : GameObjectBase<IScene>(runtime,nullptr,name,id) { State().SceneOwner = this; }
    IGameObject* CreateGameObject(const std::string& name, IGameObject* parent = nullptr) override;
    bool DestroyGameObject(IGameObject* object) override;
    void SetSimulationEnabled(bool enabled) override;
    bool IsSimulationEnabled() const override;
    bool IsParkedForPlay() const {return _ParkedForPlay;}
    bool SetMainCamera(ICameraComponent* camera) override;
    ICameraComponent* GetMainCamera() const override;
    const EnvironmentLightingSettings& GetEnvironmentLighting() const override { return _EnvironmentLighting; }
    bool SetEnvironmentLighting(const EnvironmentLightingSettings&) override;
    WeatherSettings GetAuthoredWeatherSettings() const override {return _Weather.Settings;}
    // Runtime-internal snapshot invalidation. Changes are visible before queued
    // listener dispatch, including direct Set* -> Render calls in editor/tests.
    std::uint64_t GetRenderRevision() const {return _RenderRevision;}
    bool HasSameRenderStructure(std::uint64_t after) const {return after>=_RenderStructureRevision;}
    bool HasRenderMeshChanges(std::uint64_t after) const {return after<_RenderMeshRevision;}
    bool HasRenderMaterialChanges(std::uint64_t after) const {return after<_RenderMaterialRevision;}
    void InvalidateRenderSnapshot(std::uint64_t objectID=0,RenderObjectChangeKind kind=RenderObjectChangeKind::Transform);
    void InvalidateVisual() { _VisualPending = true; }
    std::uint64_t GetSkinPoseRevision() const {return _SkinPoseRevision;}
    void InvalidateSkinPose() {++_SkinPoseRevision;InvalidateVisual();}
    std::uint64_t GetUIRevision() const { return _UIRevision; }
    void InvalidateUI() { ++_UIRevision; _VisualPending = true; }
    // False after a structural change or when the bounded journal no longer covers the revision.
    bool GetRenderObjectChanges(std::uint64_t after,std::vector<RenderObjectChange>& changes) const;
    bool TakeVisualInvalidation() {const bool pending=_VisualPending;_VisualPending=false;return pending;}
    std::uint64_t GetAuthoringRevision() const override {return _AuthoringRevision;}
    bool GetAuthoringChanges(std::uint64_t after,std::vector<SceneAuthoringChange>& changes) const override;
    void ClearMainCameraForNode(std::uint64_t id);
    bool BindObjectIdentity(IGameObject* object,std::uint64_t fileID,std::string& error) override {
        auto* node=State().Owner.Find(object);
        if(!State().Owner.IsInScene(node) || node->SceneOwner!=this) {error="Object belongs to another scene";return false;}
        return State().Owner.BindSceneObjectIdentity(object,fileID,error);
    }
    bool SetComponentIdentities(const std::vector<SceneComponentIdentity>& values,std::string& error) override {
        return State().Owner.SetSceneComponentIdentities(this,values,error);
    }
    bool BindObjectIdentities(const std::vector<SceneObjectIdentity>& identities,std::string& error) override {
        return State().Owner.BindSceneObjectIdentities(this,identities,error);
    }
    bool BindAuthoringIdentities(const std::vector<SceneObjectIdentity>& objects,
        const std::vector<SceneComponentBinding>& components,std::string& error) override {
        return State().Owner.BindAuthoringIdentities(this,objects,components,error);
    }
private:
    friend class Runtime;
    friend class Renderer;
    void RecordAuthoringChange(std::uint64_t objectID,SceneNodeEventType type,const char* component,
        std::uint64_t batch,bool builtinTransform);
    // Entries outlive renderer lookups until the scene's structure revision changes.
    CachedRenderObjectTransform* BindRenderObjectTransform(const GameObjectState& node) const;
    bool RefreshRenderObjectTransform(CachedRenderObjectTransform& cached,const GameObjectState& node) const;
    const RenderObjectTransform* GetRenderObjectTransform(std::uint64_t objectID,const GameObjectState* knownNode=nullptr) const;
    std::uint64_t _MainCameraID = 0;
    bool _SimulationEnabled = true;
    bool _ParkedForPlay = false;
    EnvironmentLightingSettings _EnvironmentLighting;
    struct WeatherSimulation {
        WeatherSettings Settings;
        WeatherState Current;
        double TimeOfDayHours=12;
        std::array<std::array<double,2>,2> CloudOffsets{};
        std::optional<WeatherPreset> TransitionTarget;
        WeatherPreset TransitionSource;
        double TransitionElapsed=0;
        float TransitionDuration=0;
        double LightningAge=0;
    } _Weather;
    std::uint64_t _RenderRevision=1;
    std::uint64_t _SkinPoseRevision=1;
    std::uint64_t _RenderStructureRevision=1;
    std::uint64_t _RenderMeshRevision=1;
    std::uint64_t _RenderMaterialRevision=1;
    std::uint64_t _DiscardedRenderObjectRevision=1;
    std::uint64_t _UIRevision=1;
    std::deque<RenderObjectChange> _RenderObjectChanges;
    mutable std::unordered_map<std::uint64_t,CachedRenderObjectTransform> _RenderObjectTransforms;
    std::uint64_t _AuthoringRevision=1;
    struct AuthoringRecord {
        SceneAuthoringChange Change;
        std::uint64_t Batch;
        bool BuiltinTransform;
    };
    std::deque<AuthoringRecord> _AuthoringChanges;
    bool _VisualPending=false;
};
}
