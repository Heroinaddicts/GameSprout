#include "GameSproutRuntime.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace GameSproutRuntime {
namespace {
double WrapPositive(double value,double period) {
    const auto remainder=std::fmod(value,period);
    const auto wrapped=remainder<0?remainder+period:remainder;
    return wrapped<period?wrapped:0;
}

float WrapDegrees(float degrees) {
    const auto wrapped=static_cast<float>(WrapPositive(degrees,360));
    return wrapped<360?wrapped:0;
}

void NormalizePreset(WeatherPreset& preset) {
    preset.StarRotationDegrees=WrapDegrees(preset.StarRotationDegrees);
    for(auto& layer:preset.Clouds)layer.WindDirectionDegrees=WrapDegrees(layer.WindDirectionDegrees);
}

float SmoothStep(float value) {
    value=std::clamp(value,0.f,1.f);
    return value*value*(3-2*value);
}

void ApplyTimeOfDay(WeatherState& state,double hours) {
    const auto rounded=static_cast<float>(hours);
    state.TimeOfDayHours=rounded<24?rounded:0;
    const auto sunElevation=std::cos((hours-12)*std::numbers::pi/12);
    state.Daylight=SmoothStep(static_cast<float>((sunElevation+.08)/.28));
}

float BlendDegrees(float from,float to,float alpha) {
    return WrapDegrees(from+std::remainder(to-from,360.f)*alpha);
}

WeatherPreset Blend(const WeatherPreset& from,const WeatherPreset& to,float alpha) {
    WeatherPreset result;
    for(std::size_t index=0;index<result.Clouds.size();++index) {
        const auto& a=from.Clouds[index];
        const auto& b=to.Clouds[index];
        result.Clouds[index]={
            std::lerp(a.Coverage,b.Coverage,alpha),
            std::lerp(a.Density,b.Density,alpha),
            std::lerp(a.Scale,b.Scale,alpha),
            BlendDegrees(a.WindDirectionDegrees,b.WindDirectionDegrees,alpha),
            std::lerp(a.WindSpeed,b.WindSpeed,alpha)
        };
    }
    result.StarIntensity=std::lerp(from.StarIntensity,to.StarIntensity,alpha);
    result.StarDensity=std::lerp(from.StarDensity,to.StarDensity,alpha);
    result.StarRotationDegrees=BlendDegrees(from.StarRotationDegrees,to.StarRotationDegrees,alpha);
    result.CloudColor={
        std::lerp(from.CloudColor.R,to.CloudColor.R,alpha),
        std::lerp(from.CloudColor.G,to.CloudColor.G,alpha),
        std::lerp(from.CloudColor.B,to.CloudColor.B,alpha),
        std::lerp(from.CloudColor.A,to.CloudColor.A,alpha)
    };
    result.FogStrength=std::lerp(from.FogStrength,to.FogStrength,alpha);
    result.DaylightMultiplier=std::lerp(from.DaylightMultiplier,to.DaylightMultiplier,alpha);
    result.AmbientMultiplier=std::lerp(from.AmbientMultiplier,to.AmbientMultiplier,alpha);
    return result;
}

float FlashAt(double age,float duration) {
    const double progress=age/duration;
    if(progress>=1)return 0;
    const auto echo=std::max(0.0,1-std::abs(progress-.28)/.12);
    return static_cast<float>(std::min(1.0,std::exp(-8*progress)+.45*echo)*(1-progress));
}
}

Scene* Runtime::FindWeatherScene(const IScene& scene) const {
    auto* state=Find(const_cast<IScene*>(&scene));
    return IsInScene(state)&&state->IsSceneRoot()?state->SceneOwner:nullptr;
}

bool Runtime::GetWeatherSettings(const IScene& scene,WeatherSettings& settings) const {
    const auto* candidate=FindWeatherScene(scene);
    if(!candidate)return false;
    settings=candidate->_Weather.Settings;
    return true;
}

bool Runtime::SetWeatherSettings(IScene& scene,const WeatherSettings& settings) {
    auto* candidate=FindWeatherScene(scene);
    if(!candidate||!IsValidWeatherSettings(settings))return false;
    auto normalized=settings;
    normalized.TimeOfDayHours=static_cast<float>(WrapPositive(settings.TimeOfDayHours,24));
    if(normalized.TimeOfDayHours>=24)normalized.TimeOfDayHours=0;
    NormalizePreset(normalized.Preset);
    auto& sim=candidate->_Weather;
    const auto before=sim.Settings;
    if(before==normalized)return true;
    sim.Settings=normalized;
    sim.Current.Enabled=normalized.Enabled;
    if(!before.Enabled||!normalized.Enabled||before.TimeOfDayHours!=normalized.TimeOfDayHours) {
        sim.TimeOfDayHours=normalized.TimeOfDayHours;
        ApplyTimeOfDay(sim.Current,sim.TimeOfDayHours);
    }
    if(!before.Enabled||!normalized.Enabled||before.Preset!=normalized.Preset) {
        sim.Current.Preset=normalized.Preset;
        sim.TransitionTarget.reset();
    }
    if(!normalized.Enabled) {
        sim.CloudOffsets={};
        sim.Current.CloudOffsets={};
        std::erase_if(sim.Lightning,[](const auto& lightning){return lightning.Legacy;});
        sim.Current.LightningFlash=0;
        sim.Current.LightningSequence=0;
        if(!sim.Lightning.empty()) {
            const auto& latest=sim.Lightning.back().State;
            sim.Current.Lightning=latest.Strike;
            sim.Current.LightningFlash=latest.Flash;
            sim.Current.LightningSequence=latest.Sequence;
        }
    }
    candidate->InvalidateVisual();
    RecordNodeChange(candidate->State(),SceneNodeEventType::WeatherSettingsChanged,nullptr,0);
    return true;
}

bool Runtime::GetWeatherState(const IScene& scene,WeatherState& state) const {
    const auto* candidate=FindWeatherScene(scene);
    if(!candidate)return false;
    state=candidate->_Weather.Current;
    return true;
}

bool Runtime::SetTimeOfDay(IScene& scene,float hours) {
    auto* candidate=FindWeatherScene(scene);
    if(!candidate||!candidate->_Weather.Current.Enabled||!std::isfinite(hours))return false;
    auto& sim=candidate->_Weather;
    const auto wrapped=WrapPositive(hours,24);
    if(wrapped==sim.TimeOfDayHours)return true;
    sim.TimeOfDayHours=wrapped;
    ApplyTimeOfDay(sim.Current,wrapped);
    candidate->InvalidateVisual();
    return true;
}

bool Runtime::TransitionWeather(IScene& scene,const WeatherPreset& preset,float durationSeconds) {
    auto* candidate=FindWeatherScene(scene);
    if(!candidate||!candidate->_Weather.Current.Enabled||!IsValidWeatherPreset(preset)||
       !std::isfinite(durationSeconds)||durationSeconds<0)return false;
    auto target=preset;
    NormalizePreset(target);
    auto& sim=candidate->_Weather;
    if(durationSeconds>0&&sim.Current.Preset!=target) {
        sim.TransitionSource=sim.Current.Preset;
        sim.TransitionTarget=target;
        sim.TransitionElapsed=0;
        sim.TransitionDuration=durationSeconds;
    } else {
        sim.TransitionTarget.reset();
        if(sim.Current.Preset!=target) {
            sim.Current.Preset=target;
            candidate->InvalidateVisual();
        }
    }
    return true;
}

bool Runtime::TriggerLightning(IScene& scene,const WeatherLightning& strike) {
    auto* candidate=FindWeatherScene(scene);
    return candidate&&candidate->_Weather.Current.Enabled&&StartWeatherLightning(*candidate,strike,true);
}

std::uint64_t Runtime::SubscribeLightning(LightningCallback callback) {
    if(!callback||_ShuttingDown||_NextLightningListener==std::numeric_limits<std::uint64_t>::max())return 0;
    const auto id=_NextLightningListener+1;
    _LightningListeners.emplace(id,LightningListener{
        std::make_shared<LightningCallback>(std::move(callback)),_LightningSequence});
    _NextLightningListener=id;
    return id;
}

void Runtime::UnsubscribeLightning(std::uint64_t token) {
    _LightningListeners.erase(token);
}

void Runtime::DispatchWeatherEvents() {
    if(_LightningEvents.empty())return;
    std::deque<LightningRecord> batch;
    batch.swap(_LightningEvents);
    const auto lastListener=_NextLightningListener;
    for(const auto& record:batch) {
        auto* state=Find(record.SceneID);
        if(!IsInScene(state)||!state->IsSceneRoot())continue;
        RuntimeOperation operation(*this);
        auto listener=_LightningListeners.begin();
        while(listener!=_LightningListeners.end()&&listener->first<=lastListener) {
            const auto id=listener->first;
            if(record.Event.Sequence>listener->second.AfterSequence) {
                // The callback can remove its own registration while it runs.
                const auto callback=listener->second.Callback;
                const auto constructionDepth=_ConstructionDepth;
                try {(*callback)(*state->SceneOwner,record.Event);} catch(...) {}
                _ConstructionDepth=constructionDepth;
            }
            listener=_LightningListeners.upper_bound(id);
        }
    }
}

void Runtime::AdvanceSceneWeather(Scene& scene,float deltaSeconds) {
    auto& sim=scene._Weather;
    auto& state=sim.Current;
    bool changed=false;
    if(sim.Settings.DayLengthSeconds>0) {
        const auto elapsedHours=std::fmod(24*double(deltaSeconds)/sim.Settings.DayLengthSeconds,24.0);
        sim.TimeOfDayHours=WrapPositive(sim.TimeOfDayHours+elapsedHours,24);
        ApplyTimeOfDay(state,sim.TimeOfDayHours);
        changed=true;
    }
    if(sim.TransitionTarget) {
        sim.TransitionElapsed+=deltaSeconds;
        if(sim.TransitionElapsed>=sim.TransitionDuration) {
            state.Preset=*sim.TransitionTarget;
            sim.TransitionTarget.reset();
        } else {
            const auto alpha=SmoothStep(static_cast<float>(sim.TransitionElapsed/sim.TransitionDuration));
            state.Preset=Blend(sim.TransitionSource,*sim.TransitionTarget,alpha);
        }
        changed=true;
    }
    for(std::size_t index=0;index<state.Preset.Clouds.size();++index) {
        const auto& layer=state.Preset.Clouds[index];
        if(layer.WindSpeed<=0)continue;
        auto& offset=sim.CloudOffsets[index];
        const double angle=layer.WindDirectionDegrees*(std::numbers::pi/180);
        const double distance=double(deltaSeconds)*layer.WindSpeed;
        // Matched by the periodic cloud noise, preventing drift in long sessions.
        offset[0]=WrapPositive(offset[0]+std::cos(angle)*distance,4096);
        offset[1]=WrapPositive(offset[1]+std::sin(angle)*distance,4096);
        state.CloudOffsets[index]={static_cast<float>(offset[0]),static_cast<float>(offset[1])};
        changed|=layer.Coverage>0&&layer.Density>0;
    }
    if(changed)scene.InvalidateVisual();
}

void Runtime::AdvanceSceneLightning(Scene& scene,float deltaSeconds) {
    auto& sim=scene._Weather;
    if(sim.Lightning.empty())return;
    for(auto& lightning:sim.Lightning) {
        lightning.Age+=deltaSeconds;
        auto& effect=lightning.State;
        effect.Flash=FlashAt(lightning.Age,effect.Strike.DurationSeconds);
        if(effect.Sequence==sim.Current.LightningSequence)sim.Current.LightningFlash=effect.Flash;
    }
    std::erase_if(sim.Lightning,[](const auto& effect){return effect.State.Flash<=0;});
    scene.InvalidateVisual();
}

void Runtime::AdvanceWeather(float deltaSeconds) {
    if(deltaSeconds<=0)return;
    for(auto* scene:_Scenes) {
        if(!scene->IsSimulationEnabled())continue;
        if(scene->_Weather.Current.Enabled)AdvanceSceneWeather(*scene,deltaSeconds);
        AdvanceSceneLightning(*scene,deltaSeconds);
    }
}

bool Runtime::AdvanceWeatherPreview(IScene& scene,float deltaSeconds) {
    auto* candidate=FindWeatherScene(scene);
    if(!candidate||!candidate->_Weather.Current.Enabled||_ScriptExecutionEnabled||IsExecuting()||
       _ConstructionDepth||!std::isfinite(deltaSeconds)||deltaSeconds<0)return false;
    if(deltaSeconds>0) {
        AdvanceSceneWeather(*candidate,deltaSeconds);
        AdvanceSceneLightning(*candidate,deltaSeconds);
    }
    return true;
}

}
