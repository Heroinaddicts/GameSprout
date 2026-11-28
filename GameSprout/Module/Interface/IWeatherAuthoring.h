#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeWeatherApi.h"

enum class WeatherLook { Clear, Cloudy, Overcast };

struct WeatherAuthoringSnapshot {
    std::uint64_t SceneID=0;
    bool Editable=false;
    bool PreviewAvailable=false;
    bool PreviewPlaying=false;
    GameSproutRuntime::WeatherSettings Settings;
    bool operator==(const WeatherAuthoringSnapshot&) const = default;
};

class IWeatherAuthoringListener {
public:
    virtual ~IWeatherAuthoringListener() = default;
    virtual void OnWeatherAuthoringChanged(const WeatherAuthoringSnapshot&) = 0;
};

class IWeatherAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual bool ReadWeather(WeatherAuthoringSnapshot&,std::string&) const = 0;
    virtual bool ApplySettings(std::uint64_t,const GameSproutRuntime::WeatherSettings&,std::string&) = 0;
    virtual bool ApplyLook(std::uint64_t,WeatherLook,std::string&) = 0;
    virtual bool PreviewLook(std::uint64_t,WeatherLook,float seconds,std::string&) = 0;
    virtual bool PreviewTime(std::uint64_t,float hours,std::string&) = 0;
    virtual bool SetPreviewPlaying(std::uint64_t,bool,std::string&) = 0;
    virtual bool ResetPreview(std::uint64_t,std::string&) = 0;
    virtual bool TriggerLightning(std::uint64_t,const GameSproutRuntime::WeatherLightning&,std::string&) = 0;
    virtual bool ImportProfile(std::uint64_t,const std::string& filename,std::string&) = 0;
    virtual bool ExportProfile(std::uint64_t,const std::string& filename,std::string&) const = 0;
    // 主线程同步值快照。订阅后读取初值，销毁前注销。
    virtual bool RegisterWeatherListener(IWeatherAuthoringListener*) = 0;
    virtual void UnregisterWeatherListener(IWeatherAuthoringListener*) = 0;
};
