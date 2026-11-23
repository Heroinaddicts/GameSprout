#include "BuildSettingUI.h"
#include "ui_BuildSettingUI.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QSignalBlocker>

void BuildSettingUI::InitializeHarmonyForm() {
    _Ui->harmonyOutput->setItemData(0, static_cast<int>(HarmonyBuildOutput::Hap));
    _Ui->harmonyOutput->setItemData(1, static_cast<int>(HarmonyBuildOutput::DevEcoProject));
    _Ui->signingMode->setItemData(0, static_cast<int>(HarmonySigningMode::Environment));
    _Ui->signingMode->setItemData(1, static_cast<int>(HarmonySigningMode::Unsigned));
    _Ui->signingMode->setItemData(2, static_cast<int>(HarmonySigningMode::ConfigurationFile));
    const auto refreshDraftActions = [this] {
        if (!_RefreshingHarmonyForm) RefreshActions();
    };
    for (auto* field : {_Ui->applicationId, _Ui->appName, _Ui->versionName, _Ui->toolsPath, _Ui->signingConfig})
        QObject::connect(field, &QLineEdit::textChanged, _Window, refreshDraftActions);
    for (auto* field : {_Ui->versionCode, _Ui->compatibleApi, _Ui->targetApi})
        QObject::connect(field, qOverload<int>(&QSpinBox::valueChanged), _Window, refreshDraftActions);
    for (auto* field : {_Ui->harmonyOutput, _Ui->signingMode, _Ui->runDevice})
        QObject::connect(field, qOverload<int>(&QComboBox::currentIndexChanged), _Window, refreshDraftActions);
    QObject::connect(_Ui->saveHarmonyProjectButton, &QPushButton::clicked, _Window, [this] { SaveHarmonyProject(); });
    QObject::connect(_Ui->resetHarmonyProjectButton, &QPushButton::clicked, _Window, [this] { RefreshHarmonyProject(); });
    QObject::connect(_Ui->saveHarmonyLocalButton, &QPushButton::clicked, _Window, [this] { SaveHarmonyLocal(); });
    QObject::connect(_Ui->resetHarmonyLocalButton, &QPushButton::clicked, _Window, [this] { RefreshHarmonyLocal(); });
    QObject::connect(_Ui->browseToolsButton, &QPushButton::clicked, _Window, [this] { BrowseHarmonyTools(); });
    QObject::connect(_Ui->browseSigningButton, &QPushButton::clicked, _Window, [this] { BrowseHarmonySigning(); });
    QObject::connect(_Ui->refreshDevicesButton, &QPushButton::clicked, _Window, [this] { RefreshHarmonyStatus(); });
}

void BuildSettingUI::RefreshHarmonyControls(bool harmony, bool hasProjectEdits, bool hasLocalEdits) {
    const bool idle = _State.Operation == BuildOperation::Idle;
    const auto& harmonyStatus = _State.HarmonyStatus;
    _Ui->harmonyPage->setEnabled(idle);
    _Ui->saveHarmonyProjectButton->setEnabled(hasProjectEdits);
    _Ui->resetHarmonyProjectButton->setEnabled(hasProjectEdits);
    _Ui->saveHarmonyLocalButton->setEnabled(hasLocalEdits);
    _Ui->resetHarmonyLocalButton->setEnabled(hasLocalEdits);
    const bool signingFile = HarmonyLocalDraft().SigningMode == HarmonySigningMode::ConfigurationFile;
    _Ui->signingConfig->setEnabled(signingFile);
    _Ui->browseSigningButton->setEnabled(signingFile);
    _Ui->refreshDevicesButton->setEnabled(idle && !hasLocalEdits && !harmonyStatus.Checking);
    QString statusText;
    if (harmony && harmonyStatus.Checking) statusText = Translate("正在检查鸿蒙工具链、签名和设备…");
    else if (harmony) {
        const auto buildStatus = QString::fromStdString(harmonyStatus.BuildMessage);
        const auto runStatus = QString::fromStdString(harmonyStatus.RunMessage);
        statusText = buildStatus;
        if (!runStatus.isEmpty() && runStatus != buildStatus) {
            if (!statusText.isEmpty()) statusText += QLatin1Char('\n');
            statusText += runStatus;
        }
    }
    _Ui->harmonyStatusLabel->setText(statusText);
    _Ui->harmonyStatusLabel->setVisible(!statusText.isEmpty());
}

void BuildSettingUI::RefreshHarmonyProject() {
    const auto* profile = SelectedProfile();
    if (!profile) return;
    const auto& harmony = profile->Harmony;
    {
        const QScopedValueRollback changing(_RefreshingHarmonyForm, true);
        _Ui->harmonyOutput->setCurrentIndex(_Ui->harmonyOutput->findData(static_cast<int>(harmony.Output)));
        _Ui->applicationId->setText(QString::fromStdString(harmony.ApplicationId));
        _Ui->appName->setText(QString::fromStdString(harmony.AppName));
        _Ui->versionName->setText(QString::fromStdString(harmony.VersionName));
        _Ui->versionCode->setValue(harmony.VersionCode);
        _Ui->compatibleApi->setValue(harmony.CompatibleApi);
        _Ui->targetApi->setValue(harmony.TargetApi);
    }
    RefreshActions();
}

void BuildSettingUI::RefreshHarmonyLocal() {
    const auto& local = _State.HarmonyLocal;
    {
        const QScopedValueRollback changing(_RefreshingHarmonyForm, true);
        _Ui->toolsPath->setText(QString::fromStdString(local.ToolsPath));
        _Ui->signingMode->setCurrentIndex(_Ui->signingMode->findData(static_cast<int>(local.SigningMode)));
        _Ui->signingConfig->setText(QString::fromStdString(local.SigningConfig));
        RefreshHarmonyDevices(local.DeviceId);
    }
    RefreshActions();
}

void BuildSettingUI::RefreshHarmonyDevices(const std::string& device) {
    const QSignalBlocker blocked(_Ui->runDevice);
    _Ui->runDevice->clear();
    _Ui->runDevice->addItem(Translate("自动选择"), QString{});
    for (const auto& entry : _State.HarmonyStatus.Devices) {
        const auto id = QString::fromStdString(entry.Id);
        const auto name = QString::fromStdString(entry.Name);
        const auto status = QString::fromStdString(entry.Status);
        QString label = name.isEmpty() ? id : Translate("%1 · %2").arg(name, id);
        if (!status.isEmpty()) label = Translate("%1（%2）").arg(label, status);
        else if (!entry.Connected) label = Translate("%1（未连接）").arg(label);
        _Ui->runDevice->addItem(label, id);
    }
    const auto selected = QString::fromStdString(device);
    const int index = _Ui->runDevice->findData(selected);
    if (index >= 0) _Ui->runDevice->setCurrentIndex(index);
    else if (!device.empty()) {
        _Ui->runDevice->addItem(Translate("%1（未检测到）").arg(selected), selected);
        _Ui->runDevice->setCurrentIndex(_Ui->runDevice->count() - 1);
    }
}

void BuildSettingUI::RefreshHarmonyStatus() {
    if (!_Settings || _State.Selected != BuildPlatform::HarmonyOS || _State.HarmonyStatus.Checking ||
        _State.Operation != BuildOperation::Idle || HasHarmonyLocalEdits()) return;
    std::string error;
    if (!_Settings->RefreshHarmonyStatus(error)) ShowError(error);
}

HarmonyBuildSettings BuildSettingUI::HarmonyProjectDraft() const {
    HarmonyBuildSettings settings;
    settings.ApplicationId = _Ui->applicationId->text().trimmed().toStdString();
    settings.AppName = _Ui->appName->text().trimmed().toStdString();
    settings.VersionName = _Ui->versionName->text().trimmed().toStdString();
    settings.VersionCode = _Ui->versionCode->value();
    settings.CompatibleApi = _Ui->compatibleApi->value();
    settings.TargetApi = _Ui->targetApi->value();
    settings.Output = static_cast<HarmonyBuildOutput>(_Ui->harmonyOutput->currentData().toInt());
    return settings;
}

HarmonyLocalBuildSettings BuildSettingUI::HarmonyLocalDraft() const {
    HarmonyLocalBuildSettings local;
    local.ToolsPath = _Ui->toolsPath->text().trimmed().toStdString();
    local.SigningConfig = _Ui->signingConfig->text().trimmed().toStdString();
    local.DeviceId = _Ui->runDevice->currentData().toString().toStdString();
    local.SigningMode = static_cast<HarmonySigningMode>(_Ui->signingMode->currentData().toInt());
    return local;
}

bool BuildSettingUI::HasHarmonyProjectEdits() const {
    const auto* profile = SelectedProfile();
    return profile && profile->Platform == BuildPlatform::HarmonyOS && HarmonyProjectDraft() != profile->Harmony;
}

bool BuildSettingUI::HasHarmonyLocalEdits() const {
    return _State.Selected == BuildPlatform::HarmonyOS && HarmonyLocalDraft() != _State.HarmonyLocal;
}

bool BuildSettingUI::ConfirmDiscardHarmonyEdits() {
    if (!HasHarmonyProjectEdits() && !HasHarmonyLocalEdits()) return true;
    return QMessageBox::question(_Window, Translate("切换构建配置"), Translate("放弃未保存的鸿蒙设置？"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void BuildSettingUI::SaveHarmonyProject() {
    const auto* profile = SelectedProfile();
    if (!_Settings || !profile || !HasHarmonyProjectEdits()) return;
    auto draft = *profile;
    draft.Harmony = HarmonyProjectDraft();
    std::string error;
    if (!_Settings->SetProfile(draft, error)) ShowError(error);
}

void BuildSettingUI::SaveHarmonyLocal() {
    if (!_Settings || !HasHarmonyLocalEdits()) return;
    std::string error;
    if (!_Settings->SetHarmonyLocal(HarmonyLocalDraft(), error)) ShowError(error);
}

void BuildSettingUI::BrowseHarmonyTools() {
    const QString selected = QFileDialog::getExistingDirectory(_Window, Translate("选择鸿蒙工具链目录"),
        _Ui->toolsPath->text());
    if (!selected.isEmpty()) _Ui->toolsPath->setText(selected);
}

void BuildSettingUI::BrowseHarmonySigning() {
    const QString selected = QFileDialog::getOpenFileName(_Window, Translate("选择本机 DevEco 签名配置"),
        _Ui->signingConfig->text(), Translate("JSON 文件 (*.json);;所有文件 (*)"));
    if (!selected.isEmpty()) _Ui->signingConfig->setText(selected);
}
