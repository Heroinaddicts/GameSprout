#pragma once

#include "Internal/PreparedTextureLayout.h"
#include <bit>
#include <cmath>
#include <cstring>

namespace PreparedResources::EnvironmentAssetFormat {
inline constexpr char Magic[]="GSENV001";
inline constexpr std::size_t HeaderBytes=24+9*3*4;

inline std::uint32_t ReadWord(std::span<const std::uint8_t> bytes,std::size_t offset) {
    return std::uint32_t(bytes[offset])|(std::uint32_t(bytes[offset+1])<<8)|
        (std::uint32_t(bytes[offset+2])<<16)|(std::uint32_t(bytes[offset+3])<<24);
}

inline bool Describe(std::span<const std::uint8_t> bytes,GameSproutRuntime::EnvironmentRenderData& data) {
    using namespace GameSproutRuntime;
    if(bytes.size()<HeaderBytes||std::memcmp(bytes.data(),Magic,8)!=0||ReadWord(bytes,20)!=0)return false;
    const std::uint32_t skySize=ReadWord(bytes,8),specularSize=ReadWord(bytes,12),specularLevels=ReadWord(bytes,16);
    if(!std::has_single_bit(skySize)||!std::has_single_bit(specularSize)||
       skySize>UINT32_MAX/8||specularSize>UINT32_MAX/8||specularLevels!=std::bit_width(specularSize))return false;
    std::size_t coefficientOffset=24;
    for(auto& coefficient:data.DiffuseSH) {
        float channels[3];
        for(auto& channel:channels) {
            channel=std::bit_cast<float>(ReadWord(bytes,coefficientOffset));
            coefficientOffset+=4;
            if(!std::isfinite(channel))return false;
        }
        coefficient={channels[0],channels[1],channels[2]};
    }
    std::uint64_t offset=HeaderBytes;
    const auto describeCube=[&](TextureRenderData& texture,std::uint32_t size,std::uint32_t levels) {
        texture.Width=size;texture.Height=size;texture.MipLevels=levels;
        texture.Format=TexturePixelFormat::RGBA16Float;texture.Dimension=TextureDimension::Cube;
        texture.SRGB=false;texture.Sampler.MipFilter=TextureFilter::Linear;
        const std::uint64_t start=offset;
        for(std::uint32_t mip=0;mip<levels;++mip,size>>=1) {
            const std::uint32_t pitch=size*8;
            for(std::uint32_t face=0;face<6;++face) {
                if(size>(bytes.size()-offset)/pitch)return false;
                texture.Subresources.push_back({mip,face,size,size,pitch,offset-start});
                offset+=std::uint64_t(size)*pitch;
            }
        }
        return true;
    };
    return describeCube(data.Sky,skySize,1)&&describeCube(data.Specular,specularSize,specularLevels)&&offset==bytes.size();
}
}
