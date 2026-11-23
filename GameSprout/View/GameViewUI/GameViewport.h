#pragma once
#include "RuntimeInputApi.h"
#include "RuntimeRenderApi.h"
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <map>
class QInputMethodEvent;
class QScreen;
class QTabletEvent;
class QTouchEvent;
class QWindow;

// Native viewport and focused gameplay input adapter; RuntimeHost owns all clocks.
class GameViewport final : public QWidget,public GameSproutRuntime::IRuntimeListener {
    Q_OBJECT
public:
    explicit GameViewport(QWidget* parent=nullptr);
    ~GameViewport() override;
    void Initialize(GameSproutRuntime::IGameSproutRuntime*,GameSproutRuntime::IRuntimeResourceProvider*);
    void RenderGame(GameSproutRuntime::IScene*,bool continuous=false,bool animateMaterials=false);
    void SetInputScene(GameSproutRuntime::IScene*);
    void RequestRender(){_NeedsRender=true;}
    bool NeedsRender() const{return _NeedsRender;}
    void OnSceneVisualChanged(std::uint64_t sceneID) override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {}
    void Shutdown();
    void RetryRendering();
    void SetGameplayInputEnabled(bool enabled);
    void SetTransformPreview(std::uint64_t objectID,bool active);
    void ClearTransformPreview();
    void ReleaseGameInput();
    bool IsRenderingAvailable() const {return _Renderer!=nullptr;}
    GameSproutRuntime::Vector2 GetOutputSize() const {return _OutputSize;}
    std::function<void(const std::string&)> StatusChanged;
signals:
    void FrameSubmitted();
    void FrameStreamReset();
    void OutputSizeChanged();
protected:
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    QPaintEngine* paintEngine() const override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void hideEvent(QHideEvent*) override;
    bool focusNextPrevChild(bool next) override;
private:
    bool EnsureRendering();
    void DestroyGPU();
    void ObserveNativeWindow();
    void ObserveScreen();
    void UpdateInputViewportGeometry();
    void Status(const std::string&);
    void SetOutputSize(GameSproutRuntime::Vector2 size);
    static bool Headless();
    bool AcquireGameInput();
    bool HandleTouchEvent(QTouchEvent*);
    bool HandleTabletEvent(QTabletEvent*);
    static bool InputWindowOutput(void*, const GameSproutRuntime::InputOutputCommand&);
    bool SetGameCursorMode(GameSproutRuntime::InputCursorMode);
    void ResetInputWindow();
    void UpdatePointerFromMouse(QMouseEvent*);
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    GameSproutRuntime::IRuntimeResourceProvider* _Resources=nullptr;
    GameSproutRuntime::IRuntimeViewport* _Renderer=nullptr;
    QWindow* _NativeWindow=nullptr;
    QWindow* _HostWindow=nullptr;
    QPointer<QScreen> _ObservedScreen;
    QMetaObject::Connection _ScreenOrientationChanged;
    QMetaObject::Connection _ScreenDpiChanged;
    QTimer _ResizeSettle;
    bool _GameplayInputEnabled=false;
    bool _InputCaptured=false;
    bool _TextInputActive=false;
    QRectF _TextInputArea;
    int _TextInputCursor=0;
    std::map<quint32, GameSproutRuntime::InputKey> _PressedNativeKeys;
    GameSproutRuntime::InputCursorMode _CursorMode=GameSproutRuntime::InputCursorMode::Normal;
    bool _Initialized=false;
    bool _Attempted=false;
    bool _Rendering=false;
    bool _NeedsRender=true,_RuntimeSubscribed=false;
    bool _AnimatedMaterials=false;
    std::uint64_t _SceneID=0;
    std::uint64_t _TransformPreviewID=0;
    bool _TransformPreviewActive=false;
    GameSproutRuntime::RuntimeInputViewportMetrics _InputViewport;
    GameSproutRuntime::Vector2 _OutputSize;
    std::string _Status;
};
