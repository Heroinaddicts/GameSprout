#pragma once
#include "GameSproutRuntimeApi.h"
#include "RuntimeWeatherApi.h"
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

namespace GameSproutRuntime {

using EnvironmentValue=std::variant<float,bool,Vector3,Color,ResourceReference>;
enum class EnvironmentInterpolation : std::uint8_t { Linear,Angle,Step };
enum class EnvironmentEffectStage : std::uint8_t { Builtin,SkyOverlay };

inline bool EnvironmentValuesEqual(const EnvironmentValue& first,const EnvironmentValue& second) {
    if(first.index()!=second.index())return false;
    return std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        const auto& other=std::get<T>(second);
        if constexpr(std::is_same_v<T,Vector3>)return value.X==other.X&&value.Y==other.Y&&value.Z==other.Z;
        else if constexpr(std::is_same_v<T,Color>)return value.R==other.R&&value.G==other.G&&value.B==other.B&&value.A==other.A;
        else return value==other;
    },first);
}

struct EnvironmentParameterDefinition {
    std::string Id;
    EnvironmentValue Default=0.f;
    EnvironmentInterpolation Interpolation=EnvironmentInterpolation::Linear;
};
struct EnvironmentOverride {
    std::string Target; // Empty for global parameters, otherwise the effect instance ID.
    std::string Parameter;
    EnvironmentValue Value=0.f;
    bool operator==(const EnvironmentOverride& other) const {
        return Target==other.Target&&Parameter==other.Parameter&&EnvironmentValuesEqual(Value,other.Value);
    }
};
struct EnvironmentRegion {
    Vector3 Center,HalfSize;
    float BlendDistance=0; // Nonnegative metres outside the axis-aligned box.
    bool operator==(const EnvironmentRegion& other) const {
        return Center.X==other.Center.X&&Center.Y==other.Center.Y&&Center.Z==other.Center.Z&&
            HalfSize.X==other.HalfSize.X&&HalfSize.Y==other.HalfSize.Y&&HalfSize.Z==other.HalfSize.Z&&
            BlendDistance==other.BlendDistance;
    }
};
struct EnvironmentLayer {
    std::string Id;
    std::vector<EnvironmentOverride> Overrides;
    int Priority=0; // Low priority applies first. At equal priority the layer ID orders application.
    float Weight=1;
    std::optional<EnvironmentRegion> Region;
    bool operator==(const EnvironmentLayer&) const = default;
};
struct EnvironmentKeyframe {
    float Seconds=0;
    EnvironmentValue Value=0.f;
    bool operator==(const EnvironmentKeyframe& other) const {
        return Seconds==other.Seconds&&EnvironmentValuesEqual(Value,other.Value);
    }
};
struct EnvironmentCurve {
    std::string Id,Target,Parameter;
    std::vector<EnvironmentKeyframe> Keys; // Strictly increasing seconds.
    bool Loop=false;
    int Priority=0;
    float Weight=1;
    bool operator==(const EnvironmentCurve&) const = default;
};
struct EnvironmentEffectInstance {
    std::string Id,Type;
    bool operator==(const EnvironmentEffectInstance&) const = default;
};
struct EnvironmentSettings {
    std::vector<EnvironmentLayer> Layers;
    std::vector<EnvironmentCurve> Curves;
    std::vector<EnvironmentEffectInstance> Effects;
    bool operator==(const EnvironmentSettings&) const = default;
};
struct EnvironmentEffectDescriptor {
    std::string Type;
    EnvironmentEffectStage Stage=EnvironmentEffectStage::SkyOverlay;
    ResourceReference Material;
    std::vector<EnvironmentParameterDefinition> Parameters;
    bool Reflections=true;
};
struct EnvironmentResolvedEffect {
    std::string Id,Type;
    EnvironmentEffectStage Stage;
    ResourceReference Material;
    bool Reflections;
};
struct EnvironmentLightning {
    WeatherLightning Strike;
    float Flash=0;
    std::uint64_t Sequence=0;
};
struct EnvironmentSnapshot {
    std::vector<EnvironmentOverride> Values;
    std::vector<EnvironmentResolvedEffect> Effects;
    std::vector<EnvironmentLightning> Lightning;
    double TimeSeconds=0;
};

template<class T>
const T* FindEnvironmentValue(const EnvironmentSnapshot& snapshot,std::string_view target,std::string_view parameter) {
    for(const auto& entry:snapshot.Values)
        if(entry.Target==target&&entry.Parameter==parameter)return std::get_if<T>(&entry.Value);
    return nullptr;
}

struct EnvironmentEvent {
    std::string Type;
    std::uint64_t Sequence=0; // Assigned by Runtime.
    std::vector<EnvironmentOverride> Payload;
};
struct EnvironmentEventDescriptor {
    std::string Type;
    // Payload fields are optional. Defaults describe the field types and initial values.
    std::vector<EnvironmentParameterDefinition> Parameters;
};
// Built-in event payload fields are Position, Height, Tint, Intensity,
// DurationSeconds, Radius, and SeedLow16/SeedHigh16. Omitted fields use the
// scene's legacy authored strike defaults. Independent strikes can overlap.
inline constexpr std::string_view EnvironmentLightningEventType="GameSprout.Lightning";

class GAMESPROUT_RUNTIME_PUBLIC_TYPE IEnvironmentScene {
public:
    virtual ~IEnvironmentScene()=default;
    virtual EnvironmentSettings GetAuthoredEnvironmentSettings() const=0;
};

// The owner thread registers definitions and advances scene clocks. Rendering
// only resolves snapshots; it neither advances time nor dispatches events.
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IEnvironmentRuntime {
public:
    using EnvironmentEventCallback=std::function<void(IScene&,const EnvironmentEvent&)>;
    virtual ~IEnvironmentRuntime()=default;
    virtual bool RegisterEnvironmentParameter(const EnvironmentParameterDefinition&)=0;
    virtual bool RegisterEnvironmentEffect(const EnvironmentEffectDescriptor&)=0;
    virtual bool UnregisterEnvironmentParameter(const std::string&)=0;
    virtual bool UnregisterEnvironmentEffect(const std::string&)=0;
    virtual std::vector<EnvironmentParameterDefinition> GetEnvironmentParameters() const=0;
    virtual std::vector<EnvironmentEffectDescriptor> GetEnvironmentEffects() const=0;
    virtual bool RegisterEnvironmentEvent(const EnvironmentEventDescriptor&)=0;
    virtual bool UnregisterEnvironmentEvent(const std::string&)=0;
    virtual std::vector<EnvironmentEventDescriptor> GetEnvironmentEvents() const=0;
    virtual bool GetEnvironmentSettings(const IScene&,EnvironmentSettings&) const=0;
    virtual bool SetEnvironmentSettings(IScene&,const EnvironmentSettings&)=0;
    virtual bool ResolveEnvironment(const IScene&,Vector3 sample,EnvironmentSnapshot&) const=0;
    virtual bool EmitEnvironmentEvent(IScene&,const EnvironmentEvent&)=0;
    virtual std::uint64_t SubscribeEnvironmentEvents(EnvironmentEventCallback)=0;
    virtual void UnsubscribeEnvironmentEvents(std::uint64_t)=0;
    virtual bool AdvanceEnvironmentPreview(IScene&,float deltaSeconds)=0;
};

inline IEnvironmentRuntime* QueryEnvironmentRuntime(IGameSproutRuntime& runtime) {
    return dynamic_cast<IEnvironmentRuntime*>(&runtime);
}
inline const IEnvironmentRuntime* QueryEnvironmentRuntime(const IGameSproutRuntime& runtime) {
    return dynamic_cast<const IEnvironmentRuntime*>(&runtime);
}
inline const IEnvironmentScene* QueryEnvironmentScene(const IScene& scene) {
    return dynamic_cast<const IEnvironmentScene*>(&scene);
}

}
