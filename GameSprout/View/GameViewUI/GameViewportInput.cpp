#include "GameViewport.h"
#include "GameSproutRuntimeApi.h"
#include <QCoreApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#if defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#endif

namespace {
using namespace GameSproutRuntime;

QPointF MousePosition(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    return event->position();
#else
    return event->localPos();
#endif
}

bool IsEmulatedPointer(const QMouseEvent& event) {
    if (event.source() == Qt::MouseEventSynthesizedByQt) return true;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    const auto pointerType = event.pointerType();
    return pointerType == QPointingDevice::PointerType::Finger || pointerType == QPointingDevice::PointerType::Pen ||
        pointerType == QPointingDevice::PointerType::Eraser;
#else
    return false;
#endif
}

InputKey Key(const QKeyEvent& event) {
    const int key = event.key();
#if defined(Q_OS_MACOS)
    // Cocoa's physical modifier identities are independent of Qt's mapping
    // of the macOS Command key to the platform shortcut modifier.
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Meta || key == Qt::Key_Alt) {
        switch (event.nativeVirtualKey()) {
        case kVK_Shift: return InputKey::LeftShift;
        case kVK_RightShift: return InputKey::RightShift;
        case kVK_Control: return InputKey::LeftControl;
        case kVK_RightControl: return InputKey::RightControl;
        case kVK_Option: return InputKey::LeftAlt;
        case kVK_RightOption: return InputKey::RightAlt;
        case kVK_Command: return InputKey::LeftSuper;
        case kVK_RightCommand: return InputKey::RightSuper;
        default: break;
        }
    }
#endif
    if (event.modifiers() & Qt::KeypadModifier) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9)
            return static_cast<InputKey>(static_cast<int>(InputKey::Keypad0) + key - Qt::Key_0);
        switch (key) {
        case Qt::Key_Return: case Qt::Key_Enter: return InputKey::KeypadEnter;
        case Qt::Key_Period: case Qt::Key_Comma: return InputKey::KeypadDecimal;
        case Qt::Key_Slash: return InputKey::KeypadDivide;
        case Qt::Key_Asterisk: return InputKey::KeypadMultiply;
        case Qt::Key_Minus: return InputKey::KeypadSubtract;
        case Qt::Key_Plus: return InputKey::KeypadAdd;
        case Qt::Key_Equal: return InputKey::KeypadEqual;
        default: break;
        }
    }
    if(key>=Qt::Key_A&&key<=Qt::Key_Z)return static_cast<InputKey>(static_cast<int>(InputKey::A)+key-Qt::Key_A);
    if(key>=Qt::Key_0&&key<=Qt::Key_9)return static_cast<InputKey>(static_cast<int>(InputKey::Digit0)+key-Qt::Key_0);
    if(key>=Qt::Key_F1&&key<=Qt::Key_F12)return static_cast<InputKey>(static_cast<int>(InputKey::F1)+key-Qt::Key_F1);
    if(key>=Qt::Key_F13&&key<=Qt::Key_F24)return static_cast<InputKey>(static_cast<int>(InputKey::F13)+key-Qt::Key_F13);
    switch(key) {
    case Qt::Key_Escape:return InputKey::Escape;
    case Qt::Key_Return:case Qt::Key_Enter:return InputKey::Enter;
    case Qt::Key_Space:return InputKey::Space;
    case Qt::Key_Tab:return InputKey::Tab;
    case Qt::Key_Backspace:return InputKey::Backspace;
    case Qt::Key_Delete:return InputKey::Delete;
    case Qt::Key_Insert:return InputKey::Insert;
    case Qt::Key_Home:return InputKey::Home;
    case Qt::Key_End:return InputKey::End;
    case Qt::Key_PageUp:return InputKey::PageUp;
    case Qt::Key_PageDown:return InputKey::PageDown;
    case Qt::Key_Left:return InputKey::Left;
    case Qt::Key_Right:return InputKey::Right;
    case Qt::Key_Up:return InputKey::Up;
    case Qt::Key_Down:return InputKey::Down;
    case Qt::Key_Shift:return InputKey::Shift;
    case Qt::Key_Control:return InputKey::Control;
    case Qt::Key_Alt:return InputKey::Alt;
    case Qt::Key_AltGr:return InputKey::RightAlt;
    case Qt::Key_Meta:return InputKey::Super;
    case Qt::Key_CapsLock:return InputKey::CapsLock;
    case Qt::Key_Minus: return InputKey::Minus;
    case Qt::Key_Equal: return InputKey::Equal;
    case Qt::Key_BracketLeft: return InputKey::LeftBracket;
    case Qt::Key_BracketRight: return InputKey::RightBracket;
    case Qt::Key_Backslash: return InputKey::Backslash;
    case Qt::Key_Semicolon: return InputKey::Semicolon;
    case Qt::Key_Apostrophe: return InputKey::Apostrophe;
    case Qt::Key_QuoteLeft: return InputKey::Grave;
    case Qt::Key_Comma: return InputKey::Comma;
    case Qt::Key_Period: return InputKey::Period;
    case Qt::Key_Slash: return InputKey::Slash;
    case Qt::Key_Print: return InputKey::PrintScreen;
    case Qt::Key_ScrollLock: return InputKey::ScrollLock;
    case Qt::Key_Pause: return InputKey::Pause;
    case Qt::Key_NumLock: return InputKey::NumLock;
    case Qt::Key_Menu: return InputKey::Menu;
    default:return InputKey::None;
    }
}
bool Button(Qt::MouseButton button,GameSproutRuntime::InputMouseButton& result) {
    switch(button) {
    case Qt::LeftButton:result=GameSproutRuntime::InputMouseButton::Left;return true;
    case Qt::RightButton:result=GameSproutRuntime::InputMouseButton::Right;return true;
    case Qt::MiddleButton:result=GameSproutRuntime::InputMouseButton::Middle;return true;
    case Qt::BackButton:result=GameSproutRuntime::InputMouseButton::X1;return true;
    case Qt::ForwardButton:result=GameSproutRuntime::InputMouseButton::X2;return true;
    default:return false;
    }
}
RuntimeInputEvent PointerEvent(const QPointF& point, const QInputEvent& source) {
    RuntimeInputEvent event;
    event.Type = InputEventType::MouseMove;
    event.X = static_cast<float>(point.x());
    event.Y = static_cast<float>(point.y());
    event.Timestamp = static_cast<std::uint64_t>(source.timestamp()) * 1000000;
    return event;
}

bool IsPrintable(const QString& text) {
    return !text.isEmpty() && std::none_of(text.begin(), text.end(), [](QChar value) {
        return value.unicode() < 0x20 || value.unicode() == 0x7f;
    });
}

quint32 NativeKeyId(const QKeyEvent& event) {
    return event.nativeScanCode() ? event.nativeScanCode() : event.nativeVirtualKey();
}
}

void GameViewport::SetGameplayInputEnabled(bool enabled) {
    if(_GameplayInputEnabled==enabled)return;
    _GameplayInputEnabled=enabled;
    setAttribute(Qt::WA_AcceptTouchEvents, enabled);
    setAttribute(Qt::WA_TabletTracking, enabled);
    if(!enabled){ReleaseGameInput();clearFocus();}
    // Starting/resuming never steals keyboard focus from another editor panel.
}
void GameViewport::ReleaseGameInput() {
    if(_InputCaptured&&_Runtime) {
        GameSproutRuntime::RuntimeInputEvent event;event.Type=GameSproutRuntime::InputEventType::FocusLost;
        _Runtime->SubmitInputEvent(event);
    }
    if (_InputCaptured && QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    _InputCaptured=false;
    _PressedNativeKeys.clear();
    ResetInputWindow();
}

bool GameViewport::AcquireGameInput() {
    if (_InputCaptured) return true;
    if (!_GameplayInputEnabled || !_Runtime || !hasFocus()) return false;
    _InputCaptured = _Runtime->SubmitInputEvent({InputEventType::FocusGained});
    if (_InputCaptured && QCoreApplication::instance()) QCoreApplication::instance()->installEventFilter(this);
    return _InputCaptured;
}
void GameViewport::focusInEvent(QFocusEvent* event) { QWidget::focusInEvent(event); AcquireGameInput(); }
void GameViewport::focusOutEvent(QFocusEvent* event) {ReleaseGameInput();QWidget::focusOutEvent(event);}
void GameViewport::hideEvent(QHideEvent* event) {
    ReleaseGameInput();
    if(_Runtime)_Runtime->SetInputViewportMetrics(nullptr,{});
    QWidget::hideEvent(event);
}
void GameViewport::keyPressEvent(QKeyEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::keyPressEvent(event);return;}
    if(event->key()==Qt::Key_Escape){ReleaseGameInput();clearFocus();event->accept();return;}
    const auto key = Key(*event);
    const auto time = static_cast<std::uint64_t>(event->timestamp()) * 1000000;
    if (key != InputKey::None && !event->isAutoRepeat()) {
        if (const auto id = NativeKeyId(*event)) _PressedNativeKeys.emplace(id, key);
        RuntimeInputEvent input{InputEventType::KeyDown};
        input.Key = key;
        input.Timestamp = time;
        _Runtime->SubmitInputEvent(input);
    }
    const auto modifiers = event->modifiers();
    if (_TextInputActive && IsPrintable(event->text()) && !(modifiers & Qt::MetaModifier) &&
        (!(modifiers & Qt::ControlModifier) || (modifiers & (Qt::AltModifier | Qt::GroupSwitchModifier)))) {
        RuntimeInputEvent input{InputEventType::TextInput};
        input.Text = event->text().toUtf8().toStdString();
        input.Timestamp = time;
        _Runtime->SubmitInputEvent(input);
    }
    event->accept();
}
void GameViewport::keyReleaseEvent(QKeyEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::keyReleaseEvent(event);return;}
    if(event->isAutoRepeat()){event->accept();return;}
    auto key = Key(*event);
    if (const auto id = NativeKeyId(*event)) {
        if (const auto pressed = _PressedNativeKeys.find(id); pressed != _PressedNativeKeys.end()) {
            // The active layout or modifiers may have changed while the key
            // was pressed. Release the control reported on the down edge.
            key = pressed->second;
            _PressedNativeKeys.erase(pressed);
            for (const auto& entry : _PressedNativeKeys) {
                if (entry.second == key) { event->accept(); return; }
            }
        }
    }
    if(key==GameSproutRuntime::InputKey::None){QWidget::keyReleaseEvent(event);return;}
    RuntimeInputEvent input{InputEventType::KeyUp};
    input.Key=key;
    input.Timestamp = static_cast<std::uint64_t>(event->timestamp()) * 1000000;
    _Runtime->SubmitInputEvent(input);event->accept();
}
void GameViewport::mousePressEvent(QMouseEvent* event) {
    if(!_GameplayInputEnabled||!_Runtime){QWidget::mousePressEvent(event);return;}
    if (IsEmulatedPointer(*event)) { event->accept(); return; }
    setFocus(Qt::MouseFocusReason);
    // The widget may already own Qt focus from Edit mode; in that case Qt
    // emits no new FocusIn. An explicit game click still acquires gameplay input.
    if(!AcquireGameInput()){QWidget::mousePressEvent(event);return;}
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::MouseButtonDown;
    if(!Button(event->button(),input.Button)){QWidget::mousePressEvent(event);return;}
    if (_CursorMode != InputCursorMode::Locked) UpdatePointerFromMouse(event);
    input.Timestamp = static_cast<std::uint64_t>(event->timestamp()) * 1000000;
    _Runtime->SubmitInputEvent(input);event->accept();
}
void GameViewport::mouseReleaseEvent(QMouseEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::mouseReleaseEvent(event);return;}
    if (IsEmulatedPointer(*event)) { event->accept(); return; }
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::MouseButtonUp;
    if(!Button(event->button(),input.Button)){QWidget::mouseReleaseEvent(event);return;}
    if (_CursorMode != InputCursorMode::Locked) UpdatePointerFromMouse(event);
    input.Timestamp = static_cast<std::uint64_t>(event->timestamp()) * 1000000;
    _Runtime->SubmitInputEvent(input);event->accept();
}
void GameViewport::mouseMoveEvent(QMouseEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::mouseMoveEvent(event);return;}
    if (!IsEmulatedPointer(*event)) UpdatePointerFromMouse(event);
    event->accept();
}

void GameViewport::UpdatePointerFromMouse(QMouseEvent* event) {
    const auto point = MousePosition(event);
    auto input = PointerEvent(point, *event);
    if (_CursorMode == InputCursorMode::Locked) {
        const auto center = rect().center();
        const auto delta = point - QPointF(center);
        if (delta.isNull()) return;
        QCursor::setPos(mapToGlobal(center));
        input.Type = InputEventType::MouseRelativeMove;
        input.DeltaX = static_cast<float>(delta.x());
        input.DeltaY = static_cast<float>(delta.y());
    } else if (_CursorMode == InputCursorMode::Confined && !rect().contains(point.toPoint())) {
        if (rect().isEmpty()) return;
        const QPoint clamped(std::clamp(point.toPoint().x(), 0, width() - 1),
                             std::clamp(point.toPoint().y(), 0, height() - 1));
        QCursor::setPos(mapToGlobal(clamped));
        input = PointerEvent(clamped, *event);
    }
    _Runtime->SubmitInputEvent(input);
}

void GameViewport::wheelEvent(QWheelEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::wheelEvent(event);return;}
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::MouseWheel;
    input.WheelX=event->angleDelta().x()/120.f;input.WheelY=event->angleDelta().y()/120.f;
    input.Timestamp = static_cast<std::uint64_t>(event->timestamp()) * 1000000;
    _Runtime->SubmitInputEvent(input);event->accept();
}

bool GameViewport::focusNextPrevChild(bool next) {
    return _InputCaptured&&_GameplayInputEnabled?false:QWidget::focusNextPrevChild(next);
}
