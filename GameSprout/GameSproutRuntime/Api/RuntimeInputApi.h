#pragma once
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Rect.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace GameSproutRuntime {
// Gameplay key identifiers independent of the host. Layout-aware text is delivered separately.
enum class InputKey : std::uint16_t {
    None,A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    Digit0,Digit1,Digit2,Digit3,Digit4,Digit5,Digit6,Digit7,Digit8,Digit9,
    Escape,Enter,Space,Tab,Backspace,Delete,Insert,Home,End,PageUp,PageDown,
    Left,Right,Up,Down,Shift,Control,Alt,Super,CapsLock,
    F1,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,F13,F14,F15,F16,F17,F18,F19,F20,F21,F22,F23,F24,
    Minus,Equal,LeftBracket,RightBracket,Backslash,Semicolon,Apostrophe,Grave,Comma,Period,Slash,
    PrintScreen,ScrollLock,Pause,NumLock,Menu,
    Keypad0,Keypad1,Keypad2,Keypad3,Keypad4,Keypad5,Keypad6,Keypad7,Keypad8,Keypad9,
    KeypadDecimal,KeypadDivide,KeypadMultiply,KeypadSubtract,KeypadAdd,KeypadEnter,KeypadEqual,
    LeftShift,RightShift,LeftControl,RightControl,LeftAlt,RightAlt,LeftSuper,RightSuper,Count
};
enum class InputMouseButton : std::uint8_t { Left,Right,Middle,X1,X2,Count };
using InputDeviceId = std::uint64_t; // Session-local, nonzero. Device families have independent ID spaces.
using InputContactId = std::uint64_t; // Nonzero within a device and touch surface.
enum class InputPointerKind : std::uint8_t { Mouse,Touch,Pen };
enum class InputPointerPhase : std::uint8_t { Down,Move,Up,Cancel };
struct RuntimePointerId {
    InputPointerKind Kind=InputPointerKind::Mouse;
    InputDeviceId Device=0;
    InputContactId Contact=0;
    bool operator==(const RuntimePointerId&) const = default;
};
struct RuntimePointerEvent {
    RuntimePointerId Pointer;
    InputPointerPhase Phase=InputPointerPhase::Cancel;
    Vector2 Position,Delta; // Host-local logical pixels, X right, Y down.
    std::uint64_t Timestamp=0;
};

enum class InputViewportOrientation : std::uint8_t { Unknown,Landscape,LandscapeFlipped,Portrait,PortraitFlipped };
struct RuntimeInputViewportMetrics {
    Rect ContentRect; // Game image in host-local logical coordinates, excluding letterbox bars.
    Vector2 RenderSize; // Physical pixels used to render the content.
    Rect SafeArea; // In host-local logical coordinates. Empty when platform information is unavailable.
    float PixelRatio=1.0f; // Host device pixel density. Use ContentRect and RenderSize for conversions.
    float DisplayDpi=0.0f; // Physical DPI or zero when unknown.
    InputViewportOrientation Orientation=InputViewportOrientation::Unknown;
    std::uint64_t Revision=0; // Assigned by Runtime; changes with the effective viewport or its scene.
};
class IRuntimeInputViewportListener {
public:
    virtual ~IRuntimeInputViewportListener() = default;
    virtual void OnInputViewportChanged(const RuntimeInputViewportMetrics&) = 0;
};
enum class InputGamepadButton : std::uint8_t {
    South,East,West,North,Back,Guide,Start,LeftStick,RightStick,LeftShoulder,RightShoulder,
    DpadUp,DpadDown,DpadLeft,DpadRight,Misc1,RightPaddle1,LeftPaddle1,RightPaddle2,LeftPaddle2,
    Touchpad,Misc2,Misc3,Misc4,Misc5,Misc6,Count
};
enum class InputGamepadAxis : std::uint8_t { LeftX,LeftY,RightX,RightY,LeftTrigger,RightTrigger,Count };
enum class InputDeviceCapability : std::uint32_t { Rumble=1,TriggerRumble=2,Led=4 };
enum class InputJoystickHat : std::uint8_t { Centered=0,Up=1,Right=2,Down=4,Left=8 };
enum class InputPenButton : std::uint32_t { Tip=1,Eraser=2,Barrel1=4,Barrel2=8,Barrel3=16 };
enum class InputSensorKind : std::uint8_t {
    Unknown,Accelerometer,Gyroscope,AccelerometerLeft,GyroscopeLeft,AccelerometerRight,GyroscopeRight
};
enum class InputTouchPhase : std::uint8_t { Began,Moved,Stationary,Ended,Canceled };
enum class InputCursorMode : std::uint8_t { Normal,Hidden,Locked,Confined };
enum class InputEventType : std::uint8_t {
    FocusGained,FocusLost,KeyDown,KeyUp,MouseMove,MouseButtonDown,MouseButtonUp,MouseWheel,MouseRelativeMove,
    TouchBegan,TouchMoved,TouchEnded,TouchCanceled,
    PenProximityIn,PenProximityOut,PenMove,PenDown,PenUp,TextInput,TextComposition,
    GamepadAdded,GamepadRemoved,GamepadAxis,GamepadButtonDown,GamepadButtonUp,GamepadTouchBegan,GamepadTouchMoved,GamepadTouchEnded,
    JoystickAdded,JoystickRemoved,JoystickAxis,JoystickButtonDown,JoystickButtonUp,JoystickHat,
    SensorAdded,SensorRemoved,SensorUpdate
};
struct RuntimeInputEvent {
    InputEventType Type=InputEventType::FocusLost;
    InputKey Key=InputKey::None;
    InputMouseButton Button=InputMouseButton::Left;
    // Window-local logical pixels: top-left origin, X right, Y down.
    float X=0,Y=0;
    // Wheel steps; one conventional wheel detent is 1, positive Y scrolls up.
    float WheelX=0,WheelY=0;
    InputDeviceId DeviceId=0;
    InputContactId ContactId=0;
    std::uint64_t Timestamp=0; // Host monotonic time in nanoseconds; zero when unavailable.
    std::uint16_t Index=0,AxisCount=0,ButtonCount=0,HatCount=0;
    std::uint32_t Capabilities=0; // Device capability mask, or pen button mask for pen events.
    float DeltaX=0,DeltaY=0,Value=0,Pressure=0,TiltX=0,TiltY=0,Twist=0;
    bool HasRelativeDelta=false;
    bool IsStateSync=false; // Restore Held state without synthesizing button edges on focus return.
    Vector3 SensorValue;
    InputSensorKind Sensor=InputSensorKind::Unknown;
    std::string Text; // UTF-8 text, or a device name on discovery.
    std::int32_t TextStart=0,TextLength=0; // Composition offsets in Unicode code points.
};
struct InputButtonState { bool Held=false,Pressed=false,Released=false; };

struct InputTouchState {
    InputDeviceId DeviceId=0;
    InputContactId ContactId=0;
    std::uint16_t Surface=0; // Gamepad touchpad index, zero for a screen.
    std::uint64_t Timestamp=0;
    InputTouchPhase Phase=InputTouchPhase::Stationary;
    Vector2 Position,Delta; // Screen: logical pixels top-left. Gamepad touchpad: normalized top-left coordinates.
    float Pressure=0;
};

struct InputGamepadState {
    InputDeviceId Id=0;
    std::string Name;
    std::uint32_t Capabilities=0;
    std::array<InputButtonState,static_cast<std::size_t>(InputGamepadButton::Count)> Buttons{};
    std::array<float,static_cast<std::size_t>(InputGamepadAxis::Count)> Axes{};
    std::vector<InputTouchState> Touches,TouchChanges;
    bool Supports(InputDeviceCapability capability) const {return (Capabilities&static_cast<std::uint32_t>(capability))!=0;}
    InputButtonState GetButton(InputGamepadButton button) const {
        const auto index=static_cast<std::size_t>(button);
        return index<Buttons.size()?Buttons[index]:InputButtonState{};
    }
    float GetAxis(InputGamepadAxis axis) const {
        const auto index=static_cast<std::size_t>(axis);
        return index<Axes.size()?Axes[index]:0.f;
    }
    // Stick X right, Y up, each in [-1,1]. Trigger travel is [0,1].
    Vector2 LeftStick() const {return {GetAxis(InputGamepadAxis::LeftX),GetAxis(InputGamepadAxis::LeftY)};}
    Vector2 RightStick() const {return {GetAxis(InputGamepadAxis::RightX),GetAxis(InputGamepadAxis::RightY)};}
};

struct InputJoystickState {
    InputDeviceId Id=0;
    std::string Name;
    std::vector<float> Axes; // Raw device order and direction; each normalized to [-1,1].
    std::vector<InputButtonState> Buttons;
    std::vector<std::uint8_t> Hats; // InputJoystickHat bit mask. Diagonals combine two bits.
};

struct InputPenState {
    InputDeviceId Id=0;
    bool InRange=false,HasPosition=false;
    InputButtonState Tip;
    std::uint32_t Buttons=0;
    Vector2 Position,Delta,Tilt;
    float Pressure=0,Twist=0;
};

struct InputSensorState {
    InputDeviceId Id=0;
    InputSensorKind Kind=InputSensorKind::Unknown;
    std::string Name;
    bool Enabled=false,HasSample=false;
    Vector3 Value; // Acceleration m/s², angular speed rad/s; device-local axes.
    std::uint64_t Timestamp=0;
};

enum class InputDeviceKind : std::uint8_t { Gamepad,Joystick,Sensor };
struct InputDeviceChange { InputDeviceKind Kind; InputDeviceId Id; bool Connected; };

struct RuntimeInputState {
    bool Focused=false,HasMousePosition=false;
    Vector2 MousePosition,MouseDelta,WheelDelta;
    std::array<InputButtonState,static_cast<std::size_t>(InputKey::Count)> Keys{};
    std::array<InputButtonState,static_cast<std::size_t>(InputMouseButton::Count)> MouseButtons{};
    std::vector<InputGamepadState> Gamepads;
    std::vector<InputJoystickState> Joysticks;
    std::vector<InputTouchState> Touches,TouchChanges;
    std::vector<InputPenState> Pens;
    std::vector<InputSensorState> Sensors;
    std::vector<InputDeviceChange> DeviceChanges;
    // Ordered primary mouse, screen touch and pen tip events for this gameplay frame.
    std::vector<RuntimePointerEvent> PointerChanges;
    std::string TextInput,TextComposition; // Committed UTF-8 clears each frame; composition persists until replaced.
    std::int32_t TextCompositionStart=0,TextCompositionLength=0;
    InputCursorMode CursorMode=InputCursorMode::Normal;
    bool TextInputActive=false;
    InputButtonState GetKey(InputKey key) const {
        const auto index=static_cast<std::size_t>(key);
        return index<Keys.size()?Keys[index]:InputButtonState{};
    }
    InputButtonState GetMouseButton(InputMouseButton button) const {
        const auto index=static_cast<std::size_t>(button);
        return index<MouseButtons.size()?MouseButtons[index]:InputButtonState{};
    }
    const InputGamepadState* GetGamepad(InputDeviceId id) const {return Find(Gamepads,id);}
    const InputJoystickState* GetJoystick(InputDeviceId id) const {return Find(Joysticks,id);}
    const InputPenState* GetPen(InputDeviceId id) const {return Find(Pens,id);}
    const InputSensorState* GetSensor(InputDeviceId id) const {return Find(Sensors,id);}
private:
    template<class T> static const T* Find(const std::vector<T>& devices,InputDeviceId id) {
        const auto match=std::find_if(devices.begin(),devices.end(),[id](const auto& device){return device.Id==id;});
        return match==devices.end()?nullptr:&*match;
    }
};

enum class InputOutputTarget : std::uint8_t { Window,Device };
enum class InputOutputType : std::uint8_t {
    SetCursorMode,WarpCursor,StartTextInput,StopTextInput,SetTextInputArea,
    Rumble,RumbleTriggers,SetLed,EnableSensor,Reset
};

struct InputOutputCommand {
    InputOutputType Type=InputOutputType::Reset;
    InputDeviceId DeviceId=0;
    InputCursorMode CursorMode=InputCursorMode::Normal;
    float X=0,Y=0,Width=0,Height=0,Low=0,High=0;
    std::int32_t Cursor=0; // Candidate caret X offset in logical pixels within the IME rectangle.
    std::uint32_t DurationMs=0;
    std::uint8_t Red=0,Green=0,Blue=0;
    bool Enabled=false;
};
using InputOutputHandler = bool (*)(void*,const InputOutputCommand&);
}
