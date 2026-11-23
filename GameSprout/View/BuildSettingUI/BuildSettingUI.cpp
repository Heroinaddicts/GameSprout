#include "BuildSettingUI.h"
#include "ui_BuildOutputDialog.h"
#include "ui_BuildSettingUI.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QSignalBlocker>
#include <algorithm>

namespace {
QString Translate(const char* source) {
    return QCoreApplication::translate("BuildSettingUIWindow", source);
}

int SelectedSceneRow(const QTreeWidget& tree) {
    return tree.indexOfTopLevelItem(tree.currentItem());
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
    QObject::connect(_OutputDialog, &QFileDialog::accepted, _Window, [this] { Build(); });
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
    delete _Ui;
    _Ui = nullptr;
    delete _Window;
    _Window = nullptr;
    _OutputDialog = nullptr;
    _Settings = nullptr;
    _IDE = nullptr;
    _State = {};
    if (_Self == this) _Self = nullptr;
}

void BuildSettingUI::ShowMenu() {
    if (_Self) _Self->Show();
}

void BuildSettingUI::Show() {
    if (!_Window) return;
    _Window->show();
    _Window->raise();
    _Window->activateWindow();
}

void BuildSettingUI::Hide() {
    if (_OutputDialog) _OutputDialog->reject();
    if (_Window) _Window->hide();
}

void BuildSettingUI::OnBuildSettingsChanged(const BuildSettingsState& state) {
    const bool sceneListChanged = _State.Scenes != state.Scenes;
    const bool availableChanged = sceneListChanged || _State.AvailableScenes != state.AvailableScenes;
    _State = state;
    if (!_Ui) return;
    if (sceneListChanged) RefreshScenes();
    if (availableChanged) RefreshAvailableScenes();

    {
        const QSignalBlocker blocked(_Ui->platformTree);
        for (int row = 0; row < _Ui->platformTree->topLevelItemCount(); ++row) {
            auto* item = _Ui->platformTree->topLevelItem(row);
            const auto platform = static_cast<BuildPlatform>(item->data(0, Qt::UserRole).toInt());
            item->setText(1, platform == state.Active ? Translate("生效") : QString());
            if (platform == state.Selected) _Ui->platformTree->setCurrentItem(item);
        }
    }

    const QString message = QString::fromStdString(state.Message);
    _Ui->messageLabel->setText(message);
    _Ui->messageLabel->setToolTip(QString::fromStdString(state.LastOutput));
    _Ui->messageLabel->setVisible(!message.isEmpty());
    ShowError(state.Error);
    RefreshActions();
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
    const bool active = _State.Selected == _State.Active;
    const bool canExport = CanBuildPlatform(_State.Selected);
    const bool hasEnabledScene = std::any_of(_State.Scenes.begin(), _State.Scenes.end(),
        [](const BuildScene& scene) { return scene.Enabled; });
    const bool hasMissingScene = std::any_of(_State.Scenes.begin(), _State.Scenes.end(),
        [](const BuildScene& scene) { return scene.Enabled && scene.Path.empty(); });
    const int row = SelectedSceneRow(*_Ui->sceneTree);
    _Ui->platformTree->setEnabled(idle);
    _Ui->sceneGroup->setEnabled(idle);
    _Ui->addSceneButton->setEnabled(_Ui->availableScenes->count() > 0);
    _Ui->removeSceneButton->setEnabled(row >= 0);
    _Ui->moveUpButton->setEnabled(row > 0);
    _Ui->moveDownButton->setEnabled(row >= 0 && row + 1 < _Ui->sceneTree->topLevelItemCount());
    _Ui->platformSummary->setVisible(!active);
    if (!active) {
        _Ui->platformSummary->setText(Translate("选中 %1，当前生效 %2")
            .arg(QString::fromUtf8(BuildPlatformName(_State.Selected)),
                QString::fromUtf8(BuildPlatformName(_State.Active))));
    }
    _Ui->exportUnavailable->setVisible(!canExport);
    _Ui->switchButton->setVisible(!active || _State.Operation == BuildOperation::Switching);
    _Ui->switchButton->setEnabled(idle && !active);
    _Ui->buildButton->setVisible(active);
    _Ui->buildAndRunButton->setVisible(active);
    _Ui->buildButton->setEnabled(idle && active && canExport && hasEnabledScene && !hasMissingScene);
    _Ui->buildButton->setToolTip(hasMissingScene ? Translate("请移除或停用丢失的场景。") : QString());
    _Ui->buildAndRunButton->setToolTip(_Ui->buildButton->toolTip());
    _Ui->buildAndRunButton->setEnabled(_Ui->buildButton->isEnabled());
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
    std::string error;
    if (_Settings->SelectPlatform(selected, error)) return;
    OnBuildSettingsChanged(_State);
    ShowError(error);
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
    if (!_OutputDialog || !_Ui->buildButton->isEnabled()) return;
    _RunAfterBuild = run;
    const auto suffix = QString::fromLatin1(BuildPlatformOutputSuffix(_State.Active));
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

void BuildSettingUI::Build() {
    if (!_Settings || !_Ui->buildButton->isEnabled()) return;
    const QStringList files = _OutputDialog->selectedFiles();
    if (files.isEmpty()) return;
    std::string error;
    if (!_Settings->Build(files.front().toStdString(), _RunAfterBuild, error)) ShowError(error);
}
