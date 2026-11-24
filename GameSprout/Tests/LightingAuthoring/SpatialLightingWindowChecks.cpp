#include "SpatialLightingWindow.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
class Authoring final : public ILightingAuthoring {
public:
    EnvironmentLightingSnapshot Value;
    ILightingEnvironmentListener* Listener=nullptr;
    mutable int Reads=0;
    int Writes=0;
    bool CanCreate(std::uint64_t,std::uint64_t,std::string&) const override{return false;}
    bool CreateLight(std::uint64_t,std::uint64_t,LightType,std::uint64_t&,std::string&) override{return false;}
    bool ReadLight(const ObjectEditTarget&,LightSettings&,std::string&)const override{return false;}
    bool SetLight(const ObjectEditTarget&,const LightSettings&,std::string&)override{return false;}
    bool RequestShadowRefresh(const ObjectEditTarget&,std::string&)override{return false;}
    bool ReadEnvironment(EnvironmentLightingSnapshot& out,std::string&)const override{++Reads;out=Value;return true;}
    bool SetEnvironment(std::uint64_t id,const EnvironmentLightingSettings& value,std::string&)override {
        if(id!=Value.SceneID||!Value.Available||!Value.Editable||!IsValidEnvironmentLighting(value))return false;
        ++Writes;Value.Settings=value;Publish();return true;
    }
    bool RegisterEnvironmentListener(ILightingEnvironmentListener* listener)override {
        if(Listener)return false;
        Listener=listener;
        return true;
    }
    void UnregisterEnvironmentListener(ILightingEnvironmentListener* listener)override {
        if(Listener==listener)Listener=nullptr;
    }
    void Publish(){if(Listener)Listener->OnEnvironmentLightingChanged(Value);}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};

class Baker final : public ILightingBaker {
public:
    LightingBakeState State;
    LightingBakeRequest Last;
    ILightingBakeListener* Listener=nullptr;
    mutable int Reads=0;
    int Starts=0,Cancels=0;
    LightingBakeState GetBakeState()const override {++Reads;return State;}
    bool RegisterBakeListener(ILightingBakeListener* listener)override {
        if(Listener)return false;
        Listener=listener;
        return true;
    }
    void UnregisterBakeListener(ILightingBakeListener* listener)override {
        if(Listener==listener)Listener=nullptr;
    }
    bool BeginBake(const LightingBakeRequest& request,std::string&)override {
        if(State.IsActive())return false;
        Last=request;++Starts;State={LightingBakePhase::Capturing,0,6,"Capture running"};Publish();
        return true;
    }
    void CancelBake()override {++Cancels;State={};Publish();}
    void Publish(){if(Listener)Listener->OnLightingBakeChanged(State);}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};

template<class Widget> Widget& Field(QWidget& parent,const char* name) {
    auto* result=parent.findChild<Widget*>(QString::fromUtf8(name));
    Check(result!=nullptr,"Expected a Designer-authored control");
    return *result;
}
void Edit(QWidget& parent,const char* name,const char* value) {
    auto& field=Field<QLineEdit>(parent,name);
    field.setText(QString::fromUtf8(value));
    Check(QMetaObject::invokeMethod(&field,"editingFinished"),"Numeric field edit failed");
}
}

int main(int argc,char* argv[]) {
    QApplication app(argc,argv);
    try {
        Authoring authoring;
        Baker baker;
        authoring.Value.SceneID=1;authoring.Value.Available=authoring.Value.Editable=true;
        SpatialLightingWindow window;
        Check(window.Bind(&authoring,nullptr,nullptr,nullptr,nullptr,&baker),"Window did not subscribe to scene snapshots");
        Check(authoring.Reads==1&&baker.Reads==1,"Window did not read precisely one startup snapshot per service");

        Field<QCheckBox>(window,"planarEnabled").setChecked(true);
        Edit(window,"planarHeight","-1.25");
        Edit(window,"planarScale","0.25");
        Check(authoring.Value.Settings.PlanarReflection.Enabled&&authoring.Value.Settings.PlanarReflection.Height==-1.25f&&
              authoring.Value.Settings.PlanarReflection.ResolutionScale==.25f,"Planar fields did not commit");
        const auto beforeInvalid=authoring.Writes;
        Edit(window,"planarStrength","1.25");
        Check(authoring.Writes==beforeInvalid&&!Field<QLabel>(window,"statusLabel").text().isEmpty(),
            "Invalid planar strength entered a transaction");

        Field<QPushButton>(window,"probeAdd").click();
        Field<QPushButton>(window,"probeAdd").click();
        auto& probes=Field<QComboBox>(window,"probeList");
        Check(probes.count()==2&&probes.currentIndex()==1,"Probe addition did not select new value");
        Edit(window,"captureX","-6");
        Edit(window,"probeSizeX","14");
        Edit(window,"probePriority","100");
        auto& settings=authoring.Value.Settings;
        Check(settings.ReflectionProbes[0].CapturePosition.X==0&&settings.ReflectionProbes[1].CapturePosition.X==-6&&
            settings.ReflectionProbes[1].Size.X==14&&settings.ReflectionProbes[1].Priority==100,"Edits touched the wrong probe");
        probes.setCurrentIndex(0);
        settings.ReflectionProbes[0].Environment={"0123456789abcdef0123456789abcdef",1};authoring.Publish();
        auto& probeField=Field<QWidget>(window,"probeAsset");
        Field<QAction>(probeField,"clearResourceAction").trigger();
        Check(settings.ReflectionProbes[0].Environment.Guid.empty(),"Clearing probe resource failed");
        Field<QPushButton>(window,"probeRemove").click();
        Check(probes.count()==1&&settings.ReflectionProbes[0].CapturePosition.X==-6,"Remove changed wrong probe");

        Field<QComboBox>(window,"probeBakeResolution").setCurrentText("32");
        Field<QLineEdit>(window,"probeCaptureDistance").setText("125");
        Field<QPushButton>(window,"probeBake").click();
        Check(baker.Starts==1&&baker.Last.SceneID==1&&baker.Last.Kind==LightingBakeKind::ReflectionProbe&&
            baker.Last.Index==0&&baker.Last.FaceSize==32&&baker.Last.ReflectionSize==32&&baker.Last.StaticOnly&&
            baker.Last.CaptureDistance==125,"Reflection bake did not receive the selected target and Designer settings");
        baker.State.CompletedFaces=3;baker.Publish();
        Check(Field<QProgressBar>(window,"bakeProgress").value()==3&&
            !Field<QPushButton>(window,"probeBake").isEnabled(),"Bake controls did not react to pushed progress");
        Field<QPushButton>(window,"cancelBake").click();
        Check(baker.Cancels==1,"Bake cancellation did not reach its service");

        Field<QPushButton>(window,"indirectAdd").click();
        Edit(window,"indirectCenterY","3.25");
        Edit(window,"indirectIntensity","2");
        Check(settings.IndirectLightVolumes.size()==1&&settings.IndirectLightVolumes[0].Center.Y==3.25f&&
            settings.IndirectLightVolumes[0].Intensity==2,"Indirect volume values did not commit");
        settings.IndirectLightVolumes[0].Asset={"fedcba9876543210fedcba9876543210",2};authoring.Publish();
        auto& indirectField=Field<QWidget>(window,"indirectAsset");
        Field<QAction>(indirectField,"clearResourceAction").trigger();
        Check(settings.IndirectLightVolumes[0].Asset.Guid.empty(),"Clearing indirect resource failed");
        Field<QSpinBox>(window,"gridX").setValue(4);
        Field<QSpinBox>(window,"gridZ").setValue(6);
        Field<QPushButton>(window,"indirectBake").click();
        Check(baker.Starts==2&&baker.Last.Kind==LightingBakeKind::IndirectVolume&&
            baker.Last.Grid==std::array<std::uint32_t,3>{4,2,6},"Indirect bake did not receive authored grid size");
        baker.State={LightingBakePhase::Complete,6,6,"Capture complete"};baker.Publish();

        const int beforeSwitch=authoring.Writes;
        Field<QLineEdit>(window,"probeCenterX").setText("7");
        authoring.Value.SceneID=2;settings={};authoring.Publish();
        Check(probes.count()==0&&authoring.Writes==beforeSwitch,"An unfinished edit crossed a scene switch");
        authoring.Value.Editable=false;authoring.Publish();
        Check(!Field<QTabWidget>(window,"featureTabs").isEnabled(),"Play retains editable controls");
        for(int frame=0;frame<3;++frame)QCoreApplication::processEvents();
        Check(authoring.Reads==1&&baker.Reads==1,"Idle dialog polled a service");
        window.Shutdown();Check(authoring.Listener==nullptr&&baker.Listener==nullptr,"Listener survived Shutdown");
        std::cout<<"PASS Designer spatial lighting fields, operations, validation and lifecycle\n";
        return 0;
    } catch(const std::exception& failure) {
        std::cerr<<"FAIL "<<failure.what()<<'\n';
        return 1;
    }
}
