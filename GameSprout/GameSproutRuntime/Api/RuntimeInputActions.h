#pragma once

#include "RuntimeInputApi.h"
#include <cmath>
#include <map>
#include <string_view>
#include <utility>

namespace GameSproutRuntime {

enum class InputActionType : std::uint8_t { Button,Axis,Vector2 };
enum class InputActionMouseAxis : std::uint8_t { DeltaX,DeltaY,WheelX,WheelY };
enum class InputActionStick : std::uint8_t { Left,Right };

// Device ID zero matches any connected device in the selected family.
struct InputActionControl {
    enum class Kind : std::uint8_t {
        Key,MouseButton,MouseAxis,GamepadButton,GamepadAxis,JoystickButton,JoystickAxis,JoystickHat,TouchPress,PenTip
    };
    Kind Type = Kind::Key;
    std::uint16_t Index = 0;
    InputDeviceId DeviceId = 0;
    InputJoystickHat Direction = InputJoystickHat::Centered;

    static constexpr InputActionControl Key(InputKey key) { return {Kind::Key,static_cast<std::uint16_t>(key)}; }
    static constexpr InputActionControl MouseButton(InputMouseButton button) { return {Kind::MouseButton,static_cast<std::uint16_t>(button)}; }
    static constexpr InputActionControl MouseAxis(InputActionMouseAxis axis) { return {Kind::MouseAxis,static_cast<std::uint16_t>(axis)}; }
    static constexpr InputActionControl GamepadButton(InputGamepadButton button, InputDeviceId id = 0) {
        return {Kind::GamepadButton,static_cast<std::uint16_t>(button),id};
    }
    static constexpr InputActionControl GamepadAxis(InputGamepadAxis axis, InputDeviceId id = 0) {
        return {Kind::GamepadAxis,static_cast<std::uint16_t>(axis),id};
    }
    static constexpr InputActionControl JoystickButton(std::uint16_t index, InputDeviceId id = 0) {
        return {Kind::JoystickButton,index,id};
    }
    static constexpr InputActionControl JoystickAxis(std::uint16_t index, InputDeviceId id = 0) {
        return {Kind::JoystickAxis,index,id};
    }
    static constexpr InputActionControl JoystickHat(std::uint16_t index, InputJoystickHat direction, InputDeviceId id = 0) {
        return {Kind::JoystickHat,index,id,direction};
    }
    static constexpr InputActionControl TouchPress(InputDeviceId id = 0) { return {Kind::TouchPress,0,id}; }
    static constexpr InputActionControl PenTip(InputDeviceId id = 0) { return {Kind::PenTip,0,id}; }
};

struct InputActionBindingSettings {
    float DeadZone = 0.0f; // Magnitude at or below this cutoff is zero, before scaling.
    float Scale = 1.0f;
};

struct InputActionState {
    InputActionType Type = InputActionType::Button;
    InputButtonState Button{};
    float Value = 0.0f;
    Vector2 Vector{};
};

// A value owned by a script. Evaluate once per gameplay frame before reading actions.
class InputActionMap final {
public:
    bool AddAction(std::string name, InputActionType type) {
        if (name.empty() || type > InputActionType::Vector2) return false;
        auto [action, added] = _Actions.try_emplace(std::move(name));
        if (added) action->second.State.Type = type;
        return added;
    }

    bool Bind(std::string_view name, InputActionControl control, InputActionBindingSettings settings = {}) {
        if (!IsControlValid(control)) return false;
        return AddBinding(name, {BindingKind::Single,{control},settings});
    }

    bool BindAxisComposite(std::string_view name, InputActionControl negative, InputActionControl positive,
                           InputActionBindingSettings settings = {}) {
        if (!IsDigital(negative) || !IsDigital(positive)) return false;
        return AddBinding(name, {BindingKind::AxisComposite,{negative,positive},settings});
    }

    bool BindVector2Composite(std::string_view name, InputActionControl left, InputActionControl right,
                              InputActionControl down, InputActionControl up, InputActionBindingSettings settings = {}) {
        if (!IsDigital(left) || !IsDigital(right) || !IsDigital(down) || !IsDigital(up)) return false;
        return AddBinding(name, {BindingKind::VectorComposite,{left,right,down,up},settings});
    }

    bool BindGamepadStick(std::string_view name, InputActionStick stick, InputDeviceId id = 0,
                          InputActionBindingSettings settings = {}) {
        if (stick > InputActionStick::Right) return false;
        const bool right = stick == InputActionStick::Right;
        return AddBinding(name, {BindingKind::DeviceVector,
            {InputActionControl::GamepadAxis(right ? InputGamepadAxis::RightX : InputGamepadAxis::LeftX,id),
             InputActionControl::GamepadAxis(right ? InputGamepadAxis::RightY : InputGamepadAxis::LeftY,id)},settings});
    }

    bool BindJoystickAxes(std::string_view name, std::uint16_t x, std::uint16_t y, InputDeviceId id = 0,
                          InputActionBindingSettings settings = {}) {
        return AddBinding(name, {BindingKind::DeviceVector,
            {InputActionControl::JoystickAxis(x,id),InputActionControl::JoystickAxis(y,id)},settings});
    }

    bool BindMouseDelta(std::string_view name, InputActionBindingSettings settings = {}) {
        return AddBinding(name, {BindingKind::MouseDelta,{},settings});
    }

    bool BindTouchDelta(std::string_view name, InputDeviceId id = 0, InputActionBindingSettings settings = {}) {
        return AddBinding(name, {BindingKind::TouchDelta,{InputActionControl::TouchPress(id)},settings});
    }

    bool BindPenDelta(std::string_view name, InputDeviceId id = 0, InputActionBindingSettings settings = {}) {
        return AddBinding(name, {BindingKind::PenDelta,{InputActionControl::PenTip(id)},settings});
    }

    bool ClearBindings(std::string_view name) {
        const auto action = _Actions.find(name);
        if (action == _Actions.end()) return false;
        action->second.Bindings.clear();
        action->second.State = {action->second.State.Type};
        return true;
    }

    const InputActionState* GetAction(std::string_view name) const {
        const auto action = _Actions.find(name);
        return action == _Actions.end() ? nullptr : &action->second.State;
    }

    bool IsEnabled() const { return _Enabled; }
    void SetEnabled(bool enabled) {
        if (_Enabled == enabled) return;
        _Enabled = enabled;
        Reset();
    }

    void Reset() {
        _HadFocus = false;
        for (auto& [name,action] : _Actions) action.State = {action.State.Type};
    }

    void Evaluate(const RuntimeInputState& input) {
        if (!_Enabled) return;
        for (auto& [name,action] : _Actions) {
            if (input.Focused) EvaluateAction(input,action);
            else action.State = {action.State.Type,{false,false,action.State.Button.Held}};
        }
        _HadFocus = input.Focused;
    }

private:
    enum class BindingKind : std::uint8_t { Single,AxisComposite,VectorComposite,DeviceVector,MouseDelta,TouchDelta,PenDelta };
    struct Binding {
        BindingKind Kind;
        std::array<InputActionControl,4> Controls;
        InputActionBindingSettings Settings;
    };
    struct Action {
        std::vector<Binding> Bindings;
        InputActionState State;
    };
    struct Sample {
        Vector2 Value;
        bool Pressed = false;
        bool Tap = false;
    };

    bool AddBinding(std::string_view name, Binding binding) {
        if (!std::isfinite(binding.Settings.DeadZone) || binding.Settings.DeadZone < 0.f || !std::isfinite(binding.Settings.Scale))
            return false;
        const auto action = _Actions.find(name);
        if (action == _Actions.end()) return false;
        const auto type = action->second.State.Type;
        const bool isVector = binding.Kind == BindingKind::VectorComposite || binding.Kind == BindingKind::DeviceVector ||
            binding.Kind == BindingKind::MouseDelta || binding.Kind == BindingKind::TouchDelta || binding.Kind == BindingKind::PenDelta;
        if (isVector != (type == InputActionType::Vector2) ||
            (binding.Kind == BindingKind::AxisComposite && type != InputActionType::Axis)) return false;
        action->second.Bindings.push_back(binding);
        return true;
    }

    static bool IsControlValid(InputActionControl control) {
        using Kind = InputActionControl::Kind;
        switch (control.Type) {
        case Kind::Key: return control.Index > 0 && control.Index < static_cast<std::uint16_t>(InputKey::Count);
        case Kind::MouseButton: return control.Index < static_cast<std::uint16_t>(InputMouseButton::Count);
        case Kind::MouseAxis: return control.Index <= static_cast<std::uint16_t>(InputActionMouseAxis::WheelY);
        case Kind::GamepadButton: return control.Index < static_cast<std::uint16_t>(InputGamepadButton::Count);
        case Kind::GamepadAxis: return control.Index < static_cast<std::uint16_t>(InputGamepadAxis::Count);
        case Kind::JoystickButton:
        case Kind::JoystickAxis:
        case Kind::TouchPress:
        case Kind::PenTip: return true;
        case Kind::JoystickHat: return static_cast<std::uint8_t>(control.Direction) <= 15;
        }
        return false;
    }

    static bool IsDigital(InputActionControl control) {
        using Kind = InputActionControl::Kind;
        return control.Type != Kind::MouseAxis && control.Type != Kind::GamepadAxis &&
            control.Type != Kind::JoystickAxis && IsControlValid(control);
    }

    static Sample ReadButton(InputButtonState button) {
        return {{button.Held ? 1.f : 0.f,0.f},button.Pressed,button.Pressed && button.Released};
    }

    static float Strength(Vector2 value) {
        const float magnitude = value.Y == 0.f ? std::abs(value.X) : std::hypot(value.X,value.Y);
        return std::isfinite(magnitude) ? magnitude : 0.f;
    }

    static Vector2 ClampUnit(Vector2 value) {
        const float magnitude = Strength(value);
        return magnitude > 1.f ? value * (1.f / magnitude) : value;
    }

    static Sample ReadDevices(const auto& devices, InputDeviceId id, const auto& read) {
        Sample sample;
        float strength = 0.f;
        for (const auto& device : devices) {
            if (id && device.Id != id) continue;
            const Sample candidate = read(device);
            const float magnitude = Strength(candidate.Value);
            if (magnitude > strength) {
                strength = magnitude;
                sample.Value = candidate.Value;
            }
            sample.Pressed |= candidate.Pressed;
            sample.Tap |= candidate.Tap;
            if (id) break;
        }
        return sample;
    }

    static float JoystickAxis(const InputJoystickState& device, std::uint16_t index) {
        return index < device.Axes.size() ? device.Axes[index] : 0.f;
    }

    static Sample ReadTouchPress(const RuntimeInputState& input, InputDeviceId id) {
        const auto matches = [id](const InputTouchState& contact) { return !id || contact.DeviceId == id; };
        const bool held = std::any_of(input.Touches.begin(),input.Touches.end(),[&](const auto& contact) {
            return matches(contact) && contact.Phase != InputTouchPhase::Ended && contact.Phase != InputTouchPhase::Canceled;
        });
        Sample sample{{held ? 1.f : 0.f,0.f}};
        for (auto began = input.TouchChanges.begin(); began != input.TouchChanges.end(); ++began) {
            if (!matches(*began) || began->Phase != InputTouchPhase::Began) continue;
            sample.Pressed = true;
            if (held) break;
            sample.Tap = std::any_of(began + 1,input.TouchChanges.end(),[&](const auto& change) {
                return change.Phase == InputTouchPhase::Ended && change.DeviceId == began->DeviceId &&
                    change.ContactId == began->ContactId && change.Surface == began->Surface;
            });
            if (sample.Tap) break;
        }
        return sample;
    }

    static Sample ReadTouchDelta(const RuntimeInputState& input, InputDeviceId id) {
        Sample sample;
        float strongest = 0.f;
        for (const auto& contact : input.Touches) {
            if ((id && contact.DeviceId != id) || contact.Phase == InputTouchPhase::Canceled) continue;
            const float magnitude = Strength(contact.Delta);
            if (magnitude > strongest) {
                sample.Value = contact.Delta;
                strongest = magnitude;
            }
        }
        return sample;
    }

    static Sample ReadControl(const RuntimeInputState& input, InputActionControl control) {
        using Kind = InputActionControl::Kind;
        const auto index = control.Index;
        switch (control.Type) {
        case Kind::Key: return ReadButton(input.GetKey(static_cast<InputKey>(index)));
        case Kind::MouseButton: return ReadButton(input.GetMouseButton(static_cast<InputMouseButton>(index)));
        case Kind::MouseAxis: {
            const std::array values{input.MouseDelta.X,input.MouseDelta.Y,input.WheelDelta.X,input.WheelDelta.Y};
            return {{values[index],0.f}};
        }
        case Kind::GamepadButton:
            return ReadDevices(input.Gamepads,control.DeviceId,[&](const auto& device) {
                return ReadButton(device.GetButton(static_cast<InputGamepadButton>(index)));
            });
        case Kind::GamepadAxis:
            return ReadDevices(input.Gamepads,control.DeviceId,[&](const auto& device) {
                return Sample{{device.GetAxis(static_cast<InputGamepadAxis>(index)),0.f}};
            });
        case Kind::JoystickButton:
            return ReadDevices(input.Joysticks,control.DeviceId,[&](const auto& device) {
                return index < device.Buttons.size() ? ReadButton(device.Buttons[index]) : Sample{};
            });
        case Kind::JoystickAxis:
            return ReadDevices(input.Joysticks,control.DeviceId,[&](const auto& device) {
                return Sample{{JoystickAxis(device,index),0.f}};
            });
        case Kind::JoystickHat: {
            const auto direction = static_cast<std::uint8_t>(control.Direction);
            return ReadDevices(input.Joysticks,control.DeviceId,[&](const auto& device) -> Sample {
                if (index >= device.Hats.size()) return {};
                const auto hat = device.Hats[index];
                const bool held = direction ? (hat & direction) == direction : hat == 0;
                return {{held ? 1.f : 0.f,0.f}};
            });
        }
        case Kind::TouchPress: return ReadTouchPress(input,control.DeviceId);
        case Kind::PenTip:
            return ReadDevices(input.Pens,control.DeviceId,[](const auto& pen) { return ReadButton(pen.Tip); });
        }
        return {};
    }

    static Sample ReadBinding(const RuntimeInputState& input, const Binding& binding) {
        const auto& controls = binding.Controls;
        switch (binding.Kind) {
        case BindingKind::Single: return ReadControl(input,controls[0]);
        case BindingKind::AxisComposite:
        case BindingKind::VectorComposite: {
            const auto negative = ReadControl(input,controls[0]);
            const auto positive = ReadControl(input,controls[1]);
            Sample sample{{positive.Value.X - negative.Value.X,0.f},positive.Pressed || negative.Pressed};
            if (binding.Kind == BindingKind::VectorComposite) {
                const auto down = ReadControl(input,controls[2]);
                const auto up = ReadControl(input,controls[3]);
                sample.Value.Y = up.Value.X - down.Value.X;
                sample.Value = ClampUnit(sample.Value);
                sample.Pressed |= up.Pressed || down.Pressed;
            }
            return sample;
        }
        case BindingKind::DeviceVector:
            if (controls[0].Type == InputActionControl::Kind::GamepadAxis) {
                return ReadDevices(input.Gamepads,controls[0].DeviceId,[&](const auto& device) {
                    return Sample{ClampUnit({device.GetAxis(static_cast<InputGamepadAxis>(controls[0].Index)),
                        device.GetAxis(static_cast<InputGamepadAxis>(controls[1].Index))})};
                });
            }
            return ReadDevices(input.Joysticks,controls[0].DeviceId,[&](const auto& device) {
                if (controls[0].Index >= device.Axes.size() || controls[1].Index >= device.Axes.size()) return Sample{};
                return Sample{ClampUnit({JoystickAxis(device,controls[0].Index),JoystickAxis(device,controls[1].Index)})};
            });
        case BindingKind::MouseDelta: return {input.MouseDelta};
        case BindingKind::TouchDelta: return ReadTouchDelta(input,controls[0].DeviceId);
        case BindingKind::PenDelta:
            return ReadDevices(input.Pens,controls[0].DeviceId,[](const auto& pen) { return Sample{pen.Delta}; });
        }
        return {};
    }

    void EvaluateAction(const RuntimeInputState& input, Action& action) const {
        const auto type = action.State.Type;
        const bool wasHeld = action.State.Button.Held;
        bool pressed = false;
        bool tapped = false;
        Vector2 value;
        float strength = 0.f;
        for (const auto& binding : action.Bindings) {
            if (binding.Settings.Scale == 0.f) continue;
            const Sample sample = ReadBinding(input,binding);
            tapped |= sample.Tap && binding.Settings.DeadZone < 1.f;
            if (Strength(sample.Value) <= binding.Settings.DeadZone) continue;
            const Vector2 scaled = sample.Value * binding.Settings.Scale;
            const float magnitude = Strength(scaled);
            pressed |= magnitude > 0.f && sample.Pressed;
            if (magnitude > strength) {
                strength = magnitude;
                value = scaled;
            }
        }
        const bool held = strength > 0.f;
        const bool tap = type == InputActionType::Button && !wasHeld && !held && tapped;
        action.State = {type,{held,tap || (held && !wasHeld && (_HadFocus || pressed)),tap || (!held && wasHeld)}};
        if (type == InputActionType::Vector2) action.State.Vector = value;
        else action.State.Value = type == InputActionType::Button ? (held ? 1.f : 0.f) : value.X;
    }

    std::map<std::string,Action,std::less<>> _Actions;
    bool _Enabled = true;
    bool _HadFocus = false;
};

}
