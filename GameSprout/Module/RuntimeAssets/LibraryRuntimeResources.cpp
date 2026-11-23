#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include "../Shared/AssetArtifactIO.h"

LibraryRuntimeResources::LibraryRuntimeResources(IAssetsManager* assets, ILibraryManager* library)
    : _Assets(assets), _Library(library) {
    if (_Assets) _Subscribed = _Assets->RegisterAssetListener(this);
}
LibraryRuntimeResources::~LibraryRuntimeResources() {
    if (_Subscribed) _Assets->UnregisterAssetListener(this);
}

bool LibraryRuntimeResources::LocateArtifact(const GameSproutRuntime::ResourceReference& reference,
    eAssetType type, Artifact& artifact, std::string& error) {
    const Guid guid(reference.Guid);
    if (!_Library || !guid.IsValid() || reference.LocalID == 0) {
        error = "Invalid resource reference or unavailable LibraryManager";
        return false;
    }
    IAsset* asset = _Library->QueryAsset(guid, reference.LocalID);
    if (!asset || asset->GetAssetType() != type) {
        error = "Prepared resource is missing or has the wrong type: " + reference.Guid + "_" + std::to_string(reference.LocalID);
        return false;
    }
    if (asset->GetDataVersion() != AssetArtifactIO::CurrentDataVersion(type) ||
        !AssetArtifactIO::Parse(asset->GetDataDescriptorJson(), artifact.Descriptor) || !artifact.Descriptor.isObject()) {
        error = "Invalid ArtifactDB resource descriptor";
        return false;
    }
    artifact.Path = std::filesystem::u8path(asset->GetArtifactPath());
    artifact.Size = asset->GetDataSize();
    return true;
}

bool LibraryRuntimeResources::IsMissing(const GameSproutRuntime::ResourceReference& reference) {
    ObserveResource(reference);
    const Guid guid(reference.Guid);
    return _Library && guid.IsValid() && reference.LocalID && !_Library->QueryAsset(guid, reference.LocalID);
}

void LibraryRuntimeResources::MarkReadFailed(const GameSproutRuntime::ResourceReference& reference, bool failed) {
    if (auto* state = ObserveResource(reference)) state->ReadFailed = failed;
}
