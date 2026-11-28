#include "GameSproutRuntimeExport.h"
#include "RuntimeWeatherApi.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace GameSproutRuntime;

void Check(bool passed,const char* message) {
    if(!passed)throw std::runtime_error(message);
}

bool Near(float actual,float expected) {
    return std::abs(actual-expected)<.0001f;
}

WeatherState Read(IWeatherRuntime& weather,const IScene& scene) {
    WeatherState result;
    Check(weather.GetWeatherState(scene,result),"Weather state unavailable");
    return result;
}

class WeatherListener final:public IRuntimeListener {
public:
    void OnSceneNodeEvent(const SceneNodeEvent& event) override {
        if(event.Type==SceneNodeEventType::WeatherSettingsChanged)++Changes;
        if(event.Type==SceneNodeEventType::EnvironmentLightingChanged)++LightingChanges;
    }
    unsigned Changes=0,LightingChanges=0;
};

void CheckSimulation(IGameSproutRuntime& runtime,IWeatherRuntime& weather,IScene& scene,IScene& other) {
    const auto* authoredScene=QueryWeatherScene(scene);
    Check(authoredScene&&authoredScene->GetAuthoredWeatherSettings()==WeatherSettings{}&&
        !Read(weather,scene).Enabled,"Weather must be opt-in");
    Check(!weather.TriggerLightning(scene,{}),"Disabled weather accepted a strike");
    WeatherSettings settings;
    settings.Enabled=true;
    settings.TimeOfDayHours=36;
    settings.DayLengthSeconds=24;
    settings.Preset.StarRotationDegrees=-10;
    settings.Preset.Clouds[0].WindDirectionDegrees=350;
    WeatherListener listener;
    Check(runtime.RegisterListener(&listener),"Cannot register authoring event listener");
    Check(weather.SetWeatherSettings(scene,settings),"Cannot enable weather");
    Check(Read(weather,scene).Daylight==1&&Read(weather,scene).TimeOfDayHours==12&&
        Read(weather,scene).Preset.StarRotationDegrees==350&&!Read(weather,other).Enabled,
        "Authored time or rotation normalization failed, or weather crossed scenes");
    const auto source=authoredScene->GetAuthoredWeatherSettings();
    const auto authoringRevision=scene.GetAuthoringRevision();
    Check(weather.SetWeatherSettings(scene,settings),"Idempotent weather settings rejected");
    auto invalid=settings;
    invalid.Preset.Clouds[0].Coverage=std::numeric_limits<float>::quiet_NaN();
    Check(!weather.SetWeatherSettings(scene,invalid)&&authoredScene->GetAuthoredWeatherSettings()==source,
        "Invalid weather settings modified scene");

    auto overcast=source.Preset;
    overcast.Clouds[0].Coverage=.9f;
    overcast.Clouds[0].WindDirectionDegrees=10;
    overcast.FogStrength=2;
    Check(weather.TransitionWeather(scene,overcast,2),"Cannot transition to overcast");
    runtime.AdvanceFrame(1,4,.25f);
    const auto half=Read(weather,scene);
    Check(Near(half.TimeOfDayHours,13)&&Near(half.Preset.Clouds[0].Coverage,.56f)&&
         Near(half.Preset.Clouds[0].WindDirectionDegrees,0)&&Near(half.Preset.FogStrength,1.5f)&&
         Near(half.CloudOffsets[0].X,.018f),"Day, cloud or preset advanced more than once per frame");
    runtime.ProcessPendingEvents();
    Check(Near(Read(weather,scene).TimeOfDayHours,13),"Paused event dispatch advanced weather");
    runtime.AdvanceFrame(1,1,1);
    Check(Read(weather,scene).Preset==overcast,"Transition never reached target");
    Check(weather.SetTimeOfDay(scene,-.5f)&&Near(Read(weather,scene).TimeOfDayHours,23.5f)&&
        Read(weather,scene).Daylight==0,"Cannot switch to night or wrap time");
    Check(scene.GetAuthoringRevision()==authoringRevision&&listener.Changes==1&&listener.LightingChanges==0&&
        authoredScene->GetAuthoredWeatherSettings()==source,"Transient weather changed saved scene settings");
    runtime.UnregisterListener(&listener);

    scene.SetSimulationEnabled(false);
    runtime.AdvanceFrame(.25f,0,0);
    Check(Near(Read(weather,scene).TimeOfDayHours,23.5f),"Suspended scene advanced weather");
    Check(weather.AdvanceWeatherPreview(scene,1)&&Near(Read(weather,scene).TimeOfDayHours,.5f),
        "Explicit edit preview failed or day clock did not wrap");
    scene.SetSimulationEnabled(true);

    std::string error;
    auto otherSettings=settings;
    otherSettings.TimeOfDayHours=22;
    otherSettings.DayLengthSeconds=48;
    Check(weather.SetWeatherSettings(other,otherSettings),"Cannot enable weather in additive scene");
    other.SetSimulationEnabled(false);
    runtime.AdvanceFrame(2400002,0,0);
    const auto wrapped=Read(weather,scene);
    Check(Near(wrapped.TimeOfDayHours,2.5f)&&std::isfinite(wrapped.CloudOffsets[0].X)&&
        wrapped.CloudOffsets[0].X>=0&&wrapped.CloudOffsets[0].X<=4096&&
        Near(Read(weather,other).TimeOfDayHours,22),"Long running time overflowed or advanced suspended additive scene");
    Check(runtime.BeginExecution(error),"Cannot start gameplay execution");
    Check(!weather.AdvanceWeatherPreview(other,1)&&Near(Read(weather,other).TimeOfDayHours,22),
        "Editor preview advanced a scene while playing");
    other.SetSimulationEnabled(true);
    runtime.AdvanceFrame(2,0,0);
    Check(Near(Read(weather,scene).TimeOfDayHours,4.5f)&&Near(Read(weather,other).TimeOfDayHours,23),
        "Active additive scenes did not keep independent weather times");
    Check(runtime.EndExecution(error),"Cannot end gameplay execution");
    Check(weather.SetWeatherSettings(other,WeatherSettings{}),"Cannot clear additive weather");

    const auto parked=Read(weather,scene);
    Check(runtime.ParkAuthoringScenes({scene.GetInstanceID()},error),"Cannot park the authoring scene");
    WeatherState unreachable;
    Check(!weather.GetWeatherState(scene,unreachable),"Parked scene exposed live weather");
    runtime.AdvanceFrame(1,0,0);
    Check(runtime.RestoreAuthoringScenes(error)&&Near(Read(weather,scene).TimeOfDayHours,parked.TimeOfDayHours),
        "Play session changed the parked authoring weather");
}

void CheckLightning(IGameSproutRuntime& runtime,IWeatherRuntime& weather,IScene& scene,IScene& other) {
    WeatherLightning strike;
    strike.Seed=42;
    strike.DurationSeconds=.5f;
    using Call=std::pair<std::uint64_t,std::uint32_t>;
    std::vector<Call> calls;
    std::uint64_t first=0;
    unsigned selfCalls=0,lateCalls=0;
    bool queued=false;
    first=weather.SubscribeLightning([&](IScene& owner,const WeatherLightningEvent&) {
        ++selfCalls;
        weather.UnsubscribeLightning(first);
        auto next=strike;
        next.Seed=99;
        queued=weather.TriggerLightning(owner,next);
    });
    const auto observer=weather.SubscribeLightning([&](IScene& owner,const WeatherLightningEvent& event) {
        if(&owner==&scene)calls.emplace_back(event.Sequence,event.Lightning.Seed);
    });
    Check(first&&observer,"Cannot subscribe to lightning");
    Check(weather.TriggerLightning(scene,strike),"Cannot trigger lightning");
    const auto before=Read(weather,scene);
    const auto repeatedView=Read(weather,scene);
    Check(before.LightningFlash==1&&before.LightningSequence==repeatedView.LightningSequence&&
        Read(weather,other).LightningSequence==0&&calls.empty(),"View read mutated weather or dispatched a strike");
    const auto late=weather.SubscribeLightning([&](IScene&,const WeatherLightningEvent&) {++lateCalls;});
    runtime.ProcessPendingEvents();
    Check(queued&&selfCalls==1&&lateCalls==0&&calls==std::vector<Call>{{before.LightningSequence,42}},
        "Strike did not dispatch once or recursive strike escaped its event boundary");
    runtime.ProcessPendingEvents();
    Check(calls==std::vector<Call>{{before.LightningSequence,42},{before.LightningSequence+1,99}}&&lateCalls==1,
        "Reentrant strike sequence or subscription ordering failed");
    runtime.ProcessPendingEvents();
    Check(calls.size()==2&&Read(weather,scene).LightningFlash==1,"Paused notification repeated a lightning event or aged it");
    runtime.AdvanceFrame(.25f,4,.0625f);
    const auto flash=Read(weather,scene).LightningFlash;
    Check(flash>0&&flash<1,"Lightning failed to decay once per gameplay frame");
    runtime.AdvanceFrame(.25f,0,0);
    Check(Read(weather,scene).LightningFlash==0&&calls.size()==2,"Expired strike continued flashing or dispatching");
    weather.UnsubscribeLightning(late);
    weather.UnsubscribeLightning(observer);

    auto invalid=strike;
    invalid.Position.X=std::numeric_limits<float>::infinity();
    Check(!weather.TriggerLightning(scene,invalid)&&!weather.TriggerLightning(other,strike),"Invalid or disabled lightning accepted");
}
}

int main(int argc,char** argv) {
    try {
        Check(argc==2,"Runtime directory required");
        std::unique_ptr<IGameSproutRuntime> runtime(Export(argv[1]));
        Check(bool(runtime),"Runtime failed to load");
        auto* weather=QueryWeatherRuntime(*runtime);
        Check(weather,"Runtime weather interface unavailable across the SDK boundary");
        auto* scene=runtime->CreateScene("Weather");
        auto* other=runtime->CreateScene("Isolated");
        Check(scene&&other,"Cannot create scenes");
        CheckSimulation(*runtime,*weather,*scene,*other);
        CheckLightning(*runtime,*weather,*scene,*other);
        Check(runtime->DestroyScene(scene)&&runtime->DestroyScene(other),"Cannot destroy scenes with weather");
        std::cout<<"Runtime weather checks passed\n";
        return 0;
    } catch(const std::exception& exception) {
        std::cerr<<exception.what()<<'\n';
        return 1;
    }
}
