#pragma once

#include "GameSproutRuntimeApi.h"
#include <SDL3/SDL.h>
#include <map>
#include <optional>

namespace GameSproutPlayer {
class PlayerInput final {
public:
    void SetWindow(SDL_Window* window) { _Window = window; _WindowId = SDL_GetWindowID(window); }
    bool HandleEvent(GameSproutRuntime::IGameSproutRuntime&, const SDL_Event&);
    static bool Output(void*, const GameSproutRuntime::InputOutputCommand&);

private:
    struct PenAxes { float Pressure = 0, TiltX = 0, TiltY = 0, Twist = 0; };
    struct PendingWarp { float X, Y; std::uint64_t Timestamp; };
    bool MatchWindow(SDL_WindowID id) const { return !id || id == _WindowId; }
    bool HandlePen(GameSproutRuntime::IGameSproutRuntime&, const SDL_Event&);
    bool ApplyOutput(const GameSproutRuntime::InputOutputCommand&);
    bool SetCursorMode(GameSproutRuntime::InputCursorMode);

    SDL_Window* _Window = nullptr;
    SDL_WindowID _WindowId = 0;
    std::map<SDL_PenID, PenAxes> _PenAxes;
    std::optional<PendingWarp> _Warp;
};
}
