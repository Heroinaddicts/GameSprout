#pragma once

#include "Header.h"
#include "IAssetsManager.h"
#include "ITaskApi.h"
#include <map>
#include <set>

class IAssetsPipeline;
class IConsole;
class IIDEWindow;
class ILibraryManager;
class IPlayMode;
class IProject;
class IProjectSettings;
class ISceneManager;
class IScriptCompiler;
class IScriptEditor;
class QObject;
struct PlayerBuildJob;

class BuildSetting final : public IBuildSetting, private IAssetListener {
public:
    BuildSettingsState GetState() const override { return _State; }
    bool SelectPlatform(BuildPlatform, std::string&) override;
    bool SelectProfile(const std::string&, std::string&) override;
    bool CreateProfile(BuildPlatform, const std::string&, std::string&) override;
    bool SetProfile(const BuildProfile&, std::string&) override;
    bool RemoveProfile(const std::string&, std::string&) override;
    bool SetHarmonyLocal(const HarmonyLocalBuildSettings&, std::string&) override;
    bool RefreshHarmonyStatus(std::string&) override;
    bool SetScenes(const std::vector<BuildScene>&, std::string&) override;
    bool SwitchPlatform(std::string&) override;
    bool Build(const std::string&, bool run, std::string&) override;
    void Cancel() override;
    bool RegisterListener(IBuildSettingListener*) override;
    void UnregisterListener(IBuildSettingListener*) override;

private:
    void BeforeInitialize(GameSproutModuleApi* api) override;
    bool Initialize(GameSproutModuleApi*) override;
    bool Launch(GameSproutModuleApi*) override;
    void LaunchFinished(GameSproutModuleApi*) override;
    void Release(GameSproutModuleApi*) override;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
    void SceneAssetChanged(const AssetEvent&, bool removed);
    void RefreshScenePaths();
    void Notify();
    bool LoadSettings(std::string&);
    static std::string DefaultProfileId(BuildPlatform);
    static bool ValidateProfile(const BuildProfile&, std::string&);
    const BuildProfile* FindProfile(const std::string&) const;
    void ShowProfile(const BuildProfile&);
    void UpdateEffectiveScenes();
    void InvalidateHarmonyStatus();
    bool SaveProjectSettings(const std::vector<BuildScene>&, const std::vector<BuildProfile>&, std::string&) const;
    bool SaveUserSettings(const BuildProfile& selected, const BuildProfile& active,
                          const std::map<std::string, HarmonyLocalBuildSettings>&, std::string&) const;
    bool CanStart(std::string&);
    bool CollectSwitch(PlayerBuildJob&, std::string&) const;
    bool CollectBuild(PlayerBuildJob&, std::string&) const;
    bool Submit(PlayerBuildJob&&, std::string&);
    void Finish(bool, const PlayerBuildJob*);

    BuildSettingsState _State;
    std::vector<BuildScene> _GlobalScenes;
    std::map<std::string, HarmonyLocalBuildSettings> _HarmonyLocals;
    std::string _Root;
    std::map<std::string, std::string> _ScenePaths;
    std::set<std::string> _ShaderGuids;
    std::map<std::uint64_t, IBuildSettingListener*> _Listeners;
    std::uint64_t _NextListener = 0;
    std::uint64_t _ShaderRevision = 0;
    IAssetsManager* _Assets = nullptr;
    IAssetsPipeline* _Pipeline = nullptr;
    IConsole* _Console = nullptr;
    IIDEWindow* _IDE = nullptr;
    ILibraryManager* _Library = nullptr;
    IPlayMode* _Play = nullptr;
    IProject* _Project = nullptr;
    IProjectSettings* _ProjectSettings = nullptr;
    ISceneManager* _Scenes = nullptr;
    IScriptCompiler* _Compiler = nullptr;
    IScriptEditor* _Scripts = nullptr;
    QObject* _Dispatch = nullptr;
    Foundation::ITaskApi* _Tasks = nullptr;
    UInt64 _Owner = Foundation::InvalidTaskOwnerId;
    UInt64 _Task = Foundation::InvalidTaskId;
    UInt64 _HarmonyTask = Foundation::InvalidTaskId;
    std::uint64_t _HarmonyCheck = 0;
    bool _Subscribed = false;
    bool _Loading = false;
    bool _SceneNotificationQueued = false;
};
