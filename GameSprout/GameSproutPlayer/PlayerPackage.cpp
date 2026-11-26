#include "PlayerPackage.h"
#include "PreparedProjectSettings.h"
#include "ValueBinary.h"
#include <set>

namespace GameSproutPlayer {
namespace {
std::string DisplayPath(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
#else
    return path.string();
#endif
}

bool ReadTextFile(const std::filesystem::path& path, std::string& text, std::string& error) {
    std::error_code statusError;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, statusError))) {
        error = "Packaged file is missing or is not a regular file: " + DisplayPath(path);
        return false;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size <= 0 || size > 64ull * 1024 * 1024) {
        error = "Invalid packaged file size: " + DisplayPath(path);
        return false;
    }
    text.resize(static_cast<std::size_t>(size));
    if (file.seekg(0) && file.read(text.data(), static_cast<std::streamsize>(text.size()))) return true;
    error = "Cannot read packaged file: " + DisplayPath(path);
    return false;
}
}

bool Package::ReadKey(const Json::Value& value, Key& key) {
    if (!value.isObject() || !value["Guid"].isString() || !value["LocalID"].isUInt64()) return false;
    const auto guid = value["Guid"].asString();
    const auto localID = value["LocalID"].asUInt64();
    if (!PreparedResources::ValidGuid(guid) || !localID) return false;
    key = {PreparedResources::CanonicalGuid(guid), localID};
    return true;
}

bool Package::Open(const std::filesystem::path& directory, std::string& error) {
    std::string text;
    if (!ReadTextFile(directory / "Player.json", text, error)) return false;
    Json::Value manifest;
    if (!ProjectSettingsFormat::Parse(text, manifest, error)) return false;
    if (!manifest.isObject() || manifest["Version"] != 1 || !manifest["Scenes"].isArray() ||
        manifest["Scenes"].empty() || !manifest["Artifacts"].isArray() || !manifest["GameLogic"].isBool() ||
        (manifest.isMember("Headless") && !manifest["Headless"].isBool())) {
        error = "Invalid Player.json manifest";
        return false;
    }
    std::map<Key, Entry> artifacts;
    for (const auto& entry : manifest["Artifacts"]) {
        Key key;
        if (!ReadKey(entry, key) || !entry["Type"].isUInt() || entry["Type"].asUInt() > UINT16_MAX ||
            !entry["DataVersion"].isUInt() || !entry["Size"].isUInt64() || !entry["Descriptor"].isObject()) {
            error = "Invalid Player artifact record";
            return false;
        }
        const auto type = static_cast<eAssetType>(entry["Type"].asUInt());
        if (type == eAssetType::Unknown || entry["DataVersion"].asUInt() != PreparedResources::CurrentDataVersion(type) ||
            !artifacts.emplace(key, Entry{type, entry["Size"].asUInt64(), entry["Descriptor"]}).second) {
            error = "Unsupported or duplicate Player artifact: " + PreparedResources::ArtifactFilename(key.first, key.second);
            return false;
        }
    }
    std::vector<Key> scenes;
    std::set<Key> uniqueScenes;
    for (const auto& entry : manifest["Scenes"]) {
        Key key;
        if (!ReadKey(entry, key) || !uniqueScenes.insert(key).second) {
            error = "Invalid or duplicate Player scene";
            return false;
        }
        const auto artifact = artifacts.find(key);
        if (artifact == artifacts.end() || artifact->second.Type != eAssetType::Scene) {
            error = "Enabled scene is missing from the Player artifacts";
            return false;
        }
        scenes.push_back(std::move(key));
    }
    _Directory = directory;
    _Artifacts = std::move(artifacts);
    _Scenes = std::move(scenes);
    _GameLogic = manifest["GameLogic"].asBool();
    _Headless = manifest.get("Headless", false).asBool();
    return true;
}

bool Package::LocateArtifact(const GameSproutRuntime::ResourceReference& reference, eAssetType type,
    Artifact& artifact, std::string& error) {
    const auto found = _Artifacts.find({PreparedResources::CanonicalGuid(reference.Guid), reference.LocalID});
    if (found == _Artifacts.end() || found->second.Type != type) {
        error = "Packaged resource is missing or has the wrong type: " + reference.Guid + "_" + std::to_string(reference.LocalID);
        return false;
    }
    const auto path = _Directory / "Artifact" / PreparedResources::ArtifactFilename(found->first.first, found->first.second);
    std::error_code statusError;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, statusError))) {
        error = "Packaged artifact is missing or is not a regular file: " + DisplayPath(path);
        return false;
    }
    artifact = {path, found->second.Size, found->second.Descriptor};
    return true;
}

bool Package::IsMissing(const GameSproutRuntime::ResourceReference& reference) {
    return PreparedResources::ValidGuid(reference.Guid) && reference.LocalID &&
        !_Artifacts.contains({PreparedResources::CanonicalGuid(reference.Guid), reference.LocalID});
}

bool Package::LoadProjectSettings(GameSproutRuntime::IGameSproutRuntime& runtime, std::string& error) const {
    const auto path = _Directory / "ProjectSettings.json";
    if (!std::filesystem::exists(path)) return true;
    std::string text;
    GameSproutRuntime::RuntimeProjectSettings settings;
    return ReadTextFile(path, text, error) && ProjectSettingsFormat::Decode(text, settings, error) &&
        runtime.SetProjectSettings(settings, error);
}

bool Package::LoadFirstScene(GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IScene*& scene, std::string& error) {
    if (_Scenes.empty()) {
        error = "Player has no enabled scene";
        return false;
    }
    const auto& [guid, localID] = _Scenes.front();
    Json::Value descriptor;
    std::vector<std::uint8_t> bytes;
    if (!Read({guid, localID}, eAssetType::Scene, descriptor, bytes, error)) return false;
    Json::Value envelope;
    if (descriptor["Format"] != "GameSproutDocument" || descriptor["Encoding"] != "BinaryValueLE1" ||
        !ObjectDocumentBinary::Decode(bytes, envelope) || envelope["Format"] != "GameSproutDocument" ||
        !envelope["Data"].isObject()) {
        error = "Invalid prepared startup scene: " + PreparedResources::ArtifactFilename(guid, localID);
        return false;
    }
    SceneDocument::PreparedScene prepared;
    SceneDocument::ObjectIDs identities;
    return SceneDocument::Prepare(std::move(envelope["Data"]), prepared, error) &&
        SceneDocument::Instantiate(prepared, runtime, scene, identities, error);
}
}
