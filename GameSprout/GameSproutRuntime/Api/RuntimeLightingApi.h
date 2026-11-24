#pragma once
#include "GameSproutRuntimeApi.h"
#include "RuntimeIndirectLightingData.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace GameSproutRuntime {
enum class AmbientLightMode : std::uint8_t { None, Color, Gradient, SphericalHarmonics, SkyEnvironment };
enum class ToneMapping : std::uint8_t { None, Neutral, ACES };

// Scene appearance, independent of the diffuse ambient-light source. All
// colors are linear RGB; distances are world-space metres from the view camera.
struct SceneAtmosphereSettings {
    bool Enabled = false;
    bool DrawSky = true;
    Color ZenithColor{.07f,.28f,.6f,1};
    Color HorizonColor{.48f,.72f,.85f,1};
    Color FogColor{.48f,.68f,.8f,1};
    float FogStart = 150;
    float FogEnd = 1000;
    float FogMaxOpacity = .65f;

    bool operator==(const SceneAtmosphereSettings& other) const {
        const auto sameColor=[](Color a,Color b){return a.R==b.R&&a.G==b.G&&a.B==b.B&&a.A==b.A;};
        return Enabled==other.Enabled&&DrawSky==other.DrawSky&&
            sameColor(ZenithColor,other.ZenithColor)&&sameColor(HorizonColor,other.HorizonColor)&&
            sameColor(FogColor,other.FogColor)&&FogStart==other.FogStart&&FogEnd==other.FogEnd&&
            FogMaxOpacity==other.FogMaxOpacity;
    }
};

struct SpotShadowBudgetSettings {
    // Per scene and viewport. Zero disables Spot shadows while preserving light.
    std::uint32_t MaxLights = 2;
    std::uint32_t MaxResolution = 1024; // Maximum edge in pixels per tile.

    bool operator==(const SpotShadowBudgetSettings&) const = default;
};

// A world-space horizontal mirror shared by opted-in Lit materials.
struct PlanarReflectionSettings {
    bool Enabled = false;
    float Height = 0; // World-space Y in metres.
    float ResolutionScale = .5f;
    std::uint32_t MaxResolution = 1024; // Maximum render-target edge.
    float Strength = 1; // Blends the planar contribution against ordinary environment specular.
    float MaxDistance = 100; // Limit for reflection receiver and capture distance in metres.
    std::uint32_t LayerMask = ~std::uint32_t{0}; // Objects drawn into the capture.
    bool operator==(const PlanarReflectionSettings&) const = default;
};

inline bool IsValidPlanarReflection(const PlanarReflectionSettings& settings) {
    return std::isfinite(settings.Height)&&std::isfinite(settings.ResolutionScale)&&
        settings.ResolutionScale>=.125f&&settings.ResolutionScale<=1&&
        settings.MaxResolution>=128&&settings.MaxResolution<=4096&&
        std::isfinite(settings.Strength)&&settings.Strength>=0&&settings.Strength<=1&&
        std::isfinite(settings.MaxDistance)&&settings.MaxDistance>0;
}

inline bool IsValidLightShadowResolution(std::uint32_t edge) {
    return edge>=256&&edge<=4096&&(edge&(edge-1))==0;
}

struct ReflectionProbeSettings {
    bool Enabled = true;
    bool BoxProjection = true;
    ResourceReference Environment; // Prepared cubemap captured in world axes.
    Vector3 CapturePosition;        // Camera origin of that capture in metres.
    Vector3 Center;                 // World-space centre of the influence box.
    Vector3 Size{10,10,10};         // Full axis-aligned box dimensions in metres.
    float BlendDistance = 1;       // Inner distance from the box edge over which the sky fades in.
    float Intensity = 1;
    std::int32_t Priority = 0;
    std::uint32_t CullingMask = ~0u;

    bool operator==(const ReflectionProbeSettings& other) const {
        const auto equal=[](Vector3 a,Vector3 b){return a.X==b.X&&a.Y==b.Y&&a.Z==b.Z;};
        return Enabled==other.Enabled&&BoxProjection==other.BoxProjection&&Environment==other.Environment&&
            equal(CapturePosition,other.CapturePosition)&&equal(Center,other.Center)&&equal(Size,other.Size)&&
            BlendDistance==other.BlendDistance&&Intensity==other.Intensity&&Priority==other.Priority&&
            CullingMask==other.CullingMask;
    }
};

inline bool IsValidReflectionProbe(const ReflectionProbeSettings& probe) {
    if(probe.Environment.Guid.empty()!=(probe.Environment.LocalID==0)||
       !std::isfinite(probe.Intensity)||probe.Intensity<0||
       !std::isfinite(probe.BlendDistance)||probe.BlendDistance<0||
       probe.BlendDistance>std::min({probe.Size.X,probe.Size.Y,probe.Size.Z})*.5f)return false;
    const float centers[]{probe.Center.X,probe.Center.Y,probe.Center.Z};
    const float sizes[]{probe.Size.X,probe.Size.Y,probe.Size.Z};
    const float captures[]{probe.CapturePosition.X,probe.CapturePosition.Y,probe.CapturePosition.Z};
    constexpr auto limit=std::numeric_limits<float>::max();
    for(unsigned axis=0;axis<3;++axis) {
        if(!std::isfinite(centers[axis])||!std::isfinite(sizes[axis])||
           !std::isfinite(captures[axis])||sizes[axis]*.5f<=0||
           std::fabs(double(centers[axis])-captures[axis])+double(sizes[axis])*.5>limit)return false;
    }
    return true;
}

struct EnvironmentLightingSettings {
    AmbientLightMode Mode = AmbientLightMode::None;
    // Linear RGB/HDR, nonnegative and finite; alpha is unused by lighting.
    Color AmbientColor{.2f,.2f,.2f,1};
    Color SkyColor{.2f,.2f,.2f,1};
    Color EquatorColor{.1f,.1f,.1f,1};
    Color GroundColor{.05f,.05f,.05f,1};
    float Intensity = 1;
    // World-space, already diffuse-convolved E/pi polynomial coefficients.
    // Basis: [1,y,z,x,xy,yz,3*z*z-1,xz,x*x-y*y]. Signed RGB is valid.
    // Evaluate directly: do not apply normalization constants or pi again.
    std::array<Vector3,9> SHCoefficients{};
    SceneAtmosphereSettings Atmosphere;
    // Optional prepared environment asset. It supplies the background cube,
    // diffuse irradiance for SkyEnvironment mode and prefiltered reflections.
    ResourceReference SkyEnvironment;
    float SkyRotationDegrees = 0; // Yaw about world +Y in Unity-style coordinates.
    float SkyIntensity = 1;       // Background multiplier, independent of diffuse Intensity.
    float ReflectionIntensity = 1;
    bool HDREnabled = false;       // Render scene color to linear FP16 before resolving to the viewport.
    float ExposureEV = 0;         // Fixed exposure [-16,16], in stops. +1 doubles linear radiance.
    ToneMapping Tonemap = ToneMapping::None;
    SpotShadowBudgetSettings SpotShadows;
    PlanarReflectionSettings PlanarReflection;
    std::vector<ReflectionProbeSettings> ReflectionProbes;
    std::vector<BakedIndirectVolume> IndirectLightVolumes;
};

inline bool IsValidEnvironmentLighting(const EnvironmentLightingSettings& settings) {
    if(settings.Mode!=AmbientLightMode::None && settings.Mode!=AmbientLightMode::Color &&
       settings.Mode!=AmbientLightMode::Gradient && settings.Mode!=AmbientLightMode::SphericalHarmonics &&
       settings.Mode!=AmbientLightMode::SkyEnvironment)return false;
    if(!std::isfinite(settings.Intensity)||settings.Intensity<0||settings.SpotShadows.MaxLights>4||
        !IsValidLightShadowResolution(settings.SpotShadows.MaxResolution)||
        !IsValidPlanarReflection(settings.PlanarReflection))return false;
    if(settings.SkyEnvironment.Guid.empty()!=(settings.SkyEnvironment.LocalID==0)||
       !std::isfinite(settings.SkyRotationDegrees)||!std::isfinite(settings.SkyIntensity)||settings.SkyIntensity<0||
       !std::isfinite(settings.ReflectionIntensity)||settings.ReflectionIntensity<0||
       !std::isfinite(settings.ExposureEV)||settings.ExposureEV< -16||settings.ExposureEV>16||
       (settings.Tonemap!=ToneMapping::None&&settings.Tonemap!=ToneMapping::Neutral&&settings.Tonemap!=ToneMapping::ACES))return false;
    for(const auto& color:{settings.AmbientColor,settings.SkyColor,settings.EquatorColor,settings.GroundColor,
        settings.Atmosphere.ZenithColor,settings.Atmosphere.HorizonColor,settings.Atmosphere.FogColor})
        if(!std::isfinite(color.R)||!std::isfinite(color.G)||!std::isfinite(color.B)||!std::isfinite(color.A)||
           color.R<0||color.G<0||color.B<0||color.A<0||color.A>1)return false;
    for(const auto& coefficient:settings.SHCoefficients)
        if(!std::isfinite(coefficient.X)||!std::isfinite(coefficient.Y)||!std::isfinite(coefficient.Z))return false;
    if(!std::all_of(settings.ReflectionProbes.begin(),settings.ReflectionProbes.end(),IsValidReflectionProbe))return false;
    if(settings.IndirectLightVolumes.size()>MaxBakedIndirectVolumes||
       !std::all_of(settings.IndirectLightVolumes.begin(),settings.IndirectLightVolumes.end(),IsValidBakedIndirectVolume))return false;
    const auto& atmosphere=settings.Atmosphere;
    if(!std::isfinite(atmosphere.FogStart)||atmosphere.FogStart<0||
       !std::isfinite(atmosphere.FogEnd)||atmosphere.FogEnd<=atmosphere.FogStart||
       !std::isfinite(atmosphere.FogMaxOpacity)||atmosphere.FogMaxOpacity<0||atmosphere.FogMaxOpacity>1)return false;
    return true;
}

enum class LightType : std::uint8_t {
    Directional = 0, Point, Spot
};

enum class StaticShadowUpdateMode : std::uint8_t {
    AutomaticIncremental = 0, // Reuse overlap and redraw newly covered texels when possible.
    FullCascade = 1           // Redraw each invalidated cascade in full; reuse valid cascades.
};

enum class ShadowFilterQuality : std::uint8_t {
    Low = 0,    // 2x2 PCF.
    Medium = 1, // 3x3 PCF.
    High = 2    // 5x5 PCF.
};

struct LightShadowSettings {
    bool Enabled = false;
    float Strength = 1;
    // Maximum shadow distance from the view camera in metres. Resolution is
    // the atlas edge for Directional and a single tile edge for Spot.
    float Distance = 50;
    std::uint32_t Resolution = 2048;
    std::uint32_t CascadeCount = 4;
    // Biases are measured in shadow texels, scaled separately for each cascade.
    // Runtime converts them to world/depth offsets; neither value is in metres.
    float DepthBias = .5f;
    float NormalBias = 1;
    bool Soft = true;
    // Cache rigid meshes marked ShadowCasterStatic separately. Other meshes
    // and skinned meshes update the dynamic atlas as the scene/view changes.
    bool CacheStaticCasters = false;
    StaticShadowUpdateMode StaticUpdateMode = StaticShadowUpdateMode::AutomaticIncremental;
    // All zero selects automatic splits. Otherwise provide CascadeCount - 1
    // increasing fractions of the camera's near-to-effective-far shadow range.
    // Unused entries must be zero. The last cascade always ends at effective far.
    std::array<float,3> CascadeSplits{};
    // Fraction of effective maximum camera-space shadow distance. 1 disables
    // fading inside the shadow range, 0 fades over the entire distance.
    float FadeStart = .9f;
    ShadowFilterQuality FilterQuality = ShadowFilterQuality::Medium; // Used when Soft is true.
    // Spot only: projection near plane in metres and priority for the view's
    // limited number of shadow maps. Higher priority is allocated first.
    float NearPlane = .1f;
    std::uint32_t Priority = 0;
    bool operator==(const LightShadowSettings&) const = default;
};

inline bool IsValidShadowCascadeSplits(std::uint32_t cascadeCount,const std::array<float,3>& splits) {
    if(cascadeCount!=1&&cascadeCount!=2&&cascadeCount!=4)return false;
    if(splits==std::array<float,3>{})return true;
    float previous=0;
    for(std::size_t index=0;index<splits.size();++index) {
        const float split=splits[index];
        if(index>=cascadeCount-1) {
            if(split!=0)return false;
            continue;
        }
        if(!(split>previous&&split<1))return false;
        previous=split;
    }
    return true;
}

inline bool IsValidLightShadows(const LightShadowSettings& settings) {
    return std::isfinite(settings.Strength)&&settings.Strength>=0&&settings.Strength<=1&&
        std::isfinite(settings.Distance)&&settings.Distance>0&&
        IsValidLightShadowResolution(settings.Resolution)&&
        IsValidShadowCascadeSplits(settings.CascadeCount,settings.CascadeSplits)&&
        std::isfinite(settings.DepthBias)&&settings.DepthBias>=0&&settings.DepthBias<=10&&
        std::isfinite(settings.NormalBias)&&settings.NormalBias>=0&&settings.NormalBias<=10&&
        std::isfinite(settings.FadeStart)&&settings.FadeStart>=0&&settings.FadeStart<=1&&
        std::isfinite(settings.NearPlane)&&settings.NearPlane>0&&settings.Priority<=255&&
        (settings.StaticUpdateMode==StaticShadowUpdateMode::AutomaticIncremental||
         settings.StaticUpdateMode==StaticShadowUpdateMode::FullCascade)&&
        (settings.FilterQuality==ShadowFilterQuality::Low||settings.FilterQuality==ShadowFilterQuality::Medium||
         settings.FilterQuality==ShadowFilterQuality::High);
}

struct LightSettings {
    LightType Type = LightType::Directional;
    bool Enabled = true;
    // Linear RGB, as defined by Color. HDR channels are allowed; alpha is not
    // used by lighting. The Editor converts its sRGB color picker at the boundary.
    GameSproutRuntime::Color Color{1,1,1,1};
    // URP-compatible relative intensity: the Lambert pi factor is absorbed in
    // the lighting convention. This is not an SI radiometric intensity unit.
    float Intensity = 1.0f;
    // Object Layer membership, independent of the camera's visibility mask.
    std::uint32_t CullingMask = 0xffffffffu;
    LightShadowSettings Shadows;
    // Point and Spot end smoothly at Range, in world-space metres. Angles
    // are full cone angles in degrees. Scale does not change range or cone.
    float Range = 10;
    float InnerSpotAngleDegrees = 30;
    float OuterSpotAngleDegrees = 45;
};

inline bool IsValidLocalLightGeometry(const LightSettings& settings) {
    return (settings.Type==LightType::Directional||settings.Type==LightType::Point||settings.Type==LightType::Spot)&&
        std::isfinite(settings.Range)&&settings.Range>0&&
        std::isfinite(settings.InnerSpotAngleDegrees)&&std::isfinite(settings.OuterSpotAngleDegrees)&&
        settings.InnerSpotAngleDegrees>=0&&settings.InnerSpotAngleDegrees<=settings.OuterSpotAngleDegrees&&
        settings.OuterSpotAngleDegrees>=1&&settings.OuterSpotAngleDegrees<=179&&
        (settings.Type!=LightType::Spot||!settings.Shadows.Enabled||settings.Shadows.NearPlane<settings.Range);
}

class GAMESPROUT_RUNTIME_PUBLIC_TYPE ILightComponent : public IComponent {
public:
    virtual const LightSettings& GetSettings() const = 0;
    // Invalid/nonfinite/negative values leave the complete state unchanged.
    // Directional and Spot emit along local +Z. Point emits in all directions.
    // Point and Spot use world position. Directional and Spot support shadows.
    virtual bool SetSettings(const LightSettings& settings) = 0;

    bool IsEnabled() const { return GetSettings().Enabled; }
    bool SetEnabled(bool enabled) { auto settings=GetSettings();settings.Enabled=enabled;return SetSettings(settings); }
    LightType GetLightType() const { return GetSettings().Type; }
    bool SetLightType(LightType type) { auto settings=GetSettings();settings.Type=type;return SetSettings(settings); }
    GameSproutRuntime::Color GetColor() const { return GetSettings().Color; }
    bool SetColor(GameSproutRuntime::Color color) { auto settings=GetSettings();settings.Color=color;return SetSettings(settings); }
    float GetIntensity() const { return GetSettings().Intensity; }
    bool SetIntensity(float intensity) { auto settings=GetSettings();settings.Intensity=intensity;return SetSettings(settings); }
    std::uint32_t GetCullingMask() const { return GetSettings().CullingMask; }
    bool SetCullingMask(std::uint32_t mask) { auto settings=GetSettings();settings.CullingMask=mask;return SetSettings(settings); }
    float GetRange() const { return GetSettings().Range; }
    bool SetRange(float metres) { auto settings=GetSettings();settings.Range=metres;return SetSettings(settings); }
    float GetInnerSpotAngleDegrees() const { return GetSettings().InnerSpotAngleDegrees; }
    float GetOuterSpotAngleDegrees() const { return GetSettings().OuterSpotAngleDegrees; }
    bool SetSpotAngles(float innerDegrees,float outerDegrees) {
        auto settings=GetSettings();settings.InnerSpotAngleDegrees=innerDegrees;
        settings.OuterSpotAngleDegrees=outerDegrees;return SetSettings(settings);
    }
    const LightShadowSettings& GetShadows() const { return GetSettings().Shadows; }
    bool SetShadows(const LightShadowSettings& shadows) { auto settings=GetSettings();settings.Shadows=shadows;return SetSettings(settings); }
    // Rebuild all cascades or the spot tile on the next submitted shadow render
    // in each viewport. Requests coalesce and do not change authored settings.
    virtual void RequestShadowRefresh() = 0;

protected:
    using IComponent::IComponent;
};
}
