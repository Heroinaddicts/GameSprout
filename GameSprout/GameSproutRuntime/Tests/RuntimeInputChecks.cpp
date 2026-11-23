#include "GameSproutRuntimeExport.h"
#include "RuntimeInputActions.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
GameSproutRuntime::RuntimeInputEvent Focus(GameSproutRuntime::InputEventType type){GameSproutRuntime::RuntimeInputEvent event;event.Type=type;return event;}
GameSproutRuntime::RuntimeInputEvent Key(GameSproutRuntime::InputEventType type,GameSproutRuntime::InputKey key){auto event=Focus(type);event.Key=key;return event;}

struct OutputLog {
    std::vector<GameSproutRuntime::InputOutputCommand> Commands;
    static bool Receive(void* context, const GameSproutRuntime::InputOutputCommand& command) {
        static_cast<OutputLog*>(context)->Commands.push_back(command);
        return true;
    }
};

void CheckDevices(GameSproutRuntime::IGameSproutRuntime& runtime) {
    using namespace GameSproutRuntime;
    OutputLog window, device;
    runtime.SetInputOutputHandler(InputOutputTarget::Window,&OutputLog::Receive,&window);
    runtime.SetInputOutputHandler(InputOutputTarget::Device,&OutputLog::Receive,&device);
    struct Detach {
        IGameSproutRuntime& Owner;
        ~Detach() {
            Owner.SetInputOutputHandler(InputOutputTarget::Window,nullptr,nullptr);
            Owner.SetInputOutputHandler(InputOutputTarget::Device,nullptr,nullptr);
        }
    } detach{runtime};
    std::string error;
    Check(runtime.BeginExecution(error),error.c_str());
    auto submit = [&](InputEventType type, InputDeviceId id, std::uint16_t index = 0) {
        RuntimeInputEvent event{type};
        event.DeviceId = id;
        event.Index = index;
        return runtime.SubmitInputEvent(event);
    };
    RuntimeInputEvent pad{InputEventType::GamepadAdded};
    pad.DeviceId = 7;
    pad.Text = "Test Gamepad";
    pad.Capabilities = static_cast<std::uint32_t>(InputDeviceCapability::Rumble);
    Check(runtime.SubmitInputEvent(pad) && runtime.GetInputState().DeviceChanges.size() == 1,
          "Unfocused gameplay lost device discovery");
    InputActionMap actions;
    Check(actions.AddAction("Jump",InputActionType::Button) &&
          actions.Bind("Jump",InputActionControl::Key(InputKey::Space)) &&
          actions.Bind("Jump",InputActionControl::GamepadButton(InputGamepadButton::South,7)),"Cannot bind named gameplay action");
    Check(!submit(InputEventType::GamepadButtonDown,7) && !runtime.RumbleGamepad(7,1,1,100),"Unfocused game used device controls");
    runtime.SubmitInputEvent(Focus(InputEventType::FocusGained));
    RuntimeInputEvent press{InputEventType::GamepadButtonDown};
    press.DeviceId = 7;
    press.IsStateSync = true;
    Check(runtime.SubmitInputEvent(press) && runtime.GetInputState().GetGamepad(7)->Buttons[0].Held &&
          !runtime.GetInputState().GetGamepad(7)->Buttons[0].Pressed,"Initial device sync generated a press");
    actions.Evaluate(runtime.GetInputState());
    Check(actions.GetAction("Jump")->Button.Held && !actions.GetAction("Jump")->Button.Pressed,
          "Controller resync triggered a named action press");
    Check(submit(InputEventType::GamepadButtonUp,7) && submit(InputEventType::GamepadButtonDown,7),"Cannot update mapped buttons");
    auto button = runtime.GetInputState().GetGamepad(7)->GetButton(InputGamepadButton::South);
    Check(button.Held && button.Pressed && button.Released,"Same-frame gamepad up/down edges were lost");
    Check(runtime.RumbleGamepad(7,-0.5f,1.5f,100) && device.Commands.back().Low == 0 && device.Commands.back().High == 1 &&
          !runtime.SetGamepadLed(7,255,0,0) && !runtime.RumbleGamepad(99,1,1,100),"Output ignored device capability or value limits");

    RuntimeInputEvent joystick{InputEventType::JoystickAdded};
    joystick.DeviceId = 8;
    joystick.AxisCount = joystick.ButtonCount = joystick.HatCount = 1;
    Check(runtime.SubmitInputEvent(joystick) && submit(InputEventType::JoystickButtonDown,8) &&
          !submit(InputEventType::JoystickButtonDown,8,1),"Raw joystick control indices were not validated");
    RuntimeInputEvent hat{InputEventType::JoystickHat};
    hat.DeviceId = 8;
    hat.Value = 3;
    Check(runtime.SubmitInputEvent(hat) && runtime.GetInputState().GetJoystick(8)->Hats[0] == 3,"Raw joystick lost diagonal hat state");

    RuntimeInputEvent sensor{InputEventType::SensorAdded};
    sensor.DeviceId = 9;
    sensor.Sensor = InputSensorKind::Gyroscope;
    Check(runtime.SubmitInputEvent(sensor),"Sensor discovery rejected");
    sensor.Type = InputEventType::SensorUpdate;
    sensor.SensorValue = {1,2,3};
    sensor.Timestamp = 120;
    Check(!runtime.SubmitInputEvent(sensor) && runtime.EnableInputSensor(9) && runtime.SubmitInputEvent(sensor) &&
          runtime.GetInputState().GetSensor(9)->Timestamp == 120,"Sensor did not require enable or retain valid sample");

    runtime.SubmitInputEvent(Key(InputEventType::KeyDown,InputKey::LeftShift));
    runtime.SubmitInputEvent(Key(InputEventType::KeyDown,InputKey::RightShift));
    runtime.SubmitInputEvent(Key(InputEventType::KeyUp,InputKey::LeftShift));
    Check(runtime.GetInputState().GetKey(InputKey::Shift).Held && !runtime.GetInputState().GetKey(InputKey::Shift).Released,
          "Releasing one Shift released the combined control");
    runtime.SubmitInputEvent(Key(InputEventType::KeyUp,InputKey::RightShift));
    Check(runtime.GetInputState().GetKey(InputKey::Shift).Released,"Both Shifts released without an aggregate edge");
    runtime.SubmitInputEvent(Key(InputEventType::KeyDown,InputKey::Keypad1));
    Check(!runtime.GetInputState().GetKey(InputKey::Digit1).Held,"Keypad key overlapped the main keyboard");

    Check(runtime.SetCursorMode(InputCursorMode::Locked) && !runtime.WarpCursor({10,20}),"Locked cursor accepted a warp");
    RuntimeInputEvent delta{InputEventType::MouseRelativeMove};
    delta.DeltaX = 10;
    delta.DeltaY = -8;
    Check(runtime.SubmitInputEvent(delta) && runtime.GetInputState().MouseDelta.Y == -8 &&
          !runtime.GetInputState().HasMousePosition,"Relative motion depended on an absolute position");
    Check(runtime.StartTextInput() && runtime.SetTextInputArea({10,20},{100,30},5),"Text output was not forwarded");
    RuntimeInputEvent composition{InputEventType::TextComposition};
    composition.Text = "输入";
    composition.TextStart = 1;
    composition.TextLength = 1;
    Check(runtime.SubmitInputEvent(composition),"IME preedit rejected");
    runtime.CompleteInputFrame();
    Check(runtime.GetInputState().TextComposition == "输入" && runtime.GetInputState().TextCompositionStart == 1,
          "Frame completion cleared ongoing IME composition");
    RuntimeInputEvent commit{InputEventType::TextInput};
    commit.Text = "输入😀";
    Check(runtime.SubmitInputEvent(commit) && runtime.GetInputState().TextInput == "输入😀" &&
          runtime.GetInputState().TextComposition.empty(),"Text commit lost Unicode or retained preedit");

    RuntimeInputEvent touch{InputEventType::TouchBegan};
    touch.DeviceId = 4;
    touch.ContactId = 5;
    touch.X = 20;
    touch.Y = 30;
    Check(runtime.SubmitInputEvent(touch),"Screen touch began rejected");
    touch.Type = InputEventType::TouchEnded;
    touch.X = 30;
    Check(runtime.SubmitInputEvent(touch) && runtime.GetInputState().TouchChanges.size() == 2 &&
          runtime.GetInputState().TouchChanges.back().Delta.X == 10,"Same-frame screen touch edges or movement lost");
    touch.Type = InputEventType::GamepadTouchBegan;
    touch.DeviceId = 7;
    touch.X = .5f;
    touch.Y = .5f;
    Check(runtime.SubmitInputEvent(touch) && runtime.GetInputState().GetGamepad(7)->Touches.size() == 1 &&
          runtime.GetInputState().Touches.size() == 1,"Gamepad touchpad was combined with screen touch");
    RuntimeInputEvent pen{InputEventType::PenDown};
    pen.DeviceId = 71;
    pen.Pressure = .4f;
    pen.TiltX = 20;
    Check(runtime.SubmitInputEvent(pen) && runtime.GetInputState().GetPen(71)->Tip.Pressed &&
          runtime.GetInputState().GetPen(71)->Pressure == .4f,"Pen contact state or pressure lost");
    runtime.CompleteInputFrame();
    Check(runtime.GetInputState().Touches.empty() && runtime.GetInputState().TextInput.empty() &&
          runtime.GetInputState().GetGamepad(7)->Touches.front().Phase == InputTouchPhase::Stationary,
          "Transient input escaped its frame or active touch vanished");
    runtime.SubmitInputEvent(Focus(InputEventType::FocusLost));
    actions.Evaluate(runtime.GetInputState());
    Check(actions.GetAction("Jump")->Button.Released,"Focus loss left the named action held");
    const auto* gamepad = runtime.GetInputState().GetGamepad(7);
    Check(gamepad && gamepad->Buttons[0].Released && !gamepad->Buttons[0].Held &&
          gamepad->Touches.front().Phase == InputTouchPhase::Canceled && !runtime.GetInputState().GetSensor(9)->HasSample &&
          !runtime.GetInputState().TextInputActive && window.Commands.back().Type == InputOutputType::Reset &&
          device.Commands.back().Type == InputOutputType::Reset,"Focus loss did not release controls and outputs together");
    Check(submit(InputEventType::GamepadRemoved,7) && !runtime.GetInputState().GetGamepad(7) &&
          !runtime.GetInputState().DeviceChanges.back().Connected,"Unfocused unplug left a stale device");
    Check(runtime.EndExecution(error),error.c_str());
}

void CheckCanvasInput(GameSproutRuntime::IGameSproutRuntime& runtime) {
    using namespace GameSproutRuntime;
    auto* scene = runtime.CreateScene("UI input");
    Check(scene, "Cannot create UI scene");
    auto* canvas = scene->CreateGameObject("Canvas", scene);
    Check(canvas && canvas->AddComponent<IRectTransformComponent>(), "Cannot create Canvas RectTransform");
    auto* canvasComponent = canvas->AddComponent<ICanvasComponent>();
    Check(canvasComponent && canvasComponent->SetReferenceResolution({800, 600}), "Cannot configure Canvas");
    auto* object = scene->CreateGameObject("Button", canvas);
    auto* rect = object->AddComponent<IRectTransformComponent>();
    rect->SetAnchoredPosition({100, 60});
    rect->SetSizeDelta({160, 80});
    auto* button = object->AddComponent<IButtonComponent>();
    Check(button && button->GetInteractable() && !button->IsPressed(), "Cannot create Button");
    auto* label = scene->CreateGameObject("Text", object);
    auto* text = label->AddComponent<ITextComponent>();
    Check(text && text->SetText("开始") && !text->SetText("\xED\xA0\x80") && text->GetText()=="开始" &&
        !text->SetFontSize(0) && text->GetFontSize()==DefaultUIFontSize, "Text API did not preserve valid values");

    std::string error;
    Check(runtime.BeginExecution(error), error.c_str());
    runtime.SetUIInputViewport(scene, {400, 300});
    Check(runtime.GetInputViewportSize().X == 400 && runtime.GetInputViewportSize().Y == 300,
          "Input viewport did not report logical dimensions");
    Check(runtime.SubmitInputEvent(Focus(InputEventType::FocusGained)), "UI viewport focus failed");
    const auto pointer = [&](bool down, float x, float y) {
        auto move = Focus(InputEventType::MouseMove);
        move.X=x;
        move.Y=y;
        Check(runtime.SubmitInputEvent(move) &&
            runtime.SubmitInputEvent(Focus(down ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp)),
            "UI pointer event rejected");
    };
    const auto frame = [&] { runtime.AdvanceFrame(1.0f/60.0f, 0, 0); runtime.CompleteInputFrame(); };
    int clicks = 0;
    std::uint64_t token = 0;
    token = button->RegisterClick([&] { ++clicks; button->UnregisterClick(token); text->SetText("Clicked"); });
    Check(token != 0, "Cannot subscribe to a button click");
    pointer(true, 250, 120);
    Check(!button->IsPressed(), "Button callback escaped host-frame boundary");
    frame();
    Check(button->IsPressed(), "Pointer missed the anchored and scaled Button");
    pointer(false, 200, 150);
    frame();
    Check(!button->IsPressed() && clicks==0, "Release outside incorrectly clicked");
    pointer(true, 250, 120);
    pointer(false, 250, 120);
    frame();
    Check(clicks==1 && text->GetText()=="Clicked", "Two edges within one frame lost a click");
    pointer(true, 250, 120);
    frame();
    runtime.SubmitInputEvent(Focus(InputEventType::FocusLost));
    Check(!button->IsPressed(), "Focus loss retained a UI press");
    runtime.SubmitInputEvent(Focus(InputEventType::FocusGained));

    const auto objectID = object->GetInstanceID();
    Check(button->RegisterClick([&] { ++clicks; scene->DestroyGameObject(object); })!=0,
        "Cannot subscribe destructive click");
    button->SetInteractable(false);
    pointer(true, 250, 120);
    pointer(false, 250, 120);
    frame();
    Check(clicks==1 && runtime.QueryGameObject(objectID), "Disabled Button received a click");
    button->SetInteractable(true);
    pointer(true, 250, 120);
    pointer(false, 250, 120);
    frame();
    Check(clicks==2 && !runtime.QueryGameObject(objectID), "Click did not safely destroy its Button");

    auto* group = scene->CreateGameObject("Rotated group", canvas);
    auto* groupRect = group->AddComponent<IRectTransformComponent>();
    groupRect->SetSizeDelta({200, 100});
    Check(group->GetTransform()->SetLocalRotation({0, 0, 0.70710678f, 0.70710678f}), "Cannot rotate UI group");
    auto* child = scene->CreateGameObject("Nested button", group);
    auto* childRect = child->AddComponent<IRectTransformComponent>();
    childRect->SetAnchoredPosition({80, 0});
    childRect->SetSizeDelta({120, 40});
    auto* childButton = child->AddComponent<IButtonComponent>();
    Check(childButton->RegisterClick([&] { ++clicks; })!=0, "Cannot subscribe to rotated Button");
    runtime.SetUIInputViewport(scene, {400, 300}, {800, 450});
    pointer(true, 235, 150);
    pointer(false, 235, 150);
    frame();
    Check(clicks==2, "Rotated Button still hit its unrotated rectangle");
    pointer(true, 200, 135);
    pointer(false, 200, 135);
    frame();
    Check(clicks==3, "Game input did not map the logical pointer to the rotated layout at the render-target aspect");
    RuntimeInputEvent touch{InputEventType::TouchBegan};
    touch.DeviceId = 1;
    touch.ContactId = 2;
    touch.X = 200;
    touch.Y = 135;
    runtime.SubmitInputEvent(touch);
    pointer(true,200,135);
    touch.Type = InputEventType::TouchEnded;
    runtime.SubmitInputEvent(touch);
    pointer(false,200,135);
    frame();
    Check(clicks==4, "A competing pointer duplicated or swallowed a touch click");
    touch.Type = InputEventType::TouchBegan;
    runtime.SubmitInputEvent(touch);
    frame();
    Check(childButton->IsPressed(), "First screen touch did not capture a UI button");
    touch.Type = InputEventType::TouchCanceled;
    touch.ContactId = 0;
    runtime.SubmitInputEvent(touch);
    frame();
    Check(clicks==4 && !childButton->IsPressed(), "Canceled touch incorrectly clicked or left a UI press");

    UIScreenPreviewGeometry geometry;
    Check(runtime.GetUIScreenPreviewGeometry(scene, child->GetInstanceID(), {600, 300}, geometry, {800, 450}) &&
        std::fabs(geometry.CanvasBounds.Size.X/geometry.CanvasBounds.Size.Y-800.0f/450.0f)<0.001f,
        "Scene preview did not follow the Game aspect");
    const auto rotatedHit = geometry.Pivot + geometry.Right * -50.0f;
    Check(runtime.PickUIScreenPreview(scene, child->GetInstanceID(), {600, 300}, rotatedHit, {800, 450})==child->GetInstanceID(),
        "Scene preview did not hit the same nested rotated rectangle");
    runtime.SetUIInputViewport(nullptr, {});
    Check(runtime.GetInputViewportSize().X == 0 && runtime.GetInputViewportSize().Y == 0,
          "Unbound gameplay input viewport retained stale dimensions");
    Check(runtime.EndExecution(error), error.c_str());
}

void CheckViewportAndPointers(GameSproutRuntime::IGameSproutRuntime& runtime) {
    using namespace GameSproutRuntime;
    auto near = [](float a,float b) { return std::fabs(a-b)<0.001f; };
    auto* scene = runtime.CreateScene("Adaptive UI");
    Check(scene,"Cannot create adaptive UI scene");
    auto* canvas = scene->CreateGameObject("Safe-area Canvas",scene);
    auto* scaler = canvas->AddComponent<ICanvasComponent>();
    Check(scaler && scaler->SetReferenceResolution({800,600}),"Cannot configure adaptive Canvas");
    auto* stick = scene->CreateGameObject("Stick",canvas);
    auto* stickRect = stick->AddComponent<IRectTransformComponent>();
    Check(stickRect,"Cannot create stick rectangle");
    stickRect->SetSizeDelta({120,100});
    auto* look = scene->CreateGameObject("Look",canvas);
    Check(look && look->AddComponent<IRectTransformComponent>(),"Cannot create look region");
    const auto stickId = stick->GetInstanceID();
    const auto lookId = look->GetInstanceID();

    struct Listener final : IRuntimeInputViewportListener {
        unsigned Count=0;
        std::uint64_t Revision=0;
        void OnInputViewportChanged(const RuntimeInputViewportMetrics& metrics) override {
            ++Count;
            Revision=metrics.Revision;
        }
    } listener;
    Check(runtime.RegisterInputViewportListener(&listener),"Cannot register viewport listener");
    struct Unregister {
        IGameSproutRuntime& Runtime;
        IRuntimeInputViewportListener* Listener;
        ~Unregister() { Runtime.UnregisterInputViewportListener(Listener); }
    } unregister{runtime,&listener};
    RuntimeInputViewportMetrics metrics;
    metrics.ContentRect={{50,20},{400,300}};
    metrics.RenderSize={800,900};
    metrics.SafeArea={{90,55},{360,240}};
    metrics.PixelRatio=2;
    metrics.DisplayDpi=144;
    metrics.Orientation=InputViewportOrientation::Landscape;
    runtime.SetInputViewportMetrics(scene,metrics);
    runtime.SetInputViewportMetrics(scene,metrics);
    Check(listener.Count==1 && listener.Revision==runtime.GetInputViewportMetrics().Revision &&
        runtime.GetInputViewportSize().Y==300,"Unchanged viewport notified twice or lost logical size");

    auto checkScale = [&](CanvasScaleSettings settings,Vector2 center,float scale) {
        Check(scaler->SetScaleSettings(settings),"Cannot set Canvas scale mode");
        Vector2 point,local;
        Rect bounds;
        Check(runtime.UILocalPointToScreen(stickId,{0,0},point) && near(point.X,center.X) && near(point.Y,center.Y) &&
            runtime.UILocalPointToScreen(stickId,{10,6},point) && near(point.X,center.X+5*scale) && near(point.Y,center.Y-2*scale) &&
            runtime.ScreenPointToUILocal(stickId,point,local) && near(local.X,10) && near(local.Y,6) &&
            runtime.HitTestUIRect(stickId,point) && runtime.GetUIRectLocalBounds(stickId,bounds) &&
            near(bounds.Position.X,-60) && near(bounds.Position.Y,-50) && near(bounds.Size.X,120),
            "Layout, hit test and coordinate round trip disagreed at adaptive scale");
    };
    CanvasScaleSettings settings;
    checkScale(settings,{250,170},1);
    settings.MatchMode=CanvasScreenMatchMode::Shrink;
    checkScale(settings,{250,170},1.5f);
    settings.MatchMode=CanvasScreenMatchMode::MatchWidthOrHeight;
    checkScale(settings,{250,170},std::sqrt(1.5f));
    settings.UseSafeArea=true;
    settings.Mode=CanvasScaleMode::ConstantPhysicalSize;
    checkScale(settings,{270,175},2);
    metrics.DisplayDpi=0;
    runtime.SetInputViewportMetrics(scene,metrics);
    checkScale(settings,{270,175},96.0f/72.0f);
    settings.Mode=CanvasScaleMode::ConstantPixelSize;
    settings.ScaleFactor=0.75f;
    checkScale(settings,{270,175},0.75f);

    std::string error;
    Check(runtime.BeginExecution(error),error.c_str());
    Check(runtime.SubmitInputEvent(Focus(InputEventType::FocusGained)),"Cannot focus pointer test");
    auto move = Focus(InputEventType::MouseMove);
    move.X=270; move.Y=175;
    auto down = Focus(InputEventType::MouseButtonDown);
    down.Timestamp=100;
    Check(runtime.SubmitInputEvent(move) && runtime.SubmitInputEvent(down),"Cannot start logical mouse drag");
    move.X=395;
    move.Timestamp=105;
    Check(runtime.SubmitInputEvent(move),"Cannot move mouse outside stick");
    RuntimeInputEvent touch{InputEventType::TouchBegan};
    touch.DeviceId=8; touch.ContactId=23; touch.X=410; touch.Y=200;
    Check(runtime.SubmitInputEvent(touch),"Cannot start second spanning pointer");
    const RuntimePointerId mouseId{InputPointerKind::Mouse,0,0};
    const RuntimePointerId touchId{InputPointerKind::Touch,8,23};
    const auto& changes = runtime.GetInputState().PointerChanges;
    Check(changes.size()==3 && changes[0].Pointer==mouseId && changes[0].Phase==InputPointerPhase::Down &&
        changes[0].Timestamp==100 && runtime.HitTestUIRect(stickId,changes[0].Position) &&
        changes[1].Phase==InputPointerPhase::Move && changes[1].Delta.X==125 &&
        !runtime.HitTestUIRect(stickId,changes[1].Position) && changes[2].Pointer==touchId,
        "Frame-end position overwrote the ordered mouse press or a simultaneous touch");
    Check(runtime.CaptureUIPointer(mouseId,stickId) && !runtime.CaptureUIPointer(mouseId,lookId) &&
        runtime.CaptureUIPointer(touchId,lookId),"Independent UI pointer captures conflict");
    runtime.CompleteInputFrame();
    metrics.Orientation=InputViewportOrientation::Portrait;
    runtime.SetInputViewportMetrics(scene,metrics);
    Check(listener.Count==3 && runtime.GetUIPointerCapture(mouseId)==stickId && runtime.GetUIPointerCapture(touchId)==lookId &&
        changes.empty(),"Viewport change canceled an active gesture or retained an old frame");

    touch.Type=InputEventType::TouchEnded;
    Check(runtime.SubmitInputEvent(touch) && runtime.GetUIPointerCapture(touchId)==lookId,"Touch ended before its owner read Up");
    runtime.CompleteInputFrame();
    Check(runtime.GetUIPointerCapture(touchId)==0 && runtime.GetUIPointerCapture(mouseId)==stickId,
        "Frame completion retained ended capture or dropped the other capture");
    runtime.SubmitInputEvent(Focus(InputEventType::FocusLost));
    Check(runtime.GetUIPointerCapture(mouseId)==0 && changes.size()==1 && changes[0].Pointer==mouseId &&
        changes[0].Phase==InputPointerPhase::Cancel,"Focus loss did not cancel a captured drag");
    runtime.SetInputViewportMetrics(nullptr,{});
    Check(listener.Count==4 && runtime.GetInputViewportSize().X==0 &&
        !runtime.HitTestUIRect(stickId,{270,175}),"Detached viewport retained layout");
    Check(runtime.EndExecution(error),error.c_str());
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;auto* runtime=Export(argv[1]);
    try{
        Check(runtime,"Cannot load Runtime");std::string error;
        Check(!runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusGained)),"Edit mode accepted gameplay input");
        Check(runtime->BeginExecution(error),error.c_str());
        const auto down=Key(GameSproutRuntime::InputEventType::KeyDown,GameSproutRuntime::InputKey::W);
        Check(!runtime->SubmitInputEvent(down),"Unfocused gameplay accepted keys");
        Check(runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusGained)),"Cannot acquire gameplay input focus");
        Check(runtime->SubmitInputEvent(down),"Cannot press gameplay key");
        auto state=runtime->GetInputState();Check(state.GetKey(GameSproutRuntime::InputKey::W).Held&&state.GetKey(GameSproutRuntime::InputKey::W).Pressed,"Key press was not visible");
        runtime->CompleteInputFrame();Check(runtime->SubmitInputEvent(down),"Repeat down rejected");
        state=runtime->GetInputState();Check(state.GetKey(GameSproutRuntime::InputKey::W).Held&&!state.GetKey(GameSproutRuntime::InputKey::W).Pressed,"Repeated keydown retriggered Pressed");
        auto move=Focus(GameSproutRuntime::InputEventType::MouseMove);move.X=200;move.Y=300;Check(runtime->SubmitInputEvent(move),"Cannot seed mouse position");
        Check(runtime->GetInputState().MouseDelta.X==0&&runtime->GetInputState().MouseDelta.Y==0,"First focused mouse position produced a jump");
        move.X=210;move.Y=280;runtime->SubmitInputEvent(move);move.X=220;move.Y=290;runtime->SubmitInputEvent(move);
        auto wheel=Focus(GameSproutRuntime::InputEventType::MouseWheel);wheel.WheelX=.5f;wheel.WheelY=1;runtime->SubmitInputEvent(wheel);runtime->SubmitInputEvent(wheel);
        state=runtime->GetInputState();Check(state.MouseDelta.X==20&&state.MouseDelta.Y==-10&&state.WheelDelta.X==1&&state.WheelDelta.Y==2,"Mouse motion or wheel did not accumulate within the frame");
        auto mouse=Focus(GameSproutRuntime::InputEventType::MouseButtonDown);mouse.Button=GameSproutRuntime::InputMouseButton::Right;runtime->SubmitInputEvent(mouse);
        Check(runtime->GetInputState().GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Pressed,"Mouse button press was not visible");
        runtime->CompleteInputFrame();state=runtime->GetInputState();
        Check(state.MouseDelta.X==0&&state.WheelDelta.Y==0&&state.MousePosition.X==220&&state.GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Held&&!state.GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Pressed,"Frame completion lost held state or retained transient motion");
        Check(runtime->SubmitInputEvent(Key(GameSproutRuntime::InputEventType::KeyUp,GameSproutRuntime::InputKey::W)),"Cannot release key");
        Check(!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held&&runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Released,"Key release was not visible");
        runtime->SubmitInputEvent(down);runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusLost));state=runtime->GetInputState();
        Check(!state.Focused&&!state.GetKey(GameSproutRuntime::InputKey::W).Held&&state.GetKey(GameSproutRuntime::InputKey::W).Released&&!state.GetKey(GameSproutRuntime::InputKey::W).Pressed&&!state.GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Held,"Focus loss left held keys/buttons or a stale press");
        runtime->SetInputEnabled(false);Check(!runtime->SubmitInputEvent(down)&&!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Released,"Pause retained transient input");
        runtime->SetInputEnabled(true);Check(!runtime->GetInputState().Focused&&!runtime->SubmitInputEvent(down),"Resume reused stale focus");
        runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusGained));
        move.X=std::numeric_limits<float>::quiet_NaN();Check(!runtime->SubmitInputEvent(move),"Nonfinite mouse input accepted");
        Check(!runtime->SubmitInputEvent(Key(GameSproutRuntime::InputEventType::KeyDown,GameSproutRuntime::InputKey::Count)),"Invalid gameplay key accepted");
        runtime->SubmitInputEvent(down);Check(runtime->EndExecution(error),error.c_str());
        Check(!runtime->GetInputState().Focused&&!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held&&!runtime->SubmitInputEvent(down),"Stop did not reset and disable gameplay input");
        Check(runtime->BeginExecution(error)&&!runtime->GetInputState().Focused,"New session inherited old input");runtime->EndExecution(error);
        CheckDevices(*runtime);
        CheckCanvasInput(*runtime);
        CheckViewportAndPointers(*runtime);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS gameplay input: mode/focus gating, key/button edges, held state, mouse/wheel accumulation, pause/resume and stop\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
