#pragma once

#include "IEnvironmentUI.h"
#include "IEnvironmentAuthoring.h"

class IIDEWindow;
class IIDEMenu;
class QDialog;
class QPushButton;
class QTableWidget;
namespace Ui {class EnvironmentWindow;}

class EnvironmentUI final : public IEnvironmentUI,private IEnvironmentAuthoringListener {
public:
    ~EnvironmentUI() override;
    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void OnEnvironmentChanged(const EnvironmentAuthoringSnapshot&) override;
    static void ShowMenu();
    void ConnectTable(QTableWidget*,QPushButton*,QPushButton*,std::function<void()>);
    void LoadRows();
    void RefreshCatalog();
    void Apply();
    GameSproutRuntime::EnvironmentSettings ReadRows() const;

    IEnvironmentAuthoring* _Authoring=nullptr;
    IIDEWindow* _IDE=nullptr;
    IIDEMenu* _Menu=nullptr;
    QDialog* _Window=nullptr;
    Ui::EnvironmentWindow* _UI=nullptr;
    bool _Subscribed=false;
    EnvironmentAuthoringSnapshot _Snapshot;
};
