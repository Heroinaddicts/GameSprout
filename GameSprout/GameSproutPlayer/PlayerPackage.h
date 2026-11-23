#pragma once

#include "PreparedResourceProvider.h"
#include "SceneDocument.h"

namespace GameSproutPlayer {
class Package final : public PreparedResources::ResourceProvider {
public:
    bool Open(const std::filesystem::path& directory, std::string& error);
    bool LoadProjectSettings(GameSproutRuntime::IGameSproutRuntime&, std::string& error) const;
    bool LoadFirstScene(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene*&, std::string& error);
    bool HasGameLogic() const { return _GameLogic; }
    bool Headless() const { return _Headless; }
    std::uint64_t GetRevision() const override { return 1; }

private:
    using Key = std::pair<std::string, std::uint64_t>;
    struct Entry {
        eAssetType Type;
        std::uint64_t Size;
        Json::Value Descriptor;
    };
    static bool ReadKey(const Json::Value&, Key&);
    bool LocateArtifact(const GameSproutRuntime::ResourceReference&, eAssetType, Artifact&, std::string&) override;
    bool IsMissing(const GameSproutRuntime::ResourceReference&) override;

    std::filesystem::path _Directory;
    std::map<Key, Entry> _Artifacts;
    std::vector<Key> _Scenes;
    bool _GameLogic = false;
    bool _Headless = false;
};
}
