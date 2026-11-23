#include "GameSproutRuntime.h"
#include <cmath>

namespace GameSproutRuntime {
namespace {
void ApplyButton(InputButtonState& state, bool down, bool sync) {
    if (!sync && state.Held != down) (down ? state.Pressed : state.Released) = true;
    state.Held = down;
}

void ClearEdges(auto& buttons) {
    for (auto& button : buttons) button.Pressed = button.Released = false;
}

void ReleaseButtons(auto& buttons) {
    for (auto& button : buttons) {
        button.Pressed = false;
        ApplyButton(button, false, false);
    }
}

InputKey KeyAlias(InputKey key) {
    switch (key) {
    case InputKey::LeftShift: case InputKey::RightShift: return InputKey::Shift;
    case InputKey::LeftControl: case InputKey::RightControl: return InputKey::Control;
    case InputKey::LeftAlt: case InputKey::RightAlt: return InputKey::Alt;
    case InputKey::LeftSuper: case InputKey::RightSuper: return InputKey::Super;
    default: return key;
    }
}

bool IsFinite(float x, float y) { return std::isfinite(x) && std::isfinite(y); }
}

void Runtime::ResetInputOutputs() {
    const bool hadWindowOutput = _Input.CursorMode != InputCursorMode::Normal || _Input.TextInputActive;
    _Input.CursorMode = InputCursorMode::Normal;
    _Input.TextInputActive = false;
    if (hadWindowOutput && _InputWindowOutput.Handler)
        _InputWindowOutput.Handler(_InputWindowOutput.Context, {InputOutputType::Reset});
    if (_InputDeviceOutput.Handler) _InputDeviceOutput.Handler(_InputDeviceOutput.Context, {InputOutputType::Reset});
}

void Runtime::ResetInput() {
    _Input.Focused = false;
    ResetInputOutputs();
    CancelInputPointers();
    _Input = {};
    _InputHeldKeys.fill(false);
    ResetUIPointer();
}

void Runtime::SetInputEnabled(bool enabled) {
    _InputEnabled = enabled && _ScriptExecutionEnabled && !_ShuttingDown;
    ResetInput();
}

void Runtime::CompleteInputFrame() {
    ClearEdges(_Input.Keys);
    ClearEdges(_Input.MouseButtons);
    _Input.MouseDelta = {};
    _Input.WheelDelta = {};
    _Input.TextInput.clear();
    CompleteDeviceInputFrame();
    CompleteInputPointers();
}

void Runtime::LoseInputFocus() {
    _Input.Focused = false;
    ResetInputOutputs();
    ResetUIPointer();
    CancelInputPointers();
    _InputHeldKeys.fill(false);
    ReleaseButtons(_Input.Keys);
    ReleaseButtons(_Input.MouseButtons);
    ClearDeviceInputOnFocusLoss();
    _Input.HasMousePosition = false;
    _Input.MouseDelta = {};
    _Input.WheelDelta = {};
    _Input.TextInput.clear();
    _Input.TextComposition.clear();
    _Input.TextCompositionStart = _Input.TextCompositionLength = 0;
}

bool Runtime::SubmitInputEvent(const RuntimeInputEvent& event) {
    if (!_InputEnabled || !_ScriptExecutionEnabled || _ShuttingDown) return false;
    if (event.Type >= InputEventType::GamepadAdded) return SubmitDeviceInput(event);
    if (event.Type == InputEventType::FocusLost) {
        if (_Input.Focused) LoseInputFocus();
        return true;
    }
    if (event.Type == InputEventType::FocusGained) {
        if (!_Input.Focused) {
            ClearEdges(_Input.Keys);
            ClearEdges(_Input.MouseButtons);
            _Input.Focused = true;
        }
        return true;
    }
    if (!_Input.Focused) return false;

    switch (event.Type) {
    case InputEventType::KeyDown:
    case InputEventType::KeyUp: {
        const auto index = static_cast<std::size_t>(event.Key);
        if (!index || index >= _Input.Keys.size()) return false;
        const bool down = event.Type == InputEventType::KeyDown;
        _InputHeldKeys[index] = down;
        const auto aggregate = KeyAlias(event.Key);
        if (aggregate != event.Key) ApplyButton(_Input.Keys[index], down, event.IsStateSync);
        const auto aggregateIndex = static_cast<std::size_t>(aggregate);
        bool held = _InputHeldKeys[aggregateIndex];
        if (!held && aggregate >= InputKey::Shift && aggregate <= InputKey::Super) {
            const auto left = static_cast<std::size_t>(InputKey::LeftShift) + 2*(aggregateIndex-static_cast<std::size_t>(InputKey::Shift));
            held = _InputHeldKeys[left] || _InputHeldKeys[left+1];
        }
        ApplyButton(_Input.Keys[aggregateIndex], held, event.IsStateSync);
        return true;
    }
    case InputEventType::MouseButtonDown:
    case InputEventType::MouseButtonUp: {
        const auto index = static_cast<std::size_t>(event.Button);
        if (index >= _Input.MouseButtons.size()) return false;
        const bool down = event.Type == InputEventType::MouseButtonDown;
        auto& state = _Input.MouseButtons[index];
        if (event.Button == InputMouseButton::Left && state.Held != down && _Input.HasMousePosition && !event.IsStateSync &&
            _Input.CursorMode != InputCursorMode::Locked) {
            QueueUIPointer(UIInputPointer::Mouse,0,0,_Input.MousePosition,down);
            PublishInputPointer({InputPointerKind::Mouse,0,0},down ? InputPointerPhase::Down : InputPointerPhase::Up,
                _Input.MousePosition,event.Timestamp);
        }
        ApplyButton(state, down, event.IsStateSync);
        return true;
    }
    case InputEventType::MouseMove:
        if (!IsFinite(event.X,event.Y) || (event.HasRelativeDelta && !IsFinite(event.DeltaX,event.DeltaY))) return false;
        if (event.HasRelativeDelta) _Input.MouseDelta = _Input.MouseDelta + Vector2{event.DeltaX,event.DeltaY};
        else if (_Input.HasMousePosition) _Input.MouseDelta = _Input.MouseDelta + (Vector2{event.X,event.Y} - _Input.MousePosition);
        _Input.MousePosition = {event.X,event.Y};
        _Input.HasMousePosition = true;
        if (!event.IsStateSync && _Input.CursorMode != InputCursorMode::Locked)
            PublishInputPointer({InputPointerKind::Mouse,0,0},InputPointerPhase::Move,_Input.MousePosition,event.Timestamp);
        return true;
    case InputEventType::MouseRelativeMove:
        if (!IsFinite(event.DeltaX,event.DeltaY)) return false;
        _Input.MouseDelta = _Input.MouseDelta + Vector2{event.DeltaX,event.DeltaY};
        return true;
    case InputEventType::MouseWheel:
        if (!IsFinite(event.WheelX,event.WheelY)) return false;
        _Input.WheelDelta = _Input.WheelDelta + Vector2{event.WheelX,event.WheelY};
        return true;
    case InputEventType::TouchBegan: case InputEventType::TouchMoved:
    case InputEventType::TouchEnded: case InputEventType::TouchCanceled:
        return SubmitTouchInput(event);
    case InputEventType::PenProximityIn: case InputEventType::PenProximityOut:
    case InputEventType::PenMove: case InputEventType::PenDown: case InputEventType::PenUp:
        return SubmitPenInput(event);
    case InputEventType::TextInput:
        if (!_Input.TextInputActive) return false;
        _Input.TextInput += event.Text;
        _Input.TextComposition.clear();
        _Input.TextCompositionStart = _Input.TextCompositionLength = 0;
        return true;
    case InputEventType::TextComposition:
        if (!_Input.TextInputActive) return false;
        _Input.TextComposition = event.Text;
        _Input.TextCompositionStart = std::max(0,event.TextStart);
        _Input.TextCompositionLength = std::max(0,event.TextLength);
        return true;
    default:
        return false;
    }
}

void Runtime::SetInputOutputHandler(InputOutputTarget target, InputOutputHandler handler, void* context) {
    if (target != InputOutputTarget::Window && target != InputOutputTarget::Device) return;
    auto& binding = target == InputOutputTarget::Window ? _InputWindowOutput : _InputDeviceOutput;
    if (binding.Handler == handler && binding.Context == context) return;
    const auto previous = binding;
    binding = {handler,handler ? context : nullptr};
    if (previous.Handler) previous.Handler(previous.Context, {InputOutputType::Reset});
    if (target == InputOutputTarget::Window) {
        ResetUIPointer();
        CancelInputPointers();
        _Input.CursorMode = InputCursorMode::Normal;
        _Input.TextInputActive = false;
        _Input.TextComposition.clear();
        _Input.TextCompositionStart = _Input.TextCompositionLength = 0;
        _Input.HasMousePosition = false;
        _Input.MouseDelta = {};
    } else for (auto& sensor : _Input.Sensors) {
        sensor.Enabled = sensor.HasSample = false;
        sensor.Value = {};
        sensor.Timestamp = 0;
    }
}

bool Runtime::SendInputOutput(const InputOutputCommand& command) {
    if (!_InputEnabled || !_ScriptExecutionEnabled || _ShuttingDown || !_Input.Focused) return false;
    auto invoke = [&](const InputOutputBinding& output, const InputOutputCommand& value) {
        return output.Handler && output.Handler(output.Context, value);
    };
    switch (command.Type) {
    case InputOutputType::SetCursorMode:
        if (command.CursorMode > InputCursorMode::Confined || !invoke(_InputWindowOutput,command)) return false;
        if (command.CursorMode == InputCursorMode::Locked) {
            PublishInputPointer({InputPointerKind::Mouse,0,0},InputPointerPhase::Cancel,_Input.MousePosition,0);
            if (_UIPointerSource == UIInputPointer::Mouse) ResetUIPointer();
        }
        _Input.CursorMode = command.CursorMode;
        _Input.HasMousePosition = false;
        _Input.MouseDelta = {};
        return true;
    case InputOutputType::WarpCursor:
        if (!IsFinite(command.X,command.Y) || _Input.CursorMode == InputCursorMode::Locked || !invoke(_InputWindowOutput,command))
            return false;
        _Input.MousePosition = {command.X,command.Y};
        _Input.HasMousePosition = true;
        return true;
    case InputOutputType::StartTextInput:
    case InputOutputType::StopTextInput:
        if (!invoke(_InputWindowOutput,command)) return false;
        _Input.TextInputActive = command.Type == InputOutputType::StartTextInput;
        if (!_Input.TextInputActive) {
            _Input.TextComposition.clear();
            _Input.TextCompositionStart = _Input.TextCompositionLength = 0;
        }
        return true;
    case InputOutputType::SetTextInputArea:
        return IsFinite(command.X,command.Y) && IsFinite(command.Width,command.Height) &&
            command.Width >= 0 && command.Height >= 0 && command.Cursor >= 0 && invoke(_InputWindowOutput,command);
    case InputOutputType::Rumble:
    case InputOutputType::RumbleTriggers: {
        const auto* gamepad = _Input.GetGamepad(command.DeviceId);
        const auto capability = command.Type == InputOutputType::Rumble ? InputDeviceCapability::Rumble : InputDeviceCapability::TriggerRumble;
        if (!gamepad || !gamepad->Supports(capability) || !IsFinite(command.Low,command.High)) return false;
        auto normalized = command;
        normalized.Low = std::clamp(command.Low,0.f,1.f);
        normalized.High = std::clamp(command.High,0.f,1.f);
        return invoke(_InputDeviceOutput,normalized);
    }
    case InputOutputType::SetLed: {
        const auto* gamepad = _Input.GetGamepad(command.DeviceId);
        return gamepad && gamepad->Supports(InputDeviceCapability::Led) && invoke(_InputDeviceOutput,command);
    }
    case InputOutputType::EnableSensor: {
        const auto match = std::find_if(_Input.Sensors.begin(),_Input.Sensors.end(),[&](const auto& sensor) {
            return sensor.Id == command.DeviceId;
        });
        if (match == _Input.Sensors.end() || !invoke(_InputDeviceOutput,command)) return false;
        match->Enabled = command.Enabled;
        if (!command.Enabled) {
            match->HasSample = false;
            match->Value = {};
            match->Timestamp = 0;
        }
        return true;
    }
    case InputOutputType::Reset:
        return false;
    }
    return false;
}
}
