#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshRendererComponent.h"
#include "Scene.h"
#include "../../Tools/FrameTiming.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>

namespace GameSproutRuntime {
void Renderer::RefreshSkinPoses(const Scene& scene,const RenderFrustum::Frustum& frustum) {
    GAMESPROUT_FRAME_SCOPE(Runtime, "render/skin_pose_and_bounds");
    _HasStorageSkin=false;
    constexpr std::size_t minimumBatch=128,maximumBatch=1024;
    const bool matchingScene=_ObjectSnapshotKnown&&_ObjectSceneID==scene.GetInstanceID();
    auto& workers=_Runtime.GetRenderWorkers();
    if(_SkinnedDraws.size()<minimumBatch||workers.WorkerCount()==0) {
        for(auto& skin:_SkinnedDraws)RefreshSkinPose(skin,scene,matchingScene,frustum);
        return;
    }

    struct WorkItem {
        std::size_t SkinIndex;
        const MeshGPU* Geometry;
        std::span<const Mat4> Palette;
        const Mat4* Model;
        const RenderFrustum::Box* CachedWorld;
        bool CalculateLocal;
        PreparedSkinBounds Result;
    };
    struct WorkBatch {
        std::vector<WorkItem> Items;
        const RenderFrustum::Frustum& Frustum;
    } batch{{},frustum};
    batch.Items.reserve(std::min(maximumBatch,_SkinnedDraws.size()));
    const auto calculate=[](void* context,std::size_t begin,std::size_t end) noexcept {
        auto& work=*static_cast<WorkBatch*>(context);
        auto* items=work.Items.data();
        for(auto i=begin;i<end;++i) {
            auto& item=items[i];
            auto& result=item.Result;
            if(item.CalculateLocal&&!TryConservativeSkinPoseBounds(*item.Geometry,item.Palette,result.Local)) {
                result.NeedsExactFallback=true;
                continue;
            }
            if(!item.CachedWorld)
                result.World.emplace(result.Local.Center-result.Local.Extents,result.Local.Center+result.Local.Extents,*item.Model);
            result.Outside=!work.Frustum.Intersects(item.CachedWorld?*item.CachedWorld:*result.World);
        }
    };
    const auto flush=[&] {
        const bool dispatch=batch.Items.size()>=minimumBatch;
        if(dispatch) {
            GAMESPROUT_FRAME_COUNT(Runtime, "render/skin_bounds_worker_jobs", batch.Items.size());
            GAMESPROUT_FRAME_SCOPE(Runtime, "render/skin_bounds_worker_batch");
            workers.RunBlocks(batch.Items.size(),&batch,calculate);
        }
        for(const auto& item:batch.Items) {
            auto& skin=_SkinnedDraws[item.SkinIndex];
            ApplySkinPose(skin,skin.SnapshotMesh,skin.SnapshotSkin,item.Palette,frustum,dispatch?&item.Result:nullptr);
        }
        batch.Items.clear();
    };

    for(std::size_t i=0;i<_SkinnedDraws.size();++i) {
        auto& skin=_SkinnedDraws[i];
        const auto& object=_SceneObjects[skin.ObjectIndex];
        const auto* mesh=object.Geometry;
        const bool matchingStructure=matchingScene&&_ObjectAuthoringBatch==_Runtime._NextAuthoringBatch&&
            scene.HasSameRenderStructure(_ObjectSceneRevision);
        const auto* source=matchingStructure?skin.SnapshotSkin:nullptr;
        if(!mesh||!source||!skin.SnapshotMesh||source->_SkinPose.empty()||!mesh->HasCompleteSkinBindings()||
           source->_SkinPose.size()!=mesh->Data.Bones.size()||
           !_Runtime.IsInScene(source->_OwnerState)) {
            // Complete earlier draws before resource providers, Animator or custom
            // getters can run. Their callbacks may read or invalidate current data.
            flush();
            RefreshSkinPose(skin,scene,matchingScene,frustum);
            continue;
        }
        // These render-owned values and validated built-in palettes are stable
        // until the synchronous batch is joined. _SkinScratch is never borrowed.
        const std::span<const Mat4> palette(source->_SkinPose);
        const bool samePose=IsSameSkinPose(object,skin,palette,source);
        const auto* cachedWorld=samePose&&object.CullBoxKnown?&object.CullBox:nullptr;
        const bool calculateLocal=!samePose&&!HasCachedSkinBounds(*mesh,source);
        const auto& local=samePose?skin.LocalBounds:source->_SkinPoseBounds;
        batch.Items.push_back({i,mesh,palette,&object.Model,cachedWorld,calculateLocal,{local}});
        if(batch.Items.size()==maximumBatch)flush();
    }
    flush();
}

void Renderer::RefreshSkinPose(SkinnedDrawState& skin,const Scene& scene,bool matchingScene,
    const RenderFrustum::Frustum& frustum) {
    auto& object=_SceneObjects[skin.ObjectIndex];
    if(!object.Geometry)object.Geometry=Mesh(object.Mesh);
    auto* mesh=object.Geometry;
    // Check after resource-provider calls. A command batch can retire an
    // object even when it removes the last world renderer before destruction.
    // The enclosing RuntimeOperation defers frees while this pass is running.
    const bool matchingStructure=matchingScene&&_ObjectAuthoringBatch==_Runtime._NextAuthoringBatch&&
        scene.HasSameRenderStructure(_ObjectSceneRevision);
    if(!matchingStructure) {
        skin.SnapshotSkin=nullptr;
        skin.SnapshotMesh=nullptr;
    }
    auto* snapshotSkin=skin.SnapshotSkin;
    if(snapshotSkin&&snapshotSkin->_SkinPose.empty())snapshotSkin=nullptr;
    auto* node=snapshotSkin?snapshotSkin->_OwnerState->Object:_Runtime.QueryGameObject(object.ObjectID);
    SkinnedMeshRendererComponent* overrideSkin=nullptr;
    const auto palette=mesh&&node?SkinMatrices(*node,*mesh,_SkinScratch,&overrideSkin,snapshotSkin):std::nullopt;
    auto* component=snapshotSkin?skin.SnapshotMesh:
        (node?dynamic_cast<MeshComponent*>(node->GetComponent<IMeshComponent>()):nullptr);
    if(!snapshotSkin&&matchingStructure&&overrideSkin&&component) {
        // Only a validated built-in override takes this path. Custom and
        // animator-provided palettes keep their existing component lookups.
        skin.SnapshotSkin=overrideSkin;
        skin.SnapshotMesh=component;
    }
    ApplySkinPose(skin,component,overrideSkin,palette,frustum);
}

bool Renderer::IsSameSkinPose(const DrawObject& object,const SkinnedDrawState& skin,std::span<const Mat4> palette,
    const SkinnedMeshRendererComponent* overrideSkin) {
    return object.SkinPoseReady&&object.SkinMatrices.size()==palette.size()&&
        (overrideSkin?skin.OverrideRevision!=0&&skin.OverrideRevision==overrideSkin->_SkinPoseRevision:
            (palette.empty()||!std::memcmp(palette.data(),object.SkinMatrices.data(),palette.size_bytes())));
}

bool Renderer::HasCachedSkinBounds(const MeshGPU& mesh,const SkinnedMeshRendererComponent* overrideSkin) {
    return overrideSkin&&mesh.SkinBoundsIdentity&&overrideSkin->_SkinBoundsMeshIdentity==mesh.SkinBoundsIdentity;
}

void Renderer::ApplySkinPose(SkinnedDrawState& skin,MeshComponent* component,SkinnedMeshRendererComponent* overrideSkin,
    std::optional<std::span<const Mat4>> palette,const RenderFrustum::Frustum& frustum,const PreparedSkinBounds* prepared) {
    auto& object=_SceneObjects[skin.ObjectIndex];
    const auto* mesh=object.Geometry;
    const auto previousBoneCount=object.SkinMatrices.size();
    const bool usePrepared=prepared&&!prepared->NeedsExactFallback;
    bool ready=palette.has_value();
    if(ready) {
        if(!IsSameSkinPose(object,skin,*palette,overrideSkin)) {
            if(HasCachedSkinBounds(*mesh,overrideSkin)) {
                skin.LocalBounds=overrideSkin->_SkinPoseBounds;
            } else {
                if(usePrepared)skin.LocalBounds=prepared->Local;
                else ready=SkinPoseBounds(*mesh,*palette,skin.LocalBounds);
                if(ready&&overrideSkin) {
                    overrideSkin->_SkinBoundsMeshIdentity=mesh->SkinBoundsIdentity;
                    overrideSkin->_SkinPoseBounds=skin.LocalBounds;
                }
            }
            if(ready) {
                if(overrideSkin) {
                    object.SkinMatrices.resize(palette->size());
                    std::copy_n(palette->data(),palette->size(),object.SkinMatrices.data());
                } else object.SkinMatrices.swap(_SkinScratch);
                object.SkinUploadDirty=true;
                object.CullBoxKnown=false;
            }
        }
        if(ready&&component&&component->_Mesh==object.Mesh)component->_Bounds=skin.LocalBounds;
    } else if(!mesh&&component&&component->_Mesh==object.Mesh)component->_Bounds={};
    if(object.SkinPoseReady!=ready) {
        _PreparedKnown=false;
        _OpaqueDrawOrderKnown=false;
    }
    object.SkinPoseReady=ready;
    skin.OverrideRevision=overrideSkin?overrideSkin->_SkinPoseRevision:0;
    if(!ready) {object.SkinMatrices.clear();object.CullBoxKnown=false;}
    if(previousBoneCount!=object.SkinMatrices.size())_SkinPaletteLayoutDirty=true;
    if(!ready)return;
    _HasStorageSkin|=RequiresSkinStorage(object);
    // Bounds are derived from the current skin palette, including vertices
    // that move across the frustum independently of their object transform.
    // The shadow passes still read every caster's palette and pose bounds.
    if(!object.CullBoxKnown) {
        const auto& bounds=skin.LocalBounds;
        object.CullBox=usePrepared?*prepared->World:
            RenderFrustum::Box(bounds.Center-bounds.Extents,bounds.Center+bounds.Extents,object.Model);
        object.CullBoxKnown=true;
    }
    const bool outside=usePrepared?prepared->Outside:!frustum.Intersects(object.CullBox);
    if(outside!=object.SkinOutsideFrustum)_PreparedKnown=false;
    object.SkinOutsideFrustum=outside;
}

void Renderer::PrepareDraws(const RenderView& view,const Mat4& viewProjection) {
    // The color list can be kept as long as the sampled poses stay on the
    // same side of the view frustum. Planar captures have their own frustum.
    const RenderFrustum::Frustum frustum(viewProjection);
    RefreshSkinPoses(static_cast<const Scene&>(*view.Scene),frustum);
    const auto& position=view.Camera.Position;
    const auto& current=view.Camera.Projection;const auto& previous=_PreparedCamera.Projection;
    if(_PreparedKnown&&
       !std::memcmp(_PreparedViewProjection.Data,viewProjection.Data,sizeof(viewProjection.Data))&&
       !std::memcmp(_PreparedCamera.View.Data,view.Camera.View.Data,sizeof(view.Camera.View.Data))&&
       current.Projection==previous.Projection&&current.VerticalFovRadians==previous.VerticalFovRadians&&
       current.OrthographicHeight==previous.OrthographicHeight&&current.NearPlane==previous.NearPlane&&
       current.FarPlane==previous.FarPlane&&current.CullingMask==previous.CullingMask&&
       position.X==_PreparedCamera.Position.X&&position.Y==_PreparedCamera.Position.Y&&position.Z==_PreparedCamera.Position.Z) {
        _Statistics.VisibleObjects+=_PreparedVisible;_Statistics.SkippedObjects+=_PreparedSkipped;
        Diagnostic(_PreparedDiagnostic);return;
    }
    // DrawObject/material addresses are stable until Extract or SyncRevision
    // invalidates this list. Camera movement rebuilds visibility and transparent sorting.
    _PreparedKnown=false;_PreparedDraws.clear();_ColorInstanceBatchesKnown=false;
    const auto visibleBefore=_Statistics.VisibleObjects,skippedBefore=_Statistics.SkippedObjects;
    const auto diagnosticBefore=_Error.size();
    auto& objects=_SceneObjects;
    static const ResourceReference defaultMaterialReference;
    auto& visible=_VisibleDrawScratch;
    visible.clear();
    _VisibleTransparentSlots.clear();
    constexpr auto invisible=std::numeric_limits<std::size_t>::max();
    const bool rebuildOpaqueOrder=!_OpaqueDrawOrderKnown;
    std::vector<PreparedDraw> opaqueSections;
    _VisibleOpaqueSlots.clear();
    if(!rebuildOpaqueOrder)_VisibleOpaqueSlots.resize(_OpaqueDrawOrder.size(),invisible);
    std::size_t nextOpaqueSlot=0;
    bool hasOpaqueShader=false;
    for(auto& object:objects) {
        if(object.Skinned&&!object.SkinPoseReady) {++_Statistics.SkippedObjects;continue;}
        if(!object.Geometry)object.Geometry=Mesh(object.Mesh);
        auto* mesh=object.Geometry;
        // 更新已加载资源的派生 bounds；不修改节点属性、主相机、Undo 或源场景快照。
        if(!object.Skinned&&!object.BoundsInitialized)if(auto* node=_Runtime.QueryGameObject(object.ObjectID)) {
            if(auto* component=dynamic_cast<MeshComponent*>(node->GetComponent<IMeshComponent>());
               component && component->_Mesh==object.Mesh) {
                component->_Bounds=mesh?mesh->Data.LocalBounds:Bounds{};
                object.BoundsInitialized=true;
            }
        }
        if(!mesh) {++_Statistics.SkippedObjects;continue;}
        bool outside=object.Skinned&&object.SkinOutsideFrustum;
        if(!object.Skinned&&mesh->CullBoundsKnown) {
            if(!object.CullBoxKnown) {
                object.CullBox=RenderFrustum::Box(mesh->CullMinimum,mesh->CullMaximum,object.Model);
                object.CullBoxKnown=true;
            }
            outside=!frustum.Intersects(object.CullBox);
        }
        const bool shadowsOnly=object.ShadowMode==ShadowCastingMode::ShadowsOnly;
        bool drawn=false,skipped=false;
        for(const auto& section:mesh->Data.Sections) {
            if(!section.IndexCount) continue;
            const MaterialRenderData* material=&_DefaultMaterial;
            Material* cachedMaterial=nullptr;
            const ResourceReference* materialReference=&defaultMaterialReference;
            if(section.MaterialSlot<object.Materials.size()) {
                const auto& reference=object.Materials[section.MaterialSlot];
                if(!reference.Guid.empty() || reference.LocalID) {
                    auto*& cached=object.MaterialCache[section.MaterialSlot];
                    if(!cached)cached=MaterialData(reference);
                    if(!cached) {skipped=true;continue;}
                    cachedMaterial=cached;material=&cached->Data;materialReference=&reference;
                }
            }
            // The shadow pass still needs the resolved geometry and materials.
            if(shadowsOnly)continue;
            // The main viewport owns the planar pass and accepts only explicit
            // receivers. Exclude them from its mirrored view.
            if(_PlanarShadowSource&&material->PlanarReflection)continue;
            const bool builtin=material->Shader.Guid.empty();
            const auto sectionOrdinal=static_cast<std::uint64_t>(&section-mesh->Data.Sections.data());
            auto opaqueSlot=invisible;
            if(builtin) {
                opaqueSlot=nextOpaqueSlot++;
                if(rebuildOpaqueOrder) {
                    opaqueSections.push_back({&object,mesh,section,material,materialReference,nullptr,nullptr,
                        cachedMaterial,nullptr,false,0,sectionOrdinal});
                    _VisibleOpaqueSlots.push_back(invisible);
                }
            }
            // User vertex shaders have no displacement-bounds contract yet;
            // only cull built-in materials with known vertex bounds.
            if(outside&&cachedMaterial&&_TexturePrefetchEnabled&&!cachedMaterial->TexturePrefetchConsidered) {
                // A nearby object just outside the view can appear on the
                // next turn. Prefetch its material once per resource revision.
                constexpr float prefetchDistanceSquared=80.0f*80.0f;
                const Vector3 difference{object.Model(0,3)-position.X,object.Model(1,3)-position.Y,object.Model(2,3)-position.Z};
                if(difference.X*difference.X+difference.Y*difference.Y+difference.Z*difference.Z<=prefetchDistanceSquared)
                    PredictMaterialTextures(*cachedMaterial);
            }
            if(outside&&builtin){skipped=true;continue;}
            TextureGPU* texture=nullptr;TextureGPU* normalTexture=nullptr;
            ShaderGPU* shader=nullptr;bool transparent=false;
            if(!builtin) {
                if(object.Skinned){Diagnostic("User Shader currently supports static meshes; skinned Shader variants are not implemented");skipped=true;continue;}
                shader=Shader(material->Shader);
                if(!shader||!PrepareShaderMaterial(*cachedMaterial,*shader)||!EnsureShaderVertices(*mesh,object.Mesh)){skipped=true;continue;}
                transparent=shader->Data.Passes[0].Blend!=ShaderBlendMode::Opaque;
            }else if(mesh->Data.UVOffset>=0 && !material->BaseColorTexture.Guid.empty()) {
                bool pending=false;
                texture=MaterialTexture(*cachedMaterial,false,pending);
                if(!texture&&!pending) {skipped=true;continue;}
            }
            if(material->ShadingModel==MaterialShadingModel::Lit&&!material->NormalTexture.Guid.empty()) {
                if(mesh->Data.UVOffset<0){Diagnostic("Lit normal map requires mesh UV coordinates");skipped=true;continue;}
                bool pending=false;
                normalTexture=MaterialTexture(*cachedMaterial,true,pending);
                if(!normalTexture&&!pending){skipped=true;continue;}
                if(normalTexture&&normalTexture->SRGB){Diagnostic("Lit normal map must be imported in Linear color space");skipped=true;continue;}
            }
            float distance=0;
            if(transparent) {
                const Vector3 difference{object.Model(0,3)-position.X,object.Model(1,3)-position.Y,object.Model(2,3)-position.Z};
                distance=difference.X*difference.X+difference.Y*difference.Y+difference.Z*difference.Z;
                _VisibleTransparentSlots.push_back(visible.size());
            }else if(builtin) {
                _VisibleOpaqueSlots[opaqueSlot]=visible.size();
            }else hasOpaqueShader=true;
            visible.push_back({&object,mesh,section,material,materialReference,texture,normalTexture,cachedMaterial,
                shader,transparent,distance,sectionOrdinal});
            drawn=true;
        }
        if(drawn) ++_Statistics.VisibleObjects;
        if(skipped || (!drawn&&!shadowsOnly)) ++_Statistics.SkippedObjects;
    }
    RefreshDrawSortRanks();
    if(rebuildOpaqueOrder) {
        // Build from the scene's whole opaque section list. Filtering it by
        // visibility preserves state order and the original order of ties.
        _OpaqueDrawOrder.resize(opaqueSections.size());
        std::iota(_OpaqueDrawOrder.begin(),_OpaqueDrawOrder.end(),std::size_t{0});
        std::stable_sort(_OpaqueDrawOrder.begin(),_OpaqueDrawOrder.end(),[&](auto left,auto right) {
            const auto& a=opaqueSections[left];
            const auto& b=opaqueSections[right];
            if(a.Material->SortingPriority!=b.Material->SortingPriority)
                return a.Material->SortingPriority<b.Material->SortingPriority;
            return OpaqueDrawBefore(a,b);
        });
        _OpaqueDrawOrderKnown=true;
    }
    if(hasOpaqueShader) {
        // An opaque custom shader is an order barrier among draws of its
        // priority. Keep original traversal order on either side of it.
        SortPrepared(visible);
        _PreparedDraws.swap(visible);
    }else {
        _PreparedDraws.reserve(visible.size());
        for(const auto slot:_OpaqueDrawOrder) {
            const auto index=_VisibleOpaqueSlots[slot];
            if(index!=invisible)_PreparedDraws.push_back(visible[index]);
        }
        std::stable_sort(_VisibleTransparentSlots.begin(),_VisibleTransparentSlots.end(),[&](auto left,auto right) {
            return PriorityAndDepthBefore(visible[left],visible[right]);
        });
        for(const auto slot:_VisibleTransparentSlots)_PreparedDraws.push_back(visible[slot]);
    }
    _PreparedViewProjection=viewProjection;_PreparedCamera=view.Camera;
    _PreparedVisible=_Statistics.VisibleObjects-visibleBefore;_PreparedSkipped=_Statistics.SkippedObjects-skippedBefore;
    _PreparedDiagnostic=_Error.substr(diagnosticBefore);
    if(!_PreparedDiagnostic.empty()&&_PreparedDiagnostic.front()=='\n')_PreparedDiagnostic.erase(0,1);
    _PreparedKnown=true;
}
}
