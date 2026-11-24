#pragma once

#include "RuntimeRenderer.h"

namespace GameSproutRuntime {
template<class TextureData>
GRHI::TextureDesc PreparedTextureDescription(const TextureData& data) {
    GRHI::TextureDesc description;
    description.Width=data.Width;
    description.Height=data.Height;
    description.Format=data.Format==TexturePixelFormat::RGBA16Float?GRHI::TextureFormat::RGBA16_Float:
        data.SRGB?GRHI::TextureFormat::RGBA8_UNorm_sRGB:GRHI::TextureFormat::RGBA8_UNorm;
    description.Usage=GRHI::TextureUsage::Sampled;
    description.MipLevels=data.MipLevels;
    description.Dimension=data.Dimension==TextureDimension::Cube?GRHI::TextureDimension::Cube:GRHI::TextureDimension::Texture2D;
    return description;
}
}
