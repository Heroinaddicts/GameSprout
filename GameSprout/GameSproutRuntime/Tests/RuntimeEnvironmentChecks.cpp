#include "GameSproutRuntimeExport.h"
#include "RuntimeEnvironmentApi.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
using namespace GameSproutRuntime;

void Check(bool passed,const char* message) {
    if(!passed)throw std::runtime_error(message);
}
bool Near(double actual,double expected) {return std::abs(actual-expected)<.0001;}

EnvironmentSnapshot Resolve(IEnvironmentRuntime& environment,const IScene& scene,Vector3 position={}) {
    EnvironmentSnapshot snapshot;
    Check(environment.ResolveEnvironment(scene,position,snapshot),"Environment state unavailable");
    return snapshot;
}
float Value(const EnvironmentSnapshot& snapshot,std::string_view target,std::string_view parameter) {
    const auto* value=FindEnvironmentValue<float>(snapshot,target,parameter);
    if(!value)throw std::runtime_error("Missing float parameter "+std::string(target)+"/"+std::string(parameter));
    return *value;
}

class SceneListener final:public IRuntimeListener {
public:
    void OnSceneNodeEvent(const SceneNodeEvent& event) override {
        if(event.Type==SceneNodeEventType::EnvironmentSettingsChanged)++Changes;
    }
    unsigned Changes=0;
};

void CheckComposition(IGameSproutRuntime& runtime,IEnvironmentRuntime& environment,IScene& scene,IScene& other) {
    const auto* authoring=QueryEnvironmentScene(scene);
    Check(authoring&&authoring->GetAuthoredEnvironmentSettings()==EnvironmentSettings{}&&
        Resolve(environment,scene).Values.empty(),"New scene modified legacy appearance");
    const auto available=environment.GetEnvironmentEffects();
    for(const auto type:{"GameSprout.DayNight","GameSprout.Starfield","GameSprout.CloudLayer"}) {
        const auto found=std::find_if(available.begin(),available.end(),[&](const auto& effect){return effect.Type==type;});
        Check(found!=available.end()&&found->Stage==EnvironmentEffectStage::Builtin&&
            !environment.UnregisterEnvironmentEffect(type),"Missing or removable built-in effect");
    }
    Check(environment.RegisterEnvironmentParameter({"Test.Particles",2.f})&&
        !environment.RegisterEnvironmentParameter({"Test.Particles",2.f})&&
        environment.RegisterEnvironmentParameter({"Test.Gust",0.f})&&
        environment.RegisterEnvironmentParameter({"Test.Toggle",false})&&
        !environment.RegisterEnvironmentParameter({"Test.Invalid",false,EnvironmentInterpolation::Angle}),
        "Parameter registration accepted a duplicate or a mismatched interpolation type");

    EnvironmentSettings settings;
    const ResourceReference auroraImage{"9e3b4034d099539cc69df5e27c2b88ec",1};
    settings.Effects={{"day","GameSprout.DayNight"},{"stars","GameSprout.Starfield"},
        {"third","GameSprout.CloudLayer"},{"aurora","Test.Aurora"}};
    settings.Layers={
        {"local",{{"","Test.Particles",12.f},{"","Fog.Strength",.8f}},30,1,EnvironmentRegion{{},{2,1,2},2}},
        {"base",{{"","Test.Particles",10.f},{"third","Coverage",.4f}},10,.5f},
        {"lit",{{"day","Daylight",.2f}},20},
        {"adjust",{{"","Sky.Daylight",.6f}},21,.5f},
        {"aurora",{{"aurora","Strength",.8f},{"aurora","Image",auroraImage}},21,.5f},
        {"order-b",{{"","Lighting.AmbientMultiplier",2.f}},30},
        {"order-a",{{"","Lighting.AmbientMultiplier",3.f}},30}
    };
    settings.Curves={
        {"gust","","Test.Gust",{{0,0.f},{2,8.f}},true,40},
        {"star-rotation","stars","RotationDegrees",{{0,350.f},{2,10.f}},false,40},
        {"step","","Test.Toggle",{{0,false},{2,true}},false,40}
    };
    SceneListener listener;
    Check(runtime.RegisterListener(&listener),"Cannot observe the authoring operation");
    const auto revision=scene.GetAuthoringRevision();
    Check(environment.SetEnvironmentSettings(scene,settings)&&environment.SetEnvironmentSettings(scene,settings),
        "Valid environment settings rejected");
    runtime.ProcessPendingEvents();
    Check(listener.Changes==1&&scene.GetAuthoringRevision()>revision&&authoring->GetAuthoredEnvironmentSettings()==settings,
        "Setting changed event or saved state missing");

    const auto inside=Resolve(environment,scene);
    const auto edge=Resolve(environment,scene,{3,0,0});
    const auto outside=Resolve(environment,scene,{9,0,0});
    Check(inside.Effects.size()==3&&outside.Effects.size()==3&&
        !FindEnvironmentValue<float>(inside,"aurora","Strength"),"Unregistered effect was not stored as dormant");
    Check(Near(Value(inside,"","Test.Particles"),12)&&Near(Value(edge,"","Test.Particles"),9)&&
        Near(Value(outside,"","Test.Particles"),6)&&Near(Value(edge,"","Fog.Strength"),.4)&&
        !FindEnvironmentValue<float>(outside,"","Fog.Strength"),"Local layers ignored attenuation or changed unrelated views");
    Check(Near(Value(inside,"","Sky.Daylight"),.4)&&Near(Value(inside,"day","Daylight"),.2)&&
        Near(Value(inside,"","Lighting.AmbientMultiplier"),2)&&Near(Value(inside,"third","Coverage"),.31),
        "Sparse built-in alias or deterministic priority ordering failed");
    const auto neighboring=Resolve(environment,other);
    Check(!FindEnvironmentValue<float>(inside,"","Sky.StarIntensity")&&
        !FindEnvironmentValue<float>(neighboring,"","Sky.Daylight")&&Near(Value(neighboring,"","Test.Particles"),2),
        "Unchanged stock fields were overridden or custom defaults were unavailable across scenes");

    EnvironmentEffectDescriptor aurora;
    aurora.Type="Test.Aurora";
    aurora.Material={"f5723846d240a581843ed934a9aafd3c",1};
    aurora.Parameters={{"Strength",.2f},{"Image",ResourceReference{}}};
    Check(environment.RegisterEnvironmentEffect(aurora)&&!environment.RegisterEnvironmentEffect(aurora),
        "Cannot register a custom effect at GameLogic startup");
    const auto registered=Resolve(environment,scene);
    const auto* image=FindEnvironmentValue<ResourceReference>(registered,"aurora","Image");
    Check(registered.Effects.size()==4&&registered.Effects.back().Material==aurora.Material&&
        Near(Value(registered,"aurora","Strength"),.5)&&image&&*image==auroraImage,
        "Dormant authored effect did not activate or use typed parameters");

    const auto afterRegistration=scene.GetAuthoringRevision();
    runtime.AdvanceFrame(1,4,.25f);
    const auto half=Resolve(environment,scene);
    const auto* step=FindEnvironmentValue<bool>(half,"","Test.Toggle");
    Check(Near(half.TimeSeconds,1)&&Near(Resolve(environment,scene).TimeSeconds,1)&&
        Near(Value(half,"","Test.Gust"),4)&&Near(Value(half,"","Sky.StarRotationDegrees"),0)&&
        step&&!*step,"Curve clock advanced per view or fixed step, or angular/step interpolation failed");
    Check(Resolve(environment,other).TimeSeconds==0&&scene.GetAuthoringRevision()==afterRegistration,
        "Curve time leaked to another scene or changed authored data");
    auto adjusted=settings;
    adjusted.Layers.front().Weight=.5f;
    Check(environment.SetEnvironmentSettings(scene,adjusted)&&Near(Resolve(environment,scene).TimeSeconds,1),
        "Layer edits reset the independent curve clock");
    scene.SetSimulationEnabled(false);
    runtime.AdvanceFrame(4,0,0);
    Check(Near(Resolve(environment,scene).TimeSeconds,1)&&environment.AdvanceEnvironmentPreview(scene,1),
        "Paused scene ticked automatically or could not preview explicitly");
    const auto complete=Resolve(environment,scene);
    step=FindEnvironmentValue<bool>(complete,"","Test.Toggle");
    Check(Near(complete.TimeSeconds,2)&&Near(Value(complete,"","Test.Gust"),0)&&
        Near(Value(complete,"","Sky.StarRotationDegrees"),10)&&step&&*step,"Looped or clamped curve endpoint is wrong");
    scene.SetSimulationEnabled(true);

    auto invalid=adjusted;
    invalid.Layers[1].Overrides[0].Value=true;
    Check(!environment.SetEnvironmentSettings(scene,invalid),"Invalid parameter type accepted");
    invalid=adjusted;
    invalid.Curves[0].Keys.back().Seconds=0;
    Check(!environment.SetEnvironmentSettings(scene,invalid),"Duplicated keyframe time accepted");
    invalid=adjusted;
    invalid.Layers[0].Weight=std::numeric_limits<float>::quiet_NaN();
    Check(!environment.SetEnvironmentSettings(scene,invalid)&&authoring->GetAuthoredEnvironmentSettings()==adjusted,
        "Invalid sparse profile modified live data");
    invalid=adjusted;
    invalid.Layers[4].Overrides[1].Value=ResourceReference{"not-a-guid",1};
    Check(!environment.SetEnvironmentSettings(scene,invalid),"Resource reference cannot be serialized safely");
    Check(environment.UnregisterEnvironmentEffect(aurora.Type)&&
        !FindEnvironmentValue<float>(Resolve(environment,scene),"aurora","Strength")&&
        authoring->GetAuthoredEnvironmentSettings()==adjusted,"Unregistering a type erased the authored profile");
    runtime.UnregisterListener(&listener);
}

void CheckEvents(IGameSproutRuntime& runtime,IEnvironmentRuntime& environment,IScene& scene,IScene& other) {
    auto* weather=QueryWeatherRuntime(runtime);
    Check(weather,"Legacy weather API unavailable");
    const auto eventTypes=environment.GetEnvironmentEvents();
    Check(eventTypes.size()==1&&eventTypes.front().Type==EnvironmentLightningEventType&&
        !environment.UnregisterEnvironmentEvent(eventTypes.front().Type),"Built-in lightning absent from event catalog");
    Check(environment.RegisterEnvironmentEvent({"Test.Eclipse",{{"Duration",0.f}}})&&
        !environment.RegisterEnvironmentEvent({"Test.Eclipse",{}}),"Custom event registration failed");
    unsigned legacyEvents=0,independentEvents=0,eclipses=0;
    std::uint64_t lastSequence=0;
    bool nextQueued=false,ordered=true;
    const auto observer=environment.SubscribeEnvironmentEvents([&](IScene& owner,const EnvironmentEvent& event) {
        ordered&=event.Sequence>lastSequence;
        lastSequence=event.Sequence;
        if(event.Type==EnvironmentLightningEventType) {
            ++independentEvents;
            if(independentEvents==1)nextQueued=environment.EmitEnvironmentEvent(owner,{"Test.Eclipse",0,{{"","Duration",3.f}}});
        } else if(event.Type=="Test.Eclipse")++eclipses;
    });
    const auto legacy=weather->SubscribeLightning([&](IScene&,const WeatherLightningEvent&){++legacyEvents;});
    Check(observer&&legacy,"Weather consumers could not subscribe");
    Check(!environment.EmitEnvironmentEvent(scene,{"Test.Eclipse",42,{{"","Duration",true}}}),
        "Custom event dispatcher accepted an incompatible payload");
    Check(!weather->TriggerLightning(other,{}),"Legacy opt-in behavior changed");
    const auto type=std::string(EnvironmentLightningEventType);
    Check(environment.EmitEnvironmentEvent(other,{type,0,{{"","DurationSeconds",.8f},{"","SeedHigh16",65535.f}}}),
        "Composable lightning required enabling legacy weather");
    Check(Resolve(environment,scene).Lightning.empty()&&Resolve(environment,other).Lightning.size()==1&&
        Resolve(environment,other).Lightning[0].Strike.Seed==0xffff0000u&&independentEvents==0,
        "A scene read advanced, dispatched or changed an active strike");
    runtime.AdvanceFrame(.1f,1,.1f);
    Check(independentEvents==1&&legacyEvents==1&&eclipses==0&&nextQueued,
        "Lightning duplicated, lost its legacy event, or emitted recursively");
    runtime.ProcessPendingEvents();
    Check(eclipses==1&&ordered,"Independent custom event was not dispatched in order");

    WeatherSettings compatible;
    compatible.Enabled=true;
    Check(weather->SetWeatherSettings(other,compatible),"Cannot enable legacy weather");
    WeatherLightning newer;
    newer.DurationSeconds=.1f;
    newer.Seed=123;
    Check(weather->TriggerLightning(other,newer),"Legacy trigger failed");
    const auto overlap=Resolve(environment,other);
    Check(overlap.Lightning.size()==2&&overlap.Lightning[0].Flash<1&&overlap.Lightning[1].Flash==1&&
        overlap.Lightning[0].Sequence<overlap.Lightning[1].Sequence,"New strike overwrote aging lightning");
    runtime.AdvanceFrame(.1f,0,0);
    WeatherState oldState;
    Check(Resolve(environment,other).Lightning.size()==1&&weather->GetWeatherState(other,oldState)&&
        oldState.LightningFlash==0&&oldState.Lightning.Seed==123&&independentEvents==2&&legacyEvents==2,
        "Independent expiry or legacy latest-strike mirror is incorrect");
    runtime.AdvanceFrame(1,0,0);
    Check(Resolve(environment,other).Lightning.empty()&&independentEvents==2&&legacyEvents==2,
        "Expired strike lingered or re-emitted");
    weather->UnsubscribeLightning(legacy);
    environment.UnsubscribeEnvironmentEvents(observer);
    Check(environment.UnregisterEnvironmentEvent("Test.Eclipse"),"Cannot unregister custom event");
}
}

int main(int argc,char** argv) {
    try {
        Check(argc==2,"Runtime directory required");
        std::unique_ptr<IGameSproutRuntime> runtime(Export(argv[1]));
        Check(bool(runtime),"Runtime failed to load");
        auto* environment=QueryEnvironmentRuntime(*runtime);
        Check(environment,"Environment Runtime interface unavailable across the SDK boundary");
        auto* scene=runtime->CreateScene("Environment");
        auto* other=runtime->CreateScene("Isolated");
        Check(scene&&other,"Cannot create scenes");
        CheckComposition(*runtime,*environment,*scene,*other);
        CheckEvents(*runtime,*environment,*scene,*other);
        Check(runtime->DestroyScene(scene)&&runtime->DestroyScene(other),"Cannot destroy environment scenes");
        std::cout<<"Runtime environment composition checks passed\n";
    } catch(const std::exception& exception) {
        std::cerr<<exception.what()<<'\n';
        return 1;
    }
}
