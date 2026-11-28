#pragma once
#include "IWeatherAuthoring.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "ISceneManager.h"
#include <QElapsedTimer>
#include <map>
#include <optional>

class QThread;

class WeatherAuthoring final : public IWeatherAuthoring,private ISceneListener,private IPlayModeListener,
    private IRuntimeFrameListener {
public:
    ~WeatherAuthoring() override;
    bool ReadWeather(WeatherAuthoringSnapshot&,std::string&) const override;
    bool ApplySettings(std::uint64_t,const GameSproutRuntime::WeatherSettings&,std::string&) override;
    bool ApplyLook(std::uint64_t,WeatherLook,std::string&) override;
    bool PreviewLook(std::uint64_t,WeatherLook,float,std::string&) override;
    bool PreviewTime(std::uint64_t,float,std::string&) override;
    bool SetPreviewPlaying(std::uint64_t,bool,std::string&) override;
    bool ResetPreview(std::uint64_t,std::string&) override;
    bool TriggerLightning(std::uint64_t,const GameSproutRuntime::WeatherLightning&,std::string&) override;
    bool ImportProfile(std::uint64_t,const std::string&,std::string&) override;
    bool ExportProfile(std::uint64_t,const std::string&,std::string&) const override;
    bool RegisterWeatherListener(IWeatherAuthoringListener*) override;
    void UnregisterWeatherListener(IWeatherAuthoringListener*) override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override {Publish();}
    void OnPlayModeStateChanged(const PlayModeState&) override;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override;

    GameSproutRuntime::IScene* Resolve(std::uint64_t,WeatherAuthoringSnapshot&,std::string&) const;
    bool BeginClock(std::uint64_t,float,std::string&);
    void StopClock();
    void Publish();

    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    GameSproutRuntime::IWeatherRuntime* _Weather=nullptr;
    ISceneManager* _Scenes=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    QThread* _OwnerThread=nullptr;
    bool _SceneSubscribed=false,_PlaySubscribed=false,_FrameSubscribed=false,_Publishing=false;
    std::map<std::uint64_t,IWeatherAuthoringListener*> _Listeners;
    std::uint64_t _NextListener=0,_PreviewSceneID=0;
    std::optional<WeatherAuthoringSnapshot> _Published;
    bool _PreviewPlaying=false;
    float _PreviewRemaining=0;
    QElapsedTimer _PreviewTimer;
};
