#include "GameViewport.h"
#include "GameSproutRuntimeApi.h"
#include "GameSproutRuntimeExport.h"
#include "ui_GameViewUI.h"
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void Check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
class InputService final:public GameSproutRuntime::IGameSproutRuntime {
public:
    std::vector<GameSproutRuntime::RuntimeInputEvent> Events;
    bool Focused=false;
    GameSproutRuntime::IGameSproutRuntime* Real=nullptr;
    GameSproutRuntime::InputOutputHandler WindowOutput=nullptr;
    void* WindowOutputContext=nullptr;
    void SetInputOutputHandler(GameSproutRuntime::InputOutputTarget target, GameSproutRuntime::InputOutputHandler handler,
                               void* context) override {
        if (Real) { Real->SetInputOutputHandler(target,handler,context); return; }
        if (target != GameSproutRuntime::InputOutputTarget::Window) return;
        if (WindowOutput) WindowOutput(WindowOutputContext, {});
        WindowOutput = handler;
        WindowOutputContext = context;
    }
    bool SendInputOutput(const GameSproutRuntime::InputOutputCommand& command) override {
        return Real ? Real->SendInputOutput(command) : WindowOutput && WindowOutput(WindowOutputContext,command);
    }
    bool SubmitInputEvent(const GameSproutRuntime::RuntimeInputEvent& event)override{
        if(Real&&!Real->SubmitInputEvent(event))return false;
        if(event.Type==GameSproutRuntime::InputEventType::FocusGained)Focused=true;
        if(event.Type==GameSproutRuntime::InputEventType::FocusLost)Focused=false;
        Events.push_back(event);return true;
    }
    GameSproutRuntime::IRuntimeViewport* CreateViewport(const GameSproutRuntime::NativeWindow&,GameSproutRuntime::IRuntimeResourceProvider*,std::string&)override{return nullptr;}
    GameSproutRuntime::IRuntimeViewport* CreateOffscreenRenderer(uint32_t,uint32_t,GameSproutRuntime::IRuntimeResourceProvider*,std::string&)override{return nullptr;}
    GameSproutRuntime::IScene* CreateScene(const std::string&)override{return nullptr;}
    bool DestroyScene(GameSproutRuntime::IScene*)override{return false;}
    std::size_t GetSceneCount()const override{return 0;}
    GameSproutRuntime::IScene* GetScene(std::size_t)const override{return nullptr;}
    GameSproutRuntime::IGameObject* QueryGameObject(uint64_t id)const override{return Real?Real->QueryGameObject(id):nullptr;}
    void SetInputViewportMetrics(GameSproutRuntime::IScene* scene, const GameSproutRuntime::RuntimeInputViewportMetrics& metrics) override {
        if(Real)Real->SetInputViewportMetrics(scene,metrics);
    }
    GameSproutRuntime::RuntimeProjectSettings FallbackSettings;
    const GameSproutRuntime::RuntimeProjectSettings& GetProjectSettings() const override {
        return Real?Real->GetProjectSettings():FallbackSettings;
    }
    bool CanApplyProjectSettings(const GameSproutRuntime::RuntimeProjectSettings& value,std::string& error) const override {
        return Real?Real->CanApplyProjectSettings(value,error):GameSproutRuntime::ValidateProjectSettings(value,error);
    }
    bool SetProjectSettings(const GameSproutRuntime::RuntimeProjectSettings& value,std::string& error) override {
        if(Real)return Real->SetProjectSettings(value,error);
        if(!CanApplyProjectSettings(value,error))return false;
        FallbackSettings=value;return true;
    }
    GameSproutRuntime::IGameObject* FindGameObjectWithTag(const std::string& name,const GameSproutRuntime::IScene* scene=nullptr) const override {
        return Real?Real->FindGameObjectWithTag(name,scene):nullptr;
    }
    std::vector<GameSproutRuntime::IGameObject*> FindGameObjectsWithTag(const std::string& name,const GameSproutRuntime::IScene* scene=nullptr) const override {
        return Real?Real->FindGameObjectsWithTag(name,scene):std::vector<GameSproutRuntime::IGameObject*>{};
    }
    bool RegisterListener(GameSproutRuntime::IRuntimeListener*)override{return true;}
    void UnregisterListener(GameSproutRuntime::IRuntimeListener*)override{}
    bool IsExecuting()const override{return false;}
    bool BeginConstruction(std::string& error)override{error.clear();return true;}
    bool EndConstruction(std::string& error)override{error.clear();return true;}
    bool EndFrame(std::string& error)override{error.clear();return true;}
    void Update(float)override{}
    void AdvanceFrame(float seconds,std::uint32_t fixedSteps,float fixedSeconds)override{
        if(Real)Real->AdvanceFrame(seconds,fixedSteps,fixedSeconds);
    }
    void Render()override{}
};
static void Pump(){QCoreApplication::processEvents();}

static void CheckViewportMetrics(QWidget& window, GameViewport& game, GameSproutRuntime::IGameSproutRuntime& runtime) {
    using namespace GameSproutRuntime;
    auto* scene=runtime.CreateScene("Input viewport metrics");
    Check(scene,"Could not create a scene for viewport metrics");
    game.SetInputScene(scene);
    const auto initial=runtime.GetInputViewportMetrics();
    Check(initial.ContentRect.Position.X == 0 && initial.ContentRect.Position.Y == 0 &&
          initial.ContentRect.Size.X == game.width() && initial.ContentRect.Size.Y == game.height(),
          "Game did not publish the scene's logical input rectangle before its first render");
    struct Listener final : IRuntimeInputViewportListener {
        std::vector<RuntimeInputViewportMetrics> Changes;
        void OnInputViewportChanged(const RuntimeInputViewportMetrics& metrics) override { Changes.push_back(metrics); }
    } listener;
    Check(runtime.RegisterInputViewportListener(&listener),"Could not subscribe to viewport metric changes");
    window.resize(812,497);
    Pump();
    runtime.UnregisterInputViewportListener(&listener);
    const auto resized=runtime.GetInputViewportMetrics();
    Check(resized.Revision > initial.Revision && resized.ContentRect.Size.X == game.width() &&
          resized.ContentRect.Size.Y == game.height() && resized.RenderSize.X == qRound(game.width()*game.devicePixelRatioF()) &&
          resized.RenderSize.Y == qRound(game.height()*game.devicePixelRatioF()) && resized.PixelRatio == float(game.devicePixelRatioF()),
          "Game resize mixed logical input pixels and drawable pixels");
    Check(!listener.Changes.empty() && std::all_of(listener.Changes.begin(),listener.Changes.end(),[](const auto& value) {
        return value.ContentRect.Size.X>0 && value.ContentRect.Size.Y>0;
    }),"Game resize temporarily cleared live input viewport metrics");
}

static void CheckDeviceRouting(GameViewport* game, InputService& runtime) {
    using namespace GameSproutRuntime;
    Check(runtime.SetTextInputArea({10,20},{100,16},22) && runtime.StartTextInput(), "Game IME output unavailable");
    QInputMethodQueryEvent query(Qt::ImEnabled | Qt::ImCursorRectangle);
    QCoreApplication::sendEvent(game,&query);
    Check(query.value(Qt::ImEnabled).toBool() && query.value(Qt::ImCursorRectangle).toRectF().topLeft() == QPointF(32,20),
          "IME candidate rectangle did not follow caret offset");

    QInputMethodEvent composition(QString::fromUtf8("a😀中"), {{QInputMethodEvent::Cursor,3,1,{}}});
    QCoreApplication::sendEvent(game,&composition);
    Check(runtime.Events.back().Type == InputEventType::TextComposition && runtime.Events.back().TextStart == 2,
          "IME caret was not converted to Unicode code points");
    QInputMethodEvent commit;
    commit.setCommitString(QString::fromUtf8("世界"));
    QCoreApplication::sendEvent(game,&commit);
    Check(runtime.Events[runtime.Events.size()-2].Type == InputEventType::TextInput &&
          runtime.Events[runtime.Events.size()-2].Text == "世界" && runtime.Events.back().Text.empty(),
          "IME commit or composition clear was lost");
    if (runtime.Real) Check(runtime.Real->GetInputState().TextInput == "世界" &&
                           runtime.Real->GetInputState().TextComposition.empty(), "Native Runtime lost IME state");

    const auto point = QPoint(25,36);
    auto* touch = QTest::createTouchDevice();
    QTest::touchEvent(game,touch).press(0,point,game);
    Check(runtime.Events.back().Type == InputEventType::TouchBegan && runtime.Events.back().ContactId == 1 &&
          runtime.Events.back().X == point.x() && runtime.Events.back().Y == point.y(), "Logical touch begin was lost");
    const auto beforeMouse = runtime.Events.size();
    QMouseEvent synthesized(QEvent::MouseButtonPress,point,point,game->mapToGlobal(point),Qt::LeftButton,
                            Qt::LeftButton,Qt::NoModifier,Qt::MouseEventSynthesizedByQt);
    QCoreApplication::sendEvent(game,&synthesized);
    Check(runtime.Events.size() == beforeMouse, "Touch also generated a mouse click");
    QTest::touchEvent(game,touch).release(0,point,game);
    Check(runtime.Events.back().Type == InputEventType::TouchEnded && runtime.Events.back().ContactId == 1,
          "Qt touch ID changed across begin and end");

    QPointingDevice pen("Input test pen",471,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,1,3);
    QTabletEvent press(QEvent::TabletPress,&pen,point,game->mapToGlobal(point),.5,11,22,0,33,0,
                       Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);
    QCoreApplication::sendEvent(game,&press);
    Check(runtime.Events.back().Type == InputEventType::PenDown && runtime.Events.back().Pressure == .5f &&
          runtime.Events.back().TiltX == 11 && runtime.Events.back().Twist == 33, "Tablet pressure, tilt or rotation was lost");
    Check(runtime.SetCursorMode(InputCursorMode::Hidden) && game->cursor().shape() == Qt::BlankCursor,
          "Game did not hide the viewport cursor");
}

int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        InputService runtime;
        if(argc>1){runtime.Real=Export(argv[1]);Check(runtime.Real,"Cannot load native Runtime");std::string error;Check(runtime.Real->BeginExecution(error),"Cannot begin native execution");}
        QWidget window;Ui::GameViewUIWindow ui;ui.setupUi(&window);window.resize(640,360);
        auto* game=ui.gameViewport;game->Initialize(&runtime,nullptr);
        window.show();window.activateWindow();Pump();
        if(runtime.Real)CheckViewportMetrics(window,*game,*runtime.Real);
        QTest::keyClick(game,Qt::Key_W);Check(runtime.Events.empty(),"Edit input entered Runtime");
        game->setFocus();Pump();
        game->SetGameplayInputEnabled(true);Check(runtime.Events.empty(),"Play stole input focus");
        QTest::mouseClick(game,Qt::LeftButton,Qt::NoModifier,QPoint(25,25));Check(runtime.Focused,"Already-focused Edit viewport could not capture on Play click");
        game->ReleaseGameInput();runtime.Events.clear();
        ui.playButton->setFocus();Pump();QTest::keyClick(ui.playButton,Qt::Key_W);Check(runtime.Events.empty(),"Editor toolbar keys entered game");
        QTest::mousePress(game,Qt::LeftButton,Qt::NoModifier,QPoint(80,60));
        Check(runtime.Focused&&game->hasFocus(),"Game click did not acquire focus");
        Check(runtime.Events.size()>=3&&runtime.Events[0].Type==GameSproutRuntime::InputEventType::FocusGained,"Game input missing focus gate");
        Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::MouseButtonDown,"Mouse down not forwarded");
        Check(runtime.Events[runtime.Events.size()-2].X==80&&runtime.Events[runtime.Events.size()-2].Y==60,"Mouse position is not logical local pixels");
        QTest::mouseRelease(game,Qt::LeftButton,Qt::NoModifier,QPoint(80,60));
        QTest::keyPress(game,Qt::Key_W);Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::KeyDown&&runtime.Events.back().Key==GameSproutRuntime::InputKey::W,"W mapping failed");
        if(runtime.Real)Check(runtime.Real->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held,"Native script input did not receive focused W");
        auto count=runtime.Events.size();QKeyEvent repeat(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier,"w",true);QCoreApplication::sendEvent(game,&repeat);Check(runtime.Events.size()==count,"Auto-repeat produced extra key transition");
        QTest::keyRelease(game,Qt::Key_W);Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::KeyUp,"Key release not forwarded");
        QTest::keyClick(game,Qt::Key_Tab);Check(runtime.Focused&&game->hasFocus()&&runtime.Events.back().Key==GameSproutRuntime::InputKey::Tab,"Tab escaped game capture");
        QWheelEvent wheel(QPointF(80,60),QPointF(game->mapToGlobal(QPoint(80,60))),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(game,&wheel);
        Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::MouseWheel&&runtime.Events.back().WheelY==1,"Wheel detent conversion failed");
        CheckDeviceRouting(game,runtime);
        QTest::keyClick(game,Qt::Key_Escape);Check(!runtime.Focused&&!game->hasFocus()&&runtime.Events.back().Type==GameSproutRuntime::InputEventType::FocusLost,"Escape failed to release input");
        Check(game->cursor().shape() != Qt::BlankCursor && !game->testAttribute(Qt::WA_InputMethodEnabled),
              "Focus loss kept hidden cursor or enabled IME");
        if(runtime.Real)Check(!runtime.Real->GetInputState().Focused&&!runtime.Real->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held,"Native input stuck after Escape");
        count=runtime.Events.size();QTest::keyClick(game,Qt::Key_W);Check(runtime.Events.size()==count,"Keys reached game after Escape");
        QTest::mouseClick(game,Qt::LeftButton,Qt::NoModifier,QPoint(30,40));Check(runtime.Focused,"Click did not recapture");
        game->SetGameplayInputEnabled(false);Check(!runtime.Focused&&!game->hasFocus(),"Pause did not release focus/keys");
        count=runtime.Events.size();QTest::keyClick(game,Qt::Key_A);QTest::mouseClick(game,Qt::RightButton);Check(runtime.Events.size()==count,"Paused input reached Runtime");
        game->SetGameplayInputEnabled(true);Check(runtime.Events.size()==count,"Resume stole focus");
        QTest::mouseClick(game,Qt::LeftButton);Check(runtime.Focused,"Resume click not accepted");
        ui.playButton->setFocus();Pump();Check(!runtime.Focused,"Focus loss left stuck game input");
        QTest::mouseClick(game,Qt::LeftButton);game->hide();Pump();Check(!runtime.Focused,"Hide left stuck game input");
        if(runtime.Real)Check(runtime.Real->GetInputViewportMetrics().ContentRect.Size.X==0,"Hide left an active input viewport");
        game->Shutdown();
        if(runtime.Real){std::string error;Check(runtime.Real->EndExecution(error),"Cannot end native execution");delete runtime.Real;runtime.Real=nullptr;}
        std::cout<<"PASS focused gameplay keyboard, mouse, touch and pen routing, IME, cursor output, pause, focus loss and hide\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
