#include "RuntimeRenderer.h"
#include "Components/MeshRendererComponent.h"
#include "GameObject.h"
#include "GameSproutRuntime.h"
#include "RuntimeRenderResourceCache.h"
#include "Scene.h"
#include "../../Tools/FrameTiming.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace GameSproutRuntime {
namespace {
constexpr auto InvalidTransformIndex=std::numeric_limits<std::size_t>::max();
}

void Renderer::SetDrawTransform(DrawObject& object,const RenderObjectTransform& transform) {
    object.Model=transform.Model;
    object.Normal=transform.Normal;
    object.Negative=transform.Negative;
    object.LocalLightGeneration=0;
    object.ReflectionProbeGeneration=0;
    object.CullBoxKnown=false;
}

bool Renderer::UpdateDrawTransform(DrawObject& draw,const Mat4& model,const RenderFrustum::Frustum& frustum) {
    if(!std::memcmp(draw.Model.Data,model.Data,sizeof(model.Data)))return true;
    return UpdateDrawTransform(draw,RenderObjectTransform::FromModel(model),frustum);
}

bool Renderer::UpdateDrawTransform(DrawObject& draw,const RenderObjectTransform& transform,const RenderFrustum::Frustum& frustum) {
    if(!transform.Valid)return false;
    if(!std::memcmp(draw.Model.Data,transform.Model.Data,sizeof(transform.Model.Data)))return true;
    const bool hadCullBox=_PreparedKnown&&!draw.Skinned&&draw.CullBoxKnown;
    const bool wasInFrustum=hadCullBox&&frustum.Intersects(draw.CullBox);
    const bool wasNegative=draw.Negative;
    SetDrawTransform(draw,transform);
    if(draw.Negative!=wasNegative)_OpaqueDrawOrderKnown=false;
    if(!_InstanceTransformsDirty)_ChangedInstanceTransforms.push_back(draw.InstanceIndex);
    if(hadCullBox) {
        draw.CullBox=RenderFrustum::Box(draw.Geometry->CullMinimum,draw.Geometry->CullMaximum,draw.Model);
        draw.CullBoxKnown=true;
    }
    // Prepared draws borrow this object. Its packets survive a movement that
    // keeps winding, frustum membership and the built-in material path.
    if(_PreparedKnown) {
        if(draw.Negative!=wasNegative||(hadCullBox&&frustum.Intersects(draw.CullBox)!=wasInFrustum)) {
            _PreparedKnown=false;
        } else {
            const auto* materials=draw.MaterialCache.data();
            for(std::size_t i=0;i<draw.MaterialCache.size();++i)
                if(materials[i]&&!materials[i]->Data.Shader.Guid.empty()) {
                    _PreparedKnown=false;
                    break;
                }
        }
    }
    return true;
}

bool Renderer::UpdateDrawMesh(DrawObject& draw,const IGameObject& node) {
    const auto* mesh=node.GetComponent<IMeshComponent>();
    if(!mesh)return false;
    if(draw.Mesh==mesh->GetMesh())return true;
    draw.Mesh=mesh->GetMesh();
    if(draw.Skinned)_SkinPaletteLayoutDirty=true;
    draw.Geometry=nullptr;
    draw.BoundsInitialized=false;
    draw.CullBoxKnown=false;
    draw.SkinPoseReady=false;
    draw.SkinOutsideFrustum=false;
    draw.SkinMatrices.clear();
    draw.LocalLightGeneration=0;
    draw.ReflectionProbeGeneration=0;
    _PreparedKnown=false;
    _OpaqueDrawOrderKnown=false;
    return true;
}

bool Renderer::UpdateDrawMaterials(DrawObject& draw,const IGameObject& node) {
    const auto* renderer=dynamic_cast<const MeshRendererComponent*>(node.GetComponent<IMeshRendererComponent>());
    if(!renderer)return false;
    const auto& bindings=renderer->GetMaterialBindings();
    if(draw.Materials==bindings)return true;
    // Prepared packets borrow individual entries from the binding vector.
    // The geometry, transforms and skin palette remain resident.
    _PreparedKnown=false;
    _OpaqueDrawOrderKnown=false;
    draw.Materials=bindings;
    draw.MaterialCache.assign(bindings.size(),nullptr);
    return true;
}

bool Renderer::UpdateSceneObjects(const Scene& scene,const std::vector<RenderObjectChange>& changes,std::vector<DrawObject>& objects) {
    GAMESPROUT_FRAME_SCOPE(Runtime, "render/extract_update_journal");
    GAMESPROUT_FRAME_COUNT(Runtime, "render/scene_change_entries", changes.size());
    std::vector<SceneTransformBranch*> pending;
    const auto revision=scene.GetRenderRevision();
    const RenderFrustum::Frustum preparedFrustum(_PreparedViewProjection);
    const bool shareTransforms=_ResourceCache.Leases>1||_Runtime._RenderResourceCaches.size()>1;
    auto* draws=objects.data();
    for(const auto& change:changes) {
        const auto id=change.ObjectID;
        if(change.Kind!=RenderObjectChangeKind::Transform) {
            const auto found=_SceneObjectIndices.find(id);
            if(found==_SceneObjectIndices.end()||found->second==InvalidTransformIndex)continue;
            const auto* node=_Runtime.QueryGameObject(id);
            if(!node||node->GetScene()!=&scene)return false;
            auto& draw=draws[found->second];
            if(change.Kind==RenderObjectChangeKind::Mesh?!UpdateDrawMesh(draw,*node):!UpdateDrawMaterials(draw,*node))return false;
            // Resource lookups may invalidate the scene cache before the next change.
            if(!scene.HasSameRenderStructure(revision))return false;
            continue;
        }
        const auto rootBranch=_SceneTransformBranches.find(id);
        if(rootBranch==_SceneTransformBranches.end()||rootBranch->second.VisitedRevision==revision)continue;
        // Extract verified that the scene structure is unchanged. Commands
        // queued during rendering cannot release a node until Render returns.
        const auto* root=rootBranch->second.Node;
        if(!_Runtime.IsInScene(root))return false;
        pending.push_back(&rootBranch->second);
        while(!pending.empty()) {
            auto* branch=pending.back();pending.pop_back();
            const auto* node=branch->Node;
            branch->VisitedRevision=revision;
            if(branch->DrawIndex) {
                // Crossing the singular-transform boundary changes extraction
                // membership and diagnostics. Rebuild only for that case.
                if(*branch->DrawIndex==InvalidTransformIndex)return false;
                auto& draw=draws[*branch->DrawIndex];
                if(shareTransforms) {
                    auto*& shared=branch->SharedTransform;
                    if(!shared)shared=scene.BindRenderObjectTransform(*node);
                    else if(shared->Revision!=scene.GetRenderRevision()&&!scene.RefreshRenderObjectTransform(*shared,*node))return false;
                    if(!shared||!UpdateDrawTransform(draw,shared->Value,preparedFrustum))return false;
                } else if(!UpdateDrawTransform(draw,node->Object->GetTransform()->GetPresentationWorldMatrix(),preparedFrustum))return false;
            }
            for(auto* child=branch->FirstChild;child;child=child->NextSibling)
                if(child->VisitedRevision!=revision)pending.push_back(child);
        }
    }
    return true;
}

bool Renderer::RefreshDrawObjects(const Scene& scene,std::vector<DrawObject>& objects,bool meshChanges,bool materialChanges) {
    GAMESPROUT_FRAME_SCOPE(Runtime, "render/extract_refresh_draws");
    // A burst of animation may outgrow the bounded journal without changing
    // membership. Read only previously extracted renderables and keep their
    // resources, skin data and batch slots attached to the existing objects.
    const RenderFrustum::Frustum preparedFrustum(_PreparedViewProjection);
    const bool shareTransforms=_ResourceCache.Leases>1||_Runtime._RenderResourceCaches.size()>1;
    for(auto& draw:objects) {
        const GameObjectState* node=nullptr;
        if(!shareTransforms||meshChanges||materialChanges) {
            node=_Runtime.Find(draw.ObjectID);
            if(!_Runtime.IsInScene(node)||node->SceneOwner!=&scene||(meshChanges&&!UpdateDrawMesh(draw,*node->Object))||
               (materialChanges&&!UpdateDrawMaterials(draw,*node->Object)))return false;
        }
        if(shareTransforms) {
            const auto* transform=scene.GetRenderObjectTransform(draw.ObjectID,node);
            if(!transform||!UpdateDrawTransform(draw,*transform,preparedFrustum))return false;
        } else if(!UpdateDrawTransform(draw,node->Object->GetTransform()->GetPresentationWorldMatrix(),preparedFrustum))return false;
    }
    if(_SceneObjectIndices.size()!=objects.size()) {
        for(const auto& [id,index]:_SceneObjectIndices)if(index==InvalidTransformIndex) {
            const auto* transform=scene.GetRenderObjectTransform(id);
            if(!transform||transform->Valid)return false;
        }
    }
    return true;
}

bool Renderer::RefreshSceneBindings(const Scene& scene) {
    if(_ObjectAuthoringBatch==_Runtime._NextAuthoringBatch)return true;
    // A command batch can release a cached node or component. Rebind by ID
    // before using pointers again. UI-only and unrelated edits keep the draw snapshot.
    for(auto& [id,branch]:_SceneTransformBranches) {
        const auto* node=_Runtime.Find(id);
        if(!node||node->SceneOwner!=&scene)return false;
        branch.Node=node;
    }
    for(auto& skin:_SkinnedDraws) {
        skin.SnapshotSkin=nullptr;
        skin.SnapshotMesh=nullptr;
    }
    _ObjectAuthoringBatch=_Runtime._NextAuthoringBatch;
    return true;
}

bool Renderer::Extract(const RenderView& view,std::vector<DrawObject>& objects) {
    const auto sceneID=view.Scene->GetInstanceID();
    const auto& scene=static_cast<const Scene&>(*view.Scene);
    const auto revision=scene.GetRenderRevision();
    const auto mask=view.Camera.Projection.CullingMask;
    if(_ObjectSnapshotKnown&&_ObjectSceneID==sceneID&&_ObjectCullingMask==mask&&
       _ObjectRequiredStaticFlags==view.RequiredStaticFlags&&scene.HasSameRenderStructure(_ObjectSceneRevision)&&
       RefreshSceneBindings(scene)) {
        std::vector<RenderObjectChange> changes;
        if(_ObjectSceneRevision==revision||
           (scene.GetRenderObjectChanges(_ObjectSceneRevision,changes)
                ? UpdateSceneObjects(scene,changes,objects)
                : RefreshDrawObjects(scene,objects,scene.HasRenderMeshChanges(_ObjectSceneRevision),
                    scene.HasRenderMaterialChanges(_ObjectSceneRevision)))) {
            _ObjectSceneRevision=revision;
            _Statistics.SkippedObjects+=_ExtractionSkipped;
            Diagnostic(_ExtractionDiagnostic);
            return true;
        }
    }
    GAMESPROUT_FRAME_SCOPE(Runtime, "render/extract_rebuild");
    _ObjectSnapshotKnown=false;
    _PreparedKnown=false;
    _OpaqueDrawOrderKnown=false;
    _InstanceTransformsDirty=true;
    _ChangedInstanceTransforms.clear();
    _SkinnedDraws.clear();
    _SkinPaletteLayoutDirty=true;
    objects.clear();
    _SceneObjectIndices.clear();
    _SceneTransformBranches.clear();
    const auto skippedBefore=_Statistics.SkippedObjects;
    const auto diagnosticBefore=_Error.size();
    // A lone renderer already owns the previous transform and can compare it
    // directly. Only pay for the scene lookup when another renderer can reuse it.
    const bool shareTransforms=_ResourceCache.Leases>1||_Runtime._RenderResourceCaches.size()>1;
    ++_Statistics.SceneSnapshotRebuilds;
    std::vector<const GameObjectState*> pending{&scene.State()};
    while(!pending.empty()) {
        const auto* node=pending.back();pending.pop_back();
        auto* object=node->Object;
        for(std::size_t index=node->Children.size();index>0;--index)pending.push_back(node->Children[index-1]);
        auto* renderer=object->GetComponent<IMeshRendererComponent>();
        if(!renderer) continue;
        if((object->GetStaticFlags()&view.RequiredStaticFlags)!=view.RequiredStaticFlags)continue;
        auto* mesh=object->GetComponent<IMeshComponent>();
        if(!mesh || !object->IsActiveInHierarchy() || !renderer->IsVisible() || !(view.Camera.Projection.CullingMask&(1u<<object->GetLayer()))) {
            ++_Statistics.SkippedObjects;continue;
        }
        DrawObject data;data.ObjectID=object->GetInstanceID();data.Layer=object->GetLayer();data.Name=object->GetName();
        data.ShadowMode=renderer->GetShadowCastingMode();
        data.ReceiveShadows=renderer->GetReceiveShadows();
        data.Skinned=object->GetComponent<ISkinnedMeshRendererComponent>()!=nullptr;
        data.StaticShadowCaster=!data.Skinned&&
            (object->GetStaticFlags()&StaticFlags::ShadowCasterStatic)!=StaticFlags::None;
        auto* sharedTransform=shareTransforms?scene.BindRenderObjectTransform(*node):nullptr;
        const auto transform=sharedTransform?sharedTransform->Value:[&] {
            const auto* component=object->GetTransform();
            return component?RenderObjectTransform::FromModel(component->GetPresentationWorldMatrix()):RenderObjectTransform{};
        }();
        auto [branchEntry,added]=_SceneTransformBranches.try_emplace(node->ID);
        auto* branch=&branchEntry->second;
        branch->Node=node;
        branch->SharedTransform=sharedTransform;
        branch->DrawIndex=transform.Valid?objects.size():InvalidTransformIndex;
        // Retain just the paths to eligible draws, including singular ones.
        // Map element addresses survive rehashes as the snapshot is built.
        for(auto* parent=node->Parent;added&&parent;parent=parent->Parent) {
            auto [parentEntry,parentAdded]=_SceneTransformBranches.try_emplace(parent->ID);
            auto* parentBranch=&parentEntry->second;
            parentBranch->Node=parent;
            branch->NextSibling=parentBranch->FirstChild;
            parentBranch->FirstChild=branch;
            branch=parentBranch;
            added=parentAdded;
        }
        if(!transform.Valid) {
            _SceneObjectIndices.emplace(data.ObjectID,InvalidTransformIndex);
            ++_Statistics.SkippedObjects;Diagnostic("Skipped singular/nonfinite mesh transform: "+data.Name);continue;
        }
        SetDrawTransform(data,transform);
        data.Mesh=mesh->GetMesh();
        if(objects.size()>=std::numeric_limits<std::uint32_t>::max())
            return Fail("Scene exceeds the renderer instance index range");
        data.InstanceIndex=static_cast<std::uint32_t>(objects.size());
        data.Materials.reserve(renderer->GetMaterialCount());
        for(std::size_t slot=0;slot<renderer->GetMaterialCount();++slot) data.Materials.push_back(renderer->GetMaterial(slot));
        data.MaterialCache.resize(data.Materials.size(),nullptr);
        _SceneObjectIndices.emplace(data.ObjectID,objects.size());
        if(data.Skinned) {
            data.SkinStateIndex=_SkinnedDraws.size();
            _SkinnedDraws.push_back({objects.size(),{}});
        }
        objects.push_back(std::move(data));
    }
    _ObjectSceneID=sceneID;_ObjectSceneRevision=revision;_ObjectCullingMask=mask;
    _ObjectAuthoringBatch=_Runtime._NextAuthoringBatch;
    _ObjectRequiredStaticFlags=view.RequiredStaticFlags;
    _ExtractionSkipped=_Statistics.SkippedObjects-skippedBefore;
    _ExtractionDiagnostic=_Error.substr(diagnosticBefore);
    if(!_ExtractionDiagnostic.empty()&&_ExtractionDiagnostic.front()=='\n')_ExtractionDiagnostic.erase(0,1);
    _ObjectSnapshotKnown=true;
    return true;
}
}
