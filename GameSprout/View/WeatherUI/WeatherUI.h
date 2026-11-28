#pragma once
#include "IWeatherUI.h"
#include "IWeatherAuthoring.h"

class IIDEWindow;
class IIDEMenu;
class QDialog;
namespace Ui {class WeatherWindow;}

class WeatherUI final : public IWeatherUI,private IWeatherAuthoringListener {
public:
    ~WeatherUI() override;
    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void OnWeatherAuthoringChanged(const WeatherAuthoringSnapshot&) override;
    static void ShowMenu();
    GameSproutRuntime::WeatherSettings ReadControls() const;
    void ShowError(const std::string&);
    void StopPreview();
    void PickProfile(bool save);

    IWeatherAuthoring* _Authoring=nullptr;
    IIDEWindow* _IDE=nullptr;
    IIDEMenu* _Menu=nullptr;
    QDialog* _Window=nullptr;
    Ui::WeatherWindow* _UI=nullptr;
    bool _Subscribed=false;
    WeatherAuthoringSnapshot _Snapshot;
};
