#include "BuildSetting.h"
#include "ILibraryManager.h"
#include "IScriptEditor.h"
#include "PlayerBuildJob.h"
#include "../Shared/AssetArtifactIO.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace {
bool IsShader(eAssetImporterType type) {
    return type == eAssetImporterType::Shader || type == eAssetImporterType::ComputeShader;
}

bool IsScriptSource(const std::string& path) {
    const auto extension = QFileInfo(QString::fromStdString(path)).suffix().toLower();
    return extension == "h" || extension == "hpp" || extension == "hxx" || extension == "inl" ||
           extension == "cpp" || extension == "cc" || extension == "cxx";
}

bool ReadReadyManifest(const ILibraryManager& library, const IAssetMeta& meta,
                       AssetImportManifest& manifest, std::string& error) {
    if (meta.GetImportState() != eAssetImportState::Ready || !library.GetManifest(meta._Guid, manifest) ||
        manifest.SourcePath != meta.GetPath() || manifest.Target != "Portable" || manifest.Directory != "Artifact") {
        error = "资源尚无可用的导入产物 " + meta.GetPath();
        if (!meta.GetImportError().empty()) error += "\n" + meta.GetImportError();
        return false;
    }
    return true;
}

void AppendArtifacts(const QDir& artifactDirectory, const AssetImportManifest& manifest, PlayerBuildJob& job) {
    for (const auto& asset : manifest.Assets)
        job.Artifacts.push_back({manifest.GuidString, manifest.SourcePath,
            artifactDirectory.filePath(QString::fromStdString(asset.Path)).toStdString(), asset});
}
}

bool BuildSetting::CollectSwitch(PlayerBuildJob& job, std::string& error) const {
    if (AssetProfile(job) == eAssetBuildProfile::Headless) return true;
    const QDir artifacts(QDir(QString::fromStdString(_Library->GetLibraryRoot())).filePath("Artifact"));
    for (const auto& guid : _ShaderGuids) {
        const auto* meta = _Assets->QueryAsset(Guid(guid));
        if (!meta || !IsShader(meta->_ImporterType)) continue;
        AssetImportManifest manifest;
        if (!ReadReadyManifest(*_Library, *meta, manifest, error)) return false;
        AppendArtifacts(artifacts, manifest, job);
    }
    return true;
}

bool BuildSetting::CollectBuild(PlayerBuildJob& job, std::string& error) const {
    const QDir app(QCoreApplication::applicationDirPath());
    job.Template = app.filePath("PlayerTemplate/GameSproutPlayer.app").toStdString();
    if (IsMacPlayerBuild(job.Platform) &&
        !QFileInfo(QDir(QString::fromStdString(job.Template)).filePath("Contents/MacOS/GameSproutPlayer")).isExecutable()) {
        error = "找不到 Player 构建模板，请先构建 GameSproutEditor。";
        return false;
    }

    std::map<std::string, const IAssetMeta*> sourcesByPath;
    for (const auto* meta : _Assets->GetAssetMetas()) {
        if (!meta) continue;
        sourcesByPath.emplace(meta->GetPath(), meta);
        if (meta->_ImporterType == eAssetImporterType::Script && IsScriptSource(meta->GetPath()))
            job.Scripts.Sources.push_back({meta->GetPath(), meta->_Guid.GetGuidString()});
    }
    if (!job.Scripts.Sources.empty()) {
        const auto scriptState = _Scripts->GetBuildState();
        if (scriptState.PendingChanges || !scriptState.Error.empty()) {
            error = scriptState.Error.empty() ? "请等待当前 GameLogic 编译完成。" : scriptState.Error;
            return false;
        }
        const QDir project(QString::fromStdString(_Root));
        QFile assembly(project.filePath("Info/GameLogic.json"));
        Json::Value data;
        if (!assembly.open(QIODevice::ReadOnly) || assembly.size() > 4096 ||
            !AssetArtifactIO::Parse(assembly.readAll().toStdString(), data) || !data.isObject() ||
            data["Version"] != 1 || !data["Guid"].isString() || !Guid(data["Guid"].asString()).IsValid()) {
            error = "无法读取当前 GameLogic 程序集 GUID。";
            return false;
        }
        job.Scripts.AssemblyGuid = data["Guid"].asString();
        job.Scripts.AssetsRoot = project.filePath("Assets").toStdString();
        job.Scripts.BuildRoot = QDir(QString::fromStdString(_Library->GetLibraryRoot())).filePath(
            QStringLiteral("PlayerBuild/%1/GameLogic").arg(QString::fromLatin1(PlayerBuildPlatformKey(job.Platform)))).toStdString();
        job.Scripts.PrepareSourcesOnly = !IsMacPlayerBuild(job.Platform);
        job.Scripts.LockWaitMilliseconds = 60000;
    }

    const QDir artifacts(QDir(QString::fromStdString(_Library->GetLibraryRoot())).filePath("Artifact"));
    std::vector<std::string> pending;
    std::set<std::string> visited;
    for (const auto& scene : _State.Scenes) {
        if (!scene.Enabled) continue;
        const auto* meta = _Assets->QueryAsset(Guid(scene.Guid));
        if (!meta || meta->_ImporterType != eAssetImporterType::Scene) {
            error = "已启用的构建场景缺失 " + scene.Guid;
            return false;
        }
        AssetImportManifest manifest;
        if (!ReadReadyManifest(*_Library, *meta, manifest, error)) return false;
        const auto primary = std::find_if(manifest.Assets.begin(), manifest.Assets.end(),
            [](const auto& asset) { return asset.Type == eAssetType::Scene && asset.SourceKey == "main"; });
        if (primary == manifest.Assets.end()) { error = "场景缺少主产物 " + meta->GetPath(); return false; }
        job.Scenes.push_back({scene.Guid, primary->LocalID});
        pending.push_back(scene.Guid);
    }
    if (job.Scenes.empty()) { error = "请至少启用一个构建场景。"; return false; }

    // The imported document already includes prefab composition. Keep the
    // source GUID closure so embedded subassets retain their original IDs.
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const std::string guid = pending[index];
        if (!visited.insert(guid).second) continue;
        const auto* meta = _Assets->QueryAsset(Guid(guid));
        if (!meta) { error = "构建资源依赖缺失 " + guid; return false; }
        if (meta->_ImporterType == eAssetImporterType::Script) continue;
        AssetImportManifest manifest;
        if (!ReadReadyManifest(*_Library, *meta, manifest, error)) return false;
        AppendArtifacts(artifacts, manifest, job);
        pending.insert(pending.end(), manifest.ReferencedGuids.begin(), manifest.ReferencedGuids.end());
        // Model importers also declare external textures and remapped materials by path.
        for (const auto& dependency : manifest.Dependencies) {
            const auto found = sourcesByPath.find(dependency.Path);
            if (found == sourcesByPath.end()) continue;
            const auto type = found->second->_ImporterType;
            if (type == eAssetImporterType::Texture || type == eAssetImporterType::Material || IsShader(type))
                pending.push_back(found->second->_Guid.GetGuidString());
        }
    }
    return true;
}
