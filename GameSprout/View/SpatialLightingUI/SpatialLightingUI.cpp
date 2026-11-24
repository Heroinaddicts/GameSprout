#include "SpatialLightingUI.h"
#include "SpatialLightingWindow.h"
#include "IAssetsManager.h"
#include "IIDEWindow.h"
#include "ILibraryManager.h"
#include "ISelectionManager.h"
#include <QApplication>

namespace {SpatialLightingUI* ActiveSpatialLightingView=nullptr;}

SpatialLightingUI::SpatialLightingUI()=default;
SpatialLightingUI::~SpatialLightingUI(){Release(nullptr);}

void SpatialLightingUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    if(!editor)return;
    _Authoring=editor->GetGameSproutModule<ILightingAuthoring>("LightingAuthoring");
    _Baker=editor->GetGameSproutModule<ILightingBaker>("LightingBaker");
    _ProjectSettings=editor->GetGameSproutModule<IProjectSettings>("ProjectSettings");
    _Assets=editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Library=editor->GetGameSproutModule<ILibraryManager>("LibraryManager");
    _Selection=editor->GetGameSproutModule<ISelectionManager>("SelectionManager");
    _IDE=editor->GetGameSproutView<IIDEWindow>("IDEWindow");
}

bool SpatialLightingUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Window=std::make_unique<SpatialLightingWindow>();
    return true;
}

bool SpatialLightingUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_IDE||!_Window||!_Window->Bind(_Authoring,_ProjectSettings,_Assets,_Library,_Selection,_Baker))return false;
    auto* bar=_IDE->GetMenuBar();
    auto* windowMenu=bar?bar->CreateChildMenu(_Window->WindowMenuTitle()):nullptr;
    _Menu=windowMenu?windowMenu->CreateChildMenu(_Window->windowTitle()):nullptr;
    if(!_Menu)return false;
    ActiveSpatialLightingView=this;
    _Menu->SetClickCallback(&ShowMenu);
    _Menu->SetEnabled(true);
    return true;
}

void SpatialLightingUI::ShowMenu(){if(ActiveSpatialLightingView)ActiveSpatialLightingView->Show();}
void SpatialLightingUI::Show(){if(_Window){_Window->show();_Window->raise();_Window->activateWindow();}}
void SpatialLightingUI::Hide(){if(_Window)_Window->hide();}
void SpatialLightingUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Menu){_Menu->SetClickCallback(nullptr);_Menu->SetEnabled(false);_Menu=nullptr;}
    if(ActiveSpatialLightingView==this)ActiveSpatialLightingView=nullptr;
    if(_Window)_Window->Shutdown();
    _Window.reset();
    _IDE=nullptr;_Authoring=nullptr;_ProjectSettings=nullptr;_Baker=nullptr;
    _Assets=nullptr;_Library=nullptr;_Selection=nullptr;
}
