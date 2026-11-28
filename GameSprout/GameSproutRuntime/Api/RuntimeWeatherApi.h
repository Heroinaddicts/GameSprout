#pragma once
#include "GameSproutRuntimeApi.h"
#include "Math/Vector2.h"
#include <array>
#include <cmath>

namespace GameSproutRuntime {

struct WeatherCloudLayer {
    float Coverage = .22f;          // [0, 1] fraction of the sky covered by cloud.
    float Density = .75f;           // [0, 1] opacity of covered regions.
    float Scale = 2;                // [0.125, 64] relative pattern frequency.
    float WindDirectionDegrees = 0; // World XZ, zero is +X and 90 is +Z.
    float WindSpeed = .018f;        // Texture-coordinate cycles per second, [0, 2].
    bool operator==(const WeatherCloudLayer&) const = default;
};

struct WeatherPreset {
    std::array<WeatherCloudLayer,2> Clouds{{{}, {.12f,.35f,5,70,.01f}}};
    float StarIntensity = 1;
    float StarDensity = .3f;
    float StarRotationDegrees = 0;
    Color CloudColor{1,1,1,1}; // Linear RGB.
    float FogStrength = 1;
    float DaylightMultiplier = 1;
    float AmbientMultiplier = 1;

    bool operator==(const WeatherPreset& other) const {
        return Clouds==other.Clouds&&StarIntensity==other.StarIntensity&&StarDensity==other.StarDensity&&
            StarRotationDegrees==other.StarRotationDegrees&&CloudColor.R==other.CloudColor.R&&
            CloudColor.G==other.CloudColor.G&&CloudColor.B==other.CloudColor.B&&CloudColor.A==other.CloudColor.A&&
            FogStrength==other.FogStrength&&DaylightMultiplier==other.DaylightMultiplier&&
            AmbientMultiplier==other.AmbientMultiplier;
    }
};

struct WeatherLightning {
    Vector3 Position;        // World-space strike endpoint in metres.
    float Height = 80;      // Length of the strike above the endpoint, in metres.
    Color Tint{.67f,.79f,1,1};
    float Intensity = 6;    // Peak linear radiance multiplier.
    float DurationSeconds = .45f;
    float Radius = 100;     // Radius of local illumination in metres.
    std::uint32_t Seed = 0; // Deterministic bolt shape.
    bool operator==(const WeatherLightning& other) const {
        return Position.X==other.Position.X&&Position.Y==other.Position.Y&&Position.Z==other.Position.Z&&
            Height==other.Height&&Tint.R==other.Tint.R&&Tint.G==other.Tint.G&&Tint.B==other.Tint.B&&
            Tint.A==other.Tint.A&&Intensity==other.Intensity&&DurationSeconds==other.DurationSeconds&&
            Radius==other.Radius&&Seed==other.Seed;
    }
};

struct WeatherSettings {
    bool Enabled = false;
    float TimeOfDayHours = 12; // Hours, wrapped to [0, 24).
    float DayLengthSeconds = 0; // Simulation seconds for 24 hours. Zero holds the specified time.
    WeatherPreset Preset;
    WeatherLightning Lightning; // Default strike for authoring tools.
    bool operator==(const WeatherSettings&) const = default;
};

struct WeatherState {
    bool Enabled = false;
    float TimeOfDayHours = 12;
    float Daylight = 1;      // Smoothed sunlight visibility, [0, 1].
    WeatherPreset Preset;    // Blended preset used for the current frame.
    std::array<Vector2,2> CloudOffsets{}; // XZ offsets in texture coordinates.
    float LightningFlash = 0; // [0, 1] fraction of the strike's peak intensity.
    WeatherLightning Lightning;
    std::uint64_t LightningSequence = 0;
};

struct WeatherLightningEvent {
    std::uint64_t Sequence = 0; // Unique within the owning Runtime.
    WeatherLightning Lightning;
};

inline bool IsValidWeatherPreset(const WeatherPreset& value) {
    const auto range=[](float field,float minimum,float maximum) {
        return std::isfinite(field)&&field>=minimum&&field<=maximum;
    };
    for(const auto& layer:value.Clouds) {
        if(!range(layer.Coverage,0,1)||!range(layer.Density,0,1)||!range(layer.Scale,.125f,64)||
           !std::isfinite(layer.WindDirectionDegrees)||!range(layer.WindSpeed,0,2))return false;
    }
    const auto& tint=value.CloudColor;
    return range(value.StarIntensity,0,16)&&range(value.StarDensity,0,1)&&
        std::isfinite(value.StarRotationDegrees)&&range(tint.R,0,8)&&range(tint.G,0,8)&&
        range(tint.B,0,8)&&range(tint.A,0,1)&&range(value.FogStrength,0,4)&&
        range(value.DaylightMultiplier,0,4)&&range(value.AmbientMultiplier,0,4);
}

inline bool IsValidWeatherLightning(const WeatherLightning& value) {
    const auto range=[](float field,float minimum,float maximum) {
        return std::isfinite(field)&&field>=minimum&&field<=maximum;
    };
    const auto& tint=value.Tint;
    return range(value.Position.X,-1e7f,1e7f)&&range(value.Position.Y,-1e7f,1e7f)&&
        range(value.Position.Z,-1e7f,1e7f)&&range(value.Height,.1f,1e5f)&&
        range(tint.R,0,8)&&range(tint.G,0,8)&&range(tint.B,0,8)&&range(tint.A,0,1)&&
        range(value.Intensity,0,64)&&range(value.DurationSeconds,.02f,10)&&range(value.Radius,.1f,1e5f);
}

inline bool IsValidWeatherSettings(const WeatherSettings& value) {
    return std::isfinite(value.TimeOfDayHours)&&std::isfinite(value.DayLengthSeconds)&&
        value.DayLengthSeconds>=0&&IsValidWeatherPreset(value.Preset)&&IsValidWeatherLightning(value.Lightning);
}

// Read-only query for serialization without a Runtime handle.
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IWeatherScene {
public:
    virtual ~IWeatherScene() = default;
    virtual WeatherSettings GetAuthoredWeatherSettings() const = 0;
};

inline const IWeatherScene* QueryWeatherScene(const IScene& scene) {
    return dynamic_cast<const IWeatherScene*>(&scene);
}

// Owner-thread service. The Runtime owns the interface and the scene clocks.
// Renderers use the current snapshot without advancing it or dispatching events.
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IWeatherRuntime {
public:
    using LightningCallback=std::function<void(IScene&,const WeatherLightningEvent&)>;
    virtual ~IWeatherRuntime() = default;
    virtual bool GetWeatherSettings(const IScene&,WeatherSettings&) const = 0;
    virtual bool SetWeatherSettings(IScene&,const WeatherSettings&) = 0;
    virtual bool GetWeatherState(const IScene&,WeatherState&) const = 0;
    virtual bool SetTimeOfDay(IScene&,float hours) = 0;
    virtual bool TransitionWeather(IScene&,const WeatherPreset&,float durationSeconds) = 0;
    // Starts the strike now. Listener notifications are delivered once at the
    // next Runtime event boundary. Unsubscribe before unloading callback code.
    virtual bool TriggerLightning(IScene&,const WeatherLightning&) = 0;
    virtual std::uint64_t SubscribeLightning(LightningCallback) = 0;
    virtual void UnsubscribeLightning(std::uint64_t) = 0;
    // The Editor may advance an edit-scene preview explicitly while no Play
    // execution is active. Simulation and views use the regular Runtime clock.
    virtual bool AdvanceWeatherPreview(IScene&,float deltaSeconds) = 0;
};

inline IWeatherRuntime* QueryWeatherRuntime(IGameSproutRuntime& runtime) {
    return dynamic_cast<IWeatherRuntime*>(&runtime);
}

inline const IWeatherRuntime* QueryWeatherRuntime(const IGameSproutRuntime& runtime) {
    return dynamic_cast<const IWeatherRuntime*>(&runtime);
}

}
