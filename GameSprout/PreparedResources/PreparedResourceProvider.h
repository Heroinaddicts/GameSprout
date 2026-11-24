#pragma once

#include "PreparedResourceFormat.h"
#include "RuntimeResourceApi.h"
#include <filesystem>
#include <fstream>

namespace PreparedResources {
// Both hosts consume the same artifact layouts. Their indexes and invalidation
// rules stay in the provider adapter, outside the payload decoders.
class ResourceProvider : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    bool LoadMesh(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::MeshRenderData&, std::string&) override;
    bool LoadEnvironment(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::EnvironmentRenderData&,std::string&) override;
    bool LoadIndirectLighting(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::IndirectLightingRenderData&,std::string&) override;
    bool LoadTexture(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::TextureRenderData&, std::string&) override;
    bool ResolveTexturePrefetch(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::TexturePrefetchSource&) override;
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::MaterialRenderData&, std::string&) override;
    bool LoadShader(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::ShaderRenderData&, std::string&) override;
    bool LoadPhysicsMaterial(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::PhysicsMaterialData&, std::string&) override;
    bool LoadModel(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::ModelData&, std::string&) override;
    bool LoadPrefab(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::PrefabData&, std::string&) override;
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::SkeletonData&, std::string&) override;
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AnimationClipData&, std::string&) override;
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AnimatorControllerData&, std::string&) override;
    bool LoadHumanoidAvatar(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::HumanoidAvatarData&, std::string&) override;
    bool LoadAvatarMask(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AvatarMaskData&, std::string&) override;
    bool LoadFont(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::FontRenderData&, std::string&) override;
    bool LoadMeshCollision(const GameSproutRuntime::ResourceReference&, bool, std::vector<std::uint8_t>&, std::string&) override;
protected:
    struct Artifact {
        std::filesystem::path Path;
        std::uint64_t Size = 0;
        Json::Value Descriptor;
    };
    virtual bool LocateArtifact(const GameSproutRuntime::ResourceReference&, eAssetType, Artifact&, std::string&) = 0;
    virtual bool IsMissing(const GameSproutRuntime::ResourceReference&) = 0;
    virtual void MarkReadFailed(const GameSproutRuntime::ResourceReference&, bool) {}
    virtual void ResetDependencies(const GameSproutRuntime::ResourceReference&) {}
    virtual void AddDependency(const GameSproutRuntime::ResourceReference&, const GameSproutRuntime::ResourceReference&) {}
    bool OpenArtifact(const GameSproutRuntime::ResourceReference&, eAssetType, Artifact&, std::ifstream&, std::string&);
    bool Read(const GameSproutRuntime::ResourceReference&, eAssetType, Json::Value&, std::vector<std::uint8_t>&, std::string&);
};
}
