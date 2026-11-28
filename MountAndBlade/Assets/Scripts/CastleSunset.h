#pragma once

#include "RuntimeEnvironmentApi.h"
#include "RuntimeLightingApi.h"

class CastleSunset final {
public:
    static constexpr char EffectID[] = "Castle.Sunset";
    static constexpr char EffectType[] = "MountAndBlade.Sunset";
    static constexpr char WeightParameter[] = "SunsetWeight";

    bool Register(GameSproutRuntime::IEnvironmentRuntime& environment);
    bool Bind(GameSproutRuntime::IGameSproutRuntime& runtime, GameSproutRuntime::IScene& scene);
    void SetWeight(GameSproutRuntime::IGameSproutRuntime& runtime, float weight);
    void Release(GameSproutRuntime::IGameSproutRuntime& runtime, GameSproutRuntime::IScene* scene);

private:
    GameSproutRuntime::LightSettings _DaySettings;
    GameSproutRuntime::LightSettings _SunsetSettings;
    std::uint64_t _DayID = 0;
    std::uint64_t _SunsetID = 0;
    float _Weight = 0;
    bool _Registered = false;
};
