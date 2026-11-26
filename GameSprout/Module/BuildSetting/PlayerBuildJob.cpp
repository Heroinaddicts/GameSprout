#include "PlayerBuildJob.h"
#include "HarmonyBuildEnvironment.h"
#include "../Shared/AssetArtifactIO.h"
#include "../../PreparedResources/PreparedProjectSettings.h"
#include <QDirIterator>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#if defined(Q_OS_UNIX)
#include <csignal>
#include <unistd.h>
#endif

eAssetBuildProfile AssetProfile(const PlayerBuildJob& job) {
    switch (job.Platform) {
    case BuildPlatform::MacOS: return eAssetBuildProfile::MetalMacOS;
    case BuildPlatform::Windows: return eAssetBuildProfile::Direct3D12;
    case BuildPlatform::IOS:
        return job.IOSSimulator ? eAssetBuildProfile::MetalIOSSimulator : eAssetBuildProfile::MetalIOS;
    case BuildPlatform::MacOSServer:
    case BuildPlatform::LinuxServer: return eAssetBuildProfile::Headless;
    case BuildPlatform::Android:
    case BuildPlatform::HarmonyOS:
    case BuildPlatform::Linux: return eAssetBuildProfile::Vulkan;
    }
    return eAssetBuildProfile::Headless;
}

bool IsMacPlayerBuild(BuildPlatform platform) {
    return platform == BuildPlatform::MacOS || platform == BuildPlatform::MacOSServer;
}

bool IsLocalPlayerBuild(BuildPlatform platform) {
    return IsMacPlayerBuild(platform) || platform == BuildPlatform::Windows;
}

namespace {
bool Cancelled(PlayerBuildJob& job, const std::function<bool()>& cancelled) {
    if (!cancelled()) return false;
    job.Error = "操作已取消。";
    return true;
}

bool IsDevEcoProject(const PlayerBuildJob& job) {
    return job.Platform == BuildPlatform::HarmonyOS && job.Profile.Harmony.Output == HarmonyBuildOutput::DevEcoProject;
}

bool IsBuildOutput(const PlayerBuildJob& job, const QString& path) {
    const QFileInfo info(path);
    if (info.isSymLink()) return false;
    if (IsDevEcoProject(job)) {
        const QDir directory(path);
        QFile marker(directory.filePath(QString::fromLatin1(HarmonyDevEcoProjectMarker)));
        const QFileInfo markerInfo(marker);
        const QFileInfo profileInfo(directory.filePath("build-profile.json5"));
        return info.isDir() && markerInfo.isFile() && !markerInfo.isSymLink() && marker.open(QIODevice::ReadOnly) &&
            marker.read(64) == HarmonyDevEcoProjectMarkerText && profileInfo.isFile() && !profileInfo.isSymLink();
    }
    const auto platform = job.Platform;
    if (platform == BuildPlatform::Windows) {
        const QDir directory(path);
        QFile marker(directory.filePath(QString::fromLatin1(WindowsPlayerMarker)));
        return info.isDir() && !info.isJunction() && !QFileInfo(marker).isSymLink() &&
            marker.open(QIODevice::ReadOnly) && marker.read(64) == WindowsPlayerMarkerText &&
            QFileInfo(directory.filePath("GameSproutPlayer.exe")).isFile() &&
            QFileInfo(directory.filePath("Game/Player.json")).isFile();
    }
    if (platform == BuildPlatform::IOS)
        return info.isDir() && QFileInfo(QDir(path).filePath("Game/Player.json")).isFile();
    if (!IsMacPlayerBuild(platform)) {
        QFile file(path);
        if (!info.isFile() || !file.open(QIODevice::ReadOnly)) return false;
        const auto header = file.read(4);
        return platform == BuildPlatform::Linux || platform == BuildPlatform::LinuxServer
            ? header.startsWith(QByteArray::fromHex("1f8b08"))
            : header == QByteArray::fromHex("504b0304");
    }
    const QDir bundle(path);
    return QFileInfo(bundle.filePath("Contents/MacOS/GameSproutPlayer")).isExecutable() &&
        QFileInfo(bundle.filePath("Contents/Resources/Game/Player.json")).isFile();
}

bool ResolveOutput(PlayerBuildJob& job, QString& output) {
    const QFileInfo request(QString::fromStdString(job.Output));
    const QString parent = QFileInfo(request.absolutePath()).canonicalFilePath();
    const auto extension = QString::fromLatin1(BuildPlatformOutputSuffix(job.Profile));
    const auto suffix = extension.isEmpty() ? QString{} : '.' + extension;
    const auto name = request.fileName();
    if (parent.isEmpty() || name.isEmpty() || name == "." || name == ".." ||
        (!suffix.isEmpty() && (name.size() <= suffix.size() || !name.endsWith(suffix, Qt::CaseInsensitive)))) {
        job.Error = suffix.isEmpty() ? "请选择已有目录下的输出目录。"
                                    : "输出必须是已有目录中的 " + suffix.toStdString() + " 路径。";
        return false;
    }
    output = QDir(parent).filePath(name);
    const QDir project(QString::fromStdString(job.ProjectRoot));
#if defined(Q_OS_WIN)
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    if (output.compare(project.absolutePath(), pathCase) == 0 || project.absolutePath().startsWith(output + '/', pathCase)) {
        job.Error = "输出目录不能覆盖当前项目。";
        return false;
    }
    for (const QString& folder : {QStringLiteral("Assets"), QStringLiteral("Library"),
                                  QStringLiteral("ProjectSettings"), QStringLiteral("UserSettings"), QStringLiteral("Info")}) {
        const QString reserved = project.filePath(folder);
        if (output.compare(reserved, pathCase) == 0 || output.startsWith(reserved + '/', pathCase)) {
            job.Error = "请选择项目资源与缓存目录之外的输出路径。";
            return false;
        }
    }
    const QFileInfo current(output);
    if (current.isSymLink() || current.isJunction() || (current.exists() && !IsBuildOutput(job, output))) {
        job.Error = "输出已存在，且无法确认为 GameSprout Player 构建。";
        return false;
    }
    job.Output = output.toStdString();
    return true;
}

bool CopyTemplate(const QDir& source, const QDir& destination, PlayerBuildJob& job,
                  const std::function<bool()>& cancelled) {
    if (QFileInfo(source.absolutePath()).isSymLink() || !source.exists() || !QDir().mkpath(destination.absolutePath())) {
        job.Error = "无法创建 Player 打包暂存目录。";
        return false;
    }
    const bool windows = job.Platform == BuildPlatform::Windows;
    QDirIterator entries(source.absolutePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                         windows ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        if (Cancelled(job, cancelled)) return false;
        const auto path = entries.next();
        const auto info = entries.fileInfo();
        if (windows && info.fileName() != "GameSproutPlayer.exe" &&
            info.suffix().compare("dll", Qt::CaseInsensitive) != 0) continue;
        const auto target = destination.filePath(source.relativeFilePath(path));
        if (info.isSymLink() || !(info.isDir() ? QDir().mkpath(target) :
            info.isFile() && QDir().mkpath(QFileInfo(target).absolutePath()) && QFile::copy(path, target))) {
            job.Error = "无法复制 Player 模板文件 " + path.toStdString();
            return false;
        }
    }
    return true;
}

bool CopyVerified(const QString& source, const QString& destination, const std::string& hash,
                  PlayerBuildJob& job, const std::function<bool()>& cancelled) {
    QFile input(source), output(destination);
    if (QFileInfo(source).isSymLink() || !input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        job.Error = "无法复制构建产物 " + source.toStdString();
        return false;
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        if (Cancelled(job, cancelled)) return false;
        const QByteArray block = input.read(1024 * 1024);
        if (block.isEmpty() || output.write(block) != block.size()) {
            job.Error = "读写构建产物失败 " + source.toStdString();
            return false;
        }
        digest.addData(block);
    }
    if (input.error() != QFileDevice::NoError || !output.flush() || digest.result().toHex().toStdString() != hash) {
        job.Error = "构建产物内容发生变化，请重新构建 " + source.toStdString();
        return false;
    }
    return true;
}

bool SignBundle(const QString& bundle, PlayerBuildJob& job, const std::function<bool()>& cancelled) {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("/usr/bin/codesign", {"--force", "--sign", "-", bundle});
    if (!process.waitForStarted()) { job.Error = "无法启动 macOS 签名工具。"; return false; }
    while (!process.waitForFinished(100) && process.state() != QProcess::NotRunning) {
        if (!Cancelled(job, cancelled)) continue;
        process.kill();
        process.waitForFinished(3000);
        return false;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) return true;
    job.Error = "Player 本地签名失败\n" + process.readAll().toStdString();
    return false;
}

bool Publish(const QString& staged, const QString& destination, QTemporaryDir& temporary, PlayerBuildJob& job) {
    const QFileInfo current(destination);
    const auto backup = temporary.filePath("Previous");
    const bool replacing = current.exists();
    if (current.isSymLink() || current.isJunction() ||
        (replacing && (!IsBuildOutput(job, destination) || !QDir().rename(destination, backup)))) {
        job.Error = "无法替换现有 Player " + destination.toStdString();
        return false;
    }
    if (QDir().rename(staged, destination)) return true;
    job.Error = "无法发布 Player " + destination.toStdString();
    if (replacing && !QDir().rename(backup, destination)) {
        temporary.setAutoRemove(false);
        job.Error += "\n原构建保存在 " + backup.toStdString();
    }
    return false;
}

Json::Value SceneList(const PlayerBuildJob& job) {
    Json::Value scenes(Json::arrayValue);
    for (const auto& reference : job.Scenes) {
        Json::Value scene;
        scene["Guid"] = reference.Guid;
        scene["LocalID"] = Json::UInt64(reference.LocalID);
        scenes.append(std::move(scene));
    }
    return scenes;
}

bool PackArtifact(const QDir& destination, const PlayerBuildArtifact& item, Json::Value& artifacts,
                  PlayerBuildJob& job, const std::function<bool()>& cancelled) {
    const auto& data = item.Data;
    const auto file = QString::fromStdString(item.Guid + "_" + std::to_string(data.LocalID) + ".gsasset");
    Json::Value descriptor;
    if (!AssetArtifactIO::Parse(data.DescriptorJson, descriptor) || !descriptor.isObject()) {
        job.Error = "产物描述无效 " + item.SourcePath;
        return false;
    }
    if (!CopyVerified(QString::fromStdString(item.ArtifactPath), destination.filePath(file), data.Hash, job, cancelled)) return false;
    Json::Value artifact;
    artifact["Guid"] = item.Guid;
    artifact["LocalID"] = Json::UInt64(data.LocalID);
    artifact["Type"] = static_cast<unsigned>(data.Type);
    artifact["DataVersion"] = data.DataVersion;
    artifact["Size"] = Json::UInt64(data.DataSize);
    artifact["Descriptor"] = std::move(descriptor);
    artifacts.append(std::move(artifact));
    return true;
}

bool WritePayload(const QDir& package, PlayerBuildJob& job, const std::function<bool()>& cancelled) {
    const bool hasScripts = !job.Scripts.Sources.empty();
    if (!QDir().mkpath(package.filePath("Artifact"))) { job.Error = "无法创建 Player 资源目录。"; return false; }
    const QDir artifactDirectory(package.filePath("Artifact"));
    Json::Value manifest;
    manifest["Version"] = 1;
    manifest["Scenes"] = SceneList(job);
    manifest["GameLogic"] = hasScripts;
    manifest["Headless"] = AssetProfile(job) == eAssetBuildProfile::Headless;
    manifest["Artifacts"] = Json::Value(Json::arrayValue);
    for (const auto& artifact : job.Artifacts) {
        if (Cancelled(job, cancelled) || !PackArtifact(artifactDirectory, artifact, manifest["Artifacts"], job, cancelled)) return false;
    }
    const auto settings = ProjectSettingsFormat::EncodeDocument(job.ProjectSettings);
    if (!AssetArtifactIO::Write(package.filePath("ProjectSettings.json"),
        QByteArray::fromStdString(AssetArtifactIO::JsonText(settings)), job.Error)) return false;
    return AssetArtifactIO::Write(package.filePath("Player.json"),
        QByteArray::fromStdString(AssetArtifactIO::JsonText(manifest)), job.Error);
}

QString ExporterName(BuildPlatform platform) {
    switch (platform) {
    case BuildPlatform::Android: return QStringLiteral("Android");
    case BuildPlatform::HarmonyOS: return QStringLiteral("HarmonyOS");
    case BuildPlatform::IOS: return QStringLiteral("iOS");
    case BuildPlatform::Linux:
    case BuildPlatform::LinuxServer: return QStringLiteral("Linux");
    default: return {};
    }
}

bool RunExporter(const QString& exporter, const QString& root, const QStringList& arguments,
                 const PlayerBuildJob& job, const std::function<bool()>& cancelled, std::string& error,
                 std::string* successOutput = nullptr) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("GAMESPROUT_SOURCE_ROOT", root);
    if (job.Platform == BuildPlatform::IOS && job.IOSSimulator)
        environment.insert("GAMESPROUT_IOS_SDK", "iphonesimulator");
    if (job.Platform == BuildPlatform::HarmonyOS) {
        const auto& build = job.Profile.Harmony;
        const auto& local = job.HarmonyLocal;
        environment.insert("GAMESPROUT_APPLICATION_ID", QString::fromStdString(build.ApplicationId));
        environment.insert("GAMESPROUT_HARMONY_APP_NAME", QString::fromStdString(build.AppName));
        environment.insert("GAMESPROUT_HARMONY_VERSION_NAME", QString::fromStdString(build.VersionName));
        environment.insert("GAMESPROUT_HARMONY_VERSION_CODE", QString::number(build.VersionCode));
        environment.insert("GAMESPROUT_HARMONY_COMPATIBLE_API", QString::number(build.CompatibleApi));
        environment.insert("GAMESPROUT_HARMONY_TARGET_API", QString::number(build.TargetApi));
        environment.insert("GAMESPROUT_HARMONY_OUTPUT", IsDevEcoProject(job) ? "deveco" : "hap");
        environment.insert("GAMESPROUT_HARMONY_TOOLS", QString::fromStdString(local.ToolsPath));
        environment.insert("GAMESPROUT_HARMONY_SIGNING_CONFIG", QString::fromStdString(local.SigningConfig));
        environment.insert("GAMESPROUT_HARMONY_DEVICE", QString::fromStdString(local.DeviceId));
    }
    if (!environment.contains("GAMESPROUT_SDL_SOURCE_DIR"))
        environment.insert("GAMESPROUT_SDL_SOURCE_DIR", QDir(root).absoluteFilePath("../SDL"));
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
#if defined(Q_OS_UNIX) && QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
    process.start("/bin/bash", QStringList{exporter} + arguments);
    if (!process.waitForStarted(10000)) {
        error = process.errorString().toStdString();
        return false;
    }
    const auto pid = process.processId();
    QByteArray log;
    while (!process.waitForFinished(100) && process.state() != QProcess::NotRunning) {
        log = (log + process.readAll()).right(128 * 1024);
        if (!cancelled()) continue;
#if defined(Q_OS_UNIX) && QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#else
        process.kill();
#endif
        process.waitForFinished(5000);
        error = "操作已取消。";
        return false;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        if (successOutput)
            *successOutput = QString::fromUtf8((log + process.readAll()).right(16 * 1024)).trimmed().toStdString();
        return true;
    }
    error = QString::fromUtf8((log + process.readAll()).right(128 * 1024)).trimmed().toStdString();
    if (error.empty()) error = "目标平台工具退出，状态码 " + std::to_string(process.exitCode());
    return false;
}

bool Package(PlayerBuildJob& job, const IScriptCompiler* compiler, const std::function<bool()>& cancelled) {
    QString output;
    if (!ResolveOutput(job, output)) return false;
    QLockFile outputLock(output + ".gamesprout.lock");
    outputLock.setStaleLockTime(0);
    if (!outputLock.tryLock(0)) { job.Error = "该输出正在被另一次构建使用。"; return false; }
    QTemporaryDir temporary(QFileInfo(output).absoluteDir().filePath(".GameSproutBuild-XXXXXX"));
    if (!temporary.isValid()) { job.Error = "无法在输出目录内创建打包暂存目录。"; return false; }
    const bool windows = job.Platform == BuildPlatform::Windows;
    const auto staged = temporary.filePath(IsDevEcoProject(job) ? QStringLiteral("GameSproutDevEco") :
        windows ? QStringLiteral("GameSproutPlayer") :
        "GameSproutPlayer." + QString::fromLatin1(BuildPlatformOutputSuffix(job.Profile)));
    const bool mac = IsMacPlayerBuild(job.Platform);
    const bool local = IsLocalPlayerBuild(job.Platform);
    const QDir bundle(staged);
    const QDir payload(mac ? bundle.filePath("Contents/Resources/Game") :
        windows ? bundle.filePath("Game") : temporary.filePath("Game"));
    const auto root = QFileInfo(QString::fromUtf8(GAMESPROUT_BUILD_SOURCE_ROOT)).canonicalFilePath();
    const auto exporter = QDir(root).filePath("GameSprout/GameSproutPlayer/Platforms/" + ExporterName(job.Platform) + "/Export.sh");
    const QString platform = QString::fromLatin1(PlayerBuildPlatformKey(job.Platform));
    const QDir library(QFileInfo(QString::fromStdString(job.CacheRoot)).absolutePath());
    const auto work = library.filePath("PlayerBuild/" + platform + "/Exporter");
    QLockFile workLock(work + "/.export.lock");
    workLock.setStaleLockTime(0);
    if (local) {
        if (!CopyTemplate(QDir(QString::fromStdString(job.Template)), bundle, job, cancelled)) return false;
    } else {
        if (root.isEmpty() || !QFileInfo(exporter).isFile()) {
            job.Error = "找不到目标平台的 Player 构建工程 " + exporter.toStdString();
            return false;
        }
        if (QFileInfo(work).isSymLink() || !QDir().mkpath(work)) {
            job.Error = "无法创建目标平台构建缓存目录。";
            return false;
        }
        if (!workLock.tryLock(0)) { job.Error = "该目标平台已有构建任务。"; return false; }
    }
    ScriptCompileResult scripts;
    if (!job.Scripts.Sources.empty() && (!compiler || !compiler->Compile(job.Scripts, scripts, cancelled))) {
        job.Error = scripts.Diagnostics.empty() ? "Player GameLogic 编译失败。" : scripts.Diagnostics;
        return false;
    }
    if (!WritePayload(payload, job, cancelled)) return false;
    if (mac) {
        if (!scripts.LibraryPath.empty() && !CopyVerified(QString::fromStdString(scripts.LibraryPath),
            bundle.filePath("Contents/MacOS/libGameLogic.dylib"), scripts.LibraryHash, job, cancelled)) return false;
        if (!SignBundle(staged, job, cancelled)) return false;
    } else if (windows) {
        if (!scripts.LibraryPath.empty() && !CopyVerified(QString::fromStdString(scripts.LibraryPath),
            bundle.filePath("GameLogic.dll"), scripts.LibraryHash, job, cancelled)) return false;
        if (!AssetArtifactIO::Write(bundle.filePath(QString::fromLatin1(WindowsPlayerMarker)),
            QByteArray(WindowsPlayerMarkerText), job.Error)) return false;
    } else {
        QStringList arguments{"build", payload.absolutePath(), QString::fromStdString(scripts.SourcesDirectory),
            staged, work, QString::fromUtf8(GAMESPROUT_BUILD_CMAKE)};
        if (job.Platform == BuildPlatform::Linux || job.Platform == BuildPlatform::LinuxServer) arguments << platform;
        if (!RunExporter(exporter, root, arguments, job, cancelled, job.Error)) return false;
        if (!IsBuildOutput(job, staged)) { job.Error = "目标平台工具没有生成预期的构建产物。"; return false; }
    }
    if (Cancelled(job, cancelled)) return false;
    job.Published = Publish(staged, output, temporary, job);
    if (job.Published && job.Run && !local) {
        QStringList arguments{"run", output};
        if (job.Platform == BuildPlatform::Linux || job.Platform == BuildPlatform::LinuxServer) arguments << platform;
        job.Launched = RunExporter(exporter, root, arguments, job, cancelled, job.RunError, &job.RunOutput);
    }
    return job.Published;
}
}

bool ExecutePlayerBuild(PlayerBuildJob& job, const IAssetsPipeline& pipeline, const IScriptCompiler* compiler,
                        const std::function<bool()>& cancelled) {
    if (job.Operation == BuildOperation::Building && job.Platform == BuildPlatform::HarmonyOS) {
        auto environment = InspectHarmonyBuildEnvironment(job.Profile.Harmony, job.HarmonyLocal, cancelled);
        if (Cancelled(job, cancelled)) return false;
        if (!environment.Status.CanBuild || (job.Run && !environment.Status.CanRun)) {
            job.Error = environment.Status.CanBuild ? environment.Status.RunMessage : environment.Status.BuildMessage;
            return false;
        }
        job.HarmonyLocal.ToolsPath = std::move(environment.ToolsPath);
        job.HarmonyLocal.SigningConfig = std::move(environment.SigningConfig);
        job.HarmonyLocal.DeviceId = std::move(environment.DeviceId);
    }
    for (auto& artifact : job.Artifacts) {
        if (Cancelled(job, cancelled)) return false;
        if (artifact.Data.Type != eAssetType::Shader && artifact.Data.Type != eAssetType::ComputeShader) continue;
        AssetBuildArtifactResult result;
        if (!pipeline.PrepareBuildArtifact({artifact.Data, artifact.ArtifactPath, job.CacheRoot, AssetProfile(job), cancelled},
                                           result, job.Error)) {
            job.Error = artifact.SourcePath + "\n" + job.Error;
            return false;
        }
        artifact.Data = std::move(result.Artifact);
        artifact.ArtifactPath = std::move(result.ArtifactPath);
    }
    if (Cancelled(job, cancelled)) return false;
    return job.Operation == BuildOperation::Switching || Package(job, compiler, cancelled);
}
