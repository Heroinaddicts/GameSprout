#pragma once

#include "GameSproutRuntimeApi.h"
#include <SDL3/SDL.h>

namespace GameSproutPlayer {
bool HandleInput(GameSproutRuntime::IGameSproutRuntime&, const SDL_Event&);
}
