#pragma once

#include "../RuntimeResourceApi.h"
#include <algorithm>
#include <bit>
#include <limits>

namespace GameSproutRuntime::PreparedTextureLayout {

inline unsigned PixelBytes(TexturePixelFormat format) {
    switch(format) {
    case TexturePixelFormat::RGBA8: return 4;
    case TexturePixelFormat::RGBA16Float: return 8;
    }
    return 0;
}

template<class TextureData>
bool Validate(TextureData& data,std::uint64_t byteCount) {
    const auto bytesPerPixel=PixelBytes(data.Format);
    if(!data.Width||!data.Height||!bytesPerPixel||!data.MipLevels||
       data.MipLevels>std::bit_width(std::max(data.Width,data.Height))||
       (data.Format==TexturePixelFormat::RGBA16Float&&data.SRGB))return false;
    const bool cube=data.Dimension==TextureDimension::Cube;
    if((cube&&data.Width!=data.Height)||(!cube&&data.Dimension!=TextureDimension::Texture2D))return false;
    const auto layers=cube?6u:1u;
    if(data.Subresources.empty()&&!cube&&data.MipLevels==1&&data.Width<=UINT32_MAX/bytesPerPixel)
        data.Subresources.push_back({0,0,data.Width,data.Height,data.Width*bytesPerPixel,0});
    if(data.Subresources.size()!=data.MipLevels*layers)return false;
    std::uint64_t end=0;
    for(std::size_t i=0;i<data.Subresources.size();++i) {
        const auto& sub=data.Subresources[i];
        const auto mip=static_cast<std::uint32_t>(i/layers);
        const auto width=std::max(1u,data.Width>>mip),height=std::max(1u,data.Height>>mip);
        if(sub.MipLevel!=mip||sub.ArrayLayer!=i%layers||sub.Width!=width||sub.Height!=height||
           sub.RowPitch<std::uint64_t(width)*bytesPerPixel||sub.RowPitch%bytesPerPixel||
           sub.Offset<end||sub.Offset>byteCount||height>(byteCount-sub.Offset)/sub.RowPitch)return false;
        end=sub.Offset+std::uint64_t(sub.RowPitch)*height;
    }
    return true;
}

template<class Descriptor>
void CopyDescriptor(TextureRenderData& data,const Descriptor& descriptor) {
    data.Width=descriptor.Width;
    data.Height=descriptor.Height;
    data.SRGB=descriptor.SRGB;
    data.Sampler=descriptor.Sampler;
    data.Format=descriptor.Format;
    data.Dimension=descriptor.Dimension;
    data.MipLevels=descriptor.MipLevels;
    data.Subresources=descriptor.Subresources;
}
}
