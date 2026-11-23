#ifndef __BuildSettingUI_h__
#define __BuildSettingUI_h__

#include "Header.h"
#include "IBuildSetting.h"

QT_BEGIN_NAMESPACE
class QDialog;
class QFileDialog;
class QTreeWidgetItem;
namespace Ui {
class BuildSettingUIWindow;
}
QT_END_NAMESPACE

class BuildSettingUI final : public IBuildSettingUI, private IBuildSettingListener {
public:
    ~BuildSettingUI() override;

    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    void OnBuildSettingsChanged(const BuildSettingsState& state) override;

    static void ShowMenu();
    void RefreshScenes();
    void RefreshAvailableScenes();
    void RefreshActions();
    void ShowError(const std::string& error);
    void SelectPlatform(QTreeWidgetItem* item);
    void ChangeSceneEnabled(QTreeWidgetItem* item, int column);
    void AddScene();
    void RemoveScene();
    void MoveScene(int offset);
    void ApplyScenes(const std::vector<BuildScene>& scenes, int selectedRow);
    void SwitchPlatform();
    void PickOutput(bool run);
    void Build();

    static BuildSettingUI* _Self;
    IIDEWindow* _IDE = nullptr;
    IBuildSetting* _Settings = nullptr;
    IIDEMenu* _Menu = nullptr;
    QDialog* _Window = nullptr;
    QFileDialog* _OutputDialog = nullptr;
    Ui::BuildSettingUIWindow* _Ui = nullptr;
    BuildSettingsState _State;
    bool _Subscribed = false;
    bool _RunAfterBuild = false;
};
#endif //__BuildSettingUI_h__
