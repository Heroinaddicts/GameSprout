#include "PlayerInput.h"

namespace GameSproutPlayer {
using GameSproutRuntime::InputEventType;
using GameSproutRuntime::InputKey;
using GameSproutRuntime::InputMouseButton;
using GameSproutRuntime::RuntimeInputEvent;

namespace {
InputKey TranslateKey(SDL_Keycode key) {
    if (key >= SDLK_A && key <= SDLK_Z) return static_cast<InputKey>(
        static_cast<unsigned>(InputKey::A) + key - SDLK_A);
    if (key >= SDLK_0 && key <= SDLK_9) return static_cast<InputKey>(
        static_cast<unsigned>(InputKey::Digit0) + key - SDLK_0);
    if (key >= SDLK_F1 && key <= SDLK_F12) return static_cast<InputKey>(
        static_cast<unsigned>(InputKey::F1) + key - SDLK_F1);
    switch (key) {
    case SDLK_ESCAPE: return InputKey::Escape;
    case SDLK_RETURN: case SDLK_KP_ENTER: return InputKey::Enter;
    case SDLK_SPACE: return InputKey::Space;
    case SDLK_TAB: return InputKey::Tab;
    case SDLK_BACKSPACE: return InputKey::Backspace;
    case SDLK_DELETE: return InputKey::Delete;
    case SDLK_INSERT: return InputKey::Insert;
    case SDLK_HOME: return InputKey::Home;
    case SDLK_END: return InputKey::End;
    case SDLK_PAGEUP: return InputKey::PageUp;
    case SDLK_PAGEDOWN: return InputKey::PageDown;
    case SDLK_LEFT: return InputKey::Left;
    case SDLK_RIGHT: return InputKey::Right;
    case SDLK_UP: return InputKey::Up;
    case SDLK_DOWN: return InputKey::Down;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return InputKey::Shift;
    case SDLK_LCTRL: case SDLK_RCTRL: return InputKey::Control;
    case SDLK_LALT: case SDLK_RALT: return InputKey::Alt;
    case SDLK_LGUI: case SDLK_RGUI: return InputKey::Super;
    case SDLK_CAPSLOCK: return InputKey::CapsLock;
    default: return InputKey::None;
    }
}

InputMouseButton TranslateButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return InputMouseButton::Left;
    case SDL_BUTTON_RIGHT: return InputMouseButton::Right;
    case SDL_BUTTON_MIDDLE: return InputMouseButton::Middle;
    case SDL_BUTTON_X1: return InputMouseButton::X1;
    case SDL_BUTTON_X2: return InputMouseButton::X2;
    default: return InputMouseButton::Count;
    }
}
}

bool HandleInput(GameSproutRuntime::IGameSproutRuntime& runtime, const SDL_Event& event) {
    RuntimeInputEvent input;
    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        input.Type = InputEventType::FocusGained;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        input.Type = InputEventType::FocusLost;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        input.Type = event.type == SDL_EVENT_KEY_DOWN ? InputEventType::KeyDown : InputEventType::KeyUp;
        input.Key = TranslateKey(event.key.key);
        if (input.Key == InputKey::None) return false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        input.Type = InputEventType::MouseMove;
        input.X = event.motion.x;
        input.Y = event.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        input.Button = TranslateButton(event.button.button);
        if (input.Button == InputMouseButton::Count) return false;
        input.Type = InputEventType::MouseMove;
        input.X = event.button.x;
        input.Y = event.button.y;
        runtime.SubmitInputEvent(input);
        input.Type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp;
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        input.Type = InputEventType::MouseWheel;
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        input.WheelX = event.wheel.x * direction;
        input.WheelY = event.wheel.y * direction;
        break;
    }
    default:
        return false;
    }
    return runtime.SubmitInputEvent(input);
}
}
