#pragma once

#include "Header.h"
#include <memory>

class IAssetsManager;
class IIDEWindow;
class IIDEMenu;
class ILightingAuthoring;
class ILightingBaker;
class ILibraryManager;
class IProjectSettings;
class ISelectionManager;
class SpatialLightingWindow;

class SpatialLightingUI final : public ISpatialLightingUI {
public:
    SpatialLightingUI();
    ~SpatialLightingUI() override;
    void Show() override;
    void Hide() override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    static void ShowMenu();
    ILightingAuthoring* _Authoring=nullptr;
    ILightingBaker* _Baker=nullptr;
    IProjectSettings* _ProjectSettings=nullptr;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    ISelectionManager* _Selection=nullptr;
    IIDEWindow* _IDE=nullptr;
    IIDEMenu* _Menu=nullptr;
    std::unique_ptr<SpatialLightingWindow> _Window;
};
