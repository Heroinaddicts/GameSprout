#include "HarmonyBuildEnvironment.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace {
QString FindTool(const QString& name, const QProcessEnvironment& environment) {
    const auto paths = environment.value(QStringLiteral("PATH")).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    return QStandardPaths::findExecutable(name, paths);
}

bool RunQuery(const QString& tool, const QStringList& arguments, const QProcessEnvironment& environment,
              QString& output, const std::function<bool()>& cancelled) {
    if (tool.isEmpty() || cancelled()) return false;
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(tool, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(1000)) return false;

    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(50) && process.state() != QProcess::NotRunning) {
        if (cancelled() || timer.hasExpired(3000)) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
    }
    output = QString::fromUtf8(process.readAll()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString ToolchainPath(const HarmonyLocalBuildSettings& settings, const QProcessEnvironment& environment) {
    QString path = QString::fromStdString(settings.ToolsPath);
    if (path.isEmpty()) path = environment.value(QStringLiteral("GAMESPROUT_HARMONY_TOOLS"));
    if (!path.isEmpty()) {
        if (path.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) path = QDir(path).filePath(QStringLiteral("Contents"));
        return QFileInfo(path).absoluteFilePath();
    }
    if (!FindTool(QStringLiteral("hvigorw"), environment).isEmpty()) return {};
    for (const auto* candidate : {"/Applications/DevEco-Studio.app/Contents", "/Applications/DevEco Studio.app/Contents"}) {
        const QString root = QString::fromUtf8(candidate);
        if (QFileInfo(QDir(root).filePath(QStringLiteral("tools/hvigor/bin/hvigorw"))).isExecutable()) return root;
    }
    return {};
}

void ConfigureToolPaths(const QString& root, QProcessEnvironment& environment) {
    QStringList paths;
    if (!root.isEmpty()) {
        const QDir tools(root);
        environment.insert(QStringLiteral("DEVECO_SDK_HOME"), tools.filePath(QStringLiteral("sdk")));
        for (const auto* folder : {"bin", "tool/node/bin", "hvigor/bin", "ohpm/bin", "tools/hvigor/bin", "tools/ohpm/bin",
                                   "tools/node/bin"})
            paths.append(tools.filePath(QString::fromUtf8(folder)));
    }
    const auto sdkPath = environment.value(QStringLiteral("DEVECO_SDK_HOME"));
    if (!sdkPath.isEmpty()) paths.prepend(QDir(sdkPath).filePath(QStringLiteral("default/openharmony/toolchains")));
    paths.append(environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("PATH"), paths.join(QDir::listSeparator()));
}

bool HasSdlTemplate(const QProcessEnvironment& environment) {
    auto path = environment.value(QStringLiteral("GAMESPROUT_SDL_SOURCE_DIR"));
    if (path.isEmpty()) path = QDir(QString::fromUtf8(GAMESPROUT_BUILD_SOURCE_ROOT)).filePath(QStringLiteral("../SDL"));
    const QDir source(path);
    return QFileInfo(source.filePath(QStringLiteral("openharmony-project/entry/src/main/ets/entryability/EntryAbility.ets"))).isFile();
}

QJsonObject ReadJsonObject(const QString& path) {
    QFile file(path);
    constexpr qint64 maximumSize = 4 * 1024 * 1024;
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > maximumSize) return {};
    const auto data = file.read(maximumSize + 1);
    if (data.size() > maximumSize) return {};
    return QJsonDocument::fromJson(data).object();
}

int ApiLevel(const QString& version) {
    static const QRegularExpression pattern(QStringLiteral(R"(^(\d+)(?:\.\d+\.\d+)?$)"));
    return pattern.match(version.trimmed()).captured(1).toInt();
}

int InstalledApi(const QProcessEnvironment& environment) {
    const auto sdkPath = environment.value(QStringLiteral("DEVECO_SDK_HOME"));
    if (sdkPath.isEmpty()) return 0;
    const QDir sdk(sdkPath);
    const auto api = ReadJsonObject(sdk.filePath(QStringLiteral("default/sdk-pkg.json")))
        .value(QStringLiteral("data")).toObject().value(QStringLiteral("apiVersion"));
    return api.isDouble() ? api.toInt() : ApiLevel(api.toString());
}

bool HasJava(const QString& toolsPath, QProcessEnvironment environment, const std::function<bool()>& cancelled) {
    auto javaHome = environment.value(QStringLiteral("JAVA_HOME"));
    if (javaHome.isEmpty() && !toolsPath.isEmpty()) {
        const auto bundled = QDir(toolsPath).filePath(QStringLiteral("jbr/Contents/Home"));
        if (QFileInfo(QDir(bundled).filePath(QStringLiteral("bin/java"))).isExecutable()) javaHome = bundled;
    }
    if (javaHome.isEmpty()) {
        QString activeHome;
        if (RunQuery(QStringLiteral("/usr/libexec/java_home"), {QStringLiteral("-v"), QStringLiteral("17+")},
                     environment, activeHome, cancelled)) javaHome = activeHome;
    }
    if (!javaHome.isEmpty()) {
        const auto javaBin = QDir(javaHome).filePath(QStringLiteral("bin"));
        environment.insert(QStringLiteral("PATH"), javaBin + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    }
    QString version;
    if (!RunQuery(FindTool(QStringLiteral("java"), environment), {QStringLiteral("-version")}, environment, version, cancelled))
        return false;
    static const QRegularExpression pattern(QStringLiteral(R"re(\bversion\s+"(\d+))re"));
    return pattern.match(version).captured(1).toInt() >= 17;
}

QString SigningPath(const HarmonyLocalBuildSettings& settings, const QProcessEnvironment& environment) {
    switch (settings.SigningMode) {
    case HarmonySigningMode::ConfigurationFile: return QString::fromStdString(settings.SigningConfig);
    case HarmonySigningMode::Environment: return environment.value(QStringLiteral("GAMESPROUT_HARMONY_SIGNING_CONFIG"));
    case HarmonySigningMode::Unsigned: return {};
    }
    return {};
}

QString CheckSigningConfig(const QString& path) {
    const auto configuration = ReadJsonObject(path);
    const auto material = configuration.value(QStringLiteral("material")).toObject();
    if (configuration.value(QStringLiteral("name")).toString().trimmed().isEmpty() || material.isEmpty())
        return QStringLiteral("签名文件应是一个 DevEco signingConfigs JSON 对象，包含 name 和 material。");
    for (const auto* field : {"certpath", "profile", "storeFile", "keyAlias", "keyPassword", "storePassword", "signAlg"}) {
        const auto name = QString::fromUtf8(field);
        if (material.value(name).toString().isEmpty()) return QStringLiteral("鸿蒙签名配置缺少 %1。").arg(name);
    }
    const QDir configDirectory(QFileInfo(path).absolutePath());
    for (const auto* field : {"certpath", "profile", "storeFile"}) {
        const auto name = QString::fromUtf8(field);
        auto value = material.value(name).toString();
        if (value.startsWith("~/")) value = QDir::home().filePath(value.mid(2));
        const QFileInfo file(configDirectory.absoluteFilePath(value));
        if (!file.isFile() || !file.isReadable()) return QStringLiteral("无法读取鸿蒙签名配置中的 %1 文件。").arg(name);
    }
    return {};
}

bool ReadDevices(const QString& hdc, const QProcessEnvironment& environment, std::vector<HarmonyBuildDevice>& devices,
                 const std::function<bool()>& cancelled) {
    QString output;
    if (!RunQuery(hdc, {QStringLiteral("list"), QStringLiteral("targets"), QStringLiteral("-v")}, environment, output, cancelled))
        return false;
    if (output.isEmpty() || output == QStringLiteral("[Empty]")) return true;
    for (const auto& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto fields = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 3 || (fields[1] != QStringLiteral("USB") && fields[1] != QStringLiteral("TCP"))) continue;
        HarmonyBuildDevice device;
        device.Id = fields[0].toStdString();
        device.Name = fields[1].toStdString();
        device.Status = fields[2].toStdString();
        device.Connected = fields[2] == QStringLiteral("Connected");
        devices.push_back(std::move(device));
    }
    return !devices.empty();
}

QString CheckRunDevice(const QString& hdc, const QProcessEnvironment& environment, const HarmonyBuildSettings& build,
                       const HarmonyLocalBuildSettings& local, HarmonyBuildEnvironment& result, bool listed,
                       const std::function<bool()>& cancelled) {
    if (hdc.isEmpty()) return QStringLiteral("缺少 hdc，无法查找鸿蒙真机。");
    if (!listed) return QStringLiteral("无法读取 HDC 设备列表，请刷新设备。");
    const auto requestedDevice = local.DeviceId.empty()
        ? environment.value(QStringLiteral("GAMESPROUT_HARMONY_DEVICE")).toStdString() : local.DeviceId;
    auto& devices = result.Status.Devices;
    const auto selected = std::find_if(devices.begin(), devices.end(), [&](const HarmonyBuildDevice& device) {
        return requestedDevice.empty() ? device.Connected : device.Id == requestedDevice;
    });
    if (selected == devices.end() || !selected->Connected) {
        if (selected != devices.end() && selected->Status == "Unauthorized") return QStringLiteral("请在所选设备上允许 USB 调试。");
        if (!requestedDevice.empty()) return QStringLiteral("所选鸿蒙设备未连接，请刷新或换选设备。");
        if (std::any_of(devices.begin(), devices.end(), [](const HarmonyBuildDevice& device) { return device.Status == "Unauthorized"; }))
            return QStringLiteral("请在鸿蒙设备上允许 USB 调试。");
        return QStringLiteral("请连接一台鸿蒙设备并开启 USB 调试。");
    }
    if (requestedDevice.empty() && std::count_if(devices.begin(), devices.end(), [](const HarmonyBuildDevice& device) {
        return device.Connected;
    }) > 1) return QStringLiteral("已连接多台鸿蒙设备，请选择运行设备。");

    QString apiOutput;
    if (!RunQuery(hdc, {QStringLiteral("-t"), QString::fromStdString(selected->Id), QStringLiteral("shell"),
                       QStringLiteral("param get const.ohos.apiversion")}, environment, apiOutput, cancelled))
        return QStringLiteral("所选设备无法读取系统版本，请刷新设备后重试。");
    const auto deviceApi = ApiLevel(apiOutput);
    if (deviceApi <= 0) return QStringLiteral("所选设备未返回有效的鸿蒙 API 版本。");
    if (deviceApi < build.CompatibleApi)
        return QStringLiteral("设备 API %1 低于构建配置要求的最低 API %2。").arg(deviceApi).arg(build.CompatibleApi);
    selected->Status += " · API " + std::to_string(deviceApi);
    result.DeviceId = selected->Id;
    return {};
}
}

HarmonyBuildEnvironment InspectHarmonyBuildEnvironment(const HarmonyBuildSettings& build,
                                                       const HarmonyLocalBuildSettings& local,
                                                       const std::function<bool()>& cancelled) {
    HarmonyBuildEnvironment result;
    auto& status = result.Status;
    status.Checked = true;
    auto environment = QProcessEnvironment::systemEnvironment();
    const auto toolchain = ToolchainPath(local, environment);
    result.ToolsPath = toolchain.toStdString();
    ConfigureToolPaths(toolchain, environment);

    const auto hdc = FindTool(QStringLiteral("hdc"), environment);
    const bool listed = ReadDevices(hdc, environment, status.Devices, cancelled);
    if (cancelled()) return result;

    if (FindTool(QStringLiteral("python3"), environment).isEmpty()) {
        status.BuildMessage = "缺少生成鸿蒙工程使用的 Python 3。";
    } else if (!HasSdlTemplate(environment)) {
        status.BuildMessage = "找不到 SDL 的鸿蒙 Stage 工程模板。";
    } else if (build.Output == HarmonyBuildOutput::DevEcoProject) {
        status.CanBuild = true;
        status.BuildMessage = "可以导出 DevEco 工程。可在 DevEco Studio 中选择 SDK 并配置签名。";
        status.RunMessage = "DevEco 工程需在 DevEco Studio 中构建和运行。";
        return result;
    } else if (!toolchain.isEmpty() && !QFileInfo(toolchain).isDir()) {
        status.BuildMessage = "找不到配置的鸿蒙工具链目录。";
    } else if (FindTool(QStringLiteral("hvigorw"), environment).isEmpty()) {
        status.BuildMessage = "找不到 Hvigor，请选择 DevEco Studio 或鸿蒙命令行工具目录。";
    } else if (FindTool(QStringLiteral("node"), environment).isEmpty()) {
        status.BuildMessage = "找不到 Hvigor 使用的 Node.js。";
    } else if (!HasJava(toolchain, environment, cancelled)) {
        status.BuildMessage = "找不到可运行的 JDK 17 或更高版本，请检查 DevEco 工具链或 JAVA_HOME。";
    }
    if (!status.BuildMessage.empty() || cancelled()) {
        status.RunMessage = status.BuildMessage;
        return result;
    }

    const auto sdkApi = InstalledApi(environment);
    if (sdkApi > 0 && build.TargetApi > sdkApi) {
        status.BuildMessage = QStringLiteral("已安装 SDK 为 API %1，低于构建配置的目标 API %2。").arg(sdkApi).arg(build.TargetApi).toStdString();
        status.RunMessage = status.BuildMessage;
        return result;
    }

    const auto signing = SigningPath(local, environment);
    if (signing.isEmpty() && local.SigningMode == HarmonySigningMode::ConfigurationFile) {
        status.BuildMessage = "请选择本机鸿蒙签名配置 JSON。";
        status.RunMessage = status.BuildMessage;
        return result;
    }
    if (!signing.isEmpty()) {
        const auto signingError = CheckSigningConfig(signing);
        if (!signingError.isEmpty()) {
            status.BuildMessage = signingError.toStdString();
            status.RunMessage = status.BuildMessage;
            return result;
        }
        result.SigningConfig = QFileInfo(signing).absoluteFilePath().toStdString();
    }

    status.CanBuild = true;
    const auto description = signing.isEmpty() ? QStringLiteral("可以构建未签名 HAP。") : QStringLiteral("可以构建签名 HAP。签名材料由 Hvigor 校验。");
    status.BuildMessage = (sdkApi > 0 ? QStringLiteral("SDK API %1 · ").arg(sdkApi) + description : description).toStdString();
    const auto runError = CheckRunDevice(hdc, environment, build, local, result, listed, cancelled);
    if (signing.isEmpty()) {
        status.RunMessage = "真机安装需要有效的调试签名。请选择本机签名配置。";
        return result;
    }
    status.CanRun = runError.isEmpty();
    status.RunMessage = status.CanRun ? "已连接兼容的设备。安装时由设备校验调试签名。" : runError.toStdString();
    return result;
}
