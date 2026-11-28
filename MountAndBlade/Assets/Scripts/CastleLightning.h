#pragma once

#include "GameSproutRuntimeApi.h"
#include <array>

class CastleLightning final {
public:
    void Strike(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene&, unsigned index);
    void Update(GameSproutRuntime::IGameSproutRuntime&, float deltaSeconds);
    void Release(GameSproutRuntime::IGameSproutRuntime&, GameSproutRuntime::IScene&);

private:
    struct Branch {
        std::uint64_t ObjectID = 0;
        float Width = 0;
        float Length = 0;
    };
    bool CreateMeshes(GameSproutRuntime::IScene&);

    std::array<Branch, 8> _Branches{};
    std::uint64_t _GroupID = 0;
    std::uint64_t _ContactID = 0;
    float _Age = 0;
    bool _Active = false;
};
