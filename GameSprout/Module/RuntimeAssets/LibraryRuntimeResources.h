#pragma once
#include "../../PreparedResources/PreparedResourceProvider.h"
#include "IAssetsManager.h"
#include <map>
#include <set>
#include <functional>

class ILibraryManager;

// 编辑器适配层：查询 ArtifactDB 的布局描述，读取 Library 已预处理的产物。
// Runtime 本身不依赖编辑器模块、Qt、源文件格式或数据库。
class LibraryRuntimeResources final : public PreparedResources::ResourceProvider, public IAssetListener {
public:
    LibraryRuntimeResources(IAssetsManager* assets, ILibraryManager* library);
    ~LibraryRuntimeResources() override;
    std::uint64_t GetRevision() const override { return _Revision; }
    std::uint64_t GetResourceRevision(const GameSproutRuntime::ResourceReference&) const override;
    std::function<void()> ResourcesChanged;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
private:
    struct ResourceKey {
        std::string Guid;
        std::uint64_t LocalID=0;
        bool operator<(const ResourceKey& other) const {
            return Guid<other.Guid||(Guid==other.Guid&&LocalID<other.LocalID);
        }
        bool operator==(const ResourceKey&) const = default;
    };
    struct ResourceSignature {
        eAssetType Type=eAssetType::Unknown;
        std::uint32_t DataVersion=0;
        std::uint64_t DataSize=0;
        std::string Hash,DescriptorJson;
        bool operator==(const ResourceSignature&) const = default;
    };
    struct ResourceState {
        ResourceSignature Signature;
        std::uint64_t Revision=1;
        bool Present=false;
        bool ReadFailed=false;
        std::set<ResourceKey> Dependencies;
    };
    struct ObservedGuid {
        std::map<std::uint64_t,ResourceState> Resources;
        std::string PublicationGeneration;
    };
    // Only GUIDs actually requested by Runtime are observed. Missing entries remain
    // as tombstones so restoring the same identity invalidates a cached failure.
    ObservedGuid* ObserveGuid(const std::string&) const;
    ResourceState* ObserveResource(const GameSproutRuntime::ResourceReference&) const;
    std::map<std::uint64_t,ResourceSignature> ReadSignatures(const Guid&,std::string&) const;
    void RefreshPublishedResources(const AssetEvent&);
    void InvalidateResources(const std::set<ResourceKey>&);
    void ResetDependencies(const GameSproutRuntime::ResourceReference&) override;
    void AddDependency(const GameSproutRuntime::ResourceReference&, const GameSproutRuntime::ResourceReference&) override;
    bool LocateArtifact(const GameSproutRuntime::ResourceReference&, eAssetType, Artifact&, std::string&) override;
    bool IsMissing(const GameSproutRuntime::ResourceReference&) override;
    void MarkReadFailed(const GameSproutRuntime::ResourceReference&, bool) override;
    IAssetsManager* _Assets = nullptr;
    ILibraryManager* _Library = nullptr;
    std::uint64_t _Revision = 1;
    bool _Subscribed = false;
    mutable std::map<std::string,ObservedGuid> _ObservedGuids;
    std::map<ResourceKey,std::set<ResourceKey>> _Dependents;
};
