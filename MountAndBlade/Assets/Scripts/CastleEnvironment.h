#pragma once

#include "RuntimeScriptModule.h"

class CastleEnvironment final : public GameSproutRuntime::IModule {
protected:
    bool Initialize(GameSproutRuntime::IGameSproutRuntime* runtime) override;
    bool Launch(GameSproutRuntime::IGameSproutRuntime*) override { return true; }
    void Release(GameSproutRuntime::IGameSproutRuntime* runtime) override;

private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<CastleEnvironment>;
    explicit CastleEnvironment(GameSproutRuntime::IGameSproutRuntime*) {}

    bool _Registered = false;
};

GAMESPROUT_REGIST_MODULE(CastleEnvironment)
