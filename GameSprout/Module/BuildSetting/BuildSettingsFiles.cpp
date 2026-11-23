#include "BuildSetting.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
        !data.isObject() || data["Version"] != 1) {
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
}

bool BuildSetting::LoadSettings(std::string& error) {
    QString path;
    Json::Value scenes;
    if (!SettingsPath(_Root, "ProjectSettings", false, path, error) || !ReadSettings(path, scenes, error)) return false;
    if (!scenes.isNull()) {
        if (!scenes["Scenes"].isArray()) { error = "构建场景列表格式无效。"; return false; }
        std::vector<BuildScene> loaded;
        std::set<std::string> seen;
        for (const auto& scene : scenes["Scenes"]) {
            if (!scene.isObject() || !scene["Guid"].isString() || !scene["Enabled"].isBool()) {
                error = "构建场景条目格式无效。";
                return false;
            }
            const Guid guid(scene["Guid"].asString());
            if (!guid.IsValid() || !seen.insert(guid.GetGuidString()).second) {
                error = "构建场景 GUID 无效或重复。";
                return false;
            }
            loaded.push_back({guid.GetGuidString(), {}, scene["Enabled"].asBool()});
        }
        _State.Scenes = std::move(loaded);
    }
    Json::Value user;
    if (!SettingsPath(_Root, "UserSettings", false, path, error) || !ReadSettings(path, user, error)) return false;
    if (user.isNull()) return true;
    BuildPlatform selected, active;
    if (!ReadPlatform(user["Selected"], selected) || !ReadPlatform(user["Active"], active)) {
        error = "本机构建目标无效。";
        return false;
    }
    _State.Selected = selected;
    _State.Active = active;
    return true;
}

bool BuildSetting::SaveScenes(const std::vector<BuildScene>& scenes, std::string& error) const {
    QString path;
    if (!SettingsPath(_Root, "ProjectSettings", true, path, error)) return false;
    Json::Value data;
    data["Version"] = 1;
    data["Scenes"] = Json::Value(Json::arrayValue);
    for (const auto& scene : scenes) {
        Json::Value item;
        item["Guid"] = scene.Guid;
        item["Enabled"] = scene.Enabled;
        data["Scenes"].append(std::move(item));
    }
    return WriteSettings(path, data, error);
}

bool BuildSetting::SaveUserSettings(BuildPlatform selected, BuildPlatform active, std::string& error) const {
    QString path;
    if (!SettingsPath(_Root, "UserSettings", true, path, error)) return false;
    Json::Value data;
    data["Version"] = 1;
    data["Selected"] = PlayerBuildPlatformKey(selected);
    data["Active"] = PlayerBuildPlatformKey(active);
    return WriteSettings(path, data, error);
}
