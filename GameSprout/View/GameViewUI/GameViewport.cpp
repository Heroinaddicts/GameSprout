#include "GameViewport.h"
#include "../../Tools/FrameTiming.h"
#include <QEvent>
#include <QGuiApplication>
#include <QPaintEngine>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QWindow>

namespace {
GameSproutRuntime::InputViewportOrientation InputOrientation(const QScreen& screen) {
    using GameSproutRuntime::InputViewportOrientation;
    const auto orientation=screen.orientation()==Qt::PrimaryOrientation ? screen.primaryOrientation() : screen.orientation();
    switch (orientation) {
    case Qt::LandscapeOrientation: return InputViewportOrientation::Landscape;
    case Qt::InvertedLandscapeOrientation: return InputViewportOrientation::LandscapeFlipped;
    case Qt::PortraitOrientation: return InputViewportOrientation::Portrait;
    case Qt::InvertedPortraitOrientation: return InputViewportOrientation::PortraitFlipped;
    default: return InputViewportOrientation::Unknown;
    }
}
}

bool GameViewport::Headless() {
    const auto platform=QGuiApplication::platformName();
    return platform=="offscreen"||platform.startsWith("minimal");
}
GameViewport::GameViewport(QWidget* parent):QWidget(parent) {
    _ResizeSettle.setSingleShot(true);
    _ResizeSettle.setInterval(150);
    connect(&_ResizeSettle,&QTimer::timeout,this,&GameViewport::RequestRender);
    if (!Headless()) {
        setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);
    }
}
GameViewport::~GameViewport(){Shutdown();}
QPaintEngine* GameViewport::paintEngine() const {return Headless()?QWidget::paintEngine():nullptr;}
void GameViewport::Initialize(GameSproutRuntime::IGameSproutRuntime* runtime,
    GameSproutRuntime::IRuntimeResourceProvider* resources) {
    Shutdown();_Runtime=runtime;_Resources=resources;_Initialized=true;
    if(_Runtime) {
        _RuntimeSubscribed=_Runtime->RegisterListener(this);
        _Runtime->SetInputOutputHandler(GameSproutRuntime::InputOutputTarget::Window,
            &GameViewport::InputWindowOutput,this);
    }
    RequestRender();
}
void GameViewport::Status(const std::string& text) {
    if (_Status==text) return;
    _Status=text;if(StatusChanged)StatusChanged(text);
}
void GameViewport::SetOutputSize(GameSproutRuntime::Vector2 size) {
    if (size.X == _OutputSize.X && size.Y == _OutputSize.Y) return;
    _OutputSize = size;
    emit OutputSizeChanged();
}
void GameViewport::DestroyGPU() {
    auto* renderer=_Renderer;_Renderer=nullptr;
    _AnimatedMaterials=false;
    _Attempted=false;
    RequestRender();
    emit FrameStreamReset();
    delete renderer;
}
void GameViewport::Shutdown(){
    if(_RuntimeSubscribed&&_Runtime)_Runtime->UnregisterListener(this);
    _RuntimeSubscribed=false;_SceneID=0;
    ClearTransformPreview();
    _ResizeSettle.stop();
    SetGameplayInputEnabled(false);
    if(_Runtime)_Runtime->SetInputOutputHandler(GameSproutRuntime::InputOutputTarget::Window,nullptr,nullptr);
    if(_Runtime)_Runtime->SetInputViewportMetrics(nullptr,{});
    _Initialized=false;DestroyGPU();
    if(_NativeWindow){_NativeWindow->removeEventFilter(this);QObject::disconnect(_NativeWindow,nullptr,this,nullptr);_NativeWindow=nullptr;}
    if(_HostWindow){_HostWindow->removeEventFilter(this);QObject::disconnect(_HostWindow,nullptr,this,nullptr);_HostWindow=nullptr;}
    QObject::disconnect(_ScreenOrientationChanged);
    QObject::disconnect(_ScreenDpiChanged);
    _ObservedScreen=nullptr;
    _InputViewport={};
    _Runtime=nullptr;_Resources=nullptr;
}
void GameViewport::RetryRendering(){DestroyGPU();_Attempted=false;}
void GameViewport::SetTransformPreview(std::uint64_t objectID,bool active) {
    _TransformPreviewID=objectID;
    _TransformPreviewActive=active;
    RequestRender();
}
void GameViewport::ClearTransformPreview() {
    _TransformPreviewID=0;
    _TransformPreviewActive=false;
}
void GameViewport::OnSceneVisualChanged(std::uint64_t sceneID){if(sceneID==_SceneID)RequestRender();}
void GameViewport::SetInputScene(GameSproutRuntime::IScene* scene) {
    const auto sceneID=scene ? scene->GetInstanceID() : 0;
    if(sceneID==_SceneID)return;
    _SceneID=sceneID;
    if(scene)UpdateInputViewportGeometry();
    else if(_Runtime)_Runtime->SetInputViewportMetrics(nullptr,{});
    RequestRender();
    emit FrameStreamReset();
}
void GameViewport::UpdateInputViewportGeometry() {
    if (width() <= 0 || height() <= 0) return;
    auto& metrics=_InputViewport;
    const GameSproutRuntime::Vector2 logicalSize{float(width()),float(height())};
    const float ratio=float(devicePixelRatioF());
    if (metrics.ContentRect.Size.X!=logicalSize.X || metrics.ContentRect.Size.Y!=logicalSize.Y || metrics.PixelRatio!=ratio) {
        metrics.ContentRect.Size=logicalSize;
        metrics.PixelRatio=ratio;
        metrics.RenderSize={float(qRound(width()*ratio)),float(qRound(height()*ratio))};
    }
    const auto* display=screen();
    metrics.Orientation=display ? InputOrientation(*display) : GameSproutRuntime::InputViewportOrientation::Unknown;
    metrics.DisplayDpi=display ? float(display->physicalDotsPerInch()*display->devicePixelRatio()) : 0;
    metrics.SafeArea={};
#if QT_VERSION >= QT_VERSION_CHECK(6,9,0)
    if (auto* host=window();host && host->windowHandle()) {
        const auto safe=host->rect().marginsRemoved(host->windowHandle()->safeAreaMargins());
        const auto local=QRect(mapFrom(host,safe.topLeft()),safe.size()).intersected(rect());
        if (!local.isEmpty()) metrics.SafeArea={{float(local.x()),float(local.y())},{float(local.width()),float(local.height())}};
    }
#endif
    if (_Runtime && _SceneID && isVisible()) {
        if (auto* root=_Runtime->QueryGameObject(_SceneID)) _Runtime->SetInputViewportMetrics(root->GetScene(),metrics);
    }
    SetOutputSize(metrics.RenderSize);
}
void GameViewport::ObserveScreen() {
    auto* display=screen();
    if (display==_ObservedScreen) return;
    QObject::disconnect(_ScreenOrientationChanged);
    QObject::disconnect(_ScreenDpiChanged);
    _ObservedScreen=display;
    if (display) {
        _ScreenOrientationChanged=connect(display,&QScreen::orientationChanged,this,&GameViewport::UpdateInputViewportGeometry);
        _ScreenDpiChanged=connect(display,&QScreen::physicalDotsPerInchChanged,this,&GameViewport::UpdateInputViewportGeometry);
    }
}
void GameViewport::ObserveNativeWindow() {
    const auto observe=[this](QWindow* handle){
        if (!handle) return;
        handle->installEventFilter(this);
        connect(handle,&QWindow::screenChanged,this,[this]{ObserveScreen();UpdateInputViewportGeometry();RequestRender();});
#if QT_VERSION >= QT_VERSION_CHECK(6,9,0)
        connect(handle,&QWindow::safeAreaMarginsChanged,this,&GameViewport::UpdateInputViewportGeometry);
#endif
    };
    QWindow* native=windowHandle();
    if(native!=_NativeWindow){
        if(_NativeWindow){_NativeWindow->removeEventFilter(this);QObject::disconnect(_NativeWindow,nullptr,this,nullptr);}
        _NativeWindow=native;
        observe(native);
        if(native)QObject::connect(native,&QObject::destroyed,this,[this,native]{if(_NativeWindow==native)_NativeWindow=nullptr;});
    }
    QWindow* host=window()?window()->windowHandle():nullptr;
    if(host==native)host=nullptr;
    if(host!=_HostWindow){
        if(_HostWindow){_HostWindow->removeEventFilter(this);QObject::disconnect(_HostWindow,nullptr,this,nullptr);}
        _HostWindow=host;
        observe(host);
        if(host)QObject::connect(host,&QObject::destroyed,this,[this,host]{if(_HostWindow==host)_HostWindow=nullptr;});
    }
    ObserveScreen();
}
bool GameViewport::event(QEvent* event) {
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        if (HandleTouchEvent(static_cast<QTouchEvent*>(event))) return true;
        break;
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
        if (HandleTabletEvent(static_cast<QTabletEvent*>(event))) return true;
        break;
    case QEvent::ShortcutOverride:
        if (_InputCaptured) { event->accept(); return true; }
        break;
    default:
        break;
    }
    const bool sizeChanged = event->type() == QEvent::Resize || event->type() == QEvent::Show ||
        event->type() == QEvent::ScreenChangeInternal
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
        || event->type() == QEvent::DevicePixelRatioChange
#endif
        ;
    if(event->type()==QEvent::Resize&&_Renderer)_ResizeSettle.start();
    if (event->type()==QEvent::WinIdChange) {RetryRendering();if(_Initialized)ObserveNativeWindow();}
    if (event->type()==QEvent::PlatformSurface) {
        const auto type=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        if(type==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)DestroyGPU();
        _Attempted=false;
        RequestRender();
    }
    if (sizeChanged || event->type() == QEvent::Paint || event->type() == QEvent::Expose ||
        event->type() == QEvent::WindowActivate) RequestRender();
    const bool handled = QWidget::event(event);
    if (sizeChanged || event->type() == QEvent::Move) {
        if (_Initialized && (event->type() == QEvent::Show || event->type() == QEvent::ScreenChangeInternal)) ObserveNativeWindow();
        UpdateInputViewportGeometry();
    }
    return handled;
}
bool GameViewport::eventFilter(QObject* watched,QEvent* event) {
    if (watched == QCoreApplication::instance() &&
        (event->type() == QEvent::TabletEnterProximity || event->type() == QEvent::TabletLeaveProximity))
        HandleTabletEvent(static_cast<QTabletEvent*>(event));
    if((watched==_NativeWindow||watched==_HostWindow)&&event->type()==QEvent::WindowDeactivate){ReleaseGameInput();clearFocus();}
    if((watched==_NativeWindow||watched==_HostWindow)&&event->type()==QEvent::PlatformSurface){
        const auto type=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        // Native QWindow receives this before its platform handle is destroyed, including host teardown/reparenting.
        if(type==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)DestroyGPU();
        else if(type==QPlatformSurfaceEvent::SurfaceCreated){_Attempted=false;RequestRender();}
    }
    if((watched==_NativeWindow||watched==_HostWindow)&&
       (event->type()==QEvent::Expose||event->type()==QEvent::WindowActivate||event->type()==QEvent::ScreenChangeInternal))RequestRender();
    return QWidget::eventFilter(watched,event);
}
bool GameViewport::EnsureRendering() {
    if (_Renderer) return true;
    if (_Attempted) return false;
    _Attempted=true;
    if (!_Runtime||!_Resources) {Status("Game 渲染服务尚未就绪。");return false;}
    const auto platform=QGuiApplication::platformName();
#if defined(__APPLE__)
    const bool supported=platform=="cocoa";
#elif defined(_WIN32)
    const bool supported=platform=="windows";
#else
    const bool supported=platform=="xcb";
#endif
    if (!supported) {Status("当前平台没有可用的 Game 原生窗口句柄。");return false;}
    const WId id=winId();ObserveNativeWindow();_Attempted=true;
    if(!id){Status("Game 原生窗口尚未创建。");return false;}
    GameSproutRuntime::NativeWindow window;
#if defined(__APPLE__)
    window.System=GameSproutRuntime::NativeWindowSystem::Cocoa;
#elif defined(_WIN32)
    window.System=GameSproutRuntime::NativeWindowSystem::Win32;
#else
    window.System=GameSproutRuntime::NativeWindowSystem::X11;
#endif
    window.Handle=static_cast<std::uintptr_t>(id);
    std::string error;_Renderer=_Runtime->CreateViewport(window,_Resources,error);
    if(!_Renderer){DestroyGPU();_Attempted=true;Status(error);return false;}
    return true;
}
void GameViewport::RenderGame(GameSproutRuntime::IScene* scene,bool continuous,bool animateMaterials) {
    SetInputScene(scene);
    if(!_NeedsRender&&!continuous&&!(animateMaterials&&_AnimatedMaterials))return;
    if (!_Initialized||!isVisible()||width()<=0||height()<=0||_Rendering) return;
    if(!Headless()&&(!window()||window()->isMinimized()||!window()->windowHandle()||!window()->windowHandle()->isExposed()))return;
    _NeedsRender=false;
    GameSproutRuntime::RenderView view;
    view.Scene=scene;view.DrawGrid=false;
    view.DrawUI=scene!=nullptr;
    view.WaitForSurface=false;
    view.TransformPreviewObjectID=_TransformPreviewID;
    view.ReuseShadowsWhileNavigating=_ResizeSettle.isActive();
    view.EnableTexturePrefetch=true;
    std::string diagnostic;
    auto* camera=scene?scene->GetMainCamera():nullptr;
    if(!scene)diagnostic="尚无活动场景。";
    else if(!camera||!camera->GetGameObject()->IsActiveInHierarchy())diagnostic="场景中没有启用的 Main Camera。";
    else if(!camera->GetPresentationViewMatrix(view.Camera.View))diagnostic="Main Camera 姿态无法生成有效视图。";
    else {
        view.Camera.Projection=camera->GetSettings();
        const auto world=camera->GetGameObject()->GetTransform()->GetPresentationWorldMatrix();
        view.Camera.Position={world.Data[12],world.Data[13],world.Data[14]};
    }
    // Runtime owns target clearing as well as scene drawing; the View never records content commands.
    view.DrawScene=diagnostic.empty();
    if (Headless()) {Status(diagnostic.empty()?"离屏环境：Game 使用 Main Camera，与 Scene 共用动画姿态。":diagnostic);return;}
    if (!EnsureRendering()) return;
    GAMESPROUT_FRAME_SCOPE(GameView, "view/game");
    _Rendering=true;
    struct Scope{bool& Flag;~Scope(){Flag=false;}} scope{_Rendering};
    if (!_Renderer->Render(view)) {_AnimatedMaterials=false;Status(_Renderer->GetLastError());return;}
    const auto& statistics=_Renderer->GetStatistics();
    if(statistics.TexturePrefetchPending)RequestRender();
    if(!statistics.Width||!statistics.Height) {
        RequestRender();
        return;
    }
    GAMESPROUT_FRAME_MARK_NAMED("Game View");
    if(_TransformPreviewID&&!_TransformPreviewActive) {
        // Keep the final edited pose in the preview layer for one presented frame.
        // The follow-up frame returns to the authored shadow policy even when paused.
        _TransformPreviewID=0;
        RequestRender();
    }
    const GameSproutRuntime::Vector2 renderedSize{float(statistics.Width), float(statistics.Height)};
    if (renderedSize.X!=_InputViewport.RenderSize.X || renderedSize.Y!=_InputViewport.RenderSize.Y) {
        _InputViewport.RenderSize=renderedSize;
        if(scene)_Runtime->SetInputViewportMetrics(scene,_InputViewport);
        SetOutputSize(renderedSize);
    }
    emit FrameSubmitted();
    _AnimatedMaterials=statistics.RequiresContinuousRendering;
    if(!diagnostic.empty()){
        Status(statistics.UIDrawCalls ? diagnostic+" 已显示屏幕 UI。" : diagnostic);
        return;
    }
    std::string status=std::to_string(statistics.Width)+" × "+std::to_string(statistics.Height)+" px · "+
        std::to_string(statistics.DrawCalls)+" draws · "+std::to_string(statistics.Triangles)+" triangles";
    if(statistics.InstancedDrawCalls)status+=" · instanced "+std::to_string(statistics.InstancedInstances)+" · saved "+std::to_string(statistics.SavedDrawCalls)+" draws";
    if(statistics.ReflectionProbeDrawCalls)status+=" · 局部反射 "+std::to_string(statistics.ReflectionProbeDrawCalls)+" draws";
    if(statistics.PlanarReflectionWidth)status+=" · 平面反射 "+std::to_string(statistics.PlanarReflectionWidth)+" × "+
        std::to_string(statistics.PlanarReflectionHeight)+" px / "+std::to_string(statistics.PlanarReflectionDrawCalls)+" draws";
    if(statistics.UIDrawCalls)status+=" · UI "+std::to_string(statistics.UIDrawCalls)+" draws";
    Status(status);
}
