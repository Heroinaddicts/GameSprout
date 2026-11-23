#include "BuildSetting.h"
#include "PlayerBuildJob.h"
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

namespace {
bool KnownHarmonyApi(int api) {
    return (api >= 16 && api <= 24) || api == 26;
}

void ResolvePaths(std::vector<BuildScene>& scenes, const std::map<std::string, std::string>& paths) {
    for (auto& scene : scenes) {
        const auto found = paths.find(scene.Guid);
        scene.Path = found == paths.end() ? std::string{} : found->second;
    }
}
}

std::string BuildSetting::DefaultProfileId(BuildPlatform platform) {
    return "default:" + std::string(PlayerBuildPlatformKey(platform));
}

const BuildProfile* BuildSetting::FindProfile(const std::string& id) const {
    const auto match = std::find_if(_State.Profiles.begin(), _State.Profiles.end(),
        [&](const auto& profile) { return profile.Id == id; });
    return match == _State.Profiles.end() ? nullptr : &*match;
}

void BuildSetting::ShowProfile(const BuildProfile& profile) {
    if (_State.SelectedProfileId != profile.Id) InvalidateHarmonyStatus();
    _State.SelectedProfileId = profile.Id;
    _State.Selected = profile.Platform;
    const auto local = _HarmonyLocals.find(profile.Id);
    _State.HarmonyLocal = local == _HarmonyLocals.end() ? HarmonyLocalBuildSettings{} : local->second;
    UpdateEffectiveScenes();
}

void BuildSetting::UpdateEffectiveScenes() {
    const auto* profile = FindProfile(_State.SelectedProfileId);
    _State.Scenes = profile && profile->OverrideScenes ? profile->Scenes : _GlobalScenes;
    ResolvePaths(_State.Scenes, _ScenePaths);
}

bool BuildSetting::ValidateProfile(const BuildProfile& profile, std::string& error) {
    if (profile.Name.empty() || profile.Name.size() > 240 || QString::fromStdString(profile.Name).trimmed().isEmpty()) {
        error = "构建配置名称不能为空，且最多可用 240 字节。";
        return false;
    }
    if (profile.Platform != BuildPlatform::HarmonyOS) return true;
    const auto& options = profile.Harmony;
    static const QRegularExpression bundlePattern(R"(\A[A-Za-z](?:[A-Za-z0-9_]*[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9_]*[A-Za-z0-9])?){2,}\z)");
    static const QRegularExpression versionPattern(R"(\A[A-Za-z0-9_.{}]{1,127}\z)");
    if (options.ApplicationId.size() < 7 || options.ApplicationId.size() > 128 ||
        !bundlePattern.match(QString::fromStdString(options.ApplicationId)).hasMatch()) {
        error = "鸿蒙应用标识至少需要三段，例如 org.gamesprout.player。";
        return false;
    }
    const auto appName = QString::fromStdString(options.AppName);
    if (appName.trimmed().isEmpty() || appName.toUcs4().size() > 127 || appName.contains(QChar::Null) ||
        appName.contains('\r') || appName.contains('\n')) {
        error = "鸿蒙应用名称需要 1 到 127 个字符。";
        return false;
    }
    if (!versionPattern.match(QString::fromStdString(options.VersionName)).hasMatch() || options.VersionCode < 0) {
        error = "鸿蒙版本号或版本名称格式无效。";
        return false;
    }
    if (!KnownHarmonyApi(options.CompatibleApi) || !KnownHarmonyApi(options.TargetApi) ||
        options.CompatibleApi > options.TargetApi) {
        error = "鸿蒙 API 请选择 16 至 24 或 26，最低 API 不能大于目标 API。";
        return false;
    }
    if (options.Output != HarmonyBuildOutput::Hap && options.Output != HarmonyBuildOutput::DevEcoProject) {
        error = "未知的鸿蒙构建产物类型。";
        return false;
    }
    return true;
}

bool BuildSetting::SelectPlatform(BuildPlatform platform, std::string& error) {
    if (platform == _State.Selected) { error.clear(); return true; }
    return SelectProfile(DefaultProfileId(platform), error);
}

bool BuildSetting::SelectProfile(const std::string& id, std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    const auto* profile = FindProfile(id);
    const auto* active = FindProfile(_State.ActiveProfileId);
    if (!profile || !active) { error = "找不到构建配置。"; return false; }
    if (id == _State.SelectedProfileId) return true;
    if (!SaveUserSettings(*profile, *active, _HarmonyLocals, error)) return false;
    ShowProfile(*profile);
    _State.Message.clear();
    _State.Error.clear();
    Notify();
    return true;
}

bool BuildSetting::CreateProfile(BuildPlatform platform, const std::string& name, std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    const auto* defaults = FindProfile(DefaultProfileId(platform));
    if (!defaults) { error = "未知构建平台。"; return false; }
    BuildProfile profile = *defaults;
    profile.Id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    profile.Name = QString::fromStdString(name).trimmed().toStdString();
    profile.IsDefault = false;
    if (!ValidateProfile(profile, error)) return false;
    auto profiles = _State.Profiles;
    profiles.push_back(profile);
    if (!SaveProjectSettings(_GlobalScenes, profiles, error)) return false;
    _State.Profiles = std::move(profiles);
    if (SelectProfile(profile.Id, error)) return true;
    _State.Error = "已创建构建配置，但无法保存本机选择。\n" + error;
    Notify();
    return true;
}

bool BuildSetting::SetProfile(const BuildProfile& profile, std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    auto profiles = _State.Profiles;
    const auto entry = std::find_if(profiles.begin(), profiles.end(), [&](const auto& value) { return value.Id == profile.Id; });
    if (entry == profiles.end()) { error = "找不到构建配置。"; return false; }
    BuildProfile candidate = *entry;
    if (!candidate.IsDefault) candidate.Name = QString::fromStdString(profile.Name).trimmed().toStdString();
    if (candidate.Platform == BuildPlatform::HarmonyOS) candidate.Harmony = profile.Harmony;
    if (candidate.OverrideScenes != profile.OverrideScenes) {
        candidate.OverrideScenes = profile.OverrideScenes;
        candidate.Scenes = profile.OverrideScenes ? _GlobalScenes : std::vector<BuildScene>{};
    }
    if (candidate == *entry) return true;
    if (!ValidateProfile(candidate, error)) return false;
    const bool refreshHarmony = entry->Id == _State.SelectedProfileId && candidate.Harmony != entry->Harmony;
    *entry = std::move(candidate);
    if (!SaveProjectSettings(_GlobalScenes, profiles, error)) return false;
    _State.Profiles = std::move(profiles);
    UpdateEffectiveScenes();
    if (refreshHarmony) InvalidateHarmonyStatus();
    _State.Error.clear();
    _State.Message.clear();
    Notify();
    if (refreshHarmony) { std::string ignored; RefreshHarmonyStatus(ignored); }
    return true;
}

bool BuildSetting::RemoveProfile(const std::string& id, std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    const auto* profile = FindProfile(id);
    if (!profile || profile->IsDefault || id == _State.ActiveProfileId) {
        error = "只能删除未激活的自定义构建配置。";
        return false;
    }
    const bool selected = id == _State.SelectedProfileId;
    const auto replacement = DefaultProfileId(profile->Platform);
    auto profiles = _State.Profiles;
    std::erase_if(profiles, [&](const auto& value) { return value.Id == id; });
    if (!SaveProjectSettings(_GlobalScenes, profiles, error)) return false;
    _State.Profiles = std::move(profiles);
    _HarmonyLocals.erase(id);
    if (selected) ShowProfile(*FindProfile(replacement));
    _State.Message.clear();
    _State.Error.clear();
    // Missing profile IDs fall back to their platform defaults when loading older local settings.
    SaveUserSettings(*FindProfile(_State.SelectedProfileId), *FindProfile(_State.ActiveProfileId), _HarmonyLocals, _State.Error);
    Notify();
    return true;
}
