#pragma once

#include "GameSproutRuntimeApi.h"
#include <array>
#include <vector>

class CastleRainImpacts final {
public:
    void Update(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene&, float density, float deltaSeconds);
    void Release(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene&);

private:
    struct Impact {
        std::uint64_t ObjectID = 0;
        float Remaining = 0;
        float Width = 0;
        float Height = 0;
    };
    struct WaterTriangle {
        GameSproutRuntime::Vector3 A, B, C;
    };

    bool InitializePool(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene&);
    void CacheWaterSurface(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene&);
    bool IsOverWater(float x, float z) const;
    void Spawn(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene&,
        GameSproutRuntime::Vector3 eye, GameSproutRuntime::Vector3 forward);
    float Random();

    std::array<Impact, 18> _Ground{};
    std::array<Impact, 16> _Water{};
    std::vector<WaterTriangle> _WaterTriangles;
    std::uint64_t _PoolID = 0;
    std::uint32_t _RandomState = 0x539ca97b;
    float _WaterHeight = 0;
    float _EmitRemainder = 0;
    bool _Initialized = false;
};
