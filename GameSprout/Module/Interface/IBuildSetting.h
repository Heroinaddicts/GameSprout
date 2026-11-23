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

enum class HarmonyBuildOutput { Hap, DevEcoProject };
enum class HarmonySigningMode { Environment, Unsigned, ConfigurationFile };
inline constexpr const char* HarmonyDevEcoProjectMarker = ".gamesprout-deveco";
inline constexpr const char* HarmonyDevEcoProjectMarkerText = "GameSprout DevEco Project v1\n";

struct HarmonyBuildSettings {
    std::string ApplicationId = "org.gamesprout.player";
    std::string AppName = "GameSprout Player";
    std::string VersionName = "1.0.0";
    int VersionCode = 1000000;
    int CompatibleApi = 16;
    int TargetApi = 16;
    HarmonyBuildOutput Output = HarmonyBuildOutput::Hap;
    bool operator==(const HarmonyBuildSettings&) const = default;
};

struct HarmonyLocalBuildSettings {
    std::string ToolsPath;
    std::string SigningConfig;
    std::string DeviceId;
    HarmonySigningMode SigningMode = HarmonySigningMode::Environment;
    bool operator==(const HarmonyLocalBuildSettings&) const = default;
};

struct HarmonyBuildDevice {
    std::string Id;
    std::string Name;
    std::string Status;
    bool Connected = false;
    bool operator==(const HarmonyBuildDevice&) const = default;
};

struct HarmonyBuildStatus {
    bool Checking = false;
    bool Checked = false;
    bool CanBuild = false;
    bool CanRun = false;
    std::string BuildMessage;
    std::string RunMessage;
    std::vector<HarmonyBuildDevice> Devices;
};

struct BuildProfile {
    std::string Id;
    std::string Name;
    BuildPlatform Platform = BuildPlatform::MacOS;
    bool IsDefault = false;
    bool OverrideScenes = false;
    std::vector<BuildScene> Scenes;
    HarmonyBuildSettings Harmony;
    bool operator==(const BuildProfile&) const = default;
};

inline constexpr const char* BuildPlatformOutputSuffix(const BuildProfile& profile) {
    if (profile.Platform == BuildPlatform::HarmonyOS && profile.Harmony.Output == HarmonyBuildOutput::DevEcoProject)
        return "";
    return BuildPlatformOutputSuffix(profile.Platform);
}

struct BuildSettingsState {
    BuildPlatform Selected = BuildPlatform::MacOS;
    BuildPlatform Active = BuildPlatform::MacOS;
    BuildOperation Operation = BuildOperation::Idle;
    std::vector<BuildScene> Scenes;
    std::vector<BuildScene> AvailableScenes;
    std::string Message;
    std::string Error;
    std::string LastOutput;
    std::string SelectedProfileId;
    std::string ActiveProfileId;
    std::vector<BuildProfile> Profiles;
    HarmonyLocalBuildSettings HarmonyLocal;
    HarmonyBuildStatus HarmonyStatus;
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
    virtual bool SelectProfile(const std::string& id, std::string& error) = 0;
    virtual bool CreateProfile(BuildPlatform, const std::string& name, std::string& error) = 0;
    virtual bool SetProfile(const BuildProfile&, std::string& error) = 0;
    virtual bool RemoveProfile(const std::string& id, std::string& error) = 0;
    virtual bool SetHarmonyLocal(const HarmonyLocalBuildSettings&, std::string& error) = 0;
    virtual bool RefreshHarmonyStatus(std::string& error) = 0;
    virtual bool SetScenes(const std::vector<BuildScene>&, std::string& error) = 0;
    // True means the operation was accepted. Completion and errors arrive through the listener.
    virtual bool SwitchPlatform(std::string& error) = 0;
    virtual bool Build(const std::string& output, bool run, std::string& error) = 0;
    virtual void Cancel() = 0;
    virtual bool RegisterListener(IBuildSettingListener*) = 0;
    virtual void UnregisterListener(IBuildSettingListener*) = 0;
};
