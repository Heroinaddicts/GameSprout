#pragma once

#include "RuntimeIndirectLightingData.h"
#include <bit>
#include <cstring>
#include <span>

namespace PreparedResources::IndirectLightingAssetFormat {

inline bool Decode(std::span<const std::uint8_t> bytes,GameSproutRuntime::IndirectLightingRenderData& output) {
    constexpr std::size_t headerBytes=24;
    constexpr std::size_t coefficientBytes=4*3*9;
    if (bytes.size()<headerBytes||std::memcmp(bytes.data(),"GSIRR001",8)!=0) return false;
    const auto word=[&](std::size_t offset) {
        return std::uint32_t(bytes[offset])|(std::uint32_t(bytes[offset+1])<<8)|
            (std::uint32_t(bytes[offset+2])<<16)|(std::uint32_t(bytes[offset+3])<<24);
    };
    GameSproutRuntime::IndirectLightingRenderData data;
    data.Dimensions={word(8),word(12),word(16)};
    const auto count=GameSproutRuntime::BakedIndirectSampleCount(data.Dimensions);
    if (word(20)||!count||bytes.size()!=headerBytes+count*coefficientBytes) return false;
    data.Coefficients.resize(count);
    std::size_t offset=headerBytes;
    for (auto& sample:data.Coefficients)
        for (auto& coefficient:sample) {
            coefficient={std::bit_cast<float>(word(offset)),std::bit_cast<float>(word(offset+4)),
                         std::bit_cast<float>(word(offset+8))};
            offset+=12;
        }
    if (!GameSproutRuntime::IsValidIndirectLightingGrid(data)) return false;
    output=std::move(data);
    return true;
}

}
