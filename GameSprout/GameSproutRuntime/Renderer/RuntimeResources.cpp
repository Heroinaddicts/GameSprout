#include "RuntimeRenderer.h"
#include "RuntimeRenderResourceCache.h"
#include "GameSproutRuntime.h"
#include "RuntimeTextureUpload.h"
#include "../Api/Internal/PreparedTextureLayout.h"
#include "RuntimeMath.h"
#include "Resources/BuiltinMeshes.h"
#include <algorithm>
#include <cmath>
#include <bit>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace {
bool Attribute(std::uint32_t stride,std::int64_t offset,std::uint32_t size) {
    return offset>=0 && static_cast<std::uint64_t>(offset)+size<=stride && offset%4==0;
}
std::uint32_t Read32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
bool ValidateMesh(GameSproutRuntime::MeshRenderData& data,std::string& error) {
    if (!data.VertexCount || !data.VertexStride || !data.IndexCount || data.IndexCount%3 || data.Bytes.size()>512u*1024u*1024u ||
        !Attribute(data.VertexStride,data.PositionOffset,12) || !Attribute(data.VertexStride,data.NormalOffset,12) ||
        (data.ColorOffset!=-1 && !Attribute(data.VertexStride,data.ColorOffset,16)) ||
        (data.UVOffset!=-1 && !Attribute(data.VertexStride,data.UVOffset,8)) ||
        (data.BoneIndicesOffset!=-1 && !Attribute(data.VertexStride,data.BoneIndicesOffset,16)) ||
        (data.BoneWeightsOffset!=-1 && !Attribute(data.VertexStride,data.BoneWeightsOffset,16)) ||
        ((data.BoneIndicesOffset<0)!=(data.BoneWeightsOffset<0)) ||
        (!data.Bones.empty() && data.BoneIndicesOffset<0) ||
        data.IndexOffset%4 || std::uint64_t(data.VertexCount)*data.VertexStride>data.IndexOffset ||
        data.IndexOffset>data.Bytes.size() || std::uint64_t(data.IndexCount)*4>data.Bytes.size()-data.IndexOffset) {
        error="Invalid prepared mesh byte layout, counts or offsets";return false;
    }
    for(const auto& bone:data.Bones) if(bone.NodePath.empty() || !GameSproutRuntime::RuntimeMath::IsFinite(bone.InverseBindMatrix)) {
        error="Invalid prepared mesh bone path or inverse bind matrix";return false;
    }
    if(data.BoneIndicesOffset>=0) for(std::uint32_t vertex=0;vertex<data.VertexCount;++vertex) {
        const auto* indices=data.Bytes.data()+std::uint64_t(vertex)*data.VertexStride+data.BoneIndicesOffset;
        const auto* weights=data.Bytes.data()+std::uint64_t(vertex)*data.VertexStride+data.BoneWeightsOffset;
        double total=0;
        for(std::size_t influence=0;influence<4;++influence) {
            const auto bone=Read32(indices+influence*4);
            const float weight=std::bit_cast<float>(Read32(weights+influence*4));
            if(!std::isfinite(weight) || weight<0 || weight>1 ||
               (!data.Bones.empty() && bone>=data.Bones.size()) ||
               (data.Bones.empty() && (bone!=0 || weight!=0))) {
                error="Invalid prepared mesh bone index or weight";return false;
            }
            total+=weight;
        }
        if(!data.Bones.empty() && std::fabs(total-1.0)>0.001) {
            error="Prepared skin weights must be normalized and bind every vertex";return false;
        }
    }
    for (std::uint32_t index=0;index<data.IndexCount;++index) {
        const auto* bytes=data.Bytes.data()+data.IndexOffset+std::uint64_t(index)*4;
        const auto value=Read32(bytes);
        if (value>=data.VertexCount) {error="Prepared mesh index exceeds vertex count";return false;}
    }
    const std::int64_t offsets[]={data.PositionOffset,data.NormalOffset,data.ColorOffset,data.UVOffset};
    const int dimensions[]={3,3,4,2};
    for(std::uint32_t vertex=0;vertex<data.VertexCount;++vertex) for(int attribute=0;attribute<4;++attribute) {
        if(offsets[attribute]<0) continue;
        const auto* start=data.Bytes.data()+std::uint64_t(vertex)*data.VertexStride+offsets[attribute];
        for(int component=0;component<dimensions[attribute];++component) if(!std::isfinite(std::bit_cast<float>(Read32(start+component*4)))) {
            error="Prepared mesh contains nonfinite vertex attributes";return false;
        }
    }
    if(!GameSproutRuntime::RuntimeMath::IsFinite(data.LocalBounds.Center) || !GameSproutRuntime::RuntimeMath::IsFinite(data.LocalBounds.Extents) ||
       data.LocalBounds.Extents.X<0 || data.LocalBounds.Extents.Y<0 || data.LocalBounds.Extents.Z<0) {error="Invalid prepared mesh local bounds";return false;}
    if (data.Sections.empty()) data.Sections.push_back({0,data.IndexCount,0});
    for (const auto& section:data.Sections) if (section.FirstIndex%3 || section.IndexCount%3 ||
        section.FirstIndex>data.IndexCount || section.IndexCount>data.IndexCount-section.FirstIndex) {
        error="Prepared mesh section exceeds index data";return false;
    }
    return true;
}
}
namespace GameSproutRuntime {
namespace {
constexpr std::uint32_t VertexCacheSize = 24;

std::uint32_t CountVertexCacheMisses(std::span<const std::uint32_t> indices,
        std::vector<std::uint32_t>& cache) {
    std::fill(cache.begin(),cache.end(),0);
    std::uint32_t next=VertexCacheSize+1;
    for(const auto index:indices)
        if(next-cache[index]>VertexCacheSize)cache[index]=next++;
    return next-(VertexCacheSize+1);
}

bool OrderSkinSection(std::span<std::uint32_t> section, std::uint32_t vertexCount) {
    if(section.size()<6)return false;

    // Build the triangle fan of each remapped vertex. Emit neighboring
    // triangles while the shared vertices are still in the post-transform cache.
    std::vector<std::uint32_t> offsets(std::size_t(vertexCount)+1);
    for(const auto index:section)++offsets[index+1];
    std::partial_sum(offsets.begin(),offsets.end(),offsets.begin());
    std::vector<std::uint32_t> live(offsets.begin(),offsets.end()-1);
    std::vector<std::uint32_t> adjacent(section.size());
    for(std::uint32_t index=0;index<section.size();++index)
        adjacent[live[section[index]]++]=index/3;
    for(std::uint32_t vertex=0;vertex<vertexCount;++vertex)
        live[vertex]-=offsets[vertex];

    std::vector<unsigned char> placed(section.size()/3);
    std::vector<std::uint32_t> cache(vertexCount);
    std::vector<std::uint32_t> ordered,pending;
    ordered.reserve(section.size());
    pending.reserve(section.size());
    constexpr auto missing=std::numeric_limits<std::uint32_t>::max();
    auto vertex=section.front();
    std::size_t scan=0;
    std::uint32_t next=VertexCacheSize+1;
    while(ordered.size()<section.size()) {
        const auto first=pending.size();
        for(auto index=offsets[vertex];index<offsets[vertex+1];++index) {
            const auto triangle=adjacent[index];
            if(placed[triangle])continue;
            placed[triangle]=1;
            for(const auto corner:section.subspan(std::size_t(triangle)*3,3)) {
                ordered.push_back(corner);
                pending.push_back(corner);
                --live[corner];
                if(next-cache[corner]>VertexCacheSize)cache[corner]=next++;
            }
        }
        if(ordered.size()==section.size())break;

        vertex=missing;
        std::uint32_t best=0;
        for(std::size_t offset=first;offset<pending.size();++offset) {
            const auto candidate=pending[offset];
            if(!live[candidate])continue;
            const auto age=next-cache[candidate];
            // Prefer a live fan that can reuse a vertex before its eviction.
            const auto rank=age+2*live[candidate]<=VertexCacheSize?age:0;
            if(vertex==missing||rank>best){vertex=candidate;best=rank;}
        }
        while(vertex==missing&&!pending.empty()) {
            const auto candidate=pending.back();
            pending.pop_back();
            if(live[candidate])vertex=candidate;
        }
        if(vertex==missing) {
            while(!live[section[scan]])++scan;
            vertex=section[scan];
        }
    }
    const auto reorderedMisses=next-(VertexCacheSize+1);
    if(reorderedMisses>=CountVertexCacheMisses(section,cache))return false;
    std::copy(ordered.begin(),ordered.end(),section.begin());
    return true;
}

std::vector<std::uint32_t> OrderSkinIndices(const MeshRenderData& mesh,
        const std::vector<std::uint32_t>& indices) {
    auto sections=mesh.Sections;
    std::sort(sections.begin(),sections.end(),[](const auto& a,const auto& b){return a.FirstIndex<b.FirstIndex;});
    std::uint32_t end=0;
    for(const auto& section:sections) {
        // Two overlapping ranges represent different draws over the same
        // indices. Preserve their shared source order.
        if(section.FirstIndex<end)return {};
        end=section.FirstIndex+section.IndexCount;
    }
    auto ordered=indices;
    bool changed=false;
    for(const auto& section:sections)
        changed|=OrderSkinSection(std::span(ordered).subspan(section.FirstIndex,section.IndexCount),mesh.VertexCount);
    if(!changed)ordered.clear();
    return ordered;
}

std::unique_ptr<GRHI::IBuffer> UploadSkinIndices(GRHI::IDevice& device,
        const std::vector<std::uint32_t>& indices) {
    GRHI::BufferDesc description;
    description.Size=indices.size()*sizeof(std::uint32_t);
    description.Usage=GRHI::BufferUsage::Index;
    const GRHI::BufferInitialData initial{indices.data(),description.Size};
    return std::unique_ptr<GRHI::IBuffer>(device.CreateBuffer(description,&initial));
}

std::unique_ptr<GRHI::IBuffer> CreateSkinIndexBuffer(GRHI::IDevice& device,
    const MeshRenderData& mesh,const std::vector<std::uint32_t>& indices,bool depthOnly,
    std::unique_ptr<GRHI::IBuffer>* orderedColor=nullptr) {
    // Imported skins can have separate vertices for triangles that read the
    // same attributes. Keep their original CPU/custom-shader topology, but
    // let the built-in vertex stages reuse an index when all inputs match.
    struct Input {std::int64_t Offset; std::size_t Size;};
    const std::array inputs{Input{mesh.PositionOffset,12},Input{mesh.BoneIndicesOffset,16},
        Input{mesh.BoneWeightsOffset,16},Input{depthOnly?-1:std::int64_t(mesh.NormalOffset),12},
        Input{depthOnly?-1:mesh.ColorOffset,16},Input{depthOnly?-1:mesh.UVOffset,8}};
    const auto vertex=[&](std::uint32_t index) {return mesh.Bytes.data()+std::size_t(index)*mesh.VertexStride;};
    const auto hash=[&](std::uint32_t index) {
        std::size_t result=0;
        const auto* bytes=reinterpret_cast<const char*>(vertex(index));
        for(const auto& input:inputs) {
            if(input.Offset<0)continue;
            const auto field=std::hash<std::string_view>{}({bytes+input.Offset,input.Size});
            result^=field+std::size_t{0x9e3779b9u}+(result<<6)+(result>>2);
        }
        return result;
    };
    const auto equal=[&](std::uint32_t a,std::uint32_t b) {
        const auto* first=vertex(a);
        const auto* second=vertex(b);
        return std::all_of(inputs.begin(),inputs.end(),[&](const Input& input) {
            return input.Offset<0||std::memcmp(first+input.Offset,second+input.Offset,input.Size)==0;
        });
    };
    constexpr auto unresolved=std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> representatives(mesh.VertexCount,unresolved);
    std::unordered_set<std::uint32_t,decltype(hash),decltype(equal)> unique(0,hash,equal);
    bool changed=false;
    for(const auto index:indices) {
        auto& representative=representatives[index];
        if(representative==unresolved)representative=*unique.insert(index).first;
        changed|=representative!=index;
    }
    std::vector<std::uint32_t> remapped;
    if(changed) {
        remapped.reserve(indices.size());
        for(const auto index:indices)remapped.push_back(representatives[index]);
    }
    auto ordered=OrderSkinIndices(mesh,changed?remapped:indices);
    if(!ordered.empty()) {
        auto buffer=UploadSkinIndices(device,ordered);
        if(depthOnly&&buffer)return buffer;
        if(orderedColor)*orderedColor=std::move(buffer);
    }
    return changed?UploadSkinIndices(device,remapped):nullptr;
}

GRHI::SamplerDesc ToGRHISampler(const SamplerSettings& settings) {
    GRHI::SamplerDesc result;
    result.MinFilter=settings.MinFilter==TextureFilter::Nearest?GRHI::FilterMode::Nearest:GRHI::FilterMode::Linear;
    result.MagFilter=settings.MagFilter==TextureFilter::Nearest?GRHI::FilterMode::Nearest:GRHI::FilterMode::Linear;
    const auto address=[](TextureAddressMode mode){switch(mode){case TextureAddressMode::Repeat:return GRHI::AddressMode::Repeat;case TextureAddressMode::ClampToEdge:return GRHI::AddressMode::ClampToEdge;case TextureAddressMode::MirroredRepeat:return GRHI::AddressMode::MirroredRepeat;}return GRHI::AddressMode::ClampToEdge;};
    result.MipFilter=settings.MipFilter==TextureFilter::Nearest?GRHI::FilterMode::Nearest:GRHI::FilterMode::Linear;
    result.MaxAnisotropy=settings.MaxAnisotropy;
    result.AddressU=address(settings.AddressU);result.AddressV=address(settings.AddressV);return result;
}
}

Renderer::Renderer(Runtime& runtime,GRHI::IDevice& device,IRuntimeResourceProvider& resources)
    :_Runtime(runtime),_Device(device),_Resources(resources),
     _ResourceLease(runtime,device,resources),_ResourceCache(_ResourceLease.Get()),
     _Meshes(_ResourceCache.Meshes),_Textures(_ResourceCache.Textures),_Materials(_ResourceCache.Materials),
     _UserShaders(_ResourceCache.UserShaders),_ShaderPipelines(_ResourceCache.ShaderPipelines),_Pipelines(_ResourceCache.Pipelines),
     _Shaders(_ResourceCache.Shaders),_White(_ResourceCache.White),_DefaultSampler(_ResourceCache.DefaultSampler),
     _Grid(_ResourceCache.Grid),_GridVertices(_ResourceCache.GridVertices) {}
Renderer::~Renderer() {
    ClearResources();
    for(auto& entry:_Depths) delete entry.second.Texture;
    delete _ShadowAtlas;delete _DynamicShadowAtlas;delete _SpotShadowAtlas;delete _ShadowSampler;
    delete _ShadowRepairVertices;
    for(auto& entry:_UIPipelines)delete entry.second;
    delete _UIVertex;delete _UIFragment;delete _UIFallbackAtlas;
    delete _ColorTarget;
    if(_Surface){_Surface->Release();delete _Surface;}
}
void Renderer::ClearResources() {
    ClearUIDraws();
    _ShadowValid=false;_ShadowUniform.Info={};_StaticShadowCasters.clear();_PrimaryShadowCasterBounds.clear();
    _SpotShadowValid=false;_SpotShadowCount=0;
    for(auto& casters:_SpotShadowCasters)casters.clear();
    _StaticShadowTransformObjects.clear();
    _StaticShadowCandidates.clear();_StaticShadowBounds.clear();_DynamicShadowObjects.clear();
    _PreparedDraws.clear();_SceneObjects.clear();_SceneObjectIndices.clear();_SceneLights.clear();_LightObjectIDs.clear();_LightTransformSources.clear();
    _LocalLights.clear();_HadLocalLighting=false;
    _VisibleDrawScratch.clear();_OpaqueDrawOrder.clear();
    _VisibleOpaqueSlots.clear();_VisibleTransparentSlots.clear();_OpaqueDrawOrderKnown=false;
    _SkinnedDraws.clear();
    _SortedMeshCount=0;_SortedMaterialCount=0;
    _ObjectSnapshotKnown=false;_LightSnapshotKnown=false;
    _PreparedKnown=false;_PreparedDiagnostic.clear();
    _ExtractionDiagnostic.clear();_LightDiagnostic.clear();
    ClearInstanceBuffers();
    _ColliderMeshes.clear();
    _OutlinedMesh=nullptr;_OutlineEdges.clear();
}
void Renderer::SyncRevision() {
    _ResourceCache.Synchronize();
    if(_ResourceGeneration!=_ResourceCache.Generation) {
        // A sibling viewport may already have released the previous shared
        // generation. Discard every local borrowed address before looking up
        // resources in the new maps; private render targets remain independent.
        ClearResources();_ResourceGeneration=_ResourceCache.Generation;
    }
}
std::string Renderer::DeviceError(const std::string& operation) const {
    const auto& error=_Device.GetLastError();return operation+(error.empty()?"":": "+error);
}
bool Renderer::Fail(const std::string& error) {_Error=error;return false;}
void Renderer::Diagnostic(const std::string& error) {
    if(error.empty() || _Error.size()>4096) return;
    if(!_Error.empty()) _Error+='\n';_Error+=error;
}
void Renderer::PreparePackedSkinVertexStream(MeshGPU& mesh) {
    const auto& source=mesh.Data;
    static_assert(MaxSkinBones<=256);
    // Only the built-in stages use this stream. Their vertex indices stay
    // identical to the original mesh, picking data and imported topology.
    MeshGPU::SkinVertexStream stream;
    stream.Stride=24;
    if(mesh.SkinHasNonWhiteVertexColor) {
        stream.Color=stream.Stride;
        stream.Stride+=16;
    }
    if(source.UVOffset>=0) {
        stream.UV=stream.Stride;
        stream.Stride+=8;
    }
    stream.BoneIndices=stream.Stride;
    stream.Stride+=4;
    stream.BoneWeights=stream.Stride;
    stream.Stride+=16;
    if(stream.Stride>=source.VertexStride)return;

    // Finiteness, field ranges and joint indices have all been validated
    // before this point. Keep float fields byte-exact and narrow only IDs.
    std::vector<std::uint8_t> bytes(std::size_t(source.VertexCount)*stream.Stride);
    for(std::size_t vertex=0;vertex<source.VertexCount;++vertex) {
        const auto* input=source.Bytes.data()+vertex*source.VertexStride;
        auto* packed=bytes.data()+vertex*stream.Stride;
        std::memcpy(packed,input+source.PositionOffset,12);
        std::memcpy(packed+12,input+source.NormalOffset,12);
        if(stream.Color>=0)std::memcpy(packed+stream.Color,input+source.ColorOffset,16);
        if(stream.UV>=0)std::memcpy(packed+stream.UV,input+source.UVOffset,8);
        for(std::size_t joint=0;joint<4;++joint)
            packed[stream.BoneIndices+joint]=static_cast<std::uint8_t>(mesh.Skin[vertex].Bones[joint]);
        std::memcpy(packed+stream.BoneWeights,input+source.BoneWeightsOffset,16);
    }
    GRHI::BufferDesc description;
    description.Size=bytes.size();
    description.Usage=GRHI::BufferUsage::Vertex;
    const GRHI::BufferInitialData initial{bytes.data(),description.Size};
    stream.Buffer.reset(_Device.CreateBuffer(description,&initial));
    if(stream.Buffer)mesh.PackedSkinVertices=std::move(stream);
}

Renderer::MeshGPU* Renderer::Mesh(const ResourceReference& reference) {
    if(reference.Guid.empty() || !reference.LocalID) {Diagnostic("Mesh reference is unbound or invalid");return nullptr;}
    auto [position,inserted]=_Meshes.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            entry.ResourceRevision=IsBuiltinResource(reference)?0:_Resources.GetResourceRevision(reference);
            const bool loaded=IsBuiltinResource(reference)?BuiltinMeshes::Load(reference,entry.Data,entry.Error):
                _Resources.LoadMesh(reference,entry.Data,entry.Error);
            if(loaded && ValidateMesh(entry.Data,entry.Error)) {
                // 仅在载入/修订变化时解包一次；GPU 上传后丢弃交错原始字节，保留紧密拾取数据。
                entry.Positions.reserve(entry.Data.VertexCount);
                struct JointVertexBounds {Vector3 Minimum,Maximum;bool Used=false;};
                std::vector<JointVertexBounds> jointVertices;
                if(!entry.Data.Bones.empty()&&entry.Data.Bones.size()<=MaxSkinBones)
                    jointVertices.resize(entry.Data.Bones.size());
                for(std::uint32_t vertex=0;vertex<entry.Data.VertexCount;++vertex) {
                    const auto* bytes=entry.Data.Bytes.data()+std::uint64_t(vertex)*entry.Data.VertexStride+entry.Data.PositionOffset;
                    const Vector3 point{std::bit_cast<float>(Read32(bytes)),std::bit_cast<float>(Read32(bytes+4)),std::bit_cast<float>(Read32(bytes+8))};
                    entry.Positions.push_back(point);
                    if(vertex==0)entry.CullMinimum=entry.CullMaximum=point;
                    else {
                        entry.CullMinimum={std::min(entry.CullMinimum.X,point.X),std::min(entry.CullMinimum.Y,point.Y),std::min(entry.CullMinimum.Z,point.Z)};
                        entry.CullMaximum={std::max(entry.CullMaximum.X,point.X),std::max(entry.CullMaximum.Y,point.Y),std::max(entry.CullMaximum.Z,point.Z)};
                    }
                    if(!entry.Data.Bones.empty()) {
                        MeshGPU::SkinVertex skin;
                        const auto* source=entry.Data.Bytes.data()+std::uint64_t(vertex)*entry.Data.VertexStride;
                        if(!entry.SkinHasNonWhiteVertexColor&&entry.Data.ColorOffset>=0) {
                            const auto* color=source+entry.Data.ColorOffset;
                            constexpr auto white=std::bit_cast<std::uint32_t>(1.0f);
                            entry.SkinHasNonWhiteVertexColor=Read32(color)!=white||Read32(color+4)!=white||
                                Read32(color+8)!=white||Read32(color+12)!=white;
                        }
                        double weightSum=0;
                        for(std::size_t influence=0;influence<4;++influence) {
                            const auto joint=Read32(source+entry.Data.BoneIndicesOffset+influence*4);
                            const auto weight=std::bit_cast<float>(Read32(source+entry.Data.BoneWeightsOffset+influence*4));
                            skin.Bones[influence]=joint;
                            skin.Weights[influence]=weight;
                            weightSum+=weight;
                            if(weight==0||jointVertices.empty())continue;
                            auto& bounds=jointVertices[joint];
                            if(bounds.Used) {
                                bounds.Minimum={std::min(bounds.Minimum.X,point.X),std::min(bounds.Minimum.Y,point.Y),std::min(bounds.Minimum.Z,point.Z)};
                                bounds.Maximum={std::max(bounds.Maximum.X,point.X),std::max(bounds.Maximum.Y,point.Y),std::max(bounds.Maximum.Z,point.Z)};
                            } else {
                                bounds.Minimum=bounds.Maximum=point;
                                bounds.Used=true;
                            }
                        }
                        entry.MinimumSkinWeightSum=std::min(entry.MinimumSkinWeightSum,weightSum);
                        entry.MaximumSkinWeightSum=std::max(entry.MaximumSkinWeightSum,weightSum);
                        entry.Skin.push_back(skin);
                    }
                }
                for(std::size_t joint=0;joint<jointVertices.size();++joint) {
                    const auto& source=jointVertices[joint];
                    if(!source.Used)continue;
                    const double minimum[]{source.Minimum.X,source.Minimum.Y,source.Minimum.Z};
                    const double maximum[]{source.Maximum.X,source.Maximum.Y,source.Maximum.Z};
                    MeshGPU::SkinInfluenceBounds bounds;
                    bounds.Joint=joint;
                    for(std::size_t axis=0;axis<3;++axis) {
                        bounds.Center[axis]=(minimum[axis]+maximum[axis])*.5;
                        bounds.Extent[axis]=(maximum[axis]-minimum[axis])*.5;
                        bounds.Magnitude[axis]=std::fabs(bounds.Center[axis])+bounds.Extent[axis];
                    }
                    entry.JointBounds.push_back(bounds);
                }
                entry.CullBoundsKnown=true;
                entry.Indices.reserve(entry.Data.IndexCount);
                for(std::uint32_t index=0;index<entry.Data.IndexCount;++index)
                    entry.Indices.push_back(Read32(entry.Data.Bytes.data()+entry.Data.IndexOffset+std::uint64_t(index)*4));
                GRHI::BufferDesc description;description.Size=entry.Data.Bytes.size();description.Usage=GRHI::BufferUsage::Vertex|GRHI::BufferUsage::Index;
                GRHI::BufferInitialData initial{entry.Data.Bytes.data(),entry.Data.Bytes.size()};
                entry.Buffer=_Device.CreateBuffer(description,&initial);
                if(!entry.Buffer) entry.Error=DeviceError("Create prepared mesh buffer");
                else if(!entry.JointBounds.empty()) {
                    entry.SkinColorIndices=CreateSkinIndexBuffer(_Device,entry.Data,entry.Indices,false,
                        &entry.SkinOrderedColorIndices);
                    entry.SkinDepthIndices=CreateSkinIndexBuffer(_Device,entry.Data,entry.Indices,true);
                    PreparePackedSkinVertexStream(entry);
                    // A component can outlive a resource cache or render through another provider.
                    if(_Runtime._LastSkinBoundsMeshIdentity<std::numeric_limits<std::uint64_t>::max())
                        entry.SkinBoundsIdentity=++_Runtime._LastSkinBoundsMeshIdentity;
                }
            }
        } catch(const std::exception& exception) {entry.Error=exception.what();}
        catch(...) {entry.Error="Resource provider threw while loading mesh";}
        std::vector<std::uint8_t>().swap(entry.Data.Bytes);
        if(!entry.Buffer) {entry.Positions.clear();entry.Skin.clear();entry.JointBounds.clear();entry.Indices.clear();}
    }
    if(!entry.Buffer) {Diagnostic("Mesh "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
GRHI::ISampler* Renderer::CreateTextureSampler(const SamplerSettings& settings) {
    return _Device.CreateSampler(ToGRHISampler(settings));
}
GRHI::ITexture* Renderer::UploadPreparedTexture(const TextureRenderData& data,std::string& error) {
    TexturePrefetchSource layout;
    layout.Width=data.Width;layout.Height=data.Height;layout.Format=data.Format;
    layout.Dimension=data.Dimension;layout.MipLevels=data.MipLevels;layout.SRGB=data.SRGB;
    layout.Subresources=data.Subresources;
    if(!PreparedTextureLayout::Validate(layout,data.Pixels.size())) {
        error="Invalid prepared texture subresource layout";
        return nullptr;
    }
    std::unique_ptr<GRHI::ITexture> texture(_Device.CreateTexture(PreparedTextureDescription(data)));
    if(!texture) {
        error=DeviceError("Allocate prepared texture");
        return nullptr;
    }
    for(const auto& sub:layout.Subresources) {
        const GRHI::TextureInitialData initial{data.Pixels.data()+sub.Offset,
            std::size_t(sub.RowPitch)*sub.Height,sub.RowPitch};
        if(!_Device.UploadTextureSubresourceRows(texture.get(),sub.MipLevel,sub.ArrayLayer,0,sub.Height,initial)) {
            error=DeviceError("Upload prepared texture");
            return nullptr;
        }
    }
    return texture.release();
}
Renderer::TextureGPU* Renderer::Texture(const ResourceReference& reference,bool* pending) {
    if(pending)*pending=false;
    if(reference.Guid.empty() || !reference.LocalID) {Diagnostic("Texture reference is invalid");return nullptr;}
    if(pending&&_TexturePrefetchEnabled&&PrefetchTexture(reference,true)) {
        *pending=true;
        return nullptr;
    }
    auto [position,inserted]=_Textures.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            entry.ResourceRevision=_Resources.GetResourceRevision(reference);
            TextureRenderData data;
            if(_Resources.LoadTexture(reference,data,entry.Error)) {
                entry.SRGB=data.SRGB;
                entry.Texture=UploadPreparedTexture(data,entry.Error);
                if(entry.Texture) {
                    entry.Sampler=CreateTextureSampler(data.Sampler);
                    if(!entry.Sampler)entry.Error=DeviceError("Create prepared texture sampler");
                }
            }
        } catch(const std::exception& exception) {entry.Error=exception.what();}
        catch(...) {entry.Error="Resource provider threw while loading texture";}
        if(!entry.Texture || !entry.Sampler) {delete entry.Texture;delete entry.Sampler;entry.Texture=nullptr;entry.Sampler=nullptr;}
    }
    if(!entry.Texture) {Diagnostic("Texture "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
Renderer::Material* Renderer::MaterialData(const ResourceReference& reference) {
    auto [position,inserted]=_Materials.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            entry.ResourceRevision=_Resources.GetResourceRevision(reference);
            entry.Loaded=_Resources.LoadMaterial(reference,entry.Data,entry.Error);
            if(entry.Loaded && (!RuntimeMath::IsFinite(entry.Data.BaseColor) ||
                entry.Data.BaseColor.R<0 || entry.Data.BaseColor.G<0 || entry.Data.BaseColor.B<0 ||
                entry.Data.BaseColor.A<0 || entry.Data.BaseColor.A>1 ||
                (entry.Data.BaseColorTexture.Guid.empty()!= (entry.Data.BaseColorTexture.LocalID==0)) ||
                (entry.Data.Shader.Guid.empty()!= (entry.Data.Shader.LocalID==0)) ||
                static_cast<unsigned>(entry.Data.ShadingModel)>static_cast<unsigned>(MaterialShadingModel::Lit)||
                entry.Data.SortingPriority < -50 || entry.Data.SortingPriority > 50)) {
                entry.Error="Invalid prepared material color or texture identity";entry.Loaded=false;
            }
            if(entry.Loaded&&entry.Data.ShadingModel==MaterialShadingModel::Lit) {
                const auto& data=entry.Data;
                const auto finite=[](Vector4 value){return std::isfinite(value.X)&&std::isfinite(value.Y)&&std::isfinite(value.Z)&&std::isfinite(value.W);};
                if(!std::isfinite(data.Metallic)||data.Metallic<0||data.Metallic>1||
                    !std::isfinite(data.Roughness)||data.Roughness<0||data.Roughness>1||
                    !std::isfinite(data.NormalScale)||data.NormalScale<0||
                    !std::isfinite(data.Stylization)||data.Stylization<0||data.Stylization>1||
                    !std::isfinite(data.LightThreshold)||data.LightThreshold<0||data.LightThreshold>1||
                    !std::isfinite(data.LightSoftness)||data.LightSoftness<0||data.LightSoftness>1||
                    !std::isfinite(data.ColorSaturation)||data.ColorSaturation<0||
                    !RuntimeMath::IsFinite(data.StylizedShadowTint)||
                    data.StylizedShadowTint.R<0||data.StylizedShadowTint.R>1||
                    data.StylizedShadowTint.G<0||data.StylizedShadowTint.G>1||
                    data.StylizedShadowTint.B<0||data.StylizedShadowTint.B>1||
                    data.StylizedShadowTint.A<0||data.StylizedShadowTint.A>1||
                    !RuntimeMath::IsFinite(data.EmissionColor)||data.EmissionColor.R<0||data.EmissionColor.G<0||data.EmissionColor.B<0||
                    !finite(data.BaseColorTextureST)||!finite(data.NormalTextureST)||
                    (data.NormalTexture.Guid.empty()!=(data.NormalTexture.LocalID==0))||!data.Shader.Guid.empty()) {
                    entry.Error="Invalid prepared Lit material parameters, normal map or conflicting user Shader";entry.Loaded=false;
                }
            }
        } catch(const std::exception& exception) {entry.Error=exception.what();entry.Loaded=false;}
        catch(...) {entry.Error="Resource provider threw while loading material";entry.Loaded=false;}
    }
    if(!entry.Loaded) {Diagnostic("Material "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
Renderer::TextureGPU* Renderer::MaterialTexture(Material& material,bool normal,bool& pending) {
    auto*& texture=normal?material.NormalTexture:material.BaseColorTexture;
    bool& attempted=normal?material.NormalAttempted:material.BaseColorAttempted;
    auto& error=normal?material.NormalError:material.BaseColorError;
    if(!attempted) {
        const auto diagnosticBefore=_Error.size();
        texture=Texture(normal?material.Data.NormalTexture:material.Data.BaseColorTexture,&pending);
        attempted=!pending;
        error=_Error.substr(diagnosticBefore);
        if(!error.empty()&&error.front()=='\n')error.erase(0,1);
    }else if(!texture)Diagnostic(error);
    return texture;
}
}
