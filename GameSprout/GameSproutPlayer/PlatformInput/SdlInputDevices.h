#pragma once

#include "RuntimeInputApi.h"
#include <SDL3/SDL.h>
#include <map>
#include <optional>
#include <string>

namespace GameSproutRuntime { class IGameSproutRuntime; }

namespace GameSproutPlatform {

// Owns the SDL controller and sensor subsystems independently of the host's
// video/event loop. All calls, including output commands, are host-thread only.
class SdlInputDevices final {
public:
    SdlInputDevices() = default;
    ~SdlInputDevices();
    SdlInputDevices(const SdlInputDevices&) = delete;
    SdlInputDevices& operator=(const SdlInputDevices&) = delete;

    bool Initialize(bool externalWindowFocus = false);
    void Shutdown();
    void BeginFrame(GameSproutRuntime::IGameSproutRuntime&);
    bool HandleEvent(GameSproutRuntime::IGameSproutRuntime&, const SDL_Event&);
    void PumpDeviceEvents(GameSproutRuntime::IGameSproutRuntime&);
    static bool Output(void*, const GameSproutRuntime::InputOutputCommand&);

private:
    struct Controller {
        SDL_Gamepad* Gamepad = nullptr;
        SDL_Joystick* Joystick = nullptr;
        std::uint16_t AxisCount = 0;
        std::uint16_t ButtonCount = 0;
        std::uint16_t HatCount = 0;
        bool RumbleActive = false;
        bool TriggerRumbleActive = false;
    };
    struct Sensor {
        SDL_SensorID NativeId = 0;
        SDL_SensorType Kind = SDL_SENSOR_UNKNOWN;
        SDL_Sensor* Handle = nullptr;
        SDL_Gamepad* Gamepad = nullptr;
        bool Enabled = false;
    };

    void OpenController(SDL_JoystickID, GameSproutRuntime::IGameSproutRuntime*);
    void CloseController(SDL_JoystickID, GameSproutRuntime::IGameSproutRuntime*);
    void PublishController(GameSproutRuntime::IGameSproutRuntime&, SDL_JoystickID, const Controller&);
    void PublishSensor(GameSproutRuntime::IGameSproutRuntime&, std::uint64_t, const Sensor&);
    bool SetSensorEnabled(Sensor&, bool);
    bool ApplyOutput(const GameSproutRuntime::InputOutputCommand&);
    void ResetOutputs();

    std::map<SDL_JoystickID, Controller> _Controllers;
    std::map<std::uint64_t, Sensor> _Sensors;
    SDL_InitFlags _Subsystems = 0;
    std::optional<std::string> _OldBackgroundHint;
    bool _ChangedBackgroundHint = false;
    bool _Focused = false;
    bool _Published = false;
};

}
