#pragma once

#include "GameSproutRuntimeApi.h"
#include "RuntimeRenderApi.h"
#include <SDL3/SDL.h>

namespace GameSproutPlayer {
GameSproutRuntime::NativeWindow RenderWindow(SDL_Window* window);
bool HandleInput(GameSproutRuntime::IGameSproutRuntime&, const SDL_Event&);
}
