#include "BuildSettingUI.h"
#include "ui_BuildOutputDialog.h"
#include "ui_BuildProfileNameDialog.h"
#include "ui_BuildProjectOutputDialog.h"
#include "ui_BuildSettingUI.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QSignalBlocker>
#include <algorithm>

QString BuildSettingUI::Translate(const char* source) {
    return QCoreApplication::translate("BuildSettingUIWindow", source);
}

namespace {
int SelectedSceneRow(const QTreeWidget& tree) {
    return tree.indexOfTopLevelItem(tree.currentItem());
}

bool IsGameSproutDevEcoProject(const QString& path) {
    QFile marker(QDir(path).filePath(QString::fromLatin1(HarmonyDevEcoProjectMarker)));
    const QFileInfo info(marker);
    return info.isFile() && !info.isSymLink() && marker.open(QIODevice::ReadOnly) &&
        marker.read(64) == HarmonyDevEcoProjectMarkerText;
}
}

BuildSettingUI* BuildSettingUI::_Self = nullptr;

BuildSettingUI::~BuildSettingUI() { Release(nullptr); }

void BuildSettingUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    _Self = this;
    if (!editor) return;
    _IDE = editor->GetGameSproutView<IIDEWindow>("IDEWindow");
    _Settings = editor->GetGameSproutModule<IBuildSetting>("BuildSetting");
}

bool BuildSettingUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if (_Window) return true;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) return false;
    _Window = new QDialog();
    _Window->setAttribute(Qt::WA_QuitOnClose, false);
    _Ui = new Ui::BuildSettingUIWindow();
    _Ui->setupUi(_Window);
    _OutputDialog = new QFileDialog(_Window);
    _OutputDialog->setObjectName({});
    Ui::BuildOutputDialog output;
    output.setupUi(_OutputDialog);
    _ProfileDialog = new QDialog(_Window);
    _ProfileUi = new Ui::BuildProfileNameDialog();
    _ProfileUi->setupUi(_ProfileDialog);
    _ProjectOutputDialog = new QDialog(_Window);
    _ProjectOutputUi = new Ui::BuildProjectOutputDialog();
    _ProjectOutputUi->setupUi(_ProjectOutputDialog);

    _Ui->platformTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _Ui->platformTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _Ui->sceneTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _Ui->sceneTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (const auto platform : BuildPlatforms) {
        auto* item = new QTreeWidgetItem(_Ui->platformTree);
        item->setText(0, QString::fromUtf8(BuildPlatformName(platform)));
        item->setData(0, Qt::UserRole, static_cast<int>(platform));
    }

    QObject::connect(_Ui->platformTree, &QTreeWidget::currentItemChanged, _Window,
        [this](QTreeWidgetItem* item) { SelectPlatform(item); });
    QObject::connect(_Ui->profileList, qOverload<int>(&QComboBox::currentIndexChanged), _Window,
        [this](int index) { SelectProfile(index); });
    QObject::connect(_Ui->addProfileButton, &QPushButton::clicked, _Window,
        [this] { OpenProfileNameDialog(false); });
    QObject::connect(_Ui->renameProfileButton, &QPushButton::clicked, _Window,
        [this] { OpenProfileNameDialog(true); });
    QObject::connect(_Ui->removeProfileButton, &QPushButton::clicked, _Window, [this] { RemoveProfile(); });
    QObject::connect(_Ui->overrideScenes, &QCheckBox::toggled, _Window,
        [this](bool enabled) { SetSceneOverride(enabled); });
    QObject::connect(_ProfileUi->buttons, &QDialogButtonBox::accepted, _ProfileDialog,
        [this] { SaveProfileName(); });
    QObject::connect(_ProfileUi->buttons, &QDialogButtonBox::rejected, _ProfileDialog, &QDialog::reject);
    QObject::connect(_ProjectOutputUi->buttons, &QDialogButtonBox::accepted, _ProjectOutputDialog,
        [this] { BuildProject(); });
    QObject::connect(_ProjectOutputUi->buttons, &QDialogButtonBox::rejected, _ProjectOutputDialog, &QDialog::reject);
    QObject::connect(_ProjectOutputUi->browseFolderButton, &QPushButton::clicked, _ProjectOutputDialog, [this] {
        const QString directory = QFileDialog::getExistingDirectory(_ProjectOutputDialog,
            Translate("选择 DevEco 工程的上一级目录"), _ProjectOutputUi->parentFolder->text());
        if (!directory.isEmpty()) _ProjectOutputUi->parentFolder->setText(directory);
    });
    QObject::connect(_Ui->sceneTree, &QTreeWidget::itemChanged, _Window,
        [this](QTreeWidgetItem* item, int column) { ChangeSceneEnabled(item, column); });
    QObject::connect(_Ui->sceneTree, &QTreeWidget::currentItemChanged, _Window,
        [this] { RefreshActions(); });
    QObject::connect(_Ui->addSceneButton, &QPushButton::clicked, _Window, [this] { AddScene(); });
    QObject::connect(_Ui->removeSceneButton, &QPushButton::clicked, _Window, [this] { RemoveScene(); });
    QObject::connect(_Ui->moveUpButton, &QPushButton::clicked, _Window, [this] { MoveScene(-1); });
    QObject::connect(_Ui->moveDownButton, &QPushButton::clicked, _Window, [this] { MoveScene(1); });
    QObject::connect(_Ui->switchButton, &QPushButton::clicked, _Window, [this] { SwitchPlatform(); });
    QObject::connect(_Ui->buildButton, &QPushButton::clicked, _Window, [this] { PickOutput(false); });
    QObject::connect(_Ui->buildAndRunButton, &QPushButton::clicked, _Window, [this] { PickOutput(true); });
    QObject::connect(_Ui->cancelButton, &QPushButton::clicked, _Window, [this] { if (_Settings) _Settings->Cancel(); });
    QObject::connect(_Ui->closeButton, &QPushButton::clicked, _Window, &QDialog::hide);
    QObject::connect(_OutputDialog, &QFileDialog::accepted, _Window, [this] {
        const QStringList files = _OutputDialog->selectedFiles();
        if (!files.isEmpty()) Build(files.front());
    });

    InitializeHarmonyForm();
    return true;
}

bool BuildSettingUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if (!_IDE || !_Settings || !_Window) return false;
    _Subscribed = _Settings->RegisterListener(this);
    if (!_Subscribed) return false;

    auto* bar = _IDE->GetMenuBar();
    auto* fileMenu = bar ? bar->CreateChildMenu(_Ui->fileMenuAction->text()) : nullptr;
    _Menu = fileMenu ? fileMenu->CreateChildMenu(_Ui->buildSettingsAction->text()) : nullptr;
    if (!_Menu) return false;
    _Menu->SetClickCallback(&ShowMenu);
    return true;
}

void BuildSettingUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*) {
    if (!_Settings) return;
    OnBuildSettingsChanged(_Settings->GetState());
}

void BuildSettingUI::Update(GameSproutEditor::GameSproutViewApi*) {}

void BuildSettingUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if (_Subscribed && _Settings) _Settings->UnregisterListener(this);
    _Subscribed = false;
    if (_Menu) {
        _Menu->SetClickCallback(nullptr);
        _Menu->SetEnabled(false);
    }
    _Menu = nullptr;
    delete _ProfileUi;
    _ProfileUi = nullptr;
    delete _ProjectOutputUi;
    _ProjectOutputUi = nullptr;
    delete _Ui;
    _Ui = nullptr;
    delete _Window;
    _Window = nullptr;
    _OutputDialog = nullptr;
    _ProfileDialog = nullptr;
    _ProjectOutputDialog = nullptr;
    _Settings = nullptr;
    _IDE = nullptr;
    _State = {};
    _OutputProfile.clear();
    if (_Self == this) _Self = nullptr;
}

void BuildSettingUI::ShowMenu() {
    if (_Self) _Self->Show();
}

void BuildSettingUI::Show() {
    if (!_Window) return;
    const bool alreadyVisible = _Window->isVisible();
    _Window->show();
    _Window->raise();
    _Window->activateWindow();
    if (!alreadyVisible) RefreshHarmonyStatus();
}

void BuildSettingUI::Hide() {
    if (_OutputDialog) _OutputDialog->reject();
    if (_ProfileDialog) _ProfileDialog->reject();
    if (_ProjectOutputDialog) _ProjectOutputDialog->reject();
    if (_Window) _Window->hide();
}

void BuildSettingUI::OnBuildSettingsChanged(const BuildSettingsState& state) {
    const bool selectedChanged = _State.SelectedProfileId != state.SelectedProfileId;
    const bool profileListChanged = selectedChanged || _State.Profiles != state.Profiles ||
        _State.ActiveProfileId != state.ActiveProfileId;
    const auto* previousProfile = SelectedProfile();
    const bool harmonyProjectChanged = selectedChanged || (previousProfile &&
        std::any_of(state.Profiles.begin(), state.Profiles.end(), [&](const BuildProfile& profile) {
            return profile.Id == state.SelectedProfileId && profile.Harmony != previousProfile->Harmony;
        }));
    const bool harmonyLocalChanged = selectedChanged || _State.HarmonyLocal != state.HarmonyLocal;
    const bool harmonyDevicesChanged = _State.HarmonyStatus.Devices != state.HarmonyStatus.Devices;
    const std::string draftDevice = _Ui ? _Ui->runDevice->currentData().toString().toStdString() : std::string{};
    const bool sceneListChanged = _State.Scenes != state.Scenes;
    const bool availableChanged = sceneListChanged || _State.AvailableScenes != state.AvailableScenes;
    _State = state;
    if (!_Ui) return;
    if (profileListChanged) RefreshProfiles();
    if (sceneListChanged) RefreshScenes();
    if (availableChanged) RefreshAvailableScenes();
    if (_State.Selected == BuildPlatform::HarmonyOS) {
        if (harmonyProjectChanged) RefreshHarmonyProject();
        if (harmonyLocalChanged) RefreshHarmonyLocal();
        else if (harmonyDevicesChanged) RefreshHarmonyDevices(draftDevice);
        if (selectedChanged) _Ui->settingsTabs->setCurrentWidget(_Ui->harmonyPage);
    }

    const QString message = QString::fromStdString(state.Message);
    _Ui->messageLabel->setText(message);
    _Ui->messageLabel->setToolTip(QString::fromStdString(state.LastOutput));
    _Ui->messageLabel->setVisible(!message.isEmpty());
    ShowError(state.Error);
    RefreshActions();
}

const BuildProfile* BuildSettingUI::SelectedProfile() const {
    const auto selected = std::find_if(_State.Profiles.begin(), _State.Profiles.end(), [this](const BuildProfile& profile) {
        return profile.Id == _State.SelectedProfileId;
    });
    return selected == _State.Profiles.end() ? nullptr : &*selected;
}

void BuildSettingUI::RefreshProfiles() {
    {
        const QSignalBlocker blocked(_Ui->platformTree);
        for (int row = 0; row < _Ui->platformTree->topLevelItemCount(); ++row) {
            auto* item = _Ui->platformTree->topLevelItem(row);
            const auto platform = static_cast<BuildPlatform>(item->data(0, Qt::UserRole).toInt());
            item->setText(1, platform == _State.Active ? Translate("生效") : QString());
            if (platform == _State.Selected) _Ui->platformTree->setCurrentItem(item);
        }
    }
    const QSignalBlocker profileBlocked(_Ui->profileList);
    _Ui->profileList->clear();
    for (const auto& profile : _State.Profiles) {
        if (profile.Platform != _State.Selected) continue;
        const QString name = QString::fromStdString(profile.Name);
        _Ui->profileList->addItem(profile.Id == _State.ActiveProfileId ? Translate("%1（生效）").arg(name) : name,
            QString::fromStdString(profile.Id));
    }
    _Ui->profileList->setCurrentIndex(_Ui->profileList->findData(QString::fromStdString(_State.SelectedProfileId)));

    const bool isHarmony = _State.Selected == BuildPlatform::HarmonyOS;
    _Ui->settingsTabs->setTabVisible(_Ui->settingsTabs->indexOf(_Ui->harmonyPage), isHarmony);
    const auto* profile = SelectedProfile();
    const bool overrideScenes = profile && profile->OverrideScenes;
    const QSignalBlocker scenesBlocked(_Ui->overrideScenes);
    _Ui->overrideScenes->setChecked(overrideScenes);
    _Ui->startupHint->setText(overrideScenes ?
        Translate("编辑当前配置的场景列表。Player 从第一个启用的场景启动。") :
        Translate("编辑全局场景列表。Player 从第一个启用的场景启动。"));
}

void BuildSettingUI::RefreshScenes() {
    auto& tree = *_Ui->sceneTree;
    const auto* selected = tree.currentItem();
    const auto selectedGuid = selected ? selected->data(0, Qt::UserRole).toString() : QString();
    const QSignalBlocker blocked(&tree);
    const auto count = static_cast<int>(_State.Scenes.size());
    bool rebuild = tree.topLevelItemCount() != count;
    for (int row = 0; row < count && !rebuild; ++row) {
        rebuild = tree.topLevelItem(row)->data(0, Qt::UserRole).toString() !=
            QString::fromStdString(_State.Scenes[row].Guid);
    }
    if (rebuild) tree.clear();
    int buildIndex = 0;
    for (int row = 0; row < count; ++row) {
        const auto& scene = _State.Scenes[row];
        auto* item = rebuild ? new QTreeWidgetItem(&tree) : tree.topLevelItem(row);
        const QString guid = QString::fromStdString(scene.Guid);
        const QString path = QString::fromStdString(scene.Path);
        item->setText(0, path.isEmpty() ? Translate("场景丢失 (%1)").arg(guid) : path);
        item->setToolTip(0, item->text(0));
        item->setText(1, scene.Enabled ? QString::number(buildIndex++) : QString());
        item->setData(0, Qt::UserRole, guid);
        item->setCheckState(0, scene.Enabled ? Qt::Checked : Qt::Unchecked);
        if (rebuild && guid == selectedGuid) tree.setCurrentItem(item);
    }
}

void BuildSettingUI::RefreshAvailableScenes() {
    auto& combo = *_Ui->availableScenes;
    const QVariant current = combo.currentData();
    combo.clear();
    for (const auto& scene : _State.AvailableScenes) {
        if (std::any_of(_State.Scenes.begin(), _State.Scenes.end(),
            [&scene](const BuildScene& entry) { return entry.Guid == scene.Guid; })) continue;
        combo.addItem(QString::fromStdString(scene.Path), QString::fromStdString(scene.Guid));
    }
    const int index = combo.findData(current);
    if (index >= 0) combo.setCurrentIndex(index);
}

void BuildSettingUI::RefreshActions() {
    const bool idle = _State.Operation == BuildOperation::Idle;
    const auto* profile = SelectedProfile();
    const bool active = profile && profile->Id == _State.ActiveProfileId;
    const bool canExport = CanBuildPlatform(_State.Selected);
    const bool harmony = profile && profile->Platform == BuildPlatform::HarmonyOS;
    const bool hasProjectEdits = HasHarmonyProjectEdits();
    const bool hasLocalEdits = HasHarmonyLocalEdits();
    const bool editing = hasProjectEdits || hasLocalEdits;
    const auto& harmonyStatus = _State.HarmonyStatus;
    const bool hasEnabledScene = std::any_of(_State.Scenes.begin(), _State.Scenes.end(),
        [](const BuildScene& scene) { return scene.Enabled; });
    const bool hasMissingScene = std::any_of(_State.Scenes.begin(), _State.Scenes.end(),
        [](const BuildScene& scene) { return scene.Enabled && scene.Path.empty(); });
    const int row = SelectedSceneRow(*_Ui->sceneTree);
    _Ui->platformTree->setEnabled(idle);
    _Ui->profileList->setEnabled(idle && profile);
    _Ui->addProfileButton->setEnabled(idle && profile);
    _Ui->renameProfileButton->setEnabled(idle && profile && !profile->IsDefault);
    _Ui->removeProfileButton->setEnabled(idle && profile && !profile->IsDefault && !active);
    _Ui->sceneGroup->setEnabled(idle);
    _Ui->overrideScenes->setEnabled(profile != nullptr);
    _Ui->addSceneButton->setEnabled(_Ui->availableScenes->count() > 0);
    _Ui->removeSceneButton->setEnabled(row >= 0);
    _Ui->moveUpButton->setEnabled(row > 0);
    _Ui->moveDownButton->setEnabled(row >= 0 && row + 1 < _Ui->sceneTree->topLevelItemCount());
    _Ui->platformSummary->setVisible(profile != nullptr);
    if (profile) {
        const auto activeProfile = std::find_if(_State.Profiles.begin(), _State.Profiles.end(), [this](const BuildProfile& entry) {
            return entry.Id == _State.ActiveProfileId;
        });
        const QString activeName = activeProfile == _State.Profiles.end() ?
            QString::fromUtf8(BuildPlatformName(_State.Active)) : QString::fromStdString(activeProfile->Name);
        _Ui->platformSummary->setText(active ? Translate("生效配置  %1").arg(activeName) :
            Translate("选中 %1，当前生效 %2").arg(QString::fromStdString(profile->Name), activeName));
    }
    _Ui->exportUnavailable->setVisible(!canExport);
    _Ui->switchButton->setVisible((profile && !active) || _State.Operation == BuildOperation::Switching);
    _Ui->switchButton->setEnabled(idle && !active && !editing);
    _Ui->buildButton->setVisible(active);
    _Ui->buildAndRunButton->setVisible(active);
    const bool deveco = harmony && profile->Harmony.Output == HarmonyBuildOutput::DevEcoProject;
    _Ui->buildButton->setText(deveco ? Translate("导出 DevEco 工程") : Translate("Build"));
    const bool buildReady = idle && active && canExport && hasEnabledScene && !hasMissingScene && !editing;
    _Ui->buildButton->setEnabled(buildReady && (!harmony || (harmonyStatus.Checked && harmonyStatus.CanBuild)));
    _Ui->buildAndRunButton->setEnabled(_Ui->buildButton->isEnabled() && !deveco &&
        (!harmony || harmonyStatus.CanRun));
    QString buildHint;
    if (hasMissingScene) buildHint = Translate("请移除或停用丢失的场景。");
    else if (!hasEnabledScene) buildHint = Translate("请启用至少一个构建场景。");
    else if (editing) buildHint = Translate("请保存或还原尚未应用的鸿蒙设置。");
    else if (harmony && !harmonyStatus.CanBuild) buildHint = QString::fromStdString(harmonyStatus.BuildMessage);
    _Ui->buildButton->setToolTip(buildHint);
    _Ui->switchButton->setToolTip(editing ? Translate("请保存或还原尚未应用的鸿蒙设置。") : QString{});
    _Ui->buildAndRunButton->setToolTip(buildHint.isEmpty() && harmony && !harmonyStatus.CanRun ?
        QString::fromStdString(harmonyStatus.RunMessage) : buildHint);
    RefreshHarmonyControls(harmony, hasProjectEdits, hasLocalEdits);
    _Ui->operationProgress->setVisible(!idle);
    _Ui->cancelButton->setVisible(_State.Operation == BuildOperation::Switching ||
        _State.Operation == BuildOperation::Building);
}

void BuildSettingUI::ShowError(const std::string& error) {
    _Ui->buildDiagnostics->setPlainText(QString::fromStdString(error));
    _Ui->buildDiagnostics->setVisible(!error.empty());
}

void BuildSettingUI::SelectPlatform(QTreeWidgetItem* item) {
    if (!item || !_Settings) return;
    const auto selected = static_cast<BuildPlatform>(item->data(0, Qt::UserRole).toInt());
    if (_State.Selected == selected) return;
    if (!ConfirmDiscardHarmonyEdits()) {
        RefreshProfiles();
        return;
    }
    std::string error;
    if (_Settings->SelectPlatform(selected, error)) RefreshHarmonyStatus();
    else {
        RefreshProfiles();
        ShowError(error);
    }
}

void BuildSettingUI::SelectProfile(int index) {
    if (index < 0 || !_Settings) return;
    const auto profile = _Ui->profileList->itemData(index).toString().toStdString();
    if (_State.SelectedProfileId == profile) return;
    if (!ConfirmDiscardHarmonyEdits()) {
        RefreshProfiles();
        return;
    }
    std::string error;
    if (_Settings->SelectProfile(profile, error)) RefreshHarmonyStatus();
    else {
        RefreshProfiles();
        ShowError(error);
    }
}

void BuildSettingUI::OpenProfileNameDialog(bool rename) {
    const auto* profile = SelectedProfile();
    if (!profile || !_Settings || (rename && profile->IsDefault)) return;
    if (!rename && !ConfirmDiscardHarmonyEdits()) return;
    _RenameProfile = rename;
    _ProfileDialog->setWindowTitle(rename ? Translate("重命名构建配置") : Translate("新建构建配置"));
    _ProfileUi->profileName->setText(rename ? QString::fromStdString(profile->Name) : QString{});
    _ProfileUi->errorLabel->hide();
    _ProfileDialog->open();
    _ProfileUi->profileName->setFocus();
    _ProfileUi->profileName->selectAll();
}

void BuildSettingUI::SaveProfileName() {
    const auto* profile = SelectedProfile();
    if (!_Settings || !profile) return;
    const auto name = _ProfileUi->profileName->text().trimmed().toStdString();
    std::string error;
    bool saved = false;
    if (name.empty()) error = Translate("请输入构建配置名称。").toStdString();
    else if (_RenameProfile) {
        auto renamed = *profile;
        renamed.Name = name;
        saved = _Settings->SetProfile(renamed, error);
    } else saved = _Settings->CreateProfile(profile->Platform, name, error);
    if (!saved) {
        _ProfileUi->errorLabel->setText(QString::fromStdString(error));
        _ProfileUi->errorLabel->show();
        return;
    }
    _ProfileDialog->accept();
    if (!_RenameProfile) RefreshHarmonyStatus();
}

void BuildSettingUI::RemoveProfile() {
    const auto* profile = SelectedProfile();
    if (!_Settings || !profile || profile->IsDefault || profile->Id == _State.ActiveProfileId) return;
    const auto id = profile->Id;
    if (QMessageBox::question(_Window, Translate("删除构建配置"), Translate("删除构建配置「%1」？")
            .arg(QString::fromStdString(profile->Name)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    std::string error;
    if (_Settings->RemoveProfile(id, error)) RefreshHarmonyStatus();
    else ShowError(error);
}

void BuildSettingUI::SetSceneOverride(bool enabled) {
    const auto* profile = SelectedProfile();
    if (!_Settings || !profile || profile->OverrideScenes == enabled) return;
    auto changed = *profile;
    changed.OverrideScenes = enabled;
    std::string error;
    if (!_Settings->SetProfile(changed, error)) {
        RefreshProfiles();
        ShowError(error);
    }
}

void BuildSettingUI::ChangeSceneEnabled(QTreeWidgetItem* item, int column) {
    if (!item || column != 0) return;
    const int row = _Ui->sceneTree->indexOfTopLevelItem(item);
    if (row < 0 || static_cast<std::size_t>(row) >= _State.Scenes.size()) return;
    auto scenes = _State.Scenes;
    const bool enabled = item->checkState(0) == Qt::Checked;
    if (scenes[row].Enabled == enabled) return;
    scenes[row].Enabled = enabled;
    ApplyScenes(scenes, row);
}

void BuildSettingUI::AddScene() {
    const auto guid = _Ui->availableScenes->currentData().toString().toStdString();
    const auto found = std::find_if(_State.AvailableScenes.begin(), _State.AvailableScenes.end(),
        [&guid](const BuildScene& scene) { return scene.Guid == guid; });
    if (found == _State.AvailableScenes.end()) return;
    auto scenes = _State.Scenes;
    scenes.push_back(*found);
    scenes.back().Enabled = true;
    ApplyScenes(scenes, static_cast<int>(scenes.size()) - 1);
}

void BuildSettingUI::RemoveScene() {
    const int row = SelectedSceneRow(*_Ui->sceneTree);
    if (row < 0 || static_cast<std::size_t>(row) >= _State.Scenes.size()) return;
    auto scenes = _State.Scenes;
    scenes.erase(scenes.begin() + row);
    ApplyScenes(scenes, std::min(row, static_cast<int>(scenes.size()) - 1));
}

void BuildSettingUI::MoveScene(int offset) {
    const int row = SelectedSceneRow(*_Ui->sceneTree);
    const int next = row + offset;
    const auto count = static_cast<int>(_State.Scenes.size());
    if (row < 0 || row >= count || next < 0 || next >= count) return;
    auto scenes = _State.Scenes;
    std::swap(scenes[row], scenes[next]);
    ApplyScenes(scenes, next);
}

void BuildSettingUI::ApplyScenes(const std::vector<BuildScene>& scenes, int selectedRow) {
    if (!_Settings) return;
    std::string error;
    if (!_Settings->SetScenes(scenes, error)) {
        RefreshScenes();
        ShowError(error);
        return;
    }
    _Ui->sceneTree->setCurrentItem(_Ui->sceneTree->topLevelItem(selectedRow));
}

void BuildSettingUI::SwitchPlatform() {
    if (!_Settings) return;
    std::string error;
    if (!_Settings->SwitchPlatform(error)) ShowError(error);
}

void BuildSettingUI::PickOutput(bool run) {
    const auto* profile = SelectedProfile();
    if (!_OutputDialog || !profile || !_Ui->buildButton->isEnabled() ||
        (run && !_Ui->buildAndRunButton->isEnabled())) return;
    _RunAfterBuild = run;
    _OutputProfile = profile->Id;
    const auto suffix = QString::fromLatin1(BuildPlatformOutputSuffix(*profile));
    if (suffix.isEmpty()) {
        PickProjectOutput();
        return;
    }
    _OutputDialog->setDefaultSuffix(suffix);
    _OutputDialog->setNameFilter(Translate("Player 构建 (*.%1)").arg(suffix));
    const auto filename = QStringLiteral("GameSproutPlayer.") + suffix;
    if (_State.LastOutput.empty()) {
        _OutputDialog->setDirectory(QDir::homePath());
        _OutputDialog->selectFile(filename);
    } else {
        const QFileInfo previous(QString::fromStdString(_State.LastOutput));
        _OutputDialog->setDirectory(previous.absolutePath());
        _OutputDialog->selectFile(previous.fileName().endsWith('.' + suffix, Qt::CaseInsensitive) ?
            previous.fileName() : filename);
    }
    _OutputDialog->open();
}

void BuildSettingUI::PickProjectOutput() {
    QFileInfo previous(QString::fromStdString(_State.LastOutput));
    const bool hasPrevious = !_State.LastOutput.empty();
    _ProjectOutputUi->parentFolder->setText(hasPrevious ? previous.absolutePath() : QDir::homePath());
    const bool previousWasProject = hasPrevious && previous.isDir() &&
        IsGameSproutDevEcoProject(previous.absoluteFilePath());
    _ProjectOutputUi->projectFolderName->setText(previousWasProject ?
        previous.fileName() : QStringLiteral("GameSproutDevEco"));
    _ProjectOutputUi->errorLabel->hide();
    _ProjectOutputDialog->open();
    _ProjectOutputUi->projectFolderName->setFocus();
    _ProjectOutputUi->projectFolderName->selectAll();
}

void BuildSettingUI::BuildProject() {
    const QString name = _ProjectOutputUi->projectFolderName->text().trimmed();
    const QDir parent(_ProjectOutputUi->parentFolder->text());
    const auto showError = [this](const QString& error) {
        _ProjectOutputUi->errorLabel->setText(error);
        _ProjectOutputUi->errorLabel->show();
    };
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        showError(Translate("请输入单独的工程目录名。"));
        return;
    }
    if (!parent.exists()) {
        showError(Translate("上一级目录不存在。"));
        return;
    }
    const QString path = parent.absoluteFilePath(name);
    const QFileInfo target(path);
    if (target.isSymLink() || (target.exists() && !target.isDir())) {
        showError(Translate("目标位置已被文件或符号链接占用。"));
        return;
    }
    if (target.exists()) {
        if (!IsGameSproutDevEcoProject(path)) {
            showError(Translate("目标目录已存在。请选择新的目录名，或选择此前导出的 GameSprout 工程。"));
            return;
        }
        if (QMessageBox::question(_ProjectOutputDialog, Translate("替换 DevEco 工程"),
                Translate("导出将替换整个目录「%1」及其内容。继续吗？").arg(path),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    }
    if (Build(path)) _ProjectOutputDialog->accept();
}

bool BuildSettingUI::Build(const QString& path) {
    if (!_Settings || !_Ui->buildButton->isEnabled() || _OutputProfile != _State.ActiveProfileId ||
        (_RunAfterBuild && !_Ui->buildAndRunButton->isEnabled())) return false;
    std::string error;
    if (_Settings->Build(path.toStdString(), _RunAfterBuild, error)) return true;
    ShowError(error);
    if (_ProjectOutputDialog->isVisible()) {
        _ProjectOutputUi->errorLabel->setText(QString::fromStdString(error));
        _ProjectOutputUi->errorLabel->show();
    }
    return false;
}
