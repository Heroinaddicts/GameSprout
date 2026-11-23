#ifndef __BuildSettingUI_h__
#define __BuildSettingUI_h__

#include "Header.h"
#include "IBuildSetting.h"

QT_BEGIN_NAMESPACE
class QDialog;
class QFileDialog;
class QTreeWidgetItem;
namespace Ui {
class BuildProfileNameDialog;
class BuildProjectOutputDialog;
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
    static QString Translate(const char* source);
    const BuildProfile* SelectedProfile() const;
    void RefreshProfiles();
    void RefreshScenes();
    void RefreshAvailableScenes();
    void RefreshActions();
    void InitializeHarmonyForm();
    void RefreshHarmonyControls(bool harmony, bool hasProjectEdits, bool hasLocalEdits);
    void RefreshHarmonyProject();
    void RefreshHarmonyLocal();
    void RefreshHarmonyDevices(const std::string& device);
    void RefreshHarmonyStatus();
    HarmonyBuildSettings HarmonyProjectDraft() const;
    HarmonyLocalBuildSettings HarmonyLocalDraft() const;
    bool HasHarmonyProjectEdits() const;
    bool HasHarmonyLocalEdits() const;
    bool ConfirmDiscardHarmonyEdits();
    void SaveHarmonyProject();
    void SaveHarmonyLocal();
    void BrowseHarmonyTools();
    void BrowseHarmonySigning();
    void ShowError(const std::string& error);
    void SelectPlatform(QTreeWidgetItem* item);
    void SelectProfile(int index);
    void OpenProfileNameDialog(bool rename);
    void SaveProfileName();
    void RemoveProfile();
    void SetSceneOverride(bool enabled);
    void ChangeSceneEnabled(QTreeWidgetItem* item, int column);
    void AddScene();
    void RemoveScene();
    void MoveScene(int offset);
    void ApplyScenes(const std::vector<BuildScene>& scenes, int selectedRow);
    void SwitchPlatform();
    void PickOutput(bool run);
    void PickProjectOutput();
    void BuildProject();
    bool Build(const QString& path);

    static BuildSettingUI* _Self;
    IIDEWindow* _IDE = nullptr;
    IBuildSetting* _Settings = nullptr;
    IIDEMenu* _Menu = nullptr;
    QDialog* _Window = nullptr;
    QFileDialog* _OutputDialog = nullptr;
    QDialog* _ProfileDialog = nullptr;
    QDialog* _ProjectOutputDialog = nullptr;
    Ui::BuildProfileNameDialog* _ProfileUi = nullptr;
    Ui::BuildProjectOutputDialog* _ProjectOutputUi = nullptr;
    Ui::BuildSettingUIWindow* _Ui = nullptr;
    BuildSettingsState _State;
    bool _Subscribed = false;
    bool _RunAfterBuild = false;
    bool _RenameProfile = false;
    bool _RefreshingHarmonyForm = false;
    std::string _OutputProfile;
};
#endif //__BuildSettingUI_h__
