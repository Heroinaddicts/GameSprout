#include "SdlInputDevices.h"
#include "GameSproutRuntimeApi.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GameSproutPlatform {
using namespace GameSproutRuntime;
namespace {
constexpr SDL_SensorType GamepadSensors[]{SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO, SDL_SENSOR_ACCEL_L,
    SDL_SENSOR_GYRO_L, SDL_SENSOR_ACCEL_R, SDL_SENSOR_GYRO_R};
static_assert(static_cast<int>(InputGamepadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH &&
    static_cast<int>(InputGamepadButton::Misc6) == SDL_GAMEPAD_BUTTON_MISC6);
static_assert(static_cast<int>(InputGamepadAxis::LeftX) == SDL_GAMEPAD_AXIS_LEFTX &&
    static_cast<int>(InputGamepadAxis::RightTrigger) == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

std::uint64_t GamepadSensorId(SDL_JoystickID gamepad, SDL_SensorType kind) {
    return (1ULL << 63) | (static_cast<std::uint64_t>(gamepad) << 8) | static_cast<unsigned>(kind);
}

InputSensorKind SensorKind(SDL_SensorType kind) {
    switch (kind) {
    case SDL_SENSOR_ACCEL: return InputSensorKind::Accelerometer;
    case SDL_SENSOR_GYRO: return InputSensorKind::Gyroscope;
    case SDL_SENSOR_ACCEL_L: return InputSensorKind::AccelerometerLeft;
    case SDL_SENSOR_GYRO_L: return InputSensorKind::GyroscopeLeft;
    case SDL_SENSOR_ACCEL_R: return InputSensorKind::AccelerometerRight;
    case SDL_SENSOR_GYRO_R: return InputSensorKind::GyroscopeRight;
    default: return InputSensorKind::Unknown;
    }
}

std::uint32_t Capabilities(SDL_Joystick* joystick) {
    const auto properties = SDL_GetJoystickProperties(joystick);
    std::uint32_t result = 0;
    if (SDL_GetBooleanProperty(properties, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false))
        result |= static_cast<std::uint32_t>(InputDeviceCapability::Rumble);
    if (SDL_GetBooleanProperty(properties, SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN, false))
        result |= static_cast<std::uint32_t>(InputDeviceCapability::TriggerRumble);
    if (SDL_GetBooleanProperty(properties, SDL_PROP_JOYSTICK_CAP_RGB_LED_BOOLEAN, false) ||
        SDL_GetBooleanProperty(properties, SDL_PROP_JOYSTICK_CAP_MONO_LED_BOOLEAN, false))
        result |= static_cast<std::uint32_t>(InputDeviceCapability::Led);
    return result;
}

RuntimeInputEvent Event(InputEventType type, std::uint64_t device, std::uint64_t timestamp) {
    RuntimeInputEvent result;
    result.Type = type;
    result.DeviceId = device;
    result.Timestamp = timestamp;
    return result;
}

float AxisValue(Sint16 value) { return value < 0 ? value / 32768.0f : value / 32767.0f; }

float GamepadAxisValue(int axis, Sint16 value) {
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
        return std::max(0.0f, AxisValue(value));
    const float result = AxisValue(value);
    return axis == SDL_GAMEPAD_AXIS_LEFTY || axis == SDL_GAMEPAD_AXIS_RIGHTY ? -result : result;
}

std::uint16_t ControlCount(int count) {
    // SDL joystick events carry axes/buttons/hats as an unsigned byte.
    return static_cast<std::uint16_t>(std::clamp(count, 0, 256));
}

Uint16 MotorSpeed(float speed) {
    return static_cast<Uint16>(std::lround(std::clamp(speed, 0.0f, 1.0f) * std::numeric_limits<Uint16>::max()));
}
}

SdlInputDevices::~SdlInputDevices() { Shutdown(); }

bool SdlInputDevices::Initialize(bool externalWindowFocus) {
    if (_Subsystems) return true;
    if (externalWindowFocus) {
        // Host focus gates gameplay. SDL cannot see Qt's actual keyboard focus
        // when GRHI wraps a Qt native surface in an SDL_Window.
        if (const auto* value = SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS)) _OldBackgroundHint = value;
        _ChangedBackgroundHint = SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        _Subsystems |= SDL_INIT_GAMEPAD;
        int count = 0;
        auto* ids = SDL_GetJoysticks(&count);
        for (int index = 0; ids && index < count; ++index) OpenController(ids[index], nullptr);
        SDL_free(ids);
    }
    if (SDL_InitSubSystem(SDL_INIT_SENSOR)) {
        _Subsystems |= SDL_INIT_SENSOR;
        int count = 0;
        auto* ids = SDL_GetSensors(&count);
        for (int index = 0; ids && index < count; ++index) {
            const auto id = ids[index];
            _Sensors.emplace(id, Sensor{id, SDL_GetSensorTypeForID(id)});
        }
        SDL_free(ids);
    }
    if (_Subsystems) return true;
    Shutdown();
    return false;
}

void SdlInputDevices::Shutdown() {
    ResetOutputs();
    while (!_Controllers.empty()) CloseController(_Controllers.begin()->first, nullptr);
    for (auto& [id, sensor] : _Sensors) if (sensor.Handle) SDL_CloseSensor(sensor.Handle);
    _Sensors.clear();
    if (_Subsystems) SDL_QuitSubSystem(_Subsystems);
    _Subsystems = 0;
    if (_ChangedBackgroundHint) {
        if (_OldBackgroundHint) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, _OldBackgroundHint->c_str());
        else SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    }
    _OldBackgroundHint.reset();
    _ChangedBackgroundHint = false;
    _Focused = false;
    _Published = false;
}

void SdlInputDevices::OpenController(SDL_JoystickID id, IGameSproutRuntime* runtime) {
    if (!id) return;
    const bool mapped = SDL_IsGamepad(id);
    if (const auto it = _Controllers.find(id); it != _Controllers.end()) {
        if (static_cast<bool>(it->second.Gamepad) == mapped) return;
        CloseController(id, runtime);
    }

    Controller controller;
    if (mapped) {
        controller.Gamepad = SDL_OpenGamepad(id);
        if (controller.Gamepad) controller.Joystick = SDL_GetGamepadJoystick(controller.Gamepad);
    } else controller.Joystick = SDL_OpenJoystick(id);
    if (!controller.Joystick) {
        if (controller.Gamepad) SDL_CloseGamepad(controller.Gamepad);
        return;
    }
    controller.AxisCount = ControlCount(SDL_GetNumJoystickAxes(controller.Joystick));
    controller.ButtonCount = ControlCount(SDL_GetNumJoystickButtons(controller.Joystick));
    controller.HatCount = ControlCount(SDL_GetNumJoystickHats(controller.Joystick));
    _Controllers.emplace(id, controller);
    if (runtime) PublishController(*runtime, id, controller);
    if (!controller.Gamepad) return;
    for (const auto kind : GamepadSensors) {
        if (!SDL_GamepadHasSensor(controller.Gamepad, kind)) continue;
        const auto sensorId = GamepadSensorId(id, kind);
        const Sensor sensor{0, kind, nullptr, controller.Gamepad};
        _Sensors.emplace(sensorId, sensor);
        if (runtime) PublishSensor(*runtime, sensorId, sensor);
    }
}

void SdlInputDevices::CloseController(SDL_JoystickID id, IGameSproutRuntime* runtime) {
    const auto it = _Controllers.find(id);
    if (it == _Controllers.end()) return;
    const auto controller = it->second;
    if (runtime) runtime->SubmitInputEvent(Event(controller.Gamepad ? InputEventType::GamepadRemoved :
        InputEventType::JoystickRemoved, id, SDL_GetTicksNS()));
    if (controller.Gamepad) {
        for (const auto kind : GamepadSensors) {
            const auto sensorId = GamepadSensorId(id, kind);
            if (_Sensors.erase(sensorId) && runtime)
                runtime->SubmitInputEvent(Event(InputEventType::SensorRemoved, sensorId, SDL_GetTicksNS()));
        }
        SDL_CloseGamepad(controller.Gamepad);
    } else SDL_CloseJoystick(controller.Joystick);
    _Controllers.erase(it);
}

void SdlInputDevices::PublishController(IGameSproutRuntime& runtime, SDL_JoystickID id, const Controller& controller) {
    const auto timestamp = SDL_GetTicksNS();
    const auto pad = controller.Gamepad;
    const auto joystick = controller.Joystick;
    auto added = Event(pad ? InputEventType::GamepadAdded : InputEventType::JoystickAdded, id, timestamp);
    added.Capabilities = Capabilities(joystick);
    if (const auto* name = pad ? SDL_GetGamepadName(pad) : SDL_GetJoystickName(joystick)) added.Text = name;
    added.AxisCount = pad ? static_cast<std::uint16_t>(InputGamepadAxis::Count) : controller.AxisCount;
    added.ButtonCount = pad ? static_cast<std::uint16_t>(InputGamepadButton::Count) : controller.ButtonCount;
    added.HatCount = pad ? 0 : controller.HatCount;
    runtime.SubmitInputEvent(added);
    if (!_Focused) return;

    auto axis = Event(pad ? InputEventType::GamepadAxis : InputEventType::JoystickAxis, id, timestamp);
    axis.IsStateSync = true;
    for (std::uint16_t index = 0; index < added.AxisCount; ++index) {
        axis.Index = index;
        axis.Value = pad ? GamepadAxisValue(index, SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(index))) :
            AxisValue(SDL_GetJoystickAxis(joystick, index));
        runtime.SubmitInputEvent(axis);
    }
    auto button = Event(InputEventType::GamepadButtonDown, id, timestamp);
    button.IsStateSync = true;
    for (std::uint16_t index = 0; index < added.ButtonCount; ++index) {
        button.Index = index;
        if (pad) button.Type = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(index)) ? InputEventType::GamepadButtonDown : InputEventType::GamepadButtonUp;
        else button.Type = SDL_GetJoystickButton(joystick, index) ? InputEventType::JoystickButtonDown : InputEventType::JoystickButtonUp;
        runtime.SubmitInputEvent(button);
    }
    auto hat = Event(InputEventType::JoystickHat, id, timestamp);
    hat.IsStateSync = true;
    for (std::uint16_t index = 0; index < added.HatCount; ++index) {
        hat.Index = index;
        hat.Value = SDL_GetJoystickHat(joystick, index);
        runtime.SubmitInputEvent(hat);
    }
    if (!pad) return;
    const int surfaces = SDL_GetNumGamepadTouchpads(pad);
    for (int surface = 0; surface < surfaces && surface <= std::numeric_limits<std::uint16_t>::max(); ++surface) {
        const int fingers = SDL_GetNumGamepadTouchpadFingers(pad, surface);
        for (int finger = 0; finger < fingers; ++finger) {
            auto touch = Event(InputEventType::GamepadTouchBegan, id, timestamp);
            bool down = false;
            if (!SDL_GetGamepadTouchpadFinger(pad, surface, finger, &down, &touch.X, &touch.Y, &touch.Pressure) || !down) continue;
            touch.Index = static_cast<std::uint16_t>(surface);
            touch.ContactId = static_cast<std::uint64_t>(finger) + 1;
            touch.IsStateSync = true;
            runtime.SubmitInputEvent(touch);
        }
    }
}

void SdlInputDevices::PublishSensor(IGameSproutRuntime& runtime, std::uint64_t id, const Sensor& sensor) {
    auto event = Event(InputEventType::SensorAdded, id, SDL_GetTicksNS());
    event.Sensor = SensorKind(sensor.Kind);
    if (const auto* name = sensor.Gamepad ? SDL_GetGamepadName(sensor.Gamepad) : SDL_GetSensorNameForID(sensor.NativeId))
        event.Text = name;
    runtime.SubmitInputEvent(event);
}

void SdlInputDevices::BeginFrame(IGameSproutRuntime& runtime) {
    const bool focused = runtime.GetInputState().Focused;
    const bool gainedFocus = !_Focused && focused;
    _Focused = focused;
    if (_Published && !gainedFocus) return;
    _Published = true;
    for (const auto& [id, controller] : _Controllers) PublishController(runtime, id, controller);
    for (const auto& [id, sensor] : _Sensors) PublishSensor(runtime, id, sensor);
}

bool SdlInputDevices::HandleEvent(IGameSproutRuntime& runtime, const SDL_Event& event) {
    RuntimeInputEvent input;
    input.Timestamp = event.common.timestamp;
    switch (event.type) {
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_GAMEPAD_ADDED:
        OpenController(event.type == SDL_EVENT_JOYSTICK_ADDED ? event.jdevice.which : event.gdevice.which, &runtime);
        return true;
    case SDL_EVENT_JOYSTICK_REMOVED:
        CloseController(event.jdevice.which, &runtime);
        return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
        CloseController(event.gdevice.which, &runtime);
        return true;
    case SDL_EVENT_GAMEPAD_REMAPPED: {
        OpenController(event.gdevice.which, &runtime);
        const auto it = _Controllers.find(event.gdevice.which);
        if (it != _Controllers.end()) PublishController(runtime, it->first, it->second);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (event.gaxis.axis >= static_cast<unsigned>(InputGamepadAxis::Count)) return true;
        input.Type = InputEventType::GamepadAxis;
        input.DeviceId = event.gaxis.which;
        input.Index = event.gaxis.axis;
        input.Value = GamepadAxisValue(input.Index, event.gaxis.value);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (event.gbutton.button >= static_cast<unsigned>(InputGamepadButton::Count)) return true;
        input.Type = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? InputEventType::GamepadButtonDown : InputEventType::GamepadButtonUp;
        input.DeviceId = event.gbutton.which;
        input.Index = event.gbutton.button;
        break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        input.Type = InputEventType::JoystickAxis;
        input.DeviceId = event.jaxis.which;
        input.Index = event.jaxis.axis;
        input.Value = AxisValue(event.jaxis.value);
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        input.Type = event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ? InputEventType::JoystickButtonDown : InputEventType::JoystickButtonUp;
        input.DeviceId = event.jbutton.which;
        input.Index = event.jbutton.button;
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        input.Type = InputEventType::JoystickHat;
        input.DeviceId = event.jhat.which;
        input.Index = event.jhat.hat;
        input.Value = event.jhat.value;
        break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        if (event.gtouchpad.finger < 0 || event.gtouchpad.touchpad < 0 || event.gtouchpad.touchpad > 65535) return true;
        input.Type = event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN ? InputEventType::GamepadTouchBegan :
            event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP ? InputEventType::GamepadTouchEnded : InputEventType::GamepadTouchMoved;
        input.DeviceId = event.gtouchpad.which;
        input.ContactId = static_cast<std::uint64_t>(event.gtouchpad.finger) + 1;
        input.Index = static_cast<std::uint16_t>(event.gtouchpad.touchpad);
        input.X = event.gtouchpad.x;
        input.Y = event.gtouchpad.y;
        input.Pressure = event.gtouchpad.pressure;
        break;
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        input.Type = InputEventType::SensorUpdate;
        input.DeviceId = GamepadSensorId(event.gsensor.which, static_cast<SDL_SensorType>(event.gsensor.sensor));
        input.Sensor = SensorKind(static_cast<SDL_SensorType>(event.gsensor.sensor));
        input.SensorValue = {event.gsensor.data[0], event.gsensor.data[1], event.gsensor.data[2]};
        break;
    case SDL_EVENT_SENSOR_UPDATE: {
        const auto sensor = _Sensors.find(event.sensor.which);
        if (sensor == _Sensors.end() || !sensor->second.Handle) return true;
        input.Type = InputEventType::SensorUpdate;
        input.DeviceId = event.sensor.which;
        input.Sensor = SensorKind(sensor->second.Kind);
        input.SensorValue = {event.sensor.data[0], event.sensor.data[1], event.sensor.data[2]};
        break;
    }
    default:
        return false;
    }

    if (input.Type == InputEventType::JoystickAxis || input.Type == InputEventType::JoystickButtonDown ||
        input.Type == InputEventType::JoystickButtonUp || input.Type == InputEventType::JoystickHat) {
        const auto it = _Controllers.find(static_cast<SDL_JoystickID>(input.DeviceId));
        if (it == _Controllers.end() || it->second.Gamepad) return true;
    }
    runtime.SubmitInputEvent(input);
    return true;
}

void SdlInputDevices::PumpDeviceEvents(IGameSproutRuntime& runtime) {
    if (!_Subsystems) return;
    // Qt owns general window and OS input dispatch in the Editor. Refresh just
    // SDL's device subsystems and remove only their events from the SDL queue.
    BeginFrame(runtime);
    if (_Subsystems & SDL_INIT_GAMEPAD) SDL_UpdateJoysticks();
    if (_Subsystems & SDL_INIT_SENSOR) SDL_UpdateSensors();
    SDL_Event events[64];
    const auto read = [&](Uint32 first, Uint32 last) {
        int count = 0;
        do {
            count = SDL_PeepEvents(events, 64, SDL_GETEVENT, first, last);
            for (int index = 0; index < count; ++index) HandleEvent(runtime, events[index]);
        } while (count == 64);
    };
    read(SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED);
    read(SDL_EVENT_SENSOR_UPDATE, SDL_EVENT_SENSOR_UPDATE);
}

bool SdlInputDevices::SetSensorEnabled(Sensor& sensor, bool enabled) {
    if (sensor.Enabled == enabled) return true;
    if (sensor.Gamepad) {
        if (!SDL_SetGamepadSensorEnabled(sensor.Gamepad, sensor.Kind, enabled)) return false;
    } else if (enabled) {
        sensor.Handle = SDL_OpenSensor(sensor.NativeId);
        if (!sensor.Handle) return false;
    } else {
        SDL_CloseSensor(sensor.Handle);
        sensor.Handle = nullptr;
    }
    sensor.Enabled = enabled;
    return true;
}

bool SdlInputDevices::Output(void* context, const InputOutputCommand& command) {
    return context && static_cast<SdlInputDevices*>(context)->ApplyOutput(command);
}

bool SdlInputDevices::ApplyOutput(const InputOutputCommand& command) {
    if (command.Type == InputOutputType::Reset) {
        ResetOutputs();
        _Focused = false;
        _Published = false;
        return true;
    }
    if (command.Type == InputOutputType::EnableSensor) {
        const auto sensor = _Sensors.find(command.DeviceId);
        return sensor != _Sensors.end() && SetSensorEnabled(sensor->second, command.Enabled);
    }
    if (command.DeviceId > std::numeric_limits<SDL_JoystickID>::max()) return false;
    const auto it = _Controllers.find(static_cast<SDL_JoystickID>(command.DeviceId));
    if (it == _Controllers.end()) return false;
    auto& device = it->second;
    switch (command.Type) {
    case InputOutputType::Rumble:
    case InputOutputType::RumbleTriggers: {
        if (!std::isfinite(command.Low) || !std::isfinite(command.High)) return false;
        const auto low = MotorSpeed(command.Low), high = MotorSpeed(command.High);
        if (command.Type == InputOutputType::Rumble) {
            if (!SDL_RumbleJoystick(device.Joystick, low, high, command.DurationMs)) return false;
            device.RumbleActive = (low || high) && command.DurationMs;
        } else {
            if (!SDL_RumbleJoystickTriggers(device.Joystick, low, high, command.DurationMs)) return false;
            device.TriggerRumbleActive = (low || high) && command.DurationMs;
        }
        return true;
    }
    case InputOutputType::SetLed:
        return SDL_SetJoystickLED(device.Joystick, command.Red, command.Green, command.Blue);
    default:
        return false;
    }
}

void SdlInputDevices::ResetOutputs() {
    for (auto& [id, controller] : _Controllers) {
        if (controller.RumbleActive) SDL_RumbleJoystick(controller.Joystick, 0, 0, 0);
        if (controller.TriggerRumbleActive) SDL_RumbleJoystickTriggers(controller.Joystick, 0, 0, 0);
        controller.RumbleActive = controller.TriggerRumbleActive = false;
    }
    for (auto& [id, sensor] : _Sensors) SetSensorEnabled(sensor, false);
}

}
