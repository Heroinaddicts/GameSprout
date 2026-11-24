#include "RuntimeRenderer.h"
#include "Components/LightComponent.h"
#include "GameSproutRuntime.h"
#include "Scene.h"
#include <algorithm>
#include <bit>
#include <cstring>

namespace GameSproutRuntime {
namespace {
struct SpotPassScope {
    GRHI::ICommandBuffer& Command;
    GRHI::IRenderPass* Pass;
    ~SpotPassScope(){if(Pass)Command.EndRenderPass(Pass);}
};

bool SpotProjection(Vector3 position,Vector3 forward,float angle,float nearPlane,float farPlane,Mat4& result) {
    const Vector3 up=std::abs(forward.Y)>.99f?Vector3{0,0,1}:Vector3{0,1,0};
    Mat4 view,projection;
    if(!TryMakeLookAt({},forward,up,view)||!TryMakePerspective(angle,1,nearPlane,farPlane,projection))return false;
    for(std::size_t row=0;row<3;++row)
        view(row,3)=-Vector3{view(row,0),view(row,1),view(row,2)}.Dot(position);
    result=projection*view;
    return std::all_of(std::begin(result.Data),std::end(result.Data),[](float value){return std::isfinite(value);});
}
}

bool Renderer::RenderSpotShadows(GRHI::ICommandBuffer& command,const RenderView& view,const Mat4& viewProjection) {
    const auto previousCount=_SpotShadowCount;
    _SpotShadowCount=0;
    const auto& settings=view.Scene->GetEnvironmentLighting().SpotShadows;
    const auto maxLights=std::min(view.MaxSpotShadowLights,settings.MaxLights);
    const auto maxResolution=std::min(view.MaxSpotShadowResolution,settings.MaxResolution);
    if(!maxLights||!maxResolution) {
        delete _SpotShadowAtlas;_SpotShadowAtlas=nullptr;_SpotShadowValid=false;
        return true;
    }
    if(_LocalLights.empty())return true;

    struct Choice {std::size_t Index;double DistanceSquared;};
    std::vector<Choice> choices;
    const RenderFrustum::Frustum cameraFrustum(viewProjection);
    for(std::size_t index=0;index<_LocalLights.size();++index) {
        const auto& light=_LocalLights[index];
        if(!_VisibleLocalShadowReceivers[index]||light.Type!=LightType::Spot||!light.Shadows.Enabled||light.Shadows.Strength==0||
            !(light.CullingMask&view.Camera.Projection.CullingMask))continue;
        const auto delta=light.Position-view.Camera.Position;
        const auto distanceSquared=double(delta.X)*delta.X+double(delta.Y)*delta.Y+double(delta.Z)*delta.Z;
        const auto reach=double(light.Range)+light.Shadows.Distance;
        if(distanceSquared>=reach*reach)continue;
        Mat4 model;model(0,3)=light.Position.X;model(1,3)=light.Position.Y;model(2,3)=light.Position.Z;
        const Vector3 extent{light.Range,light.Range,light.Range};
        if(!cameraFrustum.Intersects(extent*-1,extent,model))continue;
        choices.push_back({index,distanceSquared});
    }
    if(choices.empty())return true;
    const auto budget=std::min<std::size_t>(maxLights,MaxSpotShadows);
    const auto count=std::min(choices.size(),budget);
    std::partial_sort(choices.begin(),choices.begin()+count,choices.end(),[this](const Choice& a,const Choice& b) {
        const auto& x=_LocalLights[a.Index];const auto& y=_LocalLights[b.Index];
        if(x.Shadows.Priority!=y.Shadows.Priority)return x.Shadows.Priority>y.Shadows.Priority;
        if(a.DistanceSquared!=b.DistanceSquared)return a.DistanceSquared<b.DistanceSquared;
        return x.ObjectID<y.ObjectID;
    });
    choices.resize(count);
    // Keep atlas locations stable while importance changes but membership stays the same.
    std::sort(choices.begin(),choices.end(),[this](const Choice& a,const Choice& b) {
        return _LocalLights[a.Index].ObjectID<_LocalLights[b.Index].ObjectID;
    });

    std::array<SpotShadowCacheKey,MaxSpotShadows> keys{};
    const auto resolutionLimit=std::bit_floor(maxResolution);
    std::uint32_t tileStride=0;
    for(const auto& choice:choices) {
        const auto& light=_LocalLights[choice.Index];
        auto& key=keys[_SpotShadowCount];
        if(!SpotProjection(light.Position,light.Forward,light.OuterAngle,light.Shadows.NearPlane,light.Range,key.Projection))continue;
        key.ObjectID=light.ObjectID;key.ComponentID=light.ComponentID;
        const auto* object=_Runtime.QueryGameObject(light.ObjectID);
        const auto* component=object?static_cast<const LightComponent*>(object->GetComponent<ILightComponent>()):nullptr;
        key.Refresh=component?component->GetShadowRefreshRevision():0;
        key.Edge=std::min(light.Shadows.Resolution,resolutionLimit);key.CullingMask=light.CullingMask;
        tileStride=std::max(tileStride,key.Edge);
        _SpotShadowIndices[_SpotShadowCount++]=static_cast<std::uint32_t>(choice.Index+1);
    }
    if(!_SpotShadowCount)return true;
    _Statistics.SpotShadowLights=static_cast<std::uint32_t>(_SpotShadowCount);
    const auto columns=_SpotShadowCount>1?2u:1u;
    const auto width=tileStride*columns,height=tileStride*(_SpotShadowCount>2?2u:1u);
    const bool resize=!_SpotShadowAtlas||_SpotShadowAtlas->GetWidth()!=width||_SpotShadowAtlas->GetHeight()!=height;
    if(resize) {
        GRHI::TextureDesc description;
        description.Width=width;description.Height=height;
        description.Format=GRHI::TextureFormat::D32_Float;
        description.Usage=GRHI::TextureUsage::DepthStencilTarget|GRHI::TextureUsage::Sampled;
        auto* replacement=_Device.CreateTexture(description);
        if(!replacement)return Fail(DeviceError("Create sampled spot shadow atlas"));
        delete _SpotShadowAtlas;_SpotShadowAtlas=replacement;_SpotShadowValid=false;
    }
    if(!_ShadowSampler) {
        GRHI::SamplerDesc sampler;
        sampler.MinFilter=sampler.MagFilter=GRHI::FilterMode::Nearest;
        _ShadowSampler=_Device.CreateSampler(sampler);
        if(!_ShadowSampler)return Fail(DeviceError("Create spot shadow sampler"));
    }

    struct Tile {
        GRHI::Viewport Viewport;
        std::vector<ShadowCandidate> Casters;
        std::vector<InstanceDraw> Batches;
        bool Dirty=false;
    };
    std::array<Tile,MaxSpotShadows> tiles;
    const auto sceneID=view.Scene->GetInstanceID();
    const auto revision=static_cast<const Scene&>(*view.Scene).GetRenderRevision();
    const auto cameraMask=view.Camera.Projection.CullingMask;
    const bool validAtlas=_SpotShadowValid&&_SpotShadowSceneID==sceneID&&_SpotShadowTileStride==tileStride&&
        ((_SpotShadowCount>1)==(previousCount>1));
    const bool sameScene=validAtlas&&_SpotShadowSceneRevision==revision&&_SpotShadowViewMask==cameraMask;
    bool hasSkinned=sameScene&&_SpotShadowHasSkinned;
    _SpotShadowInstances.Indices.clear();
    for(std::size_t slot=0;slot<_SpotShadowCount;++slot) {
        const auto& light=_LocalLights[_SpotShadowIndices[slot]-1];const auto& shadow=light.Shadows;
        const auto& key=keys[slot];const auto& previous=_SpotShadowKeys[slot];
        auto& tile=tiles[slot];
        const auto x=std::uint32_t(slot%columns)*tileStride,y=std::uint32_t(slot/columns)*tileStride;
        tile.Viewport={float(x),float(y),float(key.Edge),float(key.Edge),0,1};
        const float footprint=2*std::tan(light.OuterAngle*.5f)/key.Edge;
        const float depthFactor=shadow.NearPlane/(1-shadow.NearPlane/light.Range);
        _SpotShadowUniform.WorldToShadow[slot]=key.Projection;
        _SpotShadowUniform.Atlas[slot]={float(key.Edge)/width,float(key.Edge)/height,float(x)/width,float(y)/height};
        _SpotShadowUniform.Parameters[slot]={shadow.Strength,shadow.DepthBias*footprint*depthFactor,
            shadow.NormalBias*footprint,shadow.Soft?float(static_cast<unsigned>(shadow.FilterQuality)+1):0};
        _SpotShadowUniform.Distance[slot]={shadow.Distance,shadow.FadeStart*shadow.Distance,1.f/width,1.f/height};
        const bool sameLight=validAtlas&&slot<previousCount&&key.ObjectID==previous.ObjectID&&
            key.ComponentID==previous.ComponentID&&key.Refresh==previous.Refresh&&key.Edge==previous.Edge&&
            key.CullingMask==previous.CullingMask&&std::memcmp(key.Projection.Data,previous.Projection.Data,sizeof(key.Projection.Data))==0;
        if(sameScene&&sameLight&&!_SpotShadowHasSkinned)continue;

        bool skinned=false;
        const RenderFrustum::Frustum frustum(key.Projection);
        std::vector<StaticShadowCasterKey> casters;
        for(const auto& object:_SceneObjects) {
            if(object.ShadowMode==ShadowCastingMode::Off||!object.Geometry||
                !(light.CullingMask&(1u<<object.Layer))||(object.Skinned&&!object.SkinPoseReady))continue;
            auto* mesh=object.Geometry;
            if(!frustum.Intersects(LocalLightObjectBounds(object,*mesh)))continue;
            for(const auto& section:mesh->Data.Sections) {
                if(!section.IndexCount)continue;
                const MaterialRenderData* material=&_DefaultMaterial;
                if(section.MaterialSlot<object.MaterialCache.size()) {
                    const auto* cached=object.MaterialCache[section.MaterialSlot];
                    if(cached)material=&cached->Data;
                    else if(!object.Materials[section.MaterialSlot].Guid.empty())continue;
                }
                if(!material->Shader.Guid.empty())continue;
                PreparedDraw draw;
                draw.Object=&object;draw.Mesh=mesh;draw.Section=section;draw.Material=material;
                draw.ShadowDoubleSided=material->DoubleSided||object.ShadowMode==ShadowCastingMode::TwoSided;
                tile.Casters.push_back({draw,{}});
                casters.push_back({object.ObjectID,mesh,object.Model,{},section.FirstIndex,section.IndexCount,draw.ShadowDoubleSided});
                skinned|=object.Skinned;
            }
        }
        hasSkinned|=skinned;
        tile.Dirty=!sameLight||skinned||_SpotShadowCasters[slot]!=casters;
        _SpotShadowCasters[slot]=std::move(casters);
        if(!tile.Dirty)continue;
        SortShadowCandidates(tile.Casters);
        std::vector<PreparedDraw> sorted;
        sorted.reserve(tile.Casters.size());
        for(const auto& caster:tile.Casters)sorted.push_back(caster.Draw);
        PrepareInstanceBatches(sorted,view.EnableGPUInstancing,true,tile.Batches,_SpotShadowInstances.Indices);
    }
    _SpotShadowKeys=keys;_SpotShadowSceneID=sceneID;_SpotShadowSceneRevision=revision;
    _SpotShadowViewMask=cameraMask;_SpotShadowHasSkinned=hasSkinned;_SpotShadowTileStride=tileStride;
    if(std::none_of(tiles.begin(),tiles.end(),[](const Tile& tile){return tile.Dirty;}))return true;

    _SpotShadowValid=false;
    if(!UploadInstanceStream(command,_SpotShadowInstances))return false;
    GRHI::RenderPassDesc description;
    description.DepthTarget=_SpotShadowAtlas->GetDepthTargetView();
    description.DepthLoad=validAtlas?GRHI::LoadOp::Load:GRHI::LoadOp::Clear;
    description.DepthStore=GRHI::StoreOp::Store;
    description.ClearDepth=1;
    auto* pass=command.BeginRenderPass(description);
    if(!pass)return Fail(DeviceError("Begin depth-only spot shadow atlas"));
    SpotPassScope scope{command,pass};
    const auto startDraws=_Statistics.ShadowDrawCalls;
    for(std::size_t slot=0;slot<_SpotShadowCount;++slot) {
        const auto& tile=tiles[slot];
        if(!tile.Dirty)continue;
        pass->SetViewport(tile.Viewport);
        if(validAtlas&&!ClearShadowRegion(*pass))return false;
        ++_Statistics.SpotShadowTilesUpdated;
        for(const auto& batch:tile.Batches)
            if(!DrawShadow(*pass,tile.Casters[batch.First].Draw,keys[slot].Projection,batch.IndexOffset,batch.Count,
                batch.Count>1?&_SpotShadowInstances:nullptr))return false;
    }
    command.EndRenderPass(pass);scope.Pass=nullptr;
    _Statistics.SpotShadowDrawCalls=_Statistics.ShadowDrawCalls-startDraws;
    ++_Statistics.ShadowMapUpdates;
    _SpotShadowRecorded=true;
    return true;
}
}
