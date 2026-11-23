#pragma once

#include "GameSproutEditorApi.h"
#include <array>
#include <string>
#include <vector>

enum class BuildPlatform { MacOS, Android, HarmonyOS, IOS, Linux, MacOSServer, LinuxServer };
inline constexpr std::array BuildPlatforms{BuildPlatform::MacOS, BuildPlatform::Android, BuildPlatform::HarmonyOS,
    BuildPlatform::IOS, BuildPlatform::Linux, BuildPlatform::MacOSServer, BuildPlatform::LinuxServer};

inline constexpr const char* BuildPlatformName(BuildPlatform platform) {
    switch (platform) {
    case BuildPlatform::MacOS: return "macOS";
    case BuildPlatform::Android: return "Android";
    case BuildPlatform::HarmonyOS: return "鸿蒙";
    case BuildPlatform::IOS: return "iOS";
    case BuildPlatform::Linux: return "Linux";
    case BuildPlatform::MacOSServer: return "macOS Server";
    case BuildPlatform::LinuxServer: return "Linux Server";
    }
    return "";
}

inline constexpr bool CanBuildPlatform(BuildPlatform platform) {
#if defined(__APPLE__)
    return *BuildPlatformName(platform) != '\0';
#else
    return false;
#endif
}

inline constexpr const char* BuildPlatformOutputSuffix(BuildPlatform platform) {
    switch (platform) {
    case BuildPlatform::Android: return "apk";
    case BuildPlatform::HarmonyOS: return "hap";
    case BuildPlatform::Linux:
    case BuildPlatform::LinuxServer: return "tar.gz";
    default: return "app";
    }
}

enum class BuildOperation { Idle, Switching, Building };

struct BuildScene {
    std::string Guid;
    std::string Path; // Resolved from Guid. Empty when the scene is missing.
    bool Enabled = true;
    bool operator==(const BuildScene&) const = default;
};

struct BuildSettingsState {
    BuildPlatform Selected = BuildPlatform::MacOS;
    BuildPlatform Active = BuildPlatform::MacOS;
    BuildOperation Operation = BuildOperation::Idle;
    std::vector<BuildScene> Scenes;
    std::vector<BuildScene> AvailableScenes;
    std::string Message;
    std::string Error;
    std::string LastOutput;
};

class IBuildSettingListener {
public:
    virtual ~IBuildSettingListener() = default;
    virtual void OnBuildSettingsChanged(const BuildSettingsState&) = 0;
};

class IBuildSetting : public GameSproutEditor::IGameSproutModule {
public:
    virtual BuildSettingsState GetState() const = 0;
    virtual bool SelectPlatform(BuildPlatform, std::string& error) = 0;
    virtual bool SetScenes(const std::vector<BuildScene>&, std::string& error) = 0;
    // True means the operation was accepted. Completion and errors arrive through the listener.
    virtual bool SwitchPlatform(std::string& error) = 0;
    virtual bool Build(const std::string& output, bool run, std::string& error) = 0;
    virtual void Cancel() = 0;
    virtual bool RegisterListener(IBuildSettingListener*) = 0;
    virtual void UnregisterListener(IBuildSettingListener*) = 0;
};
