#include "BuildSetting.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>

const char* PlayerBuildPlatformKey(BuildPlatform platform) {
    switch (platform) {
    case BuildPlatform::MacOS: return "macOS";
    case BuildPlatform::Android: return "Android";
    case BuildPlatform::HarmonyOS: return "HarmonyOS";
    case BuildPlatform::IOS: return "iOS";
    case BuildPlatform::Linux: return "Linux";
    case BuildPlatform::MacOSServer: return "macOSServer";
    case BuildPlatform::LinuxServer: return "LinuxServer";
    }
    return "";
}

namespace {
bool ReadPlatform(const Json::Value& value, BuildPlatform& platform) {
    if (!value.isString()) return false;
    const auto key = value.asString();
    for (const auto candidate : BuildPlatforms) if (key == PlayerBuildPlatformKey(candidate)) {
        platform = candidate;
        return true;
    }
    return false;
}

bool SettingsPath(const std::string& root, const QString& directory, bool create, QString& path, std::string& error) {
    const QDir project(QString::fromStdString(root));
    const QFileInfo folder(project.filePath(directory));
    if (folder.isSymLink() || (folder.exists() && !folder.isDir()) || (create && !project.mkpath(directory))) {
        error = "无法使用构建设置目录 " + folder.filePath().toStdString();
        return false;
    }
    const QFileInfo file(QDir(folder.filePath()).filePath("BuildSettings.json"));
    if (file.isSymLink() || (file.exists() && !file.isFile())) {
        error = "构建设置必须是普通文件 " + file.filePath().toStdString();
        return false;
    }
    path = file.filePath();
    return true;
}

bool ReadSettings(const QString& path, Json::Value& data, std::string& error) {
    if (!QFileInfo::exists(path)) return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024 ||
        !AssetArtifactIO::Parse(file.readAll().toStdString(), data) || file.error() != QFileDevice::NoError ||
        !data.isObject() || (data["Version"] != 1 && data["Version"] != 2)) {
        error = "无法读取当前格式的构建设置 " + path.toStdString();
        return false;
    }
    return true;
}

bool WriteSettings(const QString& path, const Json::Value& data, std::string& error) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    writer["emitUTF8"] = true;
    return AssetArtifactIO::Write(path, QByteArray::fromStdString(Json::writeString(writer, data) + "\n"), error);
}

bool ReadScenes(const Json::Value& data, std::vector<BuildScene>& scenes, std::string& error) {
    if (!data.isArray()) { error = "构建场景列表格式无效。"; return false; }
    std::set<std::string> seen;
    for (const auto& item : data) {
        if (!item.isObject() || !item["Guid"].isString() || !item["Enabled"].isBool()) {
            error = "构建场景条目格式无效。";
            return false;
        }
        const Guid guid(item["Guid"].asString());
        if (!guid.IsValid() || !seen.insert(guid.GetGuidString()).second) {
            error = "构建场景 GUID 无效或重复。";
            return false;
        }
        scenes.push_back({guid.GetGuidString(), {}, item["Enabled"].asBool()});
    }
    return true;
}

Json::Value WriteScenes(const std::vector<BuildScene>& scenes) {
    Json::Value data(Json::arrayValue);
    for (const auto& scene : scenes) {
        Json::Value item;
        item["Guid"] = scene.Guid;
        item["Enabled"] = scene.Enabled;
        data.append(std::move(item));
    }
    return data;
}

bool ReadHarmony(const Json::Value& item, HarmonyBuildSettings& settings) {
    if (!item.isObject() || !item["ApplicationId"].isString() || !item["AppName"].isString() ||
        !item["VersionName"].isString() || !item["VersionCode"].isInt() ||
        !item["CompatibleApi"].isInt() || !item["TargetApi"].isInt()) return false;
    if (item["Output"] != "Hap" && item["Output"] != "DevEcoProject") return false;
    settings = {item["ApplicationId"].asString(), item["AppName"].asString(), item["VersionName"].asString(),
        item["VersionCode"].asInt(), item["CompatibleApi"].asInt(), item["TargetApi"].asInt(),
        item["Output"] == "Hap" ? HarmonyBuildOutput::Hap : HarmonyBuildOutput::DevEcoProject};
    return true;
}

Json::Value WriteHarmony(const HarmonyBuildSettings& settings) {
    Json::Value item;
    item["ApplicationId"] = settings.ApplicationId;
    item["AppName"] = settings.AppName;
    item["VersionName"] = settings.VersionName;
    item["VersionCode"] = settings.VersionCode;
    item["CompatibleApi"] = settings.CompatibleApi;
    item["TargetApi"] = settings.TargetApi;
    item["Output"] = settings.Output == HarmonyBuildOutput::Hap ? "Hap" : "DevEcoProject";
    return item;
}
}

bool BuildSetting::LoadSettings(std::string& error) {
    for (const auto platform : BuildPlatforms)
        _State.Profiles.push_back({DefaultProfileId(platform), BuildPlatformName(platform), platform, true});
    _State.ActiveProfileId = DefaultProfileId(_State.Active);
    ShowProfile(_State.Profiles.front());

    QString path;
    Json::Value project;
    if (!SettingsPath(_Root, "ProjectSettings", false, path, error) || !ReadSettings(path, project, error)) return false;
    auto profiles = _State.Profiles;
    std::vector<BuildScene> scenes;
    if (!project.isNull() && !ReadScenes(project["Scenes"], scenes, error)) return false;
    if (project["Version"] == 2) {
        if (!project["Profiles"].isArray()) { error = "构建配置列表格式无效。"; return false; }
        std::set<std::string> seen;
        for (const auto& item : project["Profiles"]) {
            BuildProfile profile;
            if (!item.isObject() || !item["Id"].isString() || !item["Name"].isString() ||
                !item["OverrideScenes"].isBool() || !ReadPlatform(item["Platform"], profile.Platform)) {
                error = "构建配置条目格式无效。";
                return false;
            }
            profile.Id = item["Id"].asString();
            profile.Name = item["Name"].asString();
            profile.IsDefault = profile.Id == DefaultProfileId(profile.Platform);
            profile.OverrideScenes = item["OverrideScenes"].asBool();
            if (!seen.insert(profile.Id).second || (!profile.IsDefault && QUuid(QString::fromStdString(profile.Id)).isNull())) {
                error = "构建配置 ID 无效或重复。";
                return false;
            }
            if (profile.OverrideScenes && !ReadScenes(item["Scenes"], profile.Scenes, error)) return false;
            if (profile.Platform == BuildPlatform::HarmonyOS && !ReadHarmony(item["Harmony"], profile.Harmony)) {
                error = "鸿蒙构建选项格式无效。";
                return false;
            }
            if (!ValidateProfile(profile, error)) return false;
            if (profile.IsDefault) {
                auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& entry) { return entry.Id == profile.Id; });
                *it = std::move(profile);
            } else profiles.push_back(std::move(profile));
        }
    }
    _GlobalScenes = std::move(scenes);
    _State.Profiles = std::move(profiles);
    UpdateEffectiveScenes();

    Json::Value user;
    if (!SettingsPath(_Root, "UserSettings", false, path, error) || !ReadSettings(path, user, error)) return false;
    if (user.isNull()) return true;
    BuildPlatform selected, active;
    if (!ReadPlatform(user["Selected"], selected) || !ReadPlatform(user["Active"], active)) {
        error = "本机构建目标无效。";
        return false;
    }
    const auto* selectedProfile = FindProfile(user["SelectedProfile"].isString() ? user["SelectedProfile"].asString() : std::string{});
    const auto* activeProfile = FindProfile(user["ActiveProfile"].isString() ? user["ActiveProfile"].asString() : std::string{});
    if (!selectedProfile) selectedProfile = FindProfile(DefaultProfileId(selected));
    if (!activeProfile) activeProfile = FindProfile(DefaultProfileId(active));
    auto locals = user["Harmony"];
    if (locals.isObject()) for (const auto& id : locals.getMemberNames()) {
        const auto* profile = FindProfile(id);
        if (!profile || profile->Platform != BuildPlatform::HarmonyOS) continue;
        const auto& item = locals[id];
        if (!item.isObject() || !item["ToolsPath"].isString() || !item["SigningConfig"].isString() ||
            !item["DeviceId"].isString() || !item["SigningMode"].isInt() || item["SigningMode"].asInt() < 0 ||
            item["SigningMode"].asInt() > static_cast<int>(HarmonySigningMode::ConfigurationFile)) {
            error = "本机鸿蒙构建设置无效。";
            return false;
        }
        _HarmonyLocals.emplace(id, HarmonyLocalBuildSettings{item["ToolsPath"].asString(), item["SigningConfig"].asString(),
            item["DeviceId"].asString(), static_cast<HarmonySigningMode>(item["SigningMode"].asInt())});
    }
    _State.Active = activeProfile->Platform;
    _State.ActiveProfileId = activeProfile->Id;
    ShowProfile(*selectedProfile);
    return true;
}

bool BuildSetting::SaveProjectSettings(const std::vector<BuildScene>& scenes, const std::vector<BuildProfile>& profiles,
                                       std::string& error) const {
    QString path;
    if (!SettingsPath(_Root, "ProjectSettings", true, path, error)) return false;
    Json::Value data;
    data["Version"] = 2;
    data["Scenes"] = WriteScenes(scenes);
    data["Profiles"] = Json::Value(Json::arrayValue);
    for (const auto& profile : profiles) {
        Json::Value item;
        item["Id"] = profile.Id;
        item["Name"] = profile.Name;
        item["Platform"] = PlayerBuildPlatformKey(profile.Platform);
        item["OverrideScenes"] = profile.OverrideScenes;
        if (profile.OverrideScenes) item["Scenes"] = WriteScenes(profile.Scenes);
        if (profile.Platform == BuildPlatform::HarmonyOS) item["Harmony"] = WriteHarmony(profile.Harmony);
        data["Profiles"].append(std::move(item));
    }
    return WriteSettings(path, data, error);
}

bool BuildSetting::SaveUserSettings(const BuildProfile& selected, const BuildProfile& active,
                                    const std::map<std::string, HarmonyLocalBuildSettings>& locals, std::string& error) const {
    QString path;
    if (!SettingsPath(_Root, "UserSettings", true, path, error)) return false;
    Json::Value data;
    data["Version"] = 2;
    data["Selected"] = PlayerBuildPlatformKey(selected.Platform);
    data["Active"] = PlayerBuildPlatformKey(active.Platform);
    data["SelectedProfile"] = selected.Id;
    data["ActiveProfile"] = active.Id;
    data["Harmony"] = Json::Value(Json::objectValue);
    for (const auto& [id, settings] : locals) {
        auto& item = data["Harmony"][id];
        item["ToolsPath"] = settings.ToolsPath;
        item["SigningConfig"] = settings.SigningConfig;
        item["DeviceId"] = settings.DeviceId;
        item["SigningMode"] = static_cast<int>(settings.SigningMode);
    }
    return WriteSettings(path, data, error);
}
