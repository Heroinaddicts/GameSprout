#include "GameSproutRuntime.h"
#include "Scene.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <span>
#include <tuple>

namespace GameSproutRuntime {
namespace {
using Definitions=std::span<const EnvironmentParameterDefinition>;

constexpr std::string_view CloudType="GameSprout.CloudLayer";
constexpr std::string_view DayNightType="GameSprout.DayNight";
constexpr std::string_view StarfieldType="GameSprout.Starfield";
using Method=EnvironmentInterpolation;
const EnvironmentParameterDefinition BuiltinDefinitions[]{
    {"Sky.Daylight",1.f},{"Sky.StarIntensity",1.f},{"Sky.StarDensity",.3f},
    {"Sky.StarRotationDegrees",0.f,Method::Angle},{"Sky.CloudColor",Color{1,1,1,1}},
    {"Fog.Strength",1.f},{"Fog.Start",150.f},{"Fog.End",1000.f},{"Fog.Color",Color{.48f,.68f,.8f,1}},
    {"Lighting.DaylightMultiplier",1.f},{"Lighting.AmbientMultiplier",1.f}
};
struct BuiltinField {
    std::string_view Type,Local,Global;
};
constexpr BuiltinField BuiltinFields[]{
    {DayNightType,"Daylight","Sky.Daylight"},
    {DayNightType,"DaylightMultiplier","Lighting.DaylightMultiplier"},
    {DayNightType,"AmbientMultiplier","Lighting.AmbientMultiplier"},
    {StarfieldType,"Intensity","Sky.StarIntensity"},
    {StarfieldType,"Density","Sky.StarDensity"},
    {StarfieldType,"RotationDegrees","Sky.StarRotationDegrees"}
};

bool ValidID(std::string_view id) {return !id.empty()&&id.find('\0')==std::string_view::npos;}
bool Finite(Vector3 value) {return std::isfinite(value.X)&&std::isfinite(value.Y)&&std::isfinite(value.Z);}
bool Finite(Color value) {
    return std::isfinite(value.R)&&std::isfinite(value.G)&&std::isfinite(value.B)&&std::isfinite(value.A);
}
bool ValidResource(const ResourceReference& value) {
    if(value.Guid.empty())return value.LocalID==0;
    return value.LocalID!=0&&value.Guid.size()==32&&value.Guid.find_first_not_of('0')!=std::string::npos&&
        std::all_of(value.Guid.begin(),value.Guid.end(),[](unsigned char byte){return std::isxdigit(byte);});
}
bool ValidValue(const EnvironmentValue& value) {
    return std::visit([](const auto& typed) {
        using T=std::decay_t<decltype(typed)>;
        if constexpr(std::is_same_v<T,float>)return std::isfinite(typed);
        else if constexpr(std::is_same_v<T,bool>)return true;
        else if constexpr(std::is_same_v<T,ResourceReference>)return ValidResource(typed);
        else return Finite(typed);
    },value);
}
bool ValidDefinition(const EnvironmentParameterDefinition& value) {
    if(!ValidID(value.Id)||!ValidValue(value.Default))return false;
    switch(value.Interpolation) {
    case EnvironmentInterpolation::Linear:
    case EnvironmentInterpolation::Step:return true;
    case EnvironmentInterpolation::Angle:return std::holds_alternative<float>(value.Default);
    }
    return false;
}
bool ValidDefinitions(const std::vector<EnvironmentParameterDefinition>& values) {
    std::set<std::string_view> names;
    for(const auto& value:values)
        if(!ValidDefinition(value)||!names.insert(value.Id).second)return false;
    return true;
}
bool ValidWeight(float weight) {return std::isfinite(weight)&&weight>=0&&weight<=1;}
bool ValidRegion(const EnvironmentRegion& region) {
    const auto size=region.HalfSize;
    return Finite(region.Center)&&Finite(size)&&size.X>=0&&size.Y>=0&&size.Z>=0&&
        std::isfinite(region.BlendDistance)&&region.BlendDistance>=0;
}
bool ValidOverrides(const std::vector<EnvironmentOverride>& values) {
    std::set<std::pair<std::string_view,std::string_view>> keys;
    for(const auto& value:values) {
        if((!value.Target.empty()&&!ValidID(value.Target))||!ValidID(value.Parameter)||
            !ValidValue(value.Value)||!keys.emplace(value.Target,value.Parameter).second)return false;
    }
    return true;
}
const EnvironmentParameterDefinition* FindDefinition(Definitions definitions,std::string_view id) {
    const auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& entry){return entry.Id==id;});
    return found==definitions.end()?nullptr:&*found;
}
const EnvironmentParameterDefinition* BuiltinGlobalField(std::string_view type,std::string_view local) {
    for(const auto& field:BuiltinFields)
        if(field.Type==type&&field.Local==local)return FindDefinition(BuiltinDefinitions,field.Global);
    return nullptr;
}

float RegionWeight(const EnvironmentRegion& region,Vector3 position) {
    const auto outside=[](double position,double center,double halfSize) {
        return std::max(0.0,std::abs(position-center)-halfSize);
    };
    const auto& center=region.Center;
    const auto& size=region.HalfSize;
    const auto distance=std::hypot(outside(position.X,center.X,size.X),
        outside(position.Y,center.Y,size.Y),outside(position.Z,center.Z,size.Z));
    if(distance==0)return 1;
    if(region.BlendDistance==0)return 0;
    return static_cast<float>(std::max(0.0,1.0-distance/region.BlendDistance));
}

EnvironmentValue Interpolate(const EnvironmentValue& from,const EnvironmentValue& to,float weight,
    EnvironmentInterpolation interpolation) {
    if(weight>=1)return to;
    if(weight<=0)return from;
    if(interpolation==EnvironmentInterpolation::Step)return weight<.5f?from:to;
    return std::visit([&](const auto& first)->EnvironmentValue {
        using T=std::decay_t<decltype(first)>;
        const auto& second=std::get<T>(to);
        const auto blend=[&](float a,float b){return std::lerp(a,b,weight);};
        if constexpr(std::is_same_v<T,float>) {
            if(interpolation==EnvironmentInterpolation::Angle) {
                const auto delta=std::remainder(double(second)-first,360.0);
                const auto angle=std::fmod(double(first)+delta*weight,360.0);
                return static_cast<float>(angle<0?angle+360:angle);
            }
            return blend(first,second);
        } else if constexpr(std::is_same_v<T,Vector3>) {
            return Vector3{blend(first.X,second.X),blend(first.Y,second.Y),blend(first.Z,second.Z)};
        } else if constexpr(std::is_same_v<T,Color>) {
            return Color{blend(first.R,second.R),blend(first.G,second.G),blend(first.B,second.B),blend(first.A,second.A)};
        } else {
            return weight<.5f?from:to;
        }
    },from);
}
EnvironmentValue SampleCurve(const EnvironmentCurve& curve,double time,EnvironmentInterpolation interpolation) {
    const auto& keys=curve.Keys;
    const auto period=double(keys.back().Seconds);
    const double position=curve.Loop&&period>0?std::fmod(time,period):time;
    const auto next=std::upper_bound(keys.begin(),keys.end(),position,
        [](double seconds,const EnvironmentKeyframe& key){return seconds<key.Seconds;});
    if(next==keys.begin())return keys.front().Value;
    if(next==keys.end())return keys.back().Value;
    const auto& previous=*(next-1);
    if(interpolation==EnvironmentInterpolation::Step||std::holds_alternative<bool>(previous.Value)||
        std::holds_alternative<ResourceReference>(previous.Value))return previous.Value;
    const auto weight=static_cast<float>((position-previous.Seconds)/(double(next->Seconds)-previous.Seconds));
    return Interpolate(previous.Value,next->Value,weight,interpolation);
}

EnvironmentValue GlobalBase(const EnvironmentParameterDefinition& definition,const WeatherState& weather,
    const SceneAtmosphereSettings& atmosphere) {
    const auto& id=definition.Id;
    const auto& preset=weather.Preset;
    if(id=="Sky.Daylight")return weather.Enabled?weather.Daylight:1.f;
    if(id=="Sky.StarIntensity")return weather.Enabled?preset.StarIntensity:0.f;
    if(id=="Sky.StarDensity")return weather.Enabled?preset.StarDensity:.3f;
    if(id=="Sky.StarRotationDegrees")return weather.Enabled?preset.StarRotationDegrees:0.f;
    if(id=="Sky.CloudColor")return weather.Enabled?preset.CloudColor:definition.Default;
    if(id=="Fog.Strength")return atmosphere.Enabled?(weather.Enabled?preset.FogStrength:1.f):0.f;
    if(id=="Fog.Start")return atmosphere.FogStart;
    if(id=="Fog.End")return atmosphere.FogEnd;
    if(id=="Fog.Color")return atmosphere.FogColor;
    if(id=="Lighting.DaylightMultiplier")return weather.Enabled?preset.DaylightMultiplier:1.f;
    if(id=="Lighting.AmbientMultiplier")return weather.Enabled?preset.AmbientMultiplier:1.f;
    return definition.Default;
}

template<class T>
bool ReadLightningField(const EnvironmentOverride& entry,std::string_view name,T& result) {
    if(entry.Parameter!=name)return false;
    const auto* value=std::get_if<T>(&entry.Value);
    if(!value)return false;
    result=*value;
    return true;
}
bool ReadLightningSeed(const EnvironmentOverride& entry,std::uint32_t& seed) {
    const auto* value=std::get_if<float>(&entry.Value);
    if(!value||*value<0||std::trunc(*value)!=*value)return false;
    if(entry.Parameter=="Seed"&&double(*value)<=std::numeric_limits<std::uint32_t>::max()) {
        seed=static_cast<std::uint32_t>(*value);
        return true;
    }
    if(*value>65535)return false;
    const auto bits=static_cast<std::uint32_t>(*value);
    if(entry.Parameter=="SeedLow16")seed=(seed&0xffff0000u)|bits;
    else if(entry.Parameter=="SeedHigh16")seed=(seed&0x0000ffffu)|(bits<<16);
    else return false;
    return true;
}
bool ReadLightningPayload(const EnvironmentEvent& event,WeatherLightning& strike) {
    bool wholeSeed=false,splitSeed=false;
    for(const auto& entry:event.Payload) {
        if(!entry.Target.empty())return false;
        wholeSeed|=entry.Parameter=="Seed";
        splitSeed|=entry.Parameter=="SeedLow16"||entry.Parameter=="SeedHigh16";
        if(ReadLightningField(entry,"Position",strike.Position)||
            ReadLightningField(entry,"Height",strike.Height)||ReadLightningField(entry,"Tint",strike.Tint)||
            ReadLightningField(entry,"Intensity",strike.Intensity)||
            ReadLightningField(entry,"DurationSeconds",strike.DurationSeconds)||
            ReadLightningField(entry,"Radius",strike.Radius)||ReadLightningSeed(entry,strike.Seed))continue;
        return false;
    }
    return !(wholeSeed&&splitSeed)&&IsValidWeatherLightning(strike);
}
}

Runtime::Runtime() {
    for(const auto& definition:BuiltinDefinitions)_EnvironmentParameters.emplace(definition.Id,definition);
    EnvironmentEffectDescriptor cloud;
    cloud.Type=CloudType;
    cloud.Stage=EnvironmentEffectStage::Builtin;
    cloud.Parameters={{"Coverage",.22f},{"Density",.75f},{"Scale",2.f},
        {"WindDirectionDegrees",0.f,Method::Angle},{"WindSpeed",.018f}};
    _EnvironmentEffects.emplace(cloud.Type,cloud);
    for(const auto type:{DayNightType,StarfieldType}) {
        EnvironmentEffectDescriptor effect;
        effect.Type=type;
        effect.Stage=EnvironmentEffectStage::Builtin;
        for(const auto& field:BuiltinFields) {
            if(field.Type!=type)continue;
            auto parameter=*FindDefinition(BuiltinDefinitions,field.Global);
            parameter.Id=field.Local;
            effect.Parameters.push_back(std::move(parameter));
        }
        _EnvironmentEffects.emplace(effect.Type,effect);
    }
    const WeatherLightning strike;
    EnvironmentEventDescriptor lightning;
    lightning.Type=EnvironmentLightningEventType;
    lightning.Parameters={{"Position",strike.Position},{"Height",strike.Height},{"Tint",strike.Tint},
        {"Intensity",strike.Intensity},{"DurationSeconds",strike.DurationSeconds},{"Radius",strike.Radius},
        {"SeedLow16",0.f},{"SeedHigh16",0.f}};
    _EnvironmentEventTypes.emplace(lightning.Type,lightning);
}

bool Runtime::RegisterEnvironmentParameter(const EnvironmentParameterDefinition& definition) {
    if(_ShuttingDown||!ValidDefinition(definition))return false;
    if(!_EnvironmentParameters.emplace(definition.Id,definition).second)return false;
    InvalidateEnvironmentViews();
    return true;
}
bool Runtime::RegisterEnvironmentEffect(const EnvironmentEffectDescriptor& descriptor) {
    if(_ShuttingDown||!ValidID(descriptor.Type)||descriptor.Stage!=EnvironmentEffectStage::SkyOverlay||
        !ValidResource(descriptor.Material)||descriptor.Material.Guid.empty()||!ValidDefinitions(descriptor.Parameters))return false;
    if(!_EnvironmentEffects.emplace(descriptor.Type,descriptor).second)return false;
    InvalidateEnvironmentViews();
    return true;
}
bool Runtime::UnregisterEnvironmentParameter(const std::string& id) {
    // The built-in renderer depends on the registered sky, fog, and lighting fields.
    if(FindDefinition(BuiltinDefinitions,id))return false;
    if(!_EnvironmentParameters.erase(id))return false;
    InvalidateEnvironmentViews();
    return true;
}
bool Runtime::UnregisterEnvironmentEffect(const std::string& id) {
    const auto effect=_EnvironmentEffects.find(id);
    if(effect==_EnvironmentEffects.end()||effect->second.Stage==EnvironmentEffectStage::Builtin)return false;
    _EnvironmentEffects.erase(effect);
    InvalidateEnvironmentViews();
    return true;
}
void Runtime::InvalidateEnvironmentViews() {
    for(auto* scene:_Scenes)if(IsInScene(&scene->State()))scene->InvalidateVisual();
}
std::vector<EnvironmentParameterDefinition> Runtime::GetEnvironmentParameters() const {
    std::vector<EnvironmentParameterDefinition> result;
    result.reserve(_EnvironmentParameters.size());
    for(const auto& [id,definition]:_EnvironmentParameters)result.push_back(definition);
    return result;
}
std::vector<EnvironmentEffectDescriptor> Runtime::GetEnvironmentEffects() const {
    std::vector<EnvironmentEffectDescriptor> result;
    result.reserve(_EnvironmentEffects.size());
    for(const auto& [id,descriptor]:_EnvironmentEffects)result.push_back(descriptor);
    return result;
}
bool Runtime::RegisterEnvironmentEvent(const EnvironmentEventDescriptor& descriptor) {
    return !_ShuttingDown&&ValidID(descriptor.Type)&&ValidDefinitions(descriptor.Parameters)&&
        _EnvironmentEventTypes.emplace(descriptor.Type,descriptor).second;
}
bool Runtime::UnregisterEnvironmentEvent(const std::string& id) {
    return id!=EnvironmentLightningEventType&&_EnvironmentEventTypes.erase(id)!=0;
}
std::vector<EnvironmentEventDescriptor> Runtime::GetEnvironmentEvents() const {
    std::vector<EnvironmentEventDescriptor> result;
    result.reserve(_EnvironmentEventTypes.size());
    for(const auto& [id,event]:_EnvironmentEventTypes)result.push_back(event);
    return result;
}
bool Runtime::GetEnvironmentSettings(const IScene& scene,EnvironmentSettings& settings) const {
    const auto* candidate=FindWeatherScene(scene);
    if(!candidate)return false;
    settings=candidate->_Environment.Settings;
    return true;
}
bool Runtime::SetEnvironmentSettings(IScene& scene,const EnvironmentSettings& settings) {
    auto* candidate=FindWeatherScene(scene);
    if(!candidate)return false;
    auto& simulation=candidate->_Environment;
    if(simulation.Settings==settings)return true;
    std::map<std::string_view,const EnvironmentEffectDescriptor*,std::less<>> effects;
    for(const auto& effect:settings.Effects) {
        const auto type=_EnvironmentEffects.find(effect.Type);
        if(!ValidID(effect.Id)||!ValidID(effect.Type)||
            !effects.emplace(effect.Id,type==_EnvironmentEffects.end()?nullptr:&type->second).second)return false;
    }
    const auto compatible=[&](const std::string& target,const std::string& parameter,const EnvironmentValue& value) {
        if(target.empty()) {
            const auto definition=_EnvironmentParameters.find(parameter);
            return definition==_EnvironmentParameters.end()||definition->second.Default.index()==value.index();
        }
        const auto effect=effects.find(target);
        if(effect==effects.end())return false;
        if(!effect->second)return true; // A project can register this type after loading its scene.
        const auto* definition=FindDefinition(effect->second->Parameters,parameter);
        return !definition||definition->Default.index()==value.index();
    };
    using Entry=Scene::EnvironmentSimulation::Entry;
    std::vector<Entry> order;
    order.reserve(settings.Layers.size()+settings.Curves.size());
    std::set<std::string_view> ids;
    for(std::size_t index=0;index<settings.Layers.size();++index) {
        const auto& layer=settings.Layers[index];
        if(!ValidID(layer.Id)||!ids.insert(layer.Id).second||!ValidWeight(layer.Weight)||
            (layer.Region&&!ValidRegion(*layer.Region))||!ValidOverrides(layer.Overrides))return false;
        for(const auto& value:layer.Overrides)
            if(!compatible(value.Target,value.Parameter,value.Value))return false;
        order.push_back({index,false});
    }
    for(std::size_t index=0;index<settings.Curves.size();++index) {
        const auto& curve=settings.Curves[index];
        if(!ValidID(curve.Id)||!ids.insert(curve.Id).second||!ValidID(curve.Parameter)||!ValidWeight(curve.Weight)||
            curve.Keys.empty()||!compatible(curve.Target,curve.Parameter,curve.Keys.front().Value))return false;
        float time=-1;
        for(const auto& key:curve.Keys) {
            if(!std::isfinite(key.Seconds)||key.Seconds<0||key.Seconds<=time||
                key.Value.index()!=curve.Keys.front().Value.index()||!ValidValue(key.Value))return false;
            time=key.Seconds;
        }
        order.push_back({index,true});
    }
    const auto key=[&](Entry entry) {
        if(entry.Curve) {
            const auto& curve=settings.Curves[entry.Index];
            return std::tuple<int,std::string_view>{curve.Priority,curve.Id};
        }
        const auto& layer=settings.Layers[entry.Index];
        return std::tuple<int,std::string_view>{layer.Priority,layer.Id};
    };
    std::sort(order.begin(),order.end(),[&](Entry a,Entry b){return key(a)<key(b);});
    auto next=settings;
    simulation.Settings=std::move(next);
    simulation.BlendOrder=std::move(order);
    candidate->InvalidateVisual();
    RecordNodeChange(candidate->State(),SceneNodeEventType::EnvironmentSettingsChanged,nullptr,0);
    return true;
}

bool Runtime::ResolveEnvironment(const IScene& scene,Vector3 sample,EnvironmentSnapshot& output) const {
    const auto* candidate=FindWeatherScene(scene);
    if(!candidate||!Finite(sample))return false;
    const auto& simulation=candidate->_Environment;
    output.Values.clear();
    output.Effects.clear();
    output.Lightning.clear();
    for(const auto& strike:candidate->_Weather.Lightning)output.Lightning.push_back(strike.State);
    output.TimeSeconds=simulation.TimeSeconds;
    const auto& settings=simulation.Settings;
    const auto base=[&](const EnvironmentParameterDefinition& parameter) {
        return GlobalBase(parameter,candidate->_Weather.Current,candidate->_EnvironmentLighting.Atmosphere);
    };
    for(const auto& [id,definition]:_EnvironmentParameters)
        if(!FindDefinition(BuiltinDefinitions,id))output.Values.push_back({{},id,definition.Default});
    for(const auto& effect:settings.Effects) {
        const auto found=_EnvironmentEffects.find(effect.Type);
        if(found==_EnvironmentEffects.end())continue;
        const auto& descriptor=found->second;
        output.Effects.push_back({effect.Id,effect.Type,descriptor.Stage,descriptor.Material,descriptor.Reflections});
        for(const auto& parameter:descriptor.Parameters) {
            const auto* global=BuiltinGlobalField(effect.Type,parameter.Id);
            output.Values.push_back({effect.Id,parameter.Id,global?base(*global):parameter.Default});
        }
    }
    const auto instance=[&](std::string_view target)->const EnvironmentResolvedEffect* {
        const auto effect=std::find_if(output.Effects.begin(),output.Effects.end(),[&](const auto& item){return item.Id==target;});
        return effect==output.Effects.end()?nullptr:&*effect;
    };
    const auto definition=[&](std::string_view target,std::string_view id)->const EnvironmentParameterDefinition* {
        if(target.empty()) {
            const auto found=_EnvironmentParameters.find(id);
            return found==_EnvironmentParameters.end()?nullptr:&found->second;
        }
        const auto* effect=instance(target);
        if(!effect)return nullptr;
        const auto type=_EnvironmentEffects.find(effect->Type);
        return FindDefinition(type->second.Parameters,id);
    };
    const auto applyValue=[&](const std::string& target,const EnvironmentParameterDefinition& parameter,
        const EnvironmentValue& value,float weight) {
        if(value.index()!=parameter.Default.index())return; // Stale registration with a different field type.
        auto current=std::find_if(output.Values.begin(),output.Values.end(),[&](const auto& item){
            return item.Target==target&&item.Parameter==parameter.Id;
        });
        if(current==output.Values.end()) {
            output.Values.push_back({target,parameter.Id,Interpolate(base(parameter),value,weight,parameter.Interpolation)});
            return;
        }
        current->Value=Interpolate(current->Value,value,weight,parameter.Interpolation);
    };
    const auto apply=[&](const std::string& target,const EnvironmentParameterDefinition& parameter,
        const EnvironmentValue& value,float weight) {
        applyValue(target,parameter,value,weight);
        if(const auto* effect=instance(target))
            if(const auto* global=BuiltinGlobalField(effect->Type,parameter.Id))
                applyValue({},*global,value,weight);
    };
    for(const auto entry:simulation.BlendOrder) {
        if(entry.Curve) {
            const auto& curve=settings.Curves[entry.Index];
            if(curve.Weight<=0)continue;
            if(const auto* parameter=definition(curve.Target,curve.Parameter))
                apply(curve.Target,*parameter,SampleCurve(curve,simulation.TimeSeconds,parameter->Interpolation),curve.Weight);
        } else {
            const auto& layer=settings.Layers[entry.Index];
            const auto weight=layer.Weight*(layer.Region?RegionWeight(*layer.Region,sample):1.f);
            if(weight<=0)continue;
            for(const auto& value:layer.Overrides)
                if(const auto* parameter=definition(value.Target,value.Parameter))
                    apply(value.Target,*parameter,value.Value,weight);
        }
    }
    return true;
}

void Runtime::AdvanceSceneEnvironment(Scene& scene,float deltaSeconds) {
    auto& environment=scene._Environment;
    if(environment.Settings.Curves.empty()&&environment.Settings.Effects.empty())return;
    environment.TimeSeconds+=deltaSeconds;
    scene.InvalidateVisual();
}
void Runtime::AdvanceEnvironment(float deltaSeconds) {
    if(deltaSeconds<=0)return;
    for(auto* scene:_Scenes)if(scene->IsSimulationEnabled())AdvanceSceneEnvironment(*scene,deltaSeconds);
}
bool Runtime::AdvanceEnvironmentPreview(IScene& scene,float deltaSeconds) {
    auto* candidate=FindWeatherScene(scene);
    if(!candidate||_ScriptExecutionEnabled||IsExecuting()||_ConstructionDepth||
        !std::isfinite(deltaSeconds)||deltaSeconds<0)return false;
    if(deltaSeconds>0) {
        AdvanceSceneEnvironment(*candidate,deltaSeconds);
        if(!candidate->_Weather.Current.Enabled)AdvanceSceneLightning(*candidate,deltaSeconds);
    }
    return true;
}

bool Runtime::QueueEnvironmentEvent(const Scene& scene,EnvironmentEvent event) {
    if(_EnvironmentEventSequence==std::numeric_limits<std::uint64_t>::max())return false;
    event.Sequence=_EnvironmentEventSequence+1;
    if(!_EnvironmentListeners.empty())_EnvironmentEvents.push_back({scene.GetInstanceID(),std::move(event)});
    ++_EnvironmentEventSequence;
    return true;
}
bool Runtime::EmitEnvironmentEvent(IScene& scene,const EnvironmentEvent& event) {
    auto* candidate=FindWeatherScene(scene);
    if(_ShuttingDown||!candidate||!ValidID(event.Type)||!ValidOverrides(event.Payload))return false;
    if(event.Type==EnvironmentLightningEventType) {
        auto strike=candidate->_Weather.Settings.Lightning;
        return ReadLightningPayload(event,strike)&&StartWeatherLightning(*candidate,strike,false);
    }
    const auto descriptor=_EnvironmentEventTypes.find(event.Type);
    if(descriptor!=_EnvironmentEventTypes.end()) {
        for(const auto& field:event.Payload) {
            const auto* parameter=FindDefinition(descriptor->second.Parameters,field.Parameter);
            if(!field.Target.empty()||!parameter||parameter->Default.index()!=field.Value.index())return false;
        }
    }
    return QueueEnvironmentEvent(*candidate,event);
}

bool Runtime::StartWeatherLightning(Scene& scene,const WeatherLightning& strike,bool legacy) {
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    if(_ShuttingDown||!IsValidWeatherLightning(strike)||_LightningSequence==maximum||
        _EnvironmentEventSequence==maximum)return false;
    const auto sequence=_LightningSequence+1;
    EnvironmentEvent event;
    if(!_EnvironmentListeners.empty()) {
        event.Type=EnvironmentLightningEventType;
        event.Payload={
            {"","Position",strike.Position},{"","Height",strike.Height},{"","Tint",strike.Tint},
            {"","Intensity",strike.Intensity},{"","DurationSeconds",strike.DurationSeconds},
            {"","Radius",strike.Radius},{"","SeedLow16",static_cast<float>(strike.Seed&0xffffu)},
            {"","SeedHigh16",static_cast<float>(strike.Seed>>16)}
        };
    }
    QueueEnvironmentEvent(scene,std::move(event));
    if(!_LightningListeners.empty())_LightningEvents.push_back({scene.GetInstanceID(),{sequence,strike}});
    _LightningSequence=sequence;
    auto& sim=scene._Weather;
    sim.Lightning.push_back({{strike,1,sequence},0,legacy});
    sim.Current.Lightning=strike;
    sim.Current.LightningSequence=sequence;
    sim.Current.LightningFlash=1;
    scene.InvalidateVisual();
    return true;
}
std::uint64_t Runtime::SubscribeEnvironmentEvents(EnvironmentEventCallback callback) {
    if(!callback||_ShuttingDown||_NextEnvironmentListener==std::numeric_limits<std::uint64_t>::max())return 0;
    const auto id=_NextEnvironmentListener+1;
    _EnvironmentListeners.emplace(id,EnvironmentListener{
        std::make_shared<EnvironmentEventCallback>(std::move(callback)),_EnvironmentEventSequence});
    _NextEnvironmentListener=id;
    return id;
}
void Runtime::UnsubscribeEnvironmentEvents(std::uint64_t token) {
    _EnvironmentListeners.erase(token);
}
void Runtime::DispatchEnvironmentEvents(std::uint64_t throughSequence) {
    while(!_EnvironmentEvents.empty()&&_EnvironmentEvents.front().Event.Sequence<=throughSequence) {
        auto record=std::move(_EnvironmentEvents.front());
        _EnvironmentEvents.pop_front();
        auto* state=Find(record.SceneID);
        if(!IsInScene(state)||!state->IsSceneRoot())continue;
        RuntimeOperation operation(*this);
        const auto lastListener=_NextEnvironmentListener;
        auto listener=_EnvironmentListeners.begin();
        while(listener!=_EnvironmentListeners.end()&&listener->first<=lastListener) {
            const auto id=listener->first;
            if(record.Event.Sequence>listener->second.AfterSequence) {
                const auto callback=listener->second.Callback;
                const auto constructionDepth=_ConstructionDepth;
                try {(*callback)(*state->SceneOwner,record.Event);} catch(...) {}
                _ConstructionDepth=constructionDepth;
            }
            listener=_EnvironmentListeners.upper_bound(id);
        }
    }
}

}
