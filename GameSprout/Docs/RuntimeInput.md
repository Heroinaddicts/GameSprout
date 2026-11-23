# Runtime 输入与设备输出

原生脚本和 Module 通过 `IGameSproutRuntime::GetInputState()` 读取设备状态，通过 Runtime 公共接口操作光标、输入法、震动、灯光和传感器。设备状态见 [RuntimeInputApi.h](../GameSproutRuntime/Api/RuntimeInputApi.h)，输出方法见 [GameSproutRuntimeApi.h](../GameSproutRuntime/Api/GameSproutRuntimeApi.h)，代码动作映射见 [RuntimeInputActions.h](../GameSproutRuntime/Api/RuntimeInputActions.h)。

## Unity 源码对照

对照 UnityStudy 安装的 Unity Input System 1.20.0，以下均链接对应版本的公开源码。

| 层 | Unity 的行为 | 源码 |
| --- | --- | --- |
| Input System 设备 | 原生事件携带设备 ID 和时间。`InputManager` 在更新中消费事件，分别处理设备状态、文本提交和输入法组合。默认随动态帧更新，也可选固定帧或手动更新。 | [事件头](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/Events/InputEvent.cs#L147) · [事件处理](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/InputManager.cs#L3757) · [更新时间](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/InputSettings.cs#L817) |
| 设备焦点与输出 | 失焦时按配置重置或禁用设备，重新获焦后尝试同步。Editor 默认让键盘和指针尊重 Game 视窗焦点。光标、震动与灯光通过设备命令下发，部分能力取决于硬件。 | [焦点处理](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/InputManager.cs#L3988) · [Editor 焦点策略](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/InputSettings.cs#L964) · [设备命令](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/Devices/InputDevice.cs#L603) |
| 动作与玩家 | 在设备之上，用 Action Map 定义玩法动作，将 `Move` 绑定到 WASD、手柄摇杆或屏幕控件。`InputUser` 与 `PlayerInput` 负责设备配对和控制方案，运行时还可以覆盖绑定。 | [动作状态](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/Actions/InputAction.cs#L78) · [玩家配对](https://github.com/Unity-Technologies/InputSystem/blob/27757ebb14c0659ce1dbe39822ce97b33adea6c9/Packages/com.unity.inputsystem/InputSystem/Runtime/Plugins/PlayerInput/PlayerInput.cs#L51) |

GameSprout 提供设备状态和 C++ 动作映射。脚本可以为每个角色持有自己的 `InputActionMap`，按设备 ID 绑定手柄或摇杆。Unity 的动作资产、交互式改键界面和玩家自动配对由其更上层的组件与编辑器提供。

## 设备与焦点

Editor 的 Game 视窗与独立 Player 各自接收宿主事件，并转换到同一份 Runtime 状态。Runtime 管理焦点和帧状态，宿主检查硬件能力并执行设备命令。GRHI 只负责渲染。

| 设备 | 可读状态 | 输出与可用条件 |
| --- | --- | --- |
| 键盘 | `GetKey()`，支持组合修饰键、左右修饰键、功能键和数字小键盘。 | 游戏窗口获得输入焦点后读取。文本由专门的输入接口提供。 |
| 鼠标 | `GetMouseButton()`、`MousePosition`、`MouseDelta`、`WheelDelta`。位置相对游戏窗口，左上角为原点，使用逻辑像素。纵向滚轮向上为正。 | `SetCursorMode()` 支持普通、隐藏、锁定和限制，`WarpCursor()` 请求移动指针。实际模式取决于窗口宿主，检查返回值。 |
| 标准手柄 | `Gamepads`、`GetGamepad(id)`，可读方向按键、摇杆、扳机和支持设备的触摸板。摇杆向右、向上为正，分量范围 `[-1, 1]`。扳机范围 `[0, 1]`。 | 双马达、扳机震动和灯光分别查询 `Supports()`。输入焦点与连接状态会影响命令是否成功。 |
| 通用摇杆 | `Joysticks`、`GetJoystick(id)`，提供原始轴序、按钮和方向帽。轴范围 `[-1, 1]`，方向帽斜向由两个位组合。 | 按设备映射解释各轴。 |
| 触屏 | `Touches` 为当前接触，`TouchChanges` 为本帧触点变化，使用 `DeviceId` 和 `ContactId` 识别手指。屏幕位置使用逻辑像素，手柄触摸板位置使用归一化坐标。 | 通过接触的 `Began`、`Moved`、`Ended`、`Canceled` 处理交互。手柄触摸板通过对应手柄的 `Touches` 和 `TouchChanges` 查询。 |
| 触控笔 | `Pens`、`GetPen(id)`。提供悬停、笔尖、位置、压感、角度和笔键。 | 部分笔和平台只提供其中一部分数据。 |
| 文本和输入法 | `TextInput` 为本帧已提交 UTF-8 文本，`TextComposition` 为当前组合。组合选区偏移和长度使用 Unicode 码点。 | `StartTextInput()`、`StopTextInput()` 和 `SetTextInputArea()`。候选框位置用窗口逻辑像素描述，最后一个参数是光标相对区域左边的 X 偏移。 |
| 运动传感器 | `Sensors`、`GetSensor(id)`。`HasSample` 表示已收到样本。加速度为 m/s²，角速度为 rad/s，使用设备本地轴。 | `EnableInputSensor()` 请求开启已发现的传感器，再读取 `HasSample`。可用种类取决于设备和宿主。 |

设备 ID 只在本次运行会话及对应设备类别内有效。连接和断开可通过 `DeviceChanges` 读取。在 Editor 中先点击 Game 视窗接收输入，按 Esc 释放。失焦和暂停会清理玩法状态，设备发现和可读输入是两个独立条件。返回 `false` 的输出命令可以继续走游戏自己的回退逻辑。

## 帧语义

- 在 Runtime 所在线程的脚本更新阶段调用 `GetInputState()`。返回的引用和其中的设备指针借用 Runtime 的数据，下一次输入或帧操作可能使其失效。跨帧保存设备 ID 或需要的数值。
- `Held` 表示当前按住，`Pressed` 和 `Released` 表示本帧发生的变化。一次很快的按下又松开可以让两个边沿同时为真。按键自动重复不会产生新的 `Pressed`。当同一帧执行多个固定步时，先在 `EarlyUpdate` 记录跳跃等一次性请求，再在第一个合适的 `FixedUpdate` 消费。
- `MouseDelta` 和 `WheelDelta` 是本帧累计量。一次完整的游戏帧结束后，边沿、累计量、设备变化、触点变化和已提交文本会清空。按住状态和持续接触可以跨帧读取。IME 组合在被替换、提交或取消前保留。

## 在 C++ 中配置动作

创建映射一次，每个游戏帧调用一次 `Evaluate()`，再通过 `GetAction()` 读取 `Button`、`Value` 或 `Vector`。`Button` 动作的 `Value` 为 `0` 或 `1`，`Axis` 动作保留标量的正负，`Vector2` 动作保留方向和幅度。点按数字按钮时，即使按下和松开发生在同一帧，也能同时读到两个边沿。

一个动作可有多条绑定。死区按输入幅度过滤，再应用倍率，绝对幅度最大的绑定生效，同幅度按绑定顺序取第一条。二维键盘组合限制对角线幅度。手柄摇杆和通用摇杆的二维绑定读取同一台设备的两个轴，设备 ID 为 `0` 时允许任意连接的设备。

`TouchPress()` 和 `PenTip()` 可把触屏或笔尖绑定到同一个 Button 动作。`BindTouchDelta()` 和 `BindPenDelta()` 可提供二维位移。同帧有多个触点或笔时，绑定读取位移幅度最大的单个触点或笔。触屏点按更新来自屏幕触点，取消会结束按住状态。

持续按住的动作在失焦或设备断开后释放。重新进入有焦点的状态时，之前已按住的控制会同步为 `Held`，新按钮按下依然可以产生 `Pressed`。`SetEnabled(false)` 和 `Reset()` 清除动作状态并保留绑定，可用于切换游戏、菜单或暂停。也可以通过 `ClearBindings()` 和相应的 `Bind` 方法在代码中更换绑定。

下面把 WASD 和左摇杆绑定到 `Move`，把空格和手柄下方按键绑定到 `Jump`。

```cpp
#include "GameSproutRuntimeApi.h"
#include "RuntimeInputActions.h"

using namespace GameSproutRuntime;
using Control = InputActionControl;

InputActionMap MakeControls(InputDeviceId padId = 0) {
    InputActionMap controls;
    controls.AddAction("Move", InputActionType::Vector2);
    controls.BindVector2Composite("Move", Control::Key(InputKey::A), Control::Key(InputKey::D),
        Control::Key(InputKey::S), Control::Key(InputKey::W));
    controls.BindGamepadStick("Move", InputActionStick::Left, padId, {.DeadZone = 0.15f});
    controls.AddAction("Jump", InputActionType::Button);
    controls.Bind("Jump", Control::Key(InputKey::Space));
    controls.Bind("Jump", Control::GamepadButton(InputGamepadButton::South, padId));
    controls.AddAction("PointerPress", InputActionType::Button);
    controls.Bind("PointerPress", Control::MouseButton(InputMouseButton::Left));
    controls.Bind("PointerPress", Control::TouchPress());
    controls.Bind("PointerPress", Control::PenTip());
    return controls;
}

void ReadControls(const IGameSproutRuntime& runtime, InputActionMap& controls, Vector2& move, bool& jump) {
    controls.Evaluate(runtime.GetInputState());
    move = controls.GetAction("Move")->Vector;
    jump = controls.GetAction("Jump")->Button.Pressed;
}
```

## 屏幕适配和 UI 指针

`GetInputViewportMetrics()` 返回当前 Game 画面的逻辑矩形 `ContentRect`、实际渲染像素尺寸 `RenderSize`、显示密度 `PixelRatio`、物理 DPI、安全区和朝向。鼠标、触点与 `ContentRect` 用同一套宿主局部坐标，左上起算，向右和向下为正。游戏画面和宿主的每轴缩放可能不同。用 `ScreenPointToUILocal()` 和 `UILocalPointToScreen()` 双向转换，转换结果已经考虑锚点、父级变换、缩放和旋转。UI 局部坐标以控件轴心为原点，向右和向上为正，`GetUIRectLocalBounds()` 返回同坐标系的矩形。

Canvas Inspector 和 `ICanvasComponent::SetScaleSettings()` 提供固定像素、随屏幕大小、固定物理尺寸三种模式。随屏幕大小默认使用 Expand，也可选 Shrink 或宽高匹配。选中“按安全区布局”后，Canvas 的布局边界使用宿主提供的安全区。安全区为空时使用整个 `ContentRect`，物理 DPI 为零时使用 Canvas 的回退 DPI。这套缩放计算参考了 Unity uGUI。 [Unity CanvasScaler 源码](https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/UGUI/UI/Core/Layout/CanvasScaler.cs)

在脚本更新中按顺序消费 `GetInputState().PointerChanges`。左键、屏幕手指和笔尖各有独立的 `RuntimePointerId`。在 `Down` 时用事件自己的 `Position` 调用 `HitTestUIRect()`，命中后用 `CaptureUIPointer(id, objectId)` 申请独占这个指针。拖到矩形外后仍可用 `ScreenPointToUILocal()` 换算 `Move`。在 `Up` 或 `Cancel` 时收尾并归还捕获。失焦、暂停或切换运行场景时也要清理脚本保存的拖拽状态。单次更新里可能同时出现某个指针的按下、移动和松开，摇杆和镜头可以各自捕获一根手指。

尺寸、DPI 或朝向有变化时，Runtime 更新指标版本。需要订阅时实现 `IRuntimeInputViewportListener`，注册后读取一次当前快照，并在监听对象销毁前调用 `UnregisterInputViewportListener()`。

## 读取触点和发送输出

读取一个新触点，并按设备能力让手柄短震。一次按下型反馈适合在 `Update` 中触发一次。

```cpp
bool ReadTouchStart(const IGameSproutRuntime& runtime, InputTouchState& touch) {
    for (const auto& change : runtime.GetInputState().TouchChanges) {
        if (change.Phase != InputTouchPhase::Began) continue;
        touch = change;
        return true;
    }
    return false;
}

bool RumbleOnConfirm(IGameSproutRuntime& runtime, InputDeviceId padId) {
    const auto* pad = runtime.GetInputState().GetGamepad(padId);
    return pad && pad->GetButton(InputGamepadButton::South).Pressed &&
        pad->Supports(InputDeviceCapability::Rumble) &&
        runtime.RumbleGamepad(padId, 0.45f, 0.2f, 120);
}
```

打开聊天输入框时调用 `StartTextInput()`，输入区域变化时更新 `SetTextInputArea()`，每帧消费 `TextInput` 并单独展示 `TextComposition`，关闭输入框时调用 `StopTextInput()`。鼠标锁定成功后用 `MouseDelta` 驱动镜头。运动传感器启用成功后等到 `HasSample` 再使用 `Value`，离开使用场景时传 `false` 给 `EnableInputSensor()`。
