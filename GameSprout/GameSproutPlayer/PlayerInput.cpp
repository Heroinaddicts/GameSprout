#include "PlayerInput.h"
#include <cmath>
#include <limits>

namespace GameSproutPlayer {
using namespace GameSproutRuntime;

namespace {
InputKey OffsetKey(InputKey first, int offset) {
    return static_cast<InputKey>(static_cast<int>(first) + offset);
}

InputKey TranslateKey(SDL_Scancode key) {
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) return OffsetKey(InputKey::A, key - SDL_SCANCODE_A);
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9) return OffsetKey(InputKey::Digit1, key - SDL_SCANCODE_1);
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) return OffsetKey(InputKey::F1, key - SDL_SCANCODE_F1);
    if (key >= SDL_SCANCODE_F13 && key <= SDL_SCANCODE_F24) return OffsetKey(InputKey::F13, key - SDL_SCANCODE_F13);
    if (key >= SDL_SCANCODE_KP_1 && key <= SDL_SCANCODE_KP_9) return OffsetKey(InputKey::Keypad1, key - SDL_SCANCODE_KP_1);
    switch (key) {
    case SDL_SCANCODE_0: return InputKey::Digit0;
    case SDL_SCANCODE_ESCAPE: return InputKey::Escape;
    case SDL_SCANCODE_RETURN: return InputKey::Enter;
    case SDL_SCANCODE_SPACE: return InputKey::Space;
    case SDL_SCANCODE_TAB: return InputKey::Tab;
    case SDL_SCANCODE_BACKSPACE: return InputKey::Backspace;
    case SDL_SCANCODE_DELETE: return InputKey::Delete;
    case SDL_SCANCODE_INSERT: return InputKey::Insert;
    case SDL_SCANCODE_HOME: return InputKey::Home;
    case SDL_SCANCODE_END: return InputKey::End;
    case SDL_SCANCODE_PAGEUP: return InputKey::PageUp;
    case SDL_SCANCODE_PAGEDOWN: return InputKey::PageDown;
    case SDL_SCANCODE_LEFT: return InputKey::Left;
    case SDL_SCANCODE_RIGHT: return InputKey::Right;
    case SDL_SCANCODE_UP: return InputKey::Up;
    case SDL_SCANCODE_DOWN: return InputKey::Down;
    case SDL_SCANCODE_LSHIFT: return InputKey::LeftShift;
    case SDL_SCANCODE_RSHIFT: return InputKey::RightShift;
    case SDL_SCANCODE_LCTRL: return InputKey::LeftControl;
    case SDL_SCANCODE_RCTRL: return InputKey::RightControl;
    case SDL_SCANCODE_LALT: return InputKey::LeftAlt;
    case SDL_SCANCODE_RALT: return InputKey::RightAlt;
    case SDL_SCANCODE_LGUI: return InputKey::LeftSuper;
    case SDL_SCANCODE_RGUI: return InputKey::RightSuper;
    case SDL_SCANCODE_CAPSLOCK: return InputKey::CapsLock;
    case SDL_SCANCODE_MINUS: return InputKey::Minus;
    case SDL_SCANCODE_EQUALS: return InputKey::Equal;
    case SDL_SCANCODE_LEFTBRACKET: return InputKey::LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET: return InputKey::RightBracket;
    case SDL_SCANCODE_BACKSLASH: case SDL_SCANCODE_NONUSBACKSLASH: return InputKey::Backslash;
    case SDL_SCANCODE_SEMICOLON: return InputKey::Semicolon;
    case SDL_SCANCODE_APOSTROPHE: return InputKey::Apostrophe;
    case SDL_SCANCODE_GRAVE: return InputKey::Grave;
    case SDL_SCANCODE_COMMA: return InputKey::Comma;
    case SDL_SCANCODE_PERIOD: return InputKey::Period;
    case SDL_SCANCODE_SLASH: return InputKey::Slash;
    case SDL_SCANCODE_PRINTSCREEN: return InputKey::PrintScreen;
    case SDL_SCANCODE_SCROLLLOCK: return InputKey::ScrollLock;
    case SDL_SCANCODE_PAUSE: return InputKey::Pause;
    case SDL_SCANCODE_NUMLOCKCLEAR: return InputKey::NumLock;
    case SDL_SCANCODE_APPLICATION: case SDL_SCANCODE_MENU: return InputKey::Menu;
    case SDL_SCANCODE_KP_0: return InputKey::Keypad0;
    case SDL_SCANCODE_KP_PERIOD: case SDL_SCANCODE_KP_DECIMAL: return InputKey::KeypadDecimal;
    case SDL_SCANCODE_KP_DIVIDE: return InputKey::KeypadDivide;
    case SDL_SCANCODE_KP_MULTIPLY: return InputKey::KeypadMultiply;
    case SDL_SCANCODE_KP_MINUS: return InputKey::KeypadSubtract;
    case SDL_SCANCODE_KP_PLUS: return InputKey::KeypadAdd;
    case SDL_SCANCODE_KP_ENTER: return InputKey::KeypadEnter;
    case SDL_SCANCODE_KP_EQUALS: case SDL_SCANCODE_KP_EQUALSAS400: return InputKey::KeypadEqual;
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

bool IsRealMouse(SDL_MouseID mouse) { return mouse != SDL_TOUCH_MOUSEID && mouse != SDL_PEN_MOUSEID; }

std::uint32_t TranslatePenButtons(SDL_PenInputFlags state) {
    std::uint32_t result = 0;
    if (state & SDL_PEN_INPUT_DOWN) result |= static_cast<std::uint32_t>(InputPenButton::Tip);
    if (state & SDL_PEN_INPUT_ERASER_TIP) result |= static_cast<std::uint32_t>(InputPenButton::Eraser);
    if (state & SDL_PEN_INPUT_BUTTON_1) result |= static_cast<std::uint32_t>(InputPenButton::Barrel1);
    if (state & SDL_PEN_INPUT_BUTTON_2) result |= static_cast<std::uint32_t>(InputPenButton::Barrel2);
    if (state & SDL_PEN_INPUT_BUTTON_3) result |= static_cast<std::uint32_t>(InputPenButton::Barrel3);
    return result;
}

bool RectValue(float value) {
    return std::isfinite(value) && static_cast<double>(value) >= std::numeric_limits<int>::min() &&
        static_cast<double>(value) <= std::numeric_limits<int>::max();
}
}

bool PlayerInput::HandleEvent(IGameSproutRuntime& runtime, const SDL_Event& event) {
    if (!_Window) return false;
    RuntimeInputEvent input;
    input.Timestamp = event.common.timestamp;
    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (event.window.windowID != _WindowId) return false;
        input.Type = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ? InputEventType::FocusGained : InputEventType::FocusLost;
        if (input.Type == InputEventType::FocusLost) {
            _PenAxes.clear();
            _Warp.reset();
        }
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        _PenAxes.clear();
        _Warp.reset();
        input.Type = InputEventType::FocusLost;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!MatchWindow(event.key.windowID)) return false;
        if (event.key.repeat) return true;
        input.Type = event.type == SDL_EVENT_KEY_DOWN ? InputEventType::KeyDown : InputEventType::KeyUp;
        input.DeviceId = event.key.which;
        input.Key = TranslateKey(event.key.scancode != SDL_SCANCODE_UNKNOWN ? event.key.scancode :
            SDL_GetScancodeFromKey(event.key.key, nullptr));
        if (input.Key == InputKey::None) return false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (!MatchWindow(event.motion.windowID) || !IsRealMouse(event.motion.which)) return false;
        input.Type = SDL_GetWindowRelativeMouseMode(_Window) ? InputEventType::MouseRelativeMove : InputEventType::MouseMove;
        input.DeviceId = event.motion.which;
        input.X = event.motion.x;
        input.Y = event.motion.y;
        input.DeltaX = event.motion.xrel;
        input.DeltaY = event.motion.yrel;
        input.HasRelativeDelta = true;
        // The API already applied the target position. Suppress the resulting
        // SDL warp motion without swallowing an unrelated real mouse movement.
        if (_Warp && input.Timestamp >= _Warp->Timestamp) {
            if (std::abs(_Warp->X - input.X) < 1 && std::abs(_Warp->Y - input.Y) < 1)
                input.DeltaX = input.DeltaY = 0;
            _Warp.reset();
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!MatchWindow(event.button.windowID) || !IsRealMouse(event.button.which)) return false;
        input.Button = TranslateButton(event.button.button);
        if (input.Button == InputMouseButton::Count) return false;
        input.DeviceId = event.button.which;
        if (!SDL_GetWindowRelativeMouseMode(_Window)) {
            input.Type = InputEventType::MouseMove;
            input.X = event.button.x;
            input.Y = event.button.y;
            runtime.SubmitInputEvent(input);
        }
        input.Type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp;
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        if (!MatchWindow(event.wheel.windowID) || !IsRealMouse(event.wheel.which)) return false;
        input.Type = InputEventType::MouseWheel;
        input.DeviceId = event.wheel.which;
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        input.WheelX = event.wheel.x * direction;
        input.WheelY = event.wheel.y * direction;
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (!MatchWindow(event.text.windowID) || !event.text.text) return false;
        input.Type = InputEventType::TextInput;
        input.Text = event.text.text;
        break;
    case SDL_EVENT_TEXT_EDITING:
        if (!MatchWindow(event.edit.windowID)) return false;
        input.Type = InputEventType::TextComposition;
        if (event.edit.text) input.Text = event.edit.text;
        input.TextStart = event.edit.start;
        input.TextLength = event.edit.length;
        break;
    case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION: case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: {
        const auto& touch = event.tfinger;
        if (!MatchWindow(touch.windowID) || touch.touchID == SDL_MOUSE_TOUCHID || touch.touchID == SDL_PEN_TOUCHID) return false;
        int width = 0, height = 0;
        if (!SDL_GetWindowSize(_Window, &width, &height) || width <= 0 || height <= 0) return false;
        switch (event.type) {
        case SDL_EVENT_FINGER_DOWN: input.Type = InputEventType::TouchBegan; break;
        case SDL_EVENT_FINGER_MOTION: input.Type = InputEventType::TouchMoved; break;
        case SDL_EVENT_FINGER_UP: input.Type = InputEventType::TouchEnded; break;
        default: input.Type = InputEventType::TouchCanceled; break;
        }
        input.DeviceId = static_cast<std::uint64_t>(touch.touchID);
        input.ContactId = static_cast<std::uint64_t>(touch.fingerID);
        input.X = touch.x * width;
        input.Y = touch.y * height;
        input.DeltaX = touch.dx * width;
        input.DeltaY = touch.dy * height;
        input.Pressure = touch.pressure;
        break;
    }
    default:
        return HandlePen(runtime, event);
    }
    return runtime.SubmitInputEvent(input);
}

bool PlayerInput::HandlePen(IGameSproutRuntime& runtime, const SDL_Event& event) {
    RuntimeInputEvent input;
    input.Timestamp = event.common.timestamp;
    SDL_PenInputFlags state = 0;
    SDL_PenID pen = 0;
    switch (event.type) {
    case SDL_EVENT_PEN_PROXIMITY_IN: case SDL_EVENT_PEN_PROXIMITY_OUT:
        if (!MatchWindow(event.pproximity.windowID)) return false;
        input.Type = event.type == SDL_EVENT_PEN_PROXIMITY_IN ? InputEventType::PenProximityIn : InputEventType::PenProximityOut;
        input.DeviceId = event.pproximity.which;
        if (input.Type == InputEventType::PenProximityOut) _PenAxes.erase(event.pproximity.which);
        return runtime.SubmitInputEvent(input);
    case SDL_EVENT_PEN_MOTION:
        if (!MatchWindow(event.pmotion.windowID)) return false;
        pen = event.pmotion.which;
        state = event.pmotion.pen_state;
        input.Type = InputEventType::PenMove;
        input.X = event.pmotion.x;
        input.Y = event.pmotion.y;
        break;
    case SDL_EVENT_PEN_BUTTON_DOWN: case SDL_EVENT_PEN_BUTTON_UP:
        if (!MatchWindow(event.pbutton.windowID)) return false;
        pen = event.pbutton.which;
        state = event.pbutton.pen_state;
        input.Type = InputEventType::PenMove;
        input.X = event.pbutton.x;
        input.Y = event.pbutton.y;
        break;
    case SDL_EVENT_PEN_DOWN: case SDL_EVENT_PEN_UP:
        if (!MatchWindow(event.ptouch.windowID)) return false;
        pen = event.ptouch.which;
        state = event.ptouch.pen_state;
        if (event.ptouch.eraser) state |= SDL_PEN_INPUT_ERASER_TIP;
        input.Type = event.type == SDL_EVENT_PEN_DOWN ? InputEventType::PenDown : InputEventType::PenUp;
        input.X = event.ptouch.x;
        input.Y = event.ptouch.y;
        break;
    case SDL_EVENT_PEN_AXIS:
        if (!MatchWindow(event.paxis.windowID) || !std::isfinite(event.paxis.value)) return false;
        pen = event.paxis.which;
        state = event.paxis.pen_state;
        input.Type = InputEventType::PenMove;
        input.X = event.paxis.x;
        input.Y = event.paxis.y;
        break;
    default: return false;
    }
    if (!pen || !runtime.GetInputState().Focused) return false;
    auto& axes = _PenAxes[pen];
    if (event.type == SDL_EVENT_PEN_AXIS) {
        switch (event.paxis.axis) {
        case SDL_PEN_AXIS_PRESSURE: axes.Pressure = event.paxis.value; break;
        case SDL_PEN_AXIS_XTILT: axes.TiltX = event.paxis.value; break;
        case SDL_PEN_AXIS_YTILT: axes.TiltY = event.paxis.value; break;
        case SDL_PEN_AXIS_ROTATION: axes.Twist = event.paxis.value; break;
        default: break;
        }
    }
    if (input.Type == InputEventType::PenUp) axes.Pressure = 0;
    input.DeviceId = pen;
    input.Capabilities = TranslatePenButtons(state);
    input.Pressure = axes.Pressure;
    input.TiltX = axes.TiltX;
    input.TiltY = axes.TiltY;
    input.Twist = axes.Twist;
    return runtime.SubmitInputEvent(input);
}

bool PlayerInput::Output(void* context, const InputOutputCommand& command) {
    return context && static_cast<PlayerInput*>(context)->ApplyOutput(command);
}

bool PlayerInput::SetCursorMode(InputCursorMode mode) {
    const bool locked = mode == InputCursorMode::Locked;
    const bool confined = mode == InputCursorMode::Confined;
    const bool visible = mode == InputCursorMode::Normal || confined;
    const bool wasLocked = SDL_GetWindowRelativeMouseMode(_Window);
    const bool wasConfined = SDL_GetWindowMouseGrab(_Window);
    const bool wasVisible = SDL_CursorVisible();
    // An explicit relative-mode command also disables SDL's heuristic that
    // would otherwise infer a lock from repeated warps of a hidden cursor.
    if (!SDL_SetWindowRelativeMouseMode(_Window, locked)) return false;
    if ((confined == wasConfined || SDL_SetWindowMouseGrab(_Window, confined)) &&
        (visible == wasVisible || (visible ? SDL_ShowCursor() : SDL_HideCursor()))) return true;
    SDL_SetWindowRelativeMouseMode(_Window, wasLocked);
    SDL_SetWindowMouseGrab(_Window, wasConfined);
    return false;
}

bool PlayerInput::ApplyOutput(const InputOutputCommand& command) {
    if (!_Window) return false;
    switch (command.Type) {
    case InputOutputType::SetCursorMode:
        _Warp.reset();
        return SetCursorMode(command.CursorMode);
    case InputOutputType::WarpCursor:
        if (!std::isfinite(command.X) || !std::isfinite(command.Y) || SDL_GetWindowRelativeMouseMode(_Window)) return false;
        _Warp = PendingWarp{command.X, command.Y, SDL_GetTicksNS()};
        SDL_WarpMouseInWindow(_Window, command.X, command.Y);
        return true;
    case InputOutputType::StartTextInput:
        return SDL_StartTextInput(_Window);
    case InputOutputType::StopTextInput:
        return SDL_StopTextInput(_Window);
    case InputOutputType::SetTextInputArea: {
        if (!RectValue(command.X) || !RectValue(command.Y) || !RectValue(command.Width) || !RectValue(command.Height) ||
            command.Width < 0 || command.Height < 0) return false;
        const SDL_Rect rect{static_cast<int>(command.X), static_cast<int>(command.Y),
            static_cast<int>(command.Width), static_cast<int>(command.Height)};
        return SDL_SetTextInputArea(_Window, &rect, command.Cursor);
    }
    case InputOutputType::Reset: {
        _PenAxes.clear();
        _Warp.reset();
        const bool text = !SDL_TextInputActive(_Window) || SDL_StopTextInput(_Window);
        const bool cursor = SetCursorMode(InputCursorMode::Normal);
        return text && cursor;
    }
    default:
        return false;
    }
}
}
