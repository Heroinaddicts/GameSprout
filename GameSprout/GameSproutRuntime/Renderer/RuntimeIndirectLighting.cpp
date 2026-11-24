#include "RuntimeRenderResourceCache.h"
#include "RuntimeEnvironment.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace GameSproutRuntime {
namespace {

std::uint16_t EncodeHalf(float value) {
    // Inputs are finite and in the half-float range. Round to nearest even
    // for both normal and subnormal results.
    const auto bits=std::bit_cast<std::uint32_t>(value);
    const auto sign=(bits>>16)&0x8000u;
    const auto exponent=(bits>>23)&0xffu;
    const auto mantissa=bits&0x7fffffu;
    if(exponent<102u) return static_cast<std::uint16_t>(sign);
    if(exponent<113u) {
        const auto shift=126u-exponent;
        const auto significand=mantissa|0x800000u;
        const auto round=(1u<<(shift-1))-1u+((significand>>shift)&1u);
        return static_cast<std::uint16_t>(sign|((significand+round)>>shift));
    }
    const auto rounded=(mantissa+0xfffu+((mantissa>>13)&1u))>>13;
    return static_cast<std::uint16_t>(sign|(((exponent-112u)<<10)+rounded));
}

void PackRegion(const IndirectLightingRenderData& source,std::uint32_t width,std::uint32_t firstRow,
    ByteBuffer& pixels) {
    const auto [nx,ny,nz]=source.Dimensions;
    std::size_t sample=0;
    for(std::uint32_t z=0;z<nz;++z)
        for(std::uint32_t y=0;y<ny;++y)
            for(std::uint32_t x=0;x<nx;++x) {
                for(std::uint32_t basis=0;basis<9;++basis) {
                    const auto value=source.Coefficients[sample][basis];
                    const std::uint16_t rgba[]={EncodeHalf(value.X),EncodeHalf(value.Y),EncodeHalf(value.Z),0x3c00u};
                    const std::size_t offset=((std::size_t(firstRow)+basis*ny+y)*width+z*nx+x)*sizeof(rgba);
                    std::memcpy(pixels.data()+offset,rgba,sizeof(rgba));
                }
                ++sample;
            }
}

}

const Renderer::IndirectAtlasGPU& Renderer::IndirectAtlas(const std::vector<ResourceKey>& assets) {
    auto& atlases=_ResourceCache.IndirectAtlases;
    auto cached=atlases.find(assets);
    if(cached==atlases.end()) {
        constexpr std::size_t retainedCombinations=4;
        if(atlases.size()>=retainedCombinations) {
            const auto oldest=std::min_element(atlases.begin(),atlases.end(),[](const auto& a,const auto& b) {
                return a.second.LastUsed<b.second.LastUsed;
            });
            atlases.erase(oldest);
        }
        cached=atlases.try_emplace(assets).first;
    }
    auto& atlas=cached->second;
    atlas.LastUsed=++_ResourceCache.IndirectAtlasAccess;
    std::vector<std::uint64_t> revisions;
    revisions.reserve(assets.size());
    for(const auto& asset:assets)revisions.push_back(_Resources.GetResourceRevision({asset.Guid,asset.LocalID}));
    if(atlas.ResourceRevisions==revisions)return atlas;

    std::vector<IndirectLightingRenderData> grids(assets.size());
    IndirectAtlasGPU replacement;
    replacement.LastUsed=atlas.LastUsed;
    replacement.Regions.resize(assets.size());
    replacement.ResourceRevisions=std::move(revisions);
    std::uint32_t width=0,height=0;
    for(std::size_t index=0;index<assets.size();++index) {
        const auto& asset=assets[index];
        auto& grid=grids[index];
        std::string error;
        if(!_Resources.LoadIndirectLighting({asset.Guid,asset.LocalID},grid,error)||!IsValidIndirectLightingGrid(grid)) {
            replacement.Error="Indirect lighting asset "+asset.Guid+"_"+std::to_string(asset.LocalID)+": "+
                (error.empty()?"invalid grid":error);
            continue;
        }
        replacement.Regions[index]={grid.Dimensions,height};
        width=std::max(width,grid.Dimensions[0]*grid.Dimensions[2]);
        height+=9*grid.Dimensions[1];
    }
    if(width&&height) {
        TextureRenderData data;
        data.Width=width;data.Height=height;
        data.SRGB=false;data.Format=TexturePixelFormat::RGBA16Float;
        const std::size_t bytes=std::size_t(width)*height*8;
        data.Pixels.resize(bytes);
        std::memset(data.Pixels.data(),0,bytes);
        for(std::size_t index=0;index<grids.size();++index) {
            const auto& region=replacement.Regions[index];
            if(region.Dimensions[0])PackRegion(grids[index],width,region.FirstRow,data.Pixels);
        }
        std::string uploadError;
        replacement.Texture.reset(UploadPreparedTexture(data,uploadError));
        if(!replacement.Texture)replacement.Error=std::move(uploadError);
    }
    atlas=std::move(replacement);
    return atlas;
}

void Renderer::PrepareIndirectLighting(const EnvironmentLightingSettings& settings,RenderEnvironment::Uniform& uniform) {
    _IndirectLightingAtlas=nullptr;
    if(settings.IndirectLightVolumes.empty())return;

    struct Entry {
        const BakedIndirectVolume* Volume;
        ResourceKey Asset;
    };
    std::vector<Entry> entries;
    std::vector<ResourceKey> assets;
    for(const auto& volume:settings.IndirectLightVolumes)
        if(!volume.Asset.Guid.empty()&&volume.Intensity>0) {
            entries.push_back({&volume,ResourceKey(volume.Asset)});
            assets.emplace_back(volume.Asset);
        }
    if(entries.empty())return;
    std::sort(assets.begin(),assets.end());
    assets.erase(std::unique(assets.begin(),assets.end(),[](const auto& a,const auto& b) {
        return a.Guid==b.Guid&&a.LocalID==b.LocalID;
    }),assets.end());
    const auto& atlas=IndirectAtlas(assets);
    if(!atlas.Error.empty())Diagnostic(atlas.Error);
    if(!atlas.Texture)return;

    std::stable_sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
        return a.Volume->Priority<b.Volume->Priority;
    });
    std::size_t count=0;
    for(const auto& entry:entries) {
        const auto index=std::lower_bound(assets.begin(),assets.end(),entry.Asset)-assets.begin();
        const auto& region=atlas.Regions[index];
        if(!region.Dimensions[0])continue;
        const auto& volume=*entry.Volume;
        const auto minimum=volume.Center-volume.Size*.5f;
        const auto inverseFade=volume.BlendDistance>0?1.f/volume.BlendDistance:0.f;
        uniform.IndirectVolumes[count++]={
            {minimum.X,minimum.Y,minimum.Z,inverseFade},
            {1.f/volume.Size.X,1.f/volume.Size.Y,1.f/volume.Size.Z,volume.Intensity},
            {float(region.Dimensions[0]),float(region.Dimensions[1]),float(region.Dimensions[2]),float(region.FirstRow)}};
    }
    _IndirectLightingAtlas=atlas.Texture.get();
    uniform.IndirectInfo={1.f/_IndirectLightingAtlas->GetWidth(),1.f/_IndirectLightingAtlas->GetHeight(),float(count),0.f};
}

bool Renderer::UsesIndirectLighting(const PreparedDraw&) const {
    // Every instance sees the same atlas. The shader filters by world position,
    // so a group can straddle a room edge without being split into new draws.
    return _IndirectLightingAtlas!=nullptr;
}

void Renderer::BindIndirectLighting(GRHI::IRenderPass& pass,const PreparedDraw&,RenderEnvironment::Uniform& uniform) {
    if(!_IndirectLightingAtlas)uniform.IndirectInfo={};
    pass.SetFragmentTextureSampler(8,_IndirectLightingAtlas?_IndirectLightingAtlas:_White,_DefaultSampler);
}

}
