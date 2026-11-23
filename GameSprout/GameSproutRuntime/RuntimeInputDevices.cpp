#include "GameSproutRuntime.h"
#include <cmath>
#include <iterator>

namespace GameSproutRuntime {
namespace {
auto FindDevice(auto& devices, InputDeviceId id) {
    return std::find_if(devices.begin(),devices.end(),[id](const auto& device) { return device.Id == id; });
}

bool IsFinite(Vector2 point) { return std::isfinite(point.X) && std::isfinite(point.Y); }

bool IsActive(const InputTouchState& touch) {
    return touch.Phase != InputTouchPhase::Ended && touch.Phase != InputTouchPhase::Canceled;
}

void ClearEdges(auto& buttons) {
    for (auto& button : buttons) button.Pressed = button.Released = false;
}

void ReleaseButton(InputButtonState& button) {
    button.Pressed = false;
    button.Released = button.Released || button.Held;
    button.Held = false;
}

bool ApplyButton(auto& buttons, const RuntimeInputEvent& event, bool down) {
    if (event.Index >= buttons.size()) return false;
    auto& button = buttons[event.Index];
    if (!event.IsStateSync && button.Held != down) (down ? button.Pressed : button.Released) = true;
    button.Held = down;
    return true;
}

const InputTouchState* ApplyTouch(std::vector<InputTouchState>& contacts, std::vector<InputTouchState>& changes,
                                  const RuntimeInputEvent& event, InputTouchPhase phase) {
    if (!event.DeviceId || !event.ContactId || !IsFinite({event.X,event.Y}) || !std::isfinite(event.Pressure)) return nullptr;
    auto current = std::find_if(contacts.begin(),contacts.end(),[&](const auto& item) {
        return item.DeviceId == event.DeviceId && item.ContactId == event.ContactId && item.Surface == event.Index;
    });
    if (phase == InputTouchPhase::Began) {
        if (current != contacts.end() && IsActive(*current)) return nullptr;
        if (current == contacts.end()) {
            contacts.emplace_back();
            current = std::prev(contacts.end());
        }
        *current = {};
        current->DeviceId = event.DeviceId;
        current->ContactId = event.ContactId;
        current->Surface = event.Index;
    } else if (current == contacts.end() || !IsActive(*current)) return nullptr;
    const Vector2 next{event.X,event.Y};
    const Vector2 delta = phase == InputTouchPhase::Began ? Vector2{} : next - current->Position;
    current->Phase = event.IsStateSync ? InputTouchPhase::Stationary : phase;
    current->Timestamp = event.Timestamp;
    current->Delta = current->Delta + delta;
    current->Position = next;
    current->Pressure = std::clamp(event.Pressure,0.f,1.f);
    if (event.IsStateSync) return &*current;
    changes.push_back(*current);
    changes.back().Delta = delta;
    return &changes.back();
}

void CancelContact(InputTouchState& contact, std::vector<InputTouchState>& changes, std::uint64_t time) {
    contact.Phase = InputTouchPhase::Canceled;
    contact.Timestamp = time;
    contact.Pressure = 0.f;
    changes.push_back(contact);
    changes.back().Delta = {};
}

void CompleteContacts(std::vector<InputTouchState>& contacts, std::vector<InputTouchState>& changes) {
    std::erase_if(contacts,[](const auto& contact) { return !IsActive(contact); });
    for (auto& contact : contacts) {
        contact.Phase = InputTouchPhase::Stationary;
        contact.Delta = {};
    }
    changes.clear();
}

InputTouchPhase TouchPhase(InputEventType type) {
    if (type == InputEventType::TouchBegan || type == InputEventType::GamepadTouchBegan) return InputTouchPhase::Began;
    if (type == InputEventType::TouchMoved || type == InputEventType::GamepadTouchMoved) return InputTouchPhase::Moved;
    if (type == InputEventType::TouchCanceled) return InputTouchPhase::Canceled;
    return InputTouchPhase::Ended;
}
}

bool Runtime::SubmitTouchInput(const RuntimeInputEvent& event) {
    const auto phase = TouchPhase(event.Type);
    if (phase == InputTouchPhase::Canceled && !event.ContactId) {
        for (auto& contact : _Input.Touches) {
            if (!IsActive(contact) || (event.DeviceId && contact.DeviceId != event.DeviceId)) continue;
            CancelContact(contact,_Input.TouchChanges,event.Timestamp);
            QueueUIPointer(UIInputPointer::Touch,contact.DeviceId,contact.ContactId,contact.Position,false,true);
            PublishInputPointer({InputPointerKind::Touch,contact.DeviceId,contact.ContactId},
                InputPointerPhase::Cancel,contact.Position,event.Timestamp);
        }
        return true;
    }
    const auto* touch = ApplyTouch(_Input.Touches,_Input.TouchChanges,event,phase);
    if (!touch) return false;
    if (!event.IsStateSync) {
        const auto pointerPhase = phase == InputTouchPhase::Began ? InputPointerPhase::Down :
            phase == InputTouchPhase::Moved ? InputPointerPhase::Move :
            phase == InputTouchPhase::Canceled ? InputPointerPhase::Cancel : InputPointerPhase::Up;
        PublishInputPointer({InputPointerKind::Touch,touch->DeviceId,touch->ContactId},pointerPhase,touch->Position,event.Timestamp);
        if (phase != InputTouchPhase::Moved)
            QueueUIPointer(UIInputPointer::Touch,touch->DeviceId,touch->ContactId,touch->Position,
                           phase == InputTouchPhase::Began,phase == InputTouchPhase::Canceled);
    }
    return true;
}

bool Runtime::SubmitPenInput(const RuntimeInputEvent& event) {
    if (!event.DeviceId) return false;
    auto pen = FindDevice(_Input.Pens,event.DeviceId);
    if (event.Type == InputEventType::PenProximityOut) {
        if (pen == _Input.Pens.end()) return false;
        pen->InRange = pen->HasPosition = false;
        pen->Pressure = 0;
        pen->Buttons = 0;
        ReleaseButton(pen->Tip);
        QueueUIPointer(UIInputPointer::Pen,pen->Id,0,pen->Position,false,true);
        PublishInputPointer({InputPointerKind::Pen,pen->Id,0},InputPointerPhase::Cancel,pen->Position,event.Timestamp);
        return true;
    }
    if (!IsFinite({event.X,event.Y}) || !IsFinite({event.TiltX,event.TiltY}) ||
        !std::isfinite(event.Pressure) || !std::isfinite(event.Twist)) return false;
    if (pen == _Input.Pens.end()) {
        _Input.Pens.emplace_back();
        pen = std::prev(_Input.Pens.end());
        pen->Id = event.DeviceId;
    }
    pen->InRange = true;
    if (event.Type == InputEventType::PenProximityIn) return true;
    const Vector2 next{event.X,event.Y};
    if (pen->HasPosition) pen->Delta = pen->Delta + (next - pen->Position);
    pen->HasPosition = true;
    pen->Position = next;
    pen->Pressure = std::clamp(event.Pressure,0.f,1.f);
    pen->Tilt = {event.TiltX,event.TiltY};
    pen->Twist = event.Twist;
    pen->Buttons = event.Capabilities;
    const auto tip = static_cast<std::uint32_t>(InputPenButton::Tip);
    if (event.Type == InputEventType::PenDown) pen->Buttons |= tip;
    if (event.Type == InputEventType::PenUp) pen->Buttons &= ~tip;
    // Some hosts only guarantee the tip state on make/break events.
    if (event.Type == InputEventType::PenMove && pen->Tip.Held) pen->Buttons |= tip;
    const bool down = (pen->Buttons & tip) != 0;
    if (down != pen->Tip.Held) {
        if (!event.IsStateSync) {
            (down ? pen->Tip.Pressed : pen->Tip.Released) = true;
            QueueUIPointer(UIInputPointer::Pen,pen->Id,0,pen->Position,down);
            PublishInputPointer({InputPointerKind::Pen,pen->Id,0},down ? InputPointerPhase::Down : InputPointerPhase::Up,
                pen->Position,event.Timestamp);
        }
        pen->Tip.Held = down;
    } else if (!event.IsStateSync && event.Type == InputEventType::PenMove)
        PublishInputPointer({InputPointerKind::Pen,pen->Id,0},InputPointerPhase::Move,pen->Position,event.Timestamp);
    return true;
}

bool Runtime::SubmitDeviceInput(const RuntimeInputEvent& event) {
    if (!event.DeviceId) return false;
    auto add = [&](auto& devices,InputDeviceKind kind) -> auto& {
        auto item = FindDevice(devices,event.DeviceId);
        if (item == devices.end()) {
            devices.emplace_back();
            item = std::prev(devices.end());
            item->Id = event.DeviceId;
            _Input.DeviceChanges.push_back({kind,event.DeviceId,true});
        }
        item->Name = event.Text;
        return *item;
    };
    auto remove = [&](auto& devices,InputDeviceKind kind) {
        const auto item = FindDevice(devices,event.DeviceId);
        if (item == devices.end()) return false;
        devices.erase(item);
        _Input.DeviceChanges.push_back({kind,event.DeviceId,false});
        return true;
    };
    switch (event.Type) {
    case InputEventType::GamepadAdded:
        add(_Input.Gamepads,InputDeviceKind::Gamepad).Capabilities = event.Capabilities;
        return true;
    case InputEventType::JoystickAdded: {
        auto& joystick = add(_Input.Joysticks,InputDeviceKind::Joystick);
        joystick.Axes.resize(event.AxisCount);
        joystick.Buttons.resize(event.ButtonCount);
        joystick.Hats.resize(event.HatCount);
        return true;
    }
    case InputEventType::SensorAdded:
        add(_Input.Sensors,InputDeviceKind::Sensor).Kind = event.Sensor;
        return true;
    case InputEventType::GamepadRemoved: return remove(_Input.Gamepads,InputDeviceKind::Gamepad);
    case InputEventType::JoystickRemoved: return remove(_Input.Joysticks,InputDeviceKind::Joystick);
    case InputEventType::SensorRemoved: return remove(_Input.Sensors,InputDeviceKind::Sensor);
    default: break;
    }
    if (!_Input.Focused) return false;
    switch (event.Type) {
    case InputEventType::GamepadAxis: case InputEventType::GamepadButtonDown: case InputEventType::GamepadButtonUp:
    case InputEventType::GamepadTouchBegan: case InputEventType::GamepadTouchMoved: case InputEventType::GamepadTouchEnded: {
        const auto gamepad = FindDevice(_Input.Gamepads,event.DeviceId);
        if (gamepad == _Input.Gamepads.end()) return false;
        if (event.Type == InputEventType::GamepadAxis) {
            if (event.Index >= gamepad->Axes.size() || !std::isfinite(event.Value)) return false;
            const auto triggerStart = static_cast<std::uint16_t>(InputGamepadAxis::LeftTrigger);
            gamepad->Axes[event.Index] = std::clamp(event.Value,event.Index < triggerStart ? -1.f : 0.f,1.f);
            return true;
        }
        if (event.Type == InputEventType::GamepadButtonDown || event.Type == InputEventType::GamepadButtonUp)
            return ApplyButton(gamepad->Buttons,event,event.Type == InputEventType::GamepadButtonDown);
        return ApplyTouch(gamepad->Touches,gamepad->TouchChanges,event,TouchPhase(event.Type)) != nullptr;
    }
    case InputEventType::JoystickAxis: case InputEventType::JoystickButtonDown:
    case InputEventType::JoystickButtonUp: case InputEventType::JoystickHat: {
        const auto joystick = FindDevice(_Input.Joysticks,event.DeviceId);
        if (joystick == _Input.Joysticks.end()) return false;
        if (event.Type == InputEventType::JoystickAxis) {
            if (event.Index >= joystick->Axes.size() || !std::isfinite(event.Value)) return false;
            joystick->Axes[event.Index] = std::clamp(event.Value,-1.f,1.f);
            return true;
        }
        if (event.Type == InputEventType::JoystickHat) {
            if (event.Index >= joystick->Hats.size() || !std::isfinite(event.Value) || event.Value < 0.f || event.Value > 15.f) return false;
            joystick->Hats[event.Index] = static_cast<std::uint8_t>(event.Value);
            return true;
        }
        return ApplyButton(joystick->Buttons,event,event.Type == InputEventType::JoystickButtonDown);
    }
    case InputEventType::SensorUpdate: {
        const auto sensor = FindDevice(_Input.Sensors,event.DeviceId);
        if (sensor == _Input.Sensors.end() || !sensor->Enabled || !IsFinite({event.SensorValue.X,event.SensorValue.Y}) ||
            !std::isfinite(event.SensorValue.Z)) return false;
        sensor->HasSample = true;
        sensor->Value = event.SensorValue;
        sensor->Timestamp = event.Timestamp;
        return true;
    }
    default: return false;
    }
}

void Runtime::ClearDeviceInputOnFocusLoss() {
    for (auto& gamepad : _Input.Gamepads) {
        for (auto& button : gamepad.Buttons) ReleaseButton(button);
        gamepad.Axes.fill(0.f);
        for (auto& touch : gamepad.Touches) if (IsActive(touch)) CancelContact(touch,gamepad.TouchChanges,0);
    }
    for (auto& joystick : _Input.Joysticks) {
        for (auto& button : joystick.Buttons) ReleaseButton(button);
        std::fill(joystick.Axes.begin(),joystick.Axes.end(),0.f);
        std::fill(joystick.Hats.begin(),joystick.Hats.end(),0);
    }
    for (auto& touch : _Input.Touches) if (IsActive(touch)) CancelContact(touch,_Input.TouchChanges,0);
    for (auto& pen : _Input.Pens) {
        ReleaseButton(pen.Tip);
        pen.InRange = pen.HasPosition = false;
        pen.Delta = {};
        pen.Pressure = 0.f;
        pen.Buttons = 0;
    }
    for (auto& sensor : _Input.Sensors) {
        sensor.Enabled = sensor.HasSample = false;
        sensor.Value = {};
        sensor.Timestamp = 0;
    }
}

void Runtime::CompleteDeviceInputFrame() {
    CompleteContacts(_Input.Touches,_Input.TouchChanges);
    for (auto& gamepad : _Input.Gamepads) {
        ClearEdges(gamepad.Buttons);
        CompleteContacts(gamepad.Touches,gamepad.TouchChanges);
    }
    for (auto& joystick : _Input.Joysticks) ClearEdges(joystick.Buttons);
    std::erase_if(_Input.Pens,[](const auto& pen) { return !pen.InRange; });
    for (auto& pen : _Input.Pens) {
        pen.Tip.Pressed = pen.Tip.Released = false;
        pen.Delta = {};
    }
    _Input.DeviceChanges.clear();
}
}
