#include "RuntimeRenderResourceCache.h"
#include "RuntimeTextureUpload.h"
#include "../Api/Internal/PreparedTextureLayout.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

namespace GameSproutRuntime {
namespace {
// Limit transient copying per presented render, not the resident texture set.
constexpr std::uint64_t UploadBytesPerFrame=8u*1024u*1024u;
constexpr std::size_t MaxPredictedRequests=16;
}

bool Renderer::PrefetchTexture(const ResourceReference& reference,bool visible) {
    if(reference.Guid.empty()||!reference.LocalID)return false;
    auto& cache=_ResourceCache;
    const ResourceKey key(reference);
    if(_Textures.contains(key))return false;
    if(cache.TextureUploading&&cache.TextureUploading->Request.Reference==reference)return true;
    if(cache.TextureReading&&cache.TextureReading->Reference==reference)return true;
    const auto queued=std::find_if(cache.TextureRequests.begin(),cache.TextureRequests.end(),
        [&](const auto& request){return request.Reference==reference;});
    if(queued!=cache.TextureRequests.end()) {
        queued->Visible|=visible;
        return true;
    }
    if(!visible&&cache.TextureRequests.size()>=MaxPredictedRequests)return false;
    const auto revision=_Resources.GetResourceRevision(reference);
    const auto failed=cache.TexturePrefetchFailures.find(key);
    if(failed!=cache.TexturePrefetchFailures.end()&&failed->second==revision)return false;
    TexturePrefetchSource source;
    if(!_Resources.ResolveTexturePrefetch(reference,source)||
       !PreparedTextureLayout::Validate(source,source.ArtifactSize))return false;
    cache.TextureRequests.push_back({reference,revision,std::move(source),visible});
    return true;
}

void Renderer::PredictMaterialTextures(Material& material) {
    if(material.TexturePrefetchConsidered||!_TexturePrefetchEnabled||
       _ResourceCache.TextureRequests.size()>=MaxPredictedRequests)return;
    PrefetchTexture(material.Data.BaseColorTexture,false);
    PrefetchTexture(material.Data.NormalTexture,false);
    for(const auto& binding:material.Data.Textures)PrefetchTexture(binding.Texture,false);
    material.TexturePrefetchConsidered=_ResourceCache.TextureRequests.size()<MaxPredictedRequests;
}

void Renderer::BeginTexturePrefetchFrame(bool enabled) {
    if(_TexturePrefetchEnabled!=enabled)_PreparedKnown=false;
    _TexturePrefetchEnabled=enabled;
    if(!enabled)return;
    auto& cache=_ResourceCache;
    const auto current=[&](const auto& request) {
        return !_Textures.contains(ResourceKey(request.Reference))&&
            request.Revision==_Resources.GetResourceRevision(request.Reference);
    };
    if(cache.TextureUploading&&!current(cache.TextureUploading->Request)) {
        delete cache.TextureUploadTarget;
        cache.TextureUploadTarget=nullptr;
        cache.TextureUploading.reset();
        ++cache.TexturePrefetchRevision;
    }
    if(!cache.TextureUploading&&cache.TextureRead.valid()&&
       cache.TextureRead.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        auto result=cache.TextureRead.get();
        cache.TextureReading.reset();
        if(!current(result.Request))++cache.TexturePrefetchRevision;
        else if(result.Pixels.empty()) {
            // Retry through the normal provider when visible. The provider
            // owns error reporting and tracks failed artifacts for recovery.
            cache.TexturePrefetchFailures.insert_or_assign(ResourceKey(result.Request.Reference),result.Request.Revision);
            ++cache.TexturePrefetchRevision;
        } else {
            cache.TextureUploading=std::move(result);
            cache.TextureUploadSubresource=0;
            cache.TextureUploadRow=0;
        }
    }
    if(cache.TextureUploading) {
        const auto& upload=*cache.TextureUploading;
        const auto& source=upload.Request.Source;
        if(!cache.TextureUploadTarget)
            cache.TextureUploadTarget=_Device.CreateTexture(PreparedTextureDescription(source));
        bool uploaded=cache.TextureUploadTarget!=nullptr;
        std::uint64_t budget=UploadBytesPerFrame;
        while(uploaded&&budget&&cache.TextureUploadSubresource<source.Subresources.size()) {
            const auto& sub=source.Subresources[cache.TextureUploadSubresource];
            if(budget<sub.RowPitch&&budget!=UploadBytesPerFrame)break;
            const auto rows=static_cast<std::uint32_t>(std::min<std::uint64_t>(
                sub.Height-cache.TextureUploadRow,std::max<std::uint64_t>(1,budget/sub.RowPitch)));
            const auto offset=sub.Offset+std::uint64_t(cache.TextureUploadRow)*sub.RowPitch;
            const auto bytes=std::uint64_t(rows)*sub.RowPitch;
            const GRHI::TextureInitialData data{upload.Pixels.data()+offset,static_cast<std::size_t>(bytes),sub.RowPitch};
            uploaded=_Device.UploadTextureSubresourceRows(cache.TextureUploadTarget,sub.MipLevel,sub.ArrayLayer,
                cache.TextureUploadRow,rows,data);
            if(uploaded) {
                cache.TextureUploadRow+=rows;
                _Statistics.PrefetchUploadBytes+=bytes;
                budget-=std::min(budget,bytes);
                if(cache.TextureUploadRow==sub.Height) {
                    cache.TextureUploadRow=0;
                    ++cache.TextureUploadSubresource;
                }
            }
        }
        if(!uploaded||cache.TextureUploadSubresource==source.Subresources.size()) {
            const auto position=_Textures.try_emplace(ResourceKey(upload.Request.Reference)).first;
            auto& entry=position->second;
            entry.ResourceRevision=upload.Request.Revision;
            entry.SRGB=source.SRGB;
            try {
                if(uploaded)entry.Sampler=CreateTextureSampler(source.Sampler);
                if(!entry.Sampler)entry.Error=DeviceError("Upload prefetched texture");
            } catch(...) {
                delete entry.Sampler;
                _Textures.erase(position);
                throw;
            }
            if(uploaded&&entry.Sampler) {
                entry.Texture=cache.TextureUploadTarget;
                ++_Statistics.PrefetchedTextures;
            } else {
                delete cache.TextureUploadTarget;
            }
            cache.TextureUploadTarget=nullptr;
            cache.TextureUploading.reset();
            ++cache.TexturePrefetchRevision;
        }
    }
    if(_SeenTexturePrefetchRevision!=cache.TexturePrefetchRevision) {
        _SeenTexturePrefetchRevision=cache.TexturePrefetchRevision;
        _PreparedKnown=false;
    }
}

void Renderer::EndTexturePrefetchFrame() {
    if(!_TexturePrefetchEnabled)return;
    auto& cache=_ResourceCache;
    while(!cache.TextureRead.valid()&&!cache.TextureRequests.empty()) {
        auto next=std::find_if(cache.TextureRequests.begin(),cache.TextureRequests.end(),
            [](const auto& request){return request.Visible;});
        if(next==cache.TextureRequests.end())next=cache.TextureRequests.begin();
        auto request=std::move(*next);
        cache.TextureRequests.erase(next);
        if(_Textures.contains(ResourceKey(request.Reference))||
           request.Revision!=_Resources.GetResourceRevision(request.Reference)) {
            _PreparedKnown=false;
            continue;
        }
        const auto read=[request]() mutable {
            RenderResourceCache::TextureReadResult result{std::move(request),{}};
            const auto& source=result.Request.Source;
            try {
                const auto path=std::filesystem::u8path(source.ArtifactPath);
                const auto& last=source.Subresources.back();
                const auto bytes=last.Offset+std::uint64_t(last.RowPitch)*last.Height;
                if(!path.is_absolute()||bytes>std::uint64_t(std::numeric_limits<std::streamsize>::max())||
                   bytes>std::numeric_limits<std::size_t>::max()||
                   source.ArtifactSize>std::uint64_t(std::numeric_limits<std::streamoff>::max()))return result;
                std::ifstream file(path,std::ios::binary|std::ios::ate);
                if(!file||file.tellg()!=static_cast<std::streamoff>(source.ArtifactSize))return result;
                result.Pixels.resize(static_cast<std::size_t>(bytes));
                if(!file.seekg(0)||!file.read(reinterpret_cast<char*>(result.Pixels.data()),
                    static_cast<std::streamsize>(result.Pixels.size())))result.Pixels={};
            } catch(...) {result.Pixels={};}
            return result;
        };
        try {
            cache.TextureRead=std::async(std::launch::async,read);
            cache.TextureReading=std::move(request);
        } catch(...) {
            cache.TexturePrefetchFailures.insert_or_assign(ResourceKey(request.Reference),request.Revision);
            _PreparedKnown=false;
        }
    }
    _Statistics.TexturePrefetchPending=cache.TextureRead.valid()||cache.TextureUploading.has_value()||
        !cache.TextureRequests.empty()||!_PreparedKnown;
}
}
