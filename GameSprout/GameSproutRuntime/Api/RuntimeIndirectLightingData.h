#pragma once

#include "Math/Vector3.h"
#include "ResourceReference.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace GameSproutRuntime {

// Bounds are a world-space axis-aligned volume. Coefficient sample locations
// are normalized across Size, with a centered sample on singleton axes.
struct BakedIndirectVolume {
    ResourceReference Asset;
    Vector3 Center{};
    Vector3 Size{8,4,8};
    float Intensity=1; // Zero disables this volume.
    float BlendDistance=1; // Fade from the inner boundary in metres.
    std::uint32_t Priority=0; // A larger value composites over smaller values.
    bool operator==(const BakedIndirectVolume& other) const {
        return Asset==other.Asset &&
            Center.X==other.Center.X&&Center.Y==other.Center.Y&&Center.Z==other.Center.Z&&
            Size.X==other.Size.X&&Size.Y==other.Size.Y&&Size.Z==other.Size.Z&&
            Intensity==other.Intensity&&BlendDistance==other.BlendDistance&&Priority==other.Priority;
    }
};

inline constexpr std::size_t MaxBakedIndirectVolumes=8;
inline constexpr std::uint32_t MaxBakedIndirectAxis=32;
inline bool IsValidBakedIndirectVolume(const BakedIndirectVolume& volume) {
    if (volume.Asset.Guid.empty()!=(volume.Asset.LocalID==0)||
        !std::isfinite(volume.Intensity)||volume.Intensity<0||volume.Intensity>16||
        !std::isfinite(volume.BlendDistance)||volume.BlendDistance<0||volume.Priority>255) return false;
    if (volume.BlendDistance>0&&!std::isfinite(1.f/volume.BlendDistance)) return false;
    const auto validAxis=[](float center,float size) {
        const float minimum=center-.5f*size,maximum=center+.5f*size;
        return std::isfinite(minimum)&&std::isfinite(maximum)&&minimum<maximum&&std::isfinite(1.f/size);
    };
    return validAxis(volume.Center.X,volume.Size.X)&&validAxis(volume.Center.Y,volume.Size.Y)&&
        validAxis(volume.Center.Z,volume.Size.Z);
}

struct IndirectLightingRenderData {
    std::array<std::uint32_t,3> Dimensions{};
    // X, Y, then Z (X fastest). Coefficients are world-space diffuse E/pi:
    // [1,y,z,x,xy,yz,3*z*z-1,xz,x*x-y*y], already normalized and convolved.
    std::vector<std::array<Vector3,9>> Coefficients;
};

inline std::uint32_t BakedIndirectSampleCount(const std::array<std::uint32_t,3>& dimensions) {
    std::uint32_t count=1;
    for (const auto extent:dimensions) {
        if (!extent||extent>MaxBakedIndirectAxis) return 0;
        count*=extent;
    }
    return count;
}

inline bool IsValidIndirectLightingGrid(const IndirectLightingRenderData& data) {
    const auto count=BakedIndirectSampleCount(data.Dimensions);
    if (!count||count!=data.Coefficients.size()) return false;
    // The Runtime packs valid linear coefficients into a GPU half-float atlas.
    for (const auto& sample:data.Coefficients)
        for (const auto& value:sample)
            if (!(std::fabs(value.X)<=65504.f&&std::fabs(value.Y)<=65504.f&&std::fabs(value.Z)<=65504.f)) return false;
    return true;
}

}
