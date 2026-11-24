#pragma once
#include "Api/RuntimeLightingApi.h"
#include "Api/Math/Vector4.h"
#include <array>
#include <limits>

namespace GameSproutRuntime::RenderEnvironment {
struct IndirectVolume {
    Vector4 MinimumAndInvBlend; // XYZ minimum, W inverse inner fade distance.
    Vector4 InvSizeAndIntensity; // XYZ reciprocal extent, W intensity.
    Vector4 GridAndAtlasRow; // Grid XYZ and vertical offset into the coefficient atlas.
};
// Scene-owned environment, shared by all draw calls in the view.
struct Uniform {
    Vector4 Info; // X: AmbientLightMode. Remaining components are zero padding.
    Vector4 AmbientColor,SkyColor,EquatorColor,GroundColor;
    std::array<Vector4,9> SH;
    Vector4 AtmosphereParameters; // Enabled, fog start/end metres, maximum opacity.
    Vector4 AtmosphereFogColor,AtmosphereZenithColor,AtmosphereHorizonColor;
    Vector4 SkyTransform; // Inverse yaw cosine/sine, highest reflection mip, reflection intensity.
    Vector4 ProbeCenter; // XYZ influence box centre; W inner blend distance.
    Vector4 ProbeExtent; // XYZ influence box half size; W intensity.
    Vector4 ProbePosition; // XYZ capture origin; W signed mip count (negative disables box projection).
    Vector4 IndirectInfo; // Inverse atlas width and height, active volume count.
    std::array<IndirectVolume,MaxBakedIndirectVolumes> IndirectVolumes;
};
static_assert(sizeof(IndirectVolume)==48 && sizeof(Uniform)==752,
    "Environment fragment block must match HLSL packing");

inline Vector2 SkyYaw(float degrees) {
    constexpr double radiansPerDegree=3.14159265358979323846/180.0;
    const double radians=std::remainder(double(degrees),360.0)*radiansPerDegree;
    return {float(std::cos(radians)),float(std::sin(radians))};
}

// Intensity is applied once while preparing this frame's scene state. Overflow
// in derived HDR radiance is diagnosed by the Renderer; no Infinity is uploaded.
// Inactive modes' stored colors/coefficients are deliberately not multiplied.
inline bool Prepare(const EnvironmentLightingSettings& settings,Uniform& output,
    const std::array<Vector3,9>* skyDiffuse=nullptr,float reflectionMaxLOD=0) {
    if(!IsValidEnvironmentLighting(settings))return false;
    Uniform candidate{};
    const auto mode=settings.Mode==AmbientLightMode::SkyEnvironment&&!skyDiffuse?AmbientLightMode::Gradient:settings.Mode;
    candidate.Info.X=static_cast<float>(mode);
    const auto yaw=SkyYaw(settings.SkyRotationDegrees);
    candidate.SkyTransform={yaw.X,yaw.Y,reflectionMaxLOD,settings.ReflectionIntensity};
    const auto& atmosphere=settings.Atmosphere;
    candidate.AtmosphereParameters={atmosphere.Enabled?1.0f:0.0f,atmosphere.FogStart,atmosphere.FogEnd,atmosphere.FogMaxOpacity};
    candidate.AtmosphereFogColor={atmosphere.FogColor.R,atmosphere.FogColor.G,atmosphere.FogColor.B,0};
    candidate.AtmosphereZenithColor={atmosphere.ZenithColor.R,atmosphere.ZenithColor.G,atmosphere.ZenithColor.B,0};
    candidate.AtmosphereHorizonColor={atmosphere.HorizonColor.R,atmosphere.HorizonColor.G,atmosphere.HorizonColor.B,0};
    const auto scaled=[&](double value,float& destination) {
        const double result=value*settings.Intensity;
        if(!std::isfinite(result)||std::fabs(result)>std::numeric_limits<float>::max())return false;
        destination=static_cast<float>(result);return true;
    };
    const auto color=[&](Color source,Vector4& destination) {
        return scaled(source.R,destination.X)&&scaled(source.G,destination.Y)&&scaled(source.B,destination.Z);
    };
    switch(mode) {
    case AmbientLightMode::None:break;
    case AmbientLightMode::Color:
        if(!color(settings.AmbientColor,candidate.AmbientColor))return false;
        break;
    case AmbientLightMode::Gradient:
        if(!color(settings.SkyColor,candidate.SkyColor)||!color(settings.EquatorColor,candidate.EquatorColor)||
           !color(settings.GroundColor,candidate.GroundColor))return false;
        break;
    case AmbientLightMode::SphericalHarmonics:
    case AmbientLightMode::SkyEnvironment:
        const auto& coefficients=mode==AmbientLightMode::SkyEnvironment?*skyDiffuse:settings.SHCoefficients;
        for(std::size_t index=0;index<candidate.SH.size();++index) {
            const auto source=coefficients[index];auto& destination=candidate.SH[index];
            if(!scaled(source.X,destination.X)||!scaled(source.Y,destination.Y)||!scaled(source.Z,destination.Z))return false;
        }
        // Signed finite coefficients may still overflow a float shader sum.
        // Bound every term on the unit sphere, including (3*z*z-1) in [-1,2].
        // Keep uncertain extreme HDR configurations out of the GPU evaluator.
        {
            constexpr double basisMaximum[]={1,1,1,1,.5,.5,2,.5,1};
            double maximum[3]{};
            for(std::size_t index=0;index<candidate.SH.size();++index) {
                const auto coefficient=candidate.SH[index];
                maximum[0]+=std::fabs(double(coefficient.X))*basisMaximum[index];
                maximum[1]+=std::fabs(double(coefficient.Y))*basisMaximum[index];
                maximum[2]+=std::fabs(double(coefficient.Z))*basisMaximum[index];
            }
            for(double channel:maximum)if(channel>std::numeric_limits<float>::max())return false;
        }
        break;
    }
    output=candidate;return true;
}
}
