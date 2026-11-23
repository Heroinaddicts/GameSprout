#include "BuildSetting.h"
#include "HarmonyBuildEnvironment.h"
#include <exception>

void BuildSetting::InvalidateHarmonyStatus() {
    ++_HarmonyCheck;
    if (_HarmonyTask != Foundation::InvalidTaskId) _Tasks->CancelTask(_HarmonyTask);
    _HarmonyTask = Foundation::InvalidTaskId;
    _State.HarmonyStatus = {};
}

bool BuildSetting::SetHarmonyLocal(const HarmonyLocalBuildSettings& settings, std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    const auto* selected = FindProfile(_State.SelectedProfileId);
    const auto* active = FindProfile(_State.ActiveProfileId);
    if (!active || !selected || selected->Platform != BuildPlatform::HarmonyOS) {
        error = "请选择鸿蒙构建配置。";
        return false;
    }
    if (settings.SigningMode != HarmonySigningMode::Environment && settings.SigningMode != HarmonySigningMode::Unsigned &&
        settings.SigningMode != HarmonySigningMode::ConfigurationFile) {
        error = "未知的鸿蒙签名模式。";
        return false;
    }
    if (settings == _State.HarmonyLocal) return true;
    auto locals = _HarmonyLocals;
    if (settings == HarmonyLocalBuildSettings{}) locals.erase(selected->Id);
    else locals[selected->Id] = settings;
    if (!SaveUserSettings(*selected, *active, locals, error)) return false;
    _HarmonyLocals = std::move(locals);
    _State.HarmonyLocal = settings;
    _State.Error.clear();
    _State.Message.clear();
    InvalidateHarmonyStatus();
    std::string ignored;
    if (!RefreshHarmonyStatus(ignored)) Notify();
    return true;
}

bool BuildSetting::RefreshHarmonyStatus(std::string& error) {
    error.clear();
    const auto* profile = FindProfile(_State.SelectedProfileId);
    if (!profile || profile->Platform != BuildPlatform::HarmonyOS) { error = "请选择鸿蒙构建配置。"; return false; }
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    if (_State.HarmonyStatus.Checking) return true;

    const auto check = ++_HarmonyCheck;
    Foundation::TaskSubmitOptions options;
    options._OwnerId = _Owner;
    const auto submission = _Tasks->SubmitTask(options,
        [build = profile->Harmony, local = _State.HarmonyLocal](Int64&, Foundation::ITaskContext& context) {
            auto* status = new HarmonyBuildStatus;
            if (!context.SetOwned(status)) { delete status; return false; }
            try {
                *status = InspectHarmonyBuildEnvironment(build, local, [&] { return context.IsCancellationRequested(); }).Status;
            } catch (const std::exception&) {
                status->Checked = true;
                status->BuildMessage = "无法检查鸿蒙构建环境。";
                status->RunMessage = status->BuildMessage;
            }
            return !context.IsCancellationRequested();
        }, [this, check](const Foundation::TaskCompletion&, bool success, const Foundation::ITaskContext& context) {
            if (check != _HarmonyCheck) return;
            _HarmonyTask = Foundation::InvalidTaskId;
            const auto* status = context.Get<HarmonyBuildStatus>();
            if (success && status) _State.HarmonyStatus = *status;
            else _State.HarmonyStatus = {};
            Notify();
        });
    if (!submission.Accepted()) { error = "无法提交鸿蒙构建环境检查。"; return false; }
    _HarmonyTask = submission._TaskId;
    _State.HarmonyStatus.Checking = true;
    Notify();
    return true;
}
