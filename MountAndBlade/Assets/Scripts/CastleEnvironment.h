#pragma once

#include "CastleLightning.h"
#include "CastleSunset.h"
#include "RuntimeScriptModule.h"
#include "RuntimeWeatherApi.h"
#include <array>

class CastleEnvironment final : public GameSproutRuntime::IModule {
protected:
    bool Initialize(GameSproutRuntime::IGameSproutRuntime* runtime) override;
    bool Launch(GameSproutRuntime::IGameSproutRuntime*) override { return true; }
    void LaunchFinished(GameSproutRuntime::IGameSproutRuntime* runtime) override;
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void Release(GameSproutRuntime::IGameSproutRuntime* runtime) override;

private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<CastleEnvironment>;
    explicit CastleEnvironment(GameSproutRuntime::IGameSproutRuntime*) {}

    void InstallCurves(GameSproutRuntime::IGameSproutRuntime& runtime, GameSproutRuntime::IScene& scene);
    void SetStormOverlay(GameSproutRuntime::IGameSproutRuntime& runtime, GameSproutRuntime::IScene& scene,
                         std::size_t index, float amount);

    bool _Registered = false;
    std::uint64_t _SceneID = 0;
    std::array<std::uint64_t, 2> _OverlayIDs{};
    GameSproutRuntime::WeatherSettings _InitialWeather;
    CastleLightning _Lightning;
    CastleSunset _Sunset;
    double _Elapsed = 0;
    std::size_t _Phase = 0;
    std::size_t _NextStrike = 0;
    bool _TransitionStarted = false;
};

GAMESPROUT_REGIST_MODULE(CastleEnvironment)
