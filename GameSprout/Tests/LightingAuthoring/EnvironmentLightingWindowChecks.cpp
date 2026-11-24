#include "EnvironmentLightingWindow.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
class Authoring final:public ILightingAuthoring {
public:
    EnvironmentLightingSnapshot Value;
    std::vector<ILightingEnvironmentListener*> Listeners;
    mutable int Reads=0;
    int Writes=0,Unregisters=0;
    bool CanCreate(std::uint64_t,std::uint64_t,std::string&) const override{return false;}
    bool CreateLight(std::uint64_t,std::uint64_t,GameSproutRuntime::LightType,std::uint64_t&,std::string&) override{return false;}
    bool ReadLight(const ObjectEditTarget&,GameSproutRuntime::LightSettings&,std::string&)const override{return false;}
    bool SetLight(const ObjectEditTarget&,const GameSproutRuntime::LightSettings&,std::string&)override{return false;}
    bool RequestShadowRefresh(const ObjectEditTarget&,std::string&)override{return false;}
    bool ReadEnvironment(EnvironmentLightingSnapshot& out,std::string& error)const override{++Reads;out=Value;error.clear();return true;}
    bool SetEnvironment(std::uint64_t id,const GameSproutRuntime::EnvironmentLightingSettings& value,std::string& error)override {
        if(id!=Value.SceneID||!Value.Available||!Value.Editable||!GameSproutRuntime::IsValidEnvironmentLighting(value)) {error="Rejected environment transaction";return false;}
        ++Writes;Value.Settings=value;Publish();return true;
    }
    bool RegisterEnvironmentListener(ILightingEnvironmentListener* listener)override{Listeners.push_back(listener);return true;}
    void UnregisterEnvironmentListener(ILightingEnvironmentListener* listener)override{++Unregisters;std::erase(Listeners,listener);}
    void Publish(){for(auto* listener:Listeners)listener->OnEnvironmentLightingChanged(Value);}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        Authoring authoring;authoring.Value.SceneID=1;authoring.Value.Available=authoring.Value.Editable=true;
        authoring.Value.Settings.Mode=GameSproutRuntime::AmbientLightMode::Color;
        EnvironmentLightingWindow window;Check(window.Bind(&authoring),"Environment event subscription failed");
        window.show();window.resize(360,440);QCoreApplication::processEvents();
        Check(authoring.Reads==1,"Window must read exactly one startup snapshot");
        auto* atmosphere=window.findChild<QGroupBox*>("atmosphereGroup");
        auto* drawSky=window.findChild<QCheckBox*>("drawSkyCheck");
        auto* fogStart=window.findChild<QLineEdit*>("fogStartEdit");
        auto* fogEnd=window.findChild<QLineEdit*>("fogEndEdit");
        auto* fogOpacity=window.findChild<QLineEdit*>("fogMaxOpacityEdit");
        auto* zenithRed=window.findChild<QLineEdit*>("zenithRedEdit");
        Check(atmosphere&&drawSky&&fogStart&&fogEnd&&fogOpacity&&zenithRed,"Designer atmosphere controls are missing");
        auto* mode=window.findChild<QComboBox*>("modeCombo");auto* intensity=window.findChild<QLineEdit*>("intensityEdit");
        auto* red=window.findChild<QLineEdit*>("ambientRedEdit");auto* error=window.findChild<QLabel*>("errorLabel");
        Check(mode&&intensity&&red&&error,"Designer environment controls are missing");
        red->setText("3.5");QMetaObject::invokeMethod(red,"editingFinished");
        Check(authoring.Writes==1&&authoring.Value.Settings.AmbientColor.R==3.5f,"Linear HDR color did not reach authoring");
        const int beforeInvalid=authoring.Writes;red->setText("nan");QMetaObject::invokeMethod(red,"editingFinished");
        Check(authoring.Writes==beforeInvalid&&!error->isHidden(),"Invalid input reached business service");
        atmosphere->setChecked(true);drawSky->setChecked(false);
        Check(authoring.Value.Settings.Atmosphere.Enabled&&!authoring.Value.Settings.Atmosphere.DrawSky,"Sky toggles did not reach authoring");
        zenithRed->setText("0.25");QMetaObject::invokeMethod(zenithRed,"editingFinished");
        fogStart->setText("75");QMetaObject::invokeMethod(fogStart,"editingFinished");
        fogEnd->setText("900");QMetaObject::invokeMethod(fogEnd,"editingFinished");
        fogOpacity->setText("0.6");QMetaObject::invokeMethod(fogOpacity,"editingFinished");
        Check(authoring.Value.Settings.Atmosphere.ZenithColor.R==.25f&&authoring.Value.Settings.Atmosphere.FogStart==75&&
            authoring.Value.Settings.Atmosphere.FogEnd==900&&authoring.Value.Settings.Atmosphere.FogMaxOpacity==.6f,"Atmosphere values did not reach authoring");
        const auto beforeBadFog=authoring.Writes;
        fogEnd->setText("70");QMetaObject::invokeMethod(fogEnd,"editingFinished");
        fogOpacity->setText("1.1");QMetaObject::invokeMethod(fogOpacity,"editingFinished");
        Check(authoring.Writes==beforeBadFog&&!error->isHidden(),"Invalid fog bounds reached authoring");
        authoring.Value.Settings.Atmosphere.FogEnd=1100;authoring.Publish();
        Check(fogEnd->text()=="1100"&&authoring.Reads==1,"Atmosphere snapshot event did not refresh without polling");
        intensity->setText("2.5");QMetaObject::invokeMethod(intensity,"editingFinished");
        Check(authoring.Value.Settings.Intensity==2.5f,"Intensity did not reach authoring");
        authoring.Value.Settings.Intensity=1e10f;authoring.Publish();
        Check(intensity->text().toDouble()==1e10,"HDR intensity was clamped for display");
        authoring.Value.Settings.Intensity=2.5f;authoring.Publish();
        auto* skyEnvironment=window.findChild<QWidget*>("skyEnvironmentField");
        auto* skyRotation=window.findChild<QLineEdit*>("skyRotationEdit");
        auto* skyIntensity=window.findChild<QLineEdit*>("skyIntensityEdit");
        auto* reflection=window.findChild<QLineEdit*>("reflectionIntensityEdit");
        auto* hdr=window.findChild<QCheckBox*>("hdrCheck");
        auto* exposure=window.findChild<QLineEdit*>("exposureEVEdit");
        auto* tonemap=window.findChild<QComboBox*>("tonemapCombo");
        auto* spotCount=window.findChild<QComboBox*>("spotShadowLightsCombo");
        auto* spotResolution=window.findChild<QComboBox*>("spotShadowResolutionCombo");
        Check(skyEnvironment&&skyRotation&&skyIntensity&&reflection&&hdr&&exposure&&tonemap,
            "Designer skybox, reflection and HDR controls are missing");
        Check(!exposure->isEnabled()&&!tonemap->isEnabled(),"HDR-dependent controls are active in SDR");
        Check(spotCount&&spotCount->currentIndex()==2&&spotResolution&&spotResolution->currentIndex()==2,
            "Spot shadow budget controls did not load the defaults");
        spotCount->setCurrentIndex(4);spotResolution->setCurrentIndex(1);
        Check(authoring.Value.Settings.SpotShadows==GameSproutRuntime::SpotShadowBudgetSettings{4,512},
            "Spot shadow budget edits did not reach authoring");
        mode->setCurrentIndex(int(GameSproutRuntime::AmbientLightMode::SkyEnvironment));
        skyRotation->setText("-35");QMetaObject::invokeMethod(skyRotation,"editingFinished");
        skyIntensity->setText("1.5");QMetaObject::invokeMethod(skyIntensity,"editingFinished");
        reflection->setText("0.25");QMetaObject::invokeMethod(reflection,"editingFinished");
        hdr->setChecked(true);
        exposure->setText("-1.25");QMetaObject::invokeMethod(exposure,"editingFinished");
        tonemap->setCurrentIndex(int(GameSproutRuntime::ToneMapping::ACES));
        Check(authoring.Value.Settings.Mode==GameSproutRuntime::AmbientLightMode::SkyEnvironment&&
            authoring.Value.Settings.SkyRotationDegrees==-35&&authoring.Value.Settings.SkyIntensity==1.5f&&
            authoring.Value.Settings.ReflectionIntensity==.25f&&authoring.Value.Settings.HDREnabled&&
            authoring.Value.Settings.ExposureEV==-1.25f&&authoring.Value.Settings.Tonemap==GameSproutRuntime::ToneMapping::ACES,
            "Skybox, reflection or HDR settings failed to reach authoring");
        const auto beforeBadExposure=authoring.Writes;
        exposure->setText("17");QMetaObject::invokeMethod(exposure,"editingFinished");
        Check(authoring.Writes==beforeBadExposure&&!error->isHidden(),"Invalid exposure reached authoring");
        authoring.Value.Settings.ExposureEV=1.75;
        authoring.Value.Settings.SkyEnvironment={"abcdef1234567890abcdef1234567890",7};
        authoring.Publish();
        auto* clearResource=skyEnvironment->findChild<QAction*>("clearResourceAction");
        Check(exposure->text()=="1.75"&&clearResource&&clearResource->isEnabled()&&authoring.Reads==1,
            "Source snapshot did not refresh HDR controls and the resource field");
        clearResource->trigger();
        Check(authoring.Value.Settings.SkyEnvironment.Guid.empty(),"Clearing the environment field did not reach authoring");
        mode->setCurrentIndex(int(GameSproutRuntime::AmbientLightMode::Gradient));QCoreApplication::processEvents();
        Check(window.findChild<QWidget*>("skyRow")->isVisibleTo(&window)&&window.findChild<QWidget*>("ambientRow")->isHidden(),"Mode-specific rows are incorrect");
        authoring.Value.Settings.Mode=GameSproutRuntime::AmbientLightMode::SphericalHarmonics;
        authoring.Value.Settings.SHCoefficients[5]={-.25f,.5f,.75f};authoring.Publish();
        auto* toggle=window.findChild<QToolButton*>("coefficientsToggle");auto* table=window.findChild<QTableWidget*>("coefficientsTable");
        Check(toggle&&table&&table->isHidden(),"SH advanced data must start collapsed");toggle->click();QCoreApplication::processEvents();
        Check(table->isVisibleTo(&window)&&table->editTriggers()==QAbstractItemView::NoEditTriggers&&table->item(5,0)->text()=="-0.25","Signed SH display or readonly behavior is incorrect");
        for(QWidget* field:{static_cast<QWidget*>(mode),static_cast<QWidget*>(intensity),static_cast<QWidget*>(table)}) {
            const QRect bounds(field->mapTo(&window,QPoint(0,0)),field->size());
            Check(bounds.left()>=0&&bounds.right()<window.width(),"Environment controls exceed a narrow window");
        }
        Check(window.grab().save("/tmp/gamesprout-environment-window.png"),"Cannot capture environment Designer form");
        // An external scene switch must cancel unfinished text, not commit the
        // old world's focused value against the new scene identity.
        authoring.Value.Settings.Mode=GameSproutRuntime::AmbientLightMode::Color;authoring.Publish();
        red->setFocus();red->setText("9");const int beforeSwitch=authoring.Writes;
        authoring.Value.SceneID=2;authoring.Value.Settings.AmbientColor.R=.125f;authoring.Publish();QCoreApplication::processEvents();
        Check(authoring.Writes==beforeSwitch&&red->text()=="0.125","Scene switch committed old text to new scene");
        authoring.Value.Available=authoring.Value.Editable=false;authoring.Publish();
        Check(!window.findChild<QGroupBox*>("settingsGroup")->isEnabled(),"Unavailable/Prefab environment remains editable");
        Check(!atmosphere->isEnabled()&&!skyEnvironment->isEnabled()&&!hdr->isEnabled(),"Unavailable atmosphere, resource or HDR remains editable");
        Check(!spotCount->isEnabled()&&!spotResolution->isEnabled(),"Unavailable scene shadow budget remains editable");
        const int reads=authoring.Reads;for(int i=0;i<4;++i)QCoreApplication::processEvents();
        Check(authoring.Reads==reads,"Idle UI is querying the business module");
        window.Shutdown();Check(authoring.Listeners.empty()&&authoring.Unregisters==1,"Environment listener outlives window");
        authoring.Publish();
        std::cout<<"PASS environment Designer UI, event refresh, scene identity and input validation\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
