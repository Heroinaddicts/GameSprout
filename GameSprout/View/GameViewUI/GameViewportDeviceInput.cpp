#include "GameViewport.h"
#include "GameSproutRuntimeApi.h"
#include <QCursor>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <algorithm>

namespace {
using namespace GameSproutRuntime;

void SetPosition(RuntimeInputEvent& input, const QPointF& position) {
    input.X = static_cast<float>(position.x());
    input.Y = static_cast<float>(position.y());
}

void SubmitText(IGameSproutRuntime& runtime, InputEventType type, const QString& text, int start = 0) {
    RuntimeInputEvent input{type};
    input.Text = text.toUtf8().toStdString();
    input.TextStart = start;
    runtime.SubmitInputEvent(input);
}

bool IsMouseGrabMode(InputCursorMode mode) {
    return mode == InputCursorMode::Locked || mode == InputCursorMode::Confined;
}

bool CanWarpCursor() {
    // Qt does not expose Wayland's pointer-constraints protocol to QWidget. Its
    // cursor position setter is a no-op there and on non-interactive platforms.
    const auto platform = QGuiApplication::platformName();
    return platform == "cocoa" || platform == "windows" || platform == "xcb";
}
}

bool GameViewport::HandleTouchEvent(QTouchEvent* event) {
    if (!_GameplayInputEnabled || !_Runtime) return false;
    if (event->type() == QEvent::TouchBegin) {
        setFocus(Qt::MouseFocusReason);
        AcquireGameInput();
    }
    if (!_InputCaptured) return false;

    RuntimeInputEvent input;
    input.Timestamp = static_cast<std::uint64_t>(event->timestamp()) * 1000000;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    if (const auto* device = event->pointingDevice())
        input.DeviceId = static_cast<std::uint64_t>(device->systemId()) + 1;
#else
    if (const auto* device = event->device())
        input.DeviceId = static_cast<std::uint64_t>(qHash(device->name())) + 1;
#endif
    if (!input.DeviceId) input.DeviceId = 1;
    // Qt may omit the points when the operating system cancels a gesture.
    if (event->type() == QEvent::TouchCancel) {
        input.Type = InputEventType::TouchCanceled;
        _Runtime->SubmitInputEvent(input);
        event->accept();
        return true;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    for (const auto& point : event->points()) {
        switch (point.state()) {
        case QEventPoint::Pressed: input.Type = InputEventType::TouchBegan; break;
        case QEventPoint::Updated: input.Type = InputEventType::TouchMoved; break;
        case QEventPoint::Released: input.Type = InputEventType::TouchEnded; break;
        default: continue;
        }
        SetPosition(input, point.position());
#else
    for (const auto& point : event->touchPoints()) {
        switch (point.state()) {
        case Qt::TouchPointPressed: input.Type = InputEventType::TouchBegan; break;
        case Qt::TouchPointMoved: input.Type = InputEventType::TouchMoved; break;
        case Qt::TouchPointReleased: input.Type = InputEventType::TouchEnded; break;
        default: continue;
        }
        SetPosition(input, point.pos());
#endif
        input.ContactId = static_cast<std::uint64_t>(point.id()) + 1;
        input.Pressure = static_cast<float>(point.pressure());
        _Runtime->SubmitInputEvent(input);
    }
    // Accept the native touch so Qt does not also synthesize a mouse click.
    event->accept();
    return true;
}

bool GameViewport::HandleTabletEvent(QTabletEvent* event) {
    if (!_GameplayInputEnabled || !_Runtime) return false;
    if (event->type() == QEvent::TabletPress && !_InputCaptured) {
        setFocus(Qt::MouseFocusReason);
        AcquireGameInput();
    }
    if (!_InputCaptured) return false;

    RuntimeInputEvent input;
    switch (event->type()) {
    case QEvent::TabletEnterProximity: input.Type = InputEventType::PenProximityIn; break;
    case QEvent::TabletLeaveProximity: input.Type = InputEventType::PenProximityOut; break;
    case QEvent::TabletPress: input.Type = InputEventType::PenDown; break;
    case QEvent::TabletRelease: input.Type = InputEventType::PenUp; break;
    case QEvent::TabletMove: input.Type = InputEventType::PenMove; break;
    default: return false;
    }
    input.Timestamp = static_cast<std::uint64_t>(event->timestamp()) * 1000000;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    if (const auto* device = event->pointingDevice()) {
        const auto uniqueId = device->uniqueId().numericId();
        input.DeviceId = static_cast<std::uint64_t>(uniqueId >= 0 ? uniqueId : device->systemId()) + 1;
    }
    const bool eraser = event->pointerType() == QPointingDevice::PointerType::Eraser;
#else
    input.DeviceId = static_cast<std::uint64_t>(event->uniqueId()) + 1;
    const bool eraser = event->pointerType() == QTabletEvent::Eraser;
#endif
    if (!input.DeviceId) input.DeviceId = 1;
    if (input.Type == InputEventType::PenMove || input.Type == InputEventType::PenDown || input.Type == InputEventType::PenUp) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        SetPosition(input, event->position());
#else
        SetPosition(input, event->posF());
#endif
        input.Pressure = static_cast<float>(event->pressure());
        input.TiltX = static_cast<float>(event->xTilt());
        input.TiltY = static_cast<float>(event->yTilt());
        input.Twist = static_cast<float>(event->rotation());
        if (input.Type != InputEventType::PenUp && ((event->buttons() & Qt::LeftButton) || input.Pressure > 0))
            input.Capabilities |= static_cast<std::uint32_t>(InputPenButton::Tip);
        if (event->buttons() & Qt::RightButton) input.Capabilities |= static_cast<std::uint32_t>(InputPenButton::Barrel1);
        if (event->buttons() & Qt::MiddleButton) input.Capabilities |= static_cast<std::uint32_t>(InputPenButton::Barrel2);
    }
    if (eraser) input.Capabilities |= static_cast<std::uint32_t>(InputPenButton::Eraser);
    _Runtime->SubmitInputEvent(input);
    event->accept();
    return true;
}

void GameViewport::inputMethodEvent(QInputMethodEvent* event) {
    if (!_Runtime || !_InputCaptured || !_TextInputActive) { QWidget::inputMethodEvent(event); return; }
    if (!event->commitString().isEmpty()) SubmitText(*_Runtime, InputEventType::TextInput, event->commitString());

    const auto& preedit = event->preeditString();
    int cursor = static_cast<int>(preedit.size());
    for (const auto& attribute : event->attributes()) {
        if (attribute.type == QInputMethodEvent::Cursor) {
            // Qt uses UTF-16 indices. Runtime offsets count Unicode code points.
            cursor = std::clamp(attribute.start, 0, static_cast<int>(preedit.size()));
            break;
        }
    }
    SubmitText(*_Runtime, InputEventType::TextComposition, preedit, static_cast<int>(preedit.left(cursor).toUcs4().size()));
    event->accept();
}

QVariant GameViewport::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled) return _TextInputActive && _InputCaptured;
    if (query == Qt::ImCursorRectangle || query == Qt::ImAnchorRectangle) {
        const auto position = _TextInputArea.left() + std::min<double>(_TextInputCursor, _TextInputArea.width());
        return QRectF(position, _TextInputArea.top(), 1, _TextInputArea.height());
    }
    return QWidget::inputMethodQuery(query);
}

bool GameViewport::InputWindowOutput(void* context, const InputOutputCommand& command) {
    auto& viewport = *static_cast<GameViewport*>(context);
    if (command.Type == InputOutputType::Reset) {
        viewport.ResetInputWindow();
        return true;
    }
    if (!viewport._InputCaptured || !viewport._GameplayInputEnabled) return false;
    switch (command.Type) {
    case InputOutputType::SetCursorMode:
        return viewport.SetGameCursorMode(command.CursorMode);
    case InputOutputType::WarpCursor:
        if (!CanWarpCursor() || viewport._CursorMode == InputCursorMode::Locked) return false;
        if (command.X < 0 || command.X >= viewport.width() || command.Y < 0 || command.Y >= viewport.height()) return false;
        QCursor::setPos(viewport.mapToGlobal(QPoint(std::min(qRound(command.X), viewport.width() - 1),
                                                  std::min(qRound(command.Y), viewport.height() - 1))));
        return true;
    case InputOutputType::StartTextInput:
        if (viewport._TextInputActive) return true;
        viewport._TextInputActive = true;
        viewport.setAttribute(Qt::WA_InputMethodEnabled, true);
        QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImCursorRectangle);
        QGuiApplication::inputMethod()->show();
        return true;
    case InputOutputType::StopTextInput:
        if (viewport._TextInputActive) {
            viewport._TextInputActive = false;
            QGuiApplication::inputMethod()->reset();
            QGuiApplication::inputMethod()->hide();
            viewport.setAttribute(Qt::WA_InputMethodEnabled, false);
        }
        return true;
    case InputOutputType::SetTextInputArea:
        viewport._TextInputArea = QRectF(command.X, command.Y, command.Width, command.Height);
        viewport._TextInputCursor = command.Cursor;
        if (viewport._TextInputActive) QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
        return true;
    default: return false;
    }
}

bool GameViewport::SetGameCursorMode(InputCursorMode mode) {
    if (_CursorMode == mode) return true;
    if (IsMouseGrabMode(mode)) {
        if (!CanWarpCursor() || !isVisible() || rect().isEmpty()) return false;
        if (!IsMouseGrabMode(_CursorMode)) {
            if (QWidget::mouseGrabber() && QWidget::mouseGrabber() != this) return false;
            grabMouse();
            if (QWidget::mouseGrabber() != this) return false;
        }
        if (mode == InputCursorMode::Locked) QCursor::setPos(mapToGlobal(rect().center()));
    } else if (IsMouseGrabMode(_CursorMode) && QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
    if (mode == InputCursorMode::Hidden || mode == InputCursorMode::Locked) setCursor(Qt::BlankCursor);
    else unsetCursor();
    _CursorMode = mode;
    return true;
}

void GameViewport::ResetInputWindow() {
    SetGameCursorMode(InputCursorMode::Normal);
    if (_TextInputActive) {
        _TextInputActive = false;
        QGuiApplication::inputMethod()->reset();
        QGuiApplication::inputMethod()->hide();
        setAttribute(Qt::WA_InputMethodEnabled, false);
    }
    _TextInputArea = {};
    _TextInputCursor = 0;
}
