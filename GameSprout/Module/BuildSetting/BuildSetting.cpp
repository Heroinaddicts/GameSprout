#include "BuildSetting.h"
#include "IConsole.h"
#include "IIDEWindow.h"
#include "ILibraryManager.h"
#include "IPlayMode.h"
#include "IProject.h"
#include "IProjectSettings.h"
#include "ISceneManager.h"
#include "IScriptEditor.h"
#include "PlayerBuildJob.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <algorithm>
#include <exception>

void BuildSetting::BeforeInitialize(GameSproutModuleApi* api) {
    auto* editor = api->_EditorApi;
    _Project = editor->GetGameSproutModule<IProject>("Project");
    _ProjectSettings = editor->GetGameSproutModule<IProjectSettings>("ProjectSettings");
    _Assets = editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Library = editor->GetGameSproutModule<ILibraryManager>("LibraryManager");
    _Pipeline = editor->GetGameSproutModule<IAssetsPipeline>("AssetsPipeline");
    _Console = editor->GetGameSproutModule<IConsole>("Console");
    _Play = editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Scenes = editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Compiler = editor->GetGameSproutModule<IScriptCompiler>("ScriptCompiler");
    _Scripts = editor->GetGameSproutModule<IScriptEditor>("ScriptEditor");
    _IDE = editor->GetGameSproutView<IIDEWindow>("IDEWindow");
    _Tasks = api->_Foundation->TaskApi();
}

bool BuildSetting::Initialize(GameSproutModuleApi*) {
    return _Project && _ProjectSettings && _Assets && _Library && _Pipeline && _Play && _Scenes && _IDE && _Tasks && _Compiler && _Scripts;
}

bool BuildSetting::Launch(GameSproutModuleApi*) {
    _Root = QFileInfo(QString::fromStdString(_Project->GetProjectRoot())).canonicalFilePath().toStdString();
    _Owner = _Tasks->RegisterTaskOwner();
    if (_Root.empty() || _Owner == Foundation::InvalidTaskOwnerId) return false;
    _Dispatch = new QObject();
    _Subscribed = _Assets->RegisterAssetListener(this);
    return _Subscribed;
}

void BuildSetting::LaunchFinished(GameSproutModuleApi*) {
    LoadSettings(_State.Error);
    for (const auto* meta : _Assets->GetAssetMetas()) {
        if (!meta) continue;
        if (meta->_ImporterType == eAssetImporterType::Scene)
            _ScenePaths.emplace(meta->_Guid.GetGuidString(), meta->GetPath());
        else if (meta->_ImporterType == eAssetImporterType::Shader || meta->_ImporterType == eAssetImporterType::ComputeShader)
            _ShaderGuids.insert(meta->_Guid.GetGuidString());
    }
    RefreshScenePaths();
    Notify();
}

void BuildSetting::Release(GameSproutModuleApi*) {
    if (_Subscribed) _Assets->UnregisterAssetListener(this);
    _Subscribed = false;
    if (_Owner != Foundation::InvalidTaskOwnerId) _Tasks->UnregisterTaskOwner(_Owner);
    _Owner = Foundation::InvalidTaskOwnerId;
    _Task = Foundation::InvalidTaskId;
    if (_Loading) _IDE->Unloading();
    _Loading = false;
    delete _Dispatch;
    _Dispatch = nullptr;
    _Listeners.clear();
}

bool BuildSetting::RegisterListener(IBuildSettingListener* listener) {
    if (!listener || std::any_of(_Listeners.begin(), _Listeners.end(),
        [listener](const auto& entry) { return entry.second == listener; })) return false;
    _Listeners.emplace(++_NextListener, listener);
    return true;
}

void BuildSetting::UnregisterListener(IBuildSettingListener* listener) {
    std::erase_if(_Listeners, [listener](const auto& entry) { return entry.second == listener; });
}

void BuildSetting::Notify() {
    const auto snapshot = _State;
    const auto last = _NextListener;
    auto entry = _Listeners.begin();
    while (entry != _Listeners.end() && entry->first <= last) {
        const auto id = entry->first;
        try { entry->second->OnBuildSettingsChanged(snapshot); }
        catch (...) { qWarning("Build Settings listener threw an exception"); }
        entry = _Listeners.upper_bound(id);
    }
}

void BuildSetting::OnAssetCreate(const AssetEvent& event) { SceneAssetChanged(event, false); }
void BuildSetting::OnAssetModify(const AssetEvent& event) { SceneAssetChanged(event, false); }
void BuildSetting::OnAssetRemove(const AssetEvent& event) { SceneAssetChanged(event, true); }

void BuildSetting::SceneAssetChanged(const AssetEvent& event, bool removed) {
    const auto guid = event.GuidValue.GetGuidString();
    if (event.Importer == eAssetImporterType::Shader || event.Importer == eAssetImporterType::ComputeShader) {
        if (removed) _ShaderGuids.erase(guid);
        else _ShaderGuids.insert(guid);
        ++_ShaderRevision;
    }
    if (event.Importer != eAssetImporterType::Scene) return;
    const auto existing = _ScenePaths.find(guid);
    if (removed) {
        if (existing == _ScenePaths.end()) return;
        _ScenePaths.erase(existing);
    } else {
        if (existing != _ScenePaths.end() && existing->second == event.Path) return;
        _ScenePaths.insert_or_assign(guid, event.Path);
    }
    if (!_Dispatch || _SceneNotificationQueued) return;
    _SceneNotificationQueued = true;
    QMetaObject::invokeMethod(_Dispatch, [this] {
        _SceneNotificationQueued = false;
        RefreshScenePaths();
        Notify();
    }, Qt::QueuedConnection);
}

void BuildSetting::RefreshScenePaths() {
    _State.AvailableScenes.clear();
    for (const auto& [guid, path] : _ScenePaths) _State.AvailableScenes.push_back({guid, path});
    std::sort(_State.AvailableScenes.begin(), _State.AvailableScenes.end(),
        [](const auto& a, const auto& b) { return a.Path < b.Path; });
    for (auto& scene : _State.Scenes) {
        const auto found = _ScenePaths.find(scene.Guid);
        scene.Path = found == _ScenePaths.end() ? std::string{} : found->second;
    }
}

bool BuildSetting::SelectPlatform(BuildPlatform platform, std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    if (!*BuildPlatformName(platform)) { error = "未知构建平台。"; return false; }
    if (platform == _State.Selected) return true;
    if (!SaveUserSettings(platform, _State.Active, error)) return false;
    _State.Selected = platform;
    _State.Message.clear();
    _State.Error.clear();
    Notify();
    return true;
}

bool BuildSetting::SetScenes(const std::vector<BuildScene>& scenes, std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    std::vector<BuildScene> candidate;
    std::set<std::string> seen;
    for (const auto& scene : scenes) {
        const Guid guid(scene.Guid);
        if (!guid.IsValid() || !seen.insert(guid.GetGuidString()).second) {
            error = "构建场景 GUID 无效或重复。";
            return false;
        }
        const auto path = _ScenePaths.find(guid.GetGuidString());
        if (path == _ScenePaths.end() && std::none_of(_State.Scenes.begin(), _State.Scenes.end(),
            [&](const auto& current) { return current.Guid == guid.GetGuidString(); })) {
            error = "场景资源已失效。";
            return false;
        }
        candidate.push_back({guid.GetGuidString(), path == _ScenePaths.end() ? std::string{} : path->second, scene.Enabled});
    }
    if (candidate == _State.Scenes) return true;
    if (!SaveScenes(candidate, error)) return false;
    _State.Scenes = std::move(candidate);
    _State.Message.clear();
    _State.Error.clear();
    Notify();
    return true;
}

bool BuildSetting::CanStart(std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle || _Task != Foundation::InvalidTaskId || _IDE->IsLoading()) {
        error = "请等待当前构建、导入或场景操作完成。";
        return false;
    }
    const auto mode = _Play->GetState().State;
    if (mode == EditorPlayState::Playing || mode == EditorPlayState::Paused) {
        if (!_Play->Stop(error)) return false;
    }
    if (_Play->GetState().State != EditorPlayState::Edit) { error = "请等待 Play 停止后重试。"; return false; }
    return true;
}

bool BuildSetting::SwitchPlatform(std::string& error) {
    error.clear();
    if (_State.Operation != BuildOperation::Idle) { error = "请等待当前构建操作完成。"; return false; }
    if (_State.Selected == _State.Active) return true;
    if (!CanStart(error)) return false;
    PlayerBuildJob job;
    job.Operation = BuildOperation::Switching;
    job.Platform = _State.Selected;
    job.IOSSimulator = qgetenv("GAMESPROUT_IOS_SDK") == "iphonesimulator";
    if (!CollectSwitch(job, error)) return false;
    return Submit(std::move(job), error);
}

bool BuildSetting::Build(const std::string& output, bool run, std::string& error) {
    error.clear();
    if (_State.Selected != _State.Active) { error = "请先切换到选中的目标平台。"; return false; }
    if (!CanBuildPlatform(_State.Active)) { error = "当前目标的打包工具尚未接入。"; return false; }
    if (output.empty()) { error = "请选择 Player 输出路径。"; return false; }
    if (!CanStart(error)) return false;
    if (_Scenes->HasUnsavedChanges()) { error = "请先保存当前场景的修改。"; return false; }
    PlayerBuildJob job;
    job.Operation = BuildOperation::Building;
    job.Platform = _State.Active;
    job.ProjectRoot = _Root;
    job.ProjectSettings = _ProjectSettings->GetSettings();
    job.Output = output;
    job.Run = run;
    job.IOSSimulator = job.Platform == BuildPlatform::IOS && qgetenv("GAMESPROUT_IOS_SDK") == "iphonesimulator";
    if (!CollectBuild(job, error)) return false;
    return Submit(std::move(job), error);
}

bool BuildSetting::Submit(PlayerBuildJob&& job, std::string& error) {
    const auto operation = job.Operation;
    const auto platform = job.Platform;
    job.CacheRoot = QDir(QString::fromStdString(_Library->GetLibraryRoot())).filePath("BuildArtifacts").toStdString();
    job.ShaderRevision = _ShaderRevision;
    Foundation::TaskSubmitOptions options;
    options._OwnerId = _Owner;
    const std::string title = operation == BuildOperation::Switching ? "切换构建平台" : "构建 Player";
    _State.Operation = operation;
    _Loading = true;
    _IDE->Loading(title, BuildPlatformName(platform));
    const auto submission = _Tasks->SubmitTask(options,
        [job = std::move(job), pipeline = _Pipeline, compiler = _Compiler](Int64& code, Foundation::ITaskContext& context) mutable {
            auto* work = new PlayerBuildJob(std::move(job));
            if (!context.SetOwned(work)) { delete work; return false; }
            try {
                const bool success = ExecutePlayerBuild(*work, *pipeline, compiler,
                    [&context] { return context.IsCancellationRequested(); });
                code = success ? 0 : -1;
                return success;
            } catch (const std::exception& exception) { work->Error = exception.what(); }
              catch (...) { work->Error = "构建任务发生异常。"; }
            return false;
        }, [this](const Foundation::TaskCompletion&, bool success, const Foundation::ITaskContext& context) {
            Finish(success, context.Get<PlayerBuildJob>());
        });
    if (!submission.Accepted()) {
        _IDE->Unloading();
        _Loading = false;
        _State.Operation = BuildOperation::Idle;
        error = "无法提交构建任务。";
        return false;
    }
    _Task = submission._TaskId;
    _State.Error.clear();
    _State.Message = operation == BuildOperation::Switching ? "正在准备目标平台资源…" : "正在构建 Player…";
    Notify();
    return true;
}

void BuildSetting::Finish(bool success, const PlayerBuildJob* job) {
    // Publication is the final commit point. A cancellation arriving after it
    // must not report a cancelled build whose output is already installed.
    success = success || (job && job->Published);
    _Task = Foundation::InvalidTaskId;
    if (_Loading) _IDE->Unloading();
    _Loading = false;
    _State.Message.clear();
    _State.Error = success ? std::string{} : job && !job->Error.empty() ? job->Error : "操作已取消或未完成。";
    if (success && job && job->Operation == BuildOperation::Switching) {
        if (job->ShaderRevision != _ShaderRevision) _State.Error = "准备期间 Shader 资源发生变化，请重新切换。";
        else if (SaveUserSettings(_State.Selected, job->Platform, _State.Error)) {
            _State.Active = job->Platform;
            _State.Message = "已切换到 " + std::string(BuildPlatformName(job->Platform));
        }
    }
    if (success && job && job->Operation == BuildOperation::Building) {
        _State.LastOutput = job->Output;
        _State.Message = "构建完成。";
        if (job->Run && IsMacPlayerBuild(job->Platform)) {
            const QDir bundle(QString::fromStdString(job->Output));
            const auto executable = bundle.filePath("Contents/MacOS/GameSproutPlayer");
            if (QProcess::startDetached(executable, {}, QFileInfo(executable).absolutePath()))
                _State.Message = "构建完成，已启动独立 Player。";
            else _State.Error = "构建已完成，启动独立 Player 失败。";
        } else if (job->Run) {
            if (job->Launched) {
                _State.Message = "构建完成，已启动目标平台 Player。";
                if (!job->RunOutput.empty()) {
                    GameSproutConsoleLog(_Console, eConsoleLog::Trace, "{}", job->RunOutput);
                }
            }
            else _State.Error = "构建已完成，运行失败。\n" + job->RunError;
        } else if (job->Platform == BuildPlatform::IOS && !job->IOSSimulator &&
                   qEnvironmentVariableIsEmpty("GAMESPROUT_IOS_TEAM_ID")) {
            _State.Message = "已导出 iOS .app。该构建需要签名后才能安装到真机。";
        }
    }
    _State.Operation = BuildOperation::Idle;
    Notify();
}

void BuildSetting::Cancel() {
    if (_Task != Foundation::InvalidTaskId && _Tasks->CancelTask(_Task)) {
        _State.Message = "正在取消…";
        Notify();
    }
}
