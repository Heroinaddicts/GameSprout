#include "PreparedResourceProvider.h"
#include <algorithm>
#include <limits>

namespace PreparedResources {
bool ResourceProvider::OpenArtifact(const GameSproutRuntime::ResourceReference& reference, eAssetType type,
    Artifact& artifact, std::ifstream& file, std::string& error) {
    error.clear();
    artifact = {};
    if (!LocateArtifact(reference, type, artifact, error)) {
        MarkReadFailed(reference, true);
        return false;
    }
    // Preserve the Editor's existing bound for render assets. Scenes have
    // their own structural limits in the document decoder.
    const auto maxReadable = static_cast<std::uint64_t>(
        std::min<std::uintmax_t>(std::numeric_limits<std::streamsize>::max(), std::numeric_limits<std::size_t>::max()));
    if (artifact.Path.is_absolute() && artifact.Size <= maxReadable &&
        (type == eAssetType::Scene || artifact.Size <= 512ull * 1024 * 1024)) {
        file.open(artifact.Path, std::ios::binary | std::ios::ate);
        if (file && file.tellg() == static_cast<std::streamoff>(artifact.Size)) {
            file.seekg(0);
            if (file) return true;
        }
    }
    error = "Cannot read prepared artifact: " + artifact.Path.string();
    MarkReadFailed(reference, true);
    return false;
}

bool ResourceProvider::Read(const GameSproutRuntime::ResourceReference& reference, eAssetType type,
    Json::Value& descriptor, std::vector<std::uint8_t>& bytes, std::string& error) {
    bytes.clear();
    Artifact artifact;
    std::ifstream file;
    if (!OpenArtifact(reference, type, artifact, file, error)) return false;
    bytes.resize(static_cast<std::size_t>(artifact.Size));
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        error = "Incomplete prepared artifact";
        MarkReadFailed(reference, true);
        return false;
    }
    descriptor = std::move(artifact.Descriptor);
    MarkReadFailed(reference, false);
    return true;
}

bool ResourceProvider::LoadPrefab(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::PrefabData& output, std::string& error) {
    output = {};
    Json::Value descriptor;
    std::vector<std::uint8_t> bytes;
    if (!Read(reference, eAssetType::Prefab, descriptor, bytes, error)) return false;
    if (descriptor["Encoding"] != "BinaryValueLE1" || descriptor["Format"] != "GameSproutPrefab") {
        error = "Invalid prepared Prefab descriptor";
        return false;
    }
    output.Bytes = std::move(bytes);
    return true;
}
}
