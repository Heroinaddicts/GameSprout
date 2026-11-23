#include "PlayerInput.h"
#import <Cocoa/Cocoa.h>

namespace GameSproutPlayer {
GameSproutRuntime::NativeWindow RenderWindow(SDL_Window* window) {
    NSWindow* native = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    NSView* view = native.contentView;
    GameSproutRuntime::NativeWindow result;
    result.System = GameSproutRuntime::NativeWindowSystem::Cocoa;
    result.Handle = reinterpret_cast<std::uintptr_t>((__bridge void*)view);
    return result;
}
}
