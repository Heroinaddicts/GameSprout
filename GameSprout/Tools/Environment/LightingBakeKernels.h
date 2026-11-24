#pragma once

#include "RuntimeIndirectLightingData.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace GameSproutLightingCapture {

struct LinearCubeCapture {
    std::uint32_t Edge=0;
    // Top-first rows of little-endian RGBA16F in GRHI layer order
    // +X, -X, +Y, -Y, +Z, -Z.
    std::array<std::vector<std::uint8_t>,6> Faces;
};
using SH9=std::array<GameSproutRuntime::Vector3,9>;

bool ProjectIrradiance(const LinearCubeCapture&,SH9&,std::string& error);
bool BuildReflectionAsset(const LinearCubeCapture&,std::uint32_t reflectionEdge,std::uint32_t samples,
    std::vector<std::uint8_t>& output,std::string& error,const std::function<bool()>& cancelled={});
bool BuildIrradianceAsset(const GameSproutRuntime::IndirectLightingRenderData&,
    std::vector<std::uint8_t>& output,std::string& error);

}
