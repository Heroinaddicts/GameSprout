#include "RuntimeRenderer.h"
#include <algorithm>
#include <limits>

namespace GameSproutRuntime {
void Renderer::RefreshDrawSortRanks() {
    // Assign numeric ranks in the same GUID/LocalID order as the resource maps.
    // The order stays identical to the previous string comparator, but static
    // frames no longer compare or copy GUID strings for every visible section.
    if(_SortedMeshCount!=_Meshes.size()) {
        std::uint64_t rank=0;for(auto& entry:_Meshes)entry.second.SortRank=++rank;
        _SortedMeshCount=_Meshes.size();
    }
    if(_SortedMaterialCount!=_Materials.size()) {
        std::uint64_t rank=0;for(auto& entry:_Materials)entry.second.SortRank=++rank;
        _SortedMaterialCount=_Materials.size();
    }
}

bool Renderer::PriorityAndDepthBefore(const PreparedDraw& left,const PreparedDraw& right) {
    if(left.Transparent!=right.Transparent)return !left.Transparent;
    if(left.Material->SortingPriority!=right.Material->SortingPriority)
        return left.Material->SortingPriority<right.Material->SortingPriority;
    return left.Transparent&&left.DistanceSquared>right.DistanceSquared;
}

bool Renderer::OpaqueDrawBefore(const PreparedDraw& left,const PreparedDraw& right) {
    if(left.Material->ShadingModel!=right.Material->ShadingModel)return left.Material->ShadingModel<right.Material->ShadingModel;
    if(left.Material->DoubleSided!=right.Material->DoubleSided)return left.Material->DoubleSided<right.Material->DoubleSided;
    const auto leftMaterial=left.CachedMaterial?left.CachedMaterial->SortRank:0;
    const auto rightMaterial=right.CachedMaterial?right.CachedMaterial->SortRank:0;
    if(leftMaterial!=rightMaterial)return leftMaterial<rightMaterial;
    if(left.Mesh->SortRank!=right.Mesh->SortRank)return left.Mesh->SortRank<right.Mesh->SortRank;
    if(left.Section.FirstIndex!=right.Section.FirstIndex)return left.Section.FirstIndex<right.Section.FirstIndex;
    if(left.Section.IndexCount!=right.Section.IndexCount)return left.Section.IndexCount<right.Section.IndexCount;
    if(left.Object->Negative!=right.Object->Negative)return left.Object->Negative<right.Object->Negative;
    if(left.Object->Layer!=right.Object->Layer)return left.Object->Layer<right.Object->Layer;
    if(left.Material->ShadingModel==MaterialShadingModel::Lit&&left.Object->ReceiveShadows!=right.Object->ReceiveShadows)
        return left.Object->ReceiveShadows<right.Object->ReceiveShadows;
    return left.Object->Skinned<right.Object->Skinned;
}

void Renderer::SortPrepared(std::vector<PreparedDraw>& draws) {
    // Queue class always wins over material priority. Transparent draws remain
    // back-to-front inside each priority; custom vertex/depth programs are not
    // assumed order independent and remain barriers to opaque state sorting.
    std::stable_sort(draws.begin(),draws.end(),PriorityAndDepthBefore);
    for(std::size_t begin=0;begin<draws.size();) {
        if(draws[begin].Transparent||draws[begin].Shader){++begin;continue;}
        std::size_t end=begin+1;
        while(end<draws.size()&&!draws[end].Transparent&&!draws[end].Shader&&
            draws[end].Material->SortingPriority==draws[begin].Material->SortingPriority)++end;
        std::stable_sort(draws.begin()+begin,draws.begin()+end,OpaqueDrawBefore);
        begin=end;
    }
}

namespace {
// SDL buffers take a 32-bit size. Capacity grows with scene data and never
// triggers eviction based on camera visibility or elapsed frames.
bool ReserveInstanceBuffer(GRHI::IDevice& device,GRHI::IBuffer*& buffer,
    std::size_t bytes,GRHI::BufferUsage usage) {
    if(buffer&&buffer->GetSize()>=bytes)return true;
    constexpr std::uint64_t maximum=std::numeric_limits<std::uint32_t>::max();
    if(bytes>maximum)return false;
    const std::uint64_t oldSize=buffer?buffer->GetSize():0;
    GRHI::BufferDesc description;
    description.Size=std::max<std::uint64_t>(bytes,std::min(maximum,oldSize+oldSize/2));
    description.Usage=usage;
    description.InitializeToZero=false;
    auto* replacement=device.CreateBuffer(description);
    if(!replacement)return false;
    delete buffer;
    buffer=replacement;
    return true;
}
}

void Renderer::InvalidateInstanceUploads() {
    _InstanceTransformsDirty=true;
    _SkinPaletteLayoutDirty=true;
    _ColorInstances.Uploaded.clear();
    _StaticShadowInstances.Uploaded.clear();
    _DynamicShadowInstances.Uploaded.clear();
    _SpotShadowInstances.Uploaded.clear();
}

void Renderer::ClearInstanceBuffers() {
    delete _InstanceTransformsBuffer;
    _InstanceTransformsBuffer=nullptr;
    delete _SkinPaletteOffsetsBuffer;
    delete _SkinPaletteBuffer;
    _SkinPaletteOffsetsBuffer=nullptr;
    _SkinPaletteBuffer=nullptr;
    _SkinPaletteOffsets.clear();
    _SkinPaletteOffsetScratch.clear();
    _SkinPaletteMatrices.clear();
    _InstanceTransforms.clear();
    _ChangedInstanceTransforms.clear();
    _ColorInstanceDraws.clear();
    _ColorInstanceBatchesKnown=false;
    for(auto* stream:{&_ColorInstances,&_StaticShadowInstances,&_DynamicShadowInstances,&_SpotShadowInstances}) {
        delete stream->Buffer;
        *stream={};
    }
    InvalidateInstanceUploads();
}

void Renderer::PrepareInstanceBatches(const std::vector<PreparedDraw>& draws,bool enabled,
    std::vector<InstanceDraw>& batches,std::vector<std::uint32_t>& indices) const {
    batches.clear();
    for(std::size_t begin=0;begin<draws.size();) {
        const auto& draw=draws[begin];
        const bool eligible=enabled&&draw.Material->EnableGPUInstancing&&!draw.Transparent&&!draw.Shader;
        std::size_t end=begin+1;
        if(eligible)while(end<draws.size()) {
            const auto& next=draws[end];
            if(next.Shader||next.Transparent||next.Object->Skinned!=draw.Object->Skinned||!next.Material->EnableGPUInstancing||
                next.Mesh!=draw.Mesh||next.Section.FirstIndex!=draw.Section.FirstIndex||
                next.Section.IndexCount!=draw.Section.IndexCount||next.Object->Negative!=draw.Object->Negative)break;
            if(next.Material!=draw.Material||next.Object->Layer!=draw.Object->Layer||
                (draw.Material->ShadingModel==MaterialShadingModel::Lit&&
                    (next.Object->ReceiveShadows!=draw.Object->ReceiveShadows||next.LocalLightIndices!=draw.LocalLightIndices||
                     next.ReflectionProbeIndex!=draw.ReflectionProbeIndex)))break;
            ++end;
        }
        const auto count=static_cast<std::uint32_t>(end-begin);
        batches.push_back({begin,count,indices.size()});
        if(count>1)for(auto index=begin;index<end;++index)
            indices.push_back(draws[index].Object->InstanceIndex);
        begin=end;
    }
}

bool Renderer::UploadResidentInstances(GRHI::ICommandBuffer& command) {
    if(_SceneObjects.empty())return true;
    const auto requiredBytes=_SceneObjects.size()*sizeof(InstanceVertex);
    auto& changes=_ChangedInstanceTransforms;
    if(!_InstanceTransformsDirty&&!changes.empty()) {
        std::sort(changes.begin(),changes.end());
        changes.erase(std::unique(changes.begin(),changes.end()),changes.end());
    }
    // When most instances move, replace the backing allocation as a whole.
    // Patching the same GPU buffer would wait for the previous frame's reads.
    if(_InstanceTransformsDirty||changes.size()>_SceneObjects.size()/2) {
        _InstanceTransforms.resize(_SceneObjects.size());
        for(const auto& object:_SceneObjects)
            _InstanceTransforms[object.InstanceIndex]={object.Model,object.Normal};
        if(!ReserveInstanceBuffer(_Device,_InstanceTransformsBuffer,requiredBytes,GRHI::BufferUsage::VertexStorage))
            return Fail(DeviceError("Allocate resident instance transform buffer"));
        const GRHI::BufferUpload upload{_InstanceTransformsBuffer,0,_InstanceTransforms.data(),requiredBytes,true};
        if(!command.UploadBuffers(&upload,1))return Fail(DeviceError("Upload resident instance transforms"));
        ++_Statistics.InstanceBufferUploads;
        _Statistics.InstanceTransformUploadBytes=requiredBytes;
    }else if(!changes.empty()) {
        std::vector<GRHI::BufferUpload> uploads;
        for(std::size_t start=0;start<changes.size();) {
            const auto first=changes[start];
            auto end=start;
            do {
                const auto index=changes[end++];
                const auto& object=_SceneObjects[index];
                _InstanceTransforms[index]={object.Model,object.Normal};
            }while(end<changes.size()&&changes[end]==changes[end-1]+1);
            const auto bytes=(end-start)*sizeof(InstanceVertex);
            uploads.push_back({_InstanceTransformsBuffer,first*sizeof(InstanceVertex),_InstanceTransforms.data()+first,bytes});
            _Statistics.InstanceTransformUploadBytes+=bytes;
            start=end;
        }
        if(!command.UploadBuffers(uploads.data(),static_cast<std::uint32_t>(uploads.size())))
            return Fail(DeviceError("Upload changed instance transforms"));
        ++_Statistics.InstanceBufferUploads;
    }
    _InstanceTransformsDirty=false;
    _ChangedInstanceTransforms.clear();
    return true;
}

bool Renderer::UploadInstanceStream(GRHI::ICommandBuffer& command,InstanceStream& stream) {
    if(stream.Indices.empty()||stream.Indices==stream.Uploaded)return true;
    const auto bytes=stream.Indices.size()*sizeof(std::uint32_t);
    if(!ReserveInstanceBuffer(_Device,stream.Buffer,bytes,GRHI::BufferUsage::Vertex))
        return Fail(DeviceError("Allocate visible instance index buffer"));
    const GRHI::BufferUpload upload{stream.Buffer,0,stream.Indices.data(),bytes,true};
    if(!command.UploadBuffers(&upload,1))return Fail(DeviceError("Upload visible instance indices"));
    stream.Uploaded=stream.Indices;
    ++_Statistics.InstanceBufferUploads;
    _Statistics.InstanceVisibilityUploadBytes+=bytes;
    return true;
}

bool Renderer::UploadSkinPalettes(GRHI::ICommandBuffer& command) {
    if(_SkinnedDraws.empty())return true;
    _SkinPaletteOffsetScratch.assign(_SceneObjects.size(),0);
    constexpr auto maxMatrices=std::numeric_limits<std::uint32_t>::max()/sizeof(InstanceBoneMatrix);
    std::uint32_t matrixCount=0;
    for(const auto& skinned:_SkinnedDraws) {
        const auto& object=_SceneObjects[skinned.ObjectIndex];
        if(!object.SkinPoseReady)continue;
        if(object.SkinMatrices.size()>maxMatrices-matrixCount)
            return Fail("Skinned instance palette exceeds the storage buffer limit");
        _SkinPaletteOffsetScratch[object.InstanceIndex]=matrixCount;
        matrixCount+=static_cast<std::uint32_t>(object.SkinMatrices.size());
    }
    if(!matrixCount)return true;
    const bool layoutChanged=_SkinPaletteLayoutDirty||_SkinPaletteOffsetScratch!=_SkinPaletteOffsets||
        matrixCount!=_SkinPaletteMatrices.size();
    if(layoutChanged) {
        _SkinPaletteOffsets.swap(_SkinPaletteOffsetScratch);
        _SkinPaletteMatrices.resize(matrixCount);
    }
    // Matrices for each drawable have stable offsets until scene membership or its mesh changes.
    // Merge adjacent dirty spans. For many separate spans, one full upload is cheaper.
    struct Span { std::uint32_t Begin,End; };
    constexpr std::size_t maxSpans=32;
    std::array<Span,maxSpans> spans;
    std::size_t spanCount=0;
    bool fullUpload=layoutChanged;
    for(const auto& skinned:_SkinnedDraws) {
        auto& object=_SceneObjects[skinned.ObjectIndex];
        if(!object.SkinPoseReady||(!layoutChanged&&!object.SkinUploadDirty))continue;
        const auto begin=_SkinPaletteOffsets[object.InstanceIndex];
        const auto end=begin+static_cast<std::uint32_t>(object.SkinMatrices.size());
        // Instanced skinning uses xyz of the transformed position and the
        // linear part for normals. Preserve all of those coefficients.
        std::transform(object.SkinMatrices.begin(),object.SkinMatrices.end(),_SkinPaletteMatrices.begin()+begin,
            [](const Mat4& matrix) {
                const auto& m=matrix.Data;
                return InstanceBoneMatrix{{m[0],m[4],m[8],m[12]},{m[1],m[5],m[9],m[13]},{m[2],m[6],m[10],m[14]}};
            });
        object.SkinUploadDirty=false;
        if(fullUpload)continue;
        if(spanCount&&spans[spanCount-1].End==begin)spans[spanCount-1].End=end;
        else if(spanCount==maxSpans)fullUpload=true;
        else spans[spanCount++]={begin,end};
    }
    if(!fullUpload&&!spanCount)return true;
    const auto offsetBytes=_SkinPaletteOffsets.size()*sizeof(std::uint32_t);
    const auto matrixBytes=_SkinPaletteMatrices.size()*sizeof(InstanceBoneMatrix);
    if(!ReserveInstanceBuffer(_Device,_SkinPaletteOffsetsBuffer,offsetBytes,GRHI::BufferUsage::VertexStorage)||
       !ReserveInstanceBuffer(_Device,_SkinPaletteBuffer,matrixBytes,GRHI::BufferUsage::VertexStorage))
        return Fail(DeviceError("Allocate skinned instance buffers"));
    std::array<GRHI::BufferUpload,maxSpans+1> uploads;
    std::uint32_t uploadCount=0;
    if(layoutChanged)uploads[uploadCount++]={_SkinPaletteOffsetsBuffer,0,_SkinPaletteOffsets.data(),offsetBytes,true};
    if(fullUpload)uploads[uploadCount++]={_SkinPaletteBuffer,0,_SkinPaletteMatrices.data(),matrixBytes,true};
    else for(std::size_t i=0;i<spanCount;++i) {
        const auto [begin,end]=spans[i];
        uploads[uploadCount++]={_SkinPaletteBuffer,begin*sizeof(InstanceBoneMatrix),
            _SkinPaletteMatrices.data()+begin,(end-begin)*sizeof(InstanceBoneMatrix)};
    }
    if(!command.UploadBuffers(uploads.data(),uploadCount))return Fail(DeviceError("Upload skinned instance palettes"));
    ++_Statistics.InstanceBufferUploads;
    for(std::uint32_t i=0;i<uploadCount;++i)_Statistics.InstanceSkinPaletteUploadBytes+=uploads[i].Size;
    _SkinPaletteLayoutDirty=false;
    return true;
}

bool Renderer::PrepareColorInstances(GRHI::ICommandBuffer& command,const RenderView& view) {
    if(!_ColorInstanceBatchesKnown||_ColorInstancingEnabled!=view.EnableGPUInstancing) {
        _ColorInstances.Indices.clear();
        PrepareInstanceBatches(_PreparedDraws,view.EnableGPUInstancing,_ColorInstanceDraws,_ColorInstances.Indices);
        _ColorInstanceBatchesKnown=true;
        _ColorInstancingEnabled=view.EnableGPUInstancing;
    }
    if(view.EnableGPUInstancing)
        return UploadResidentInstances(command)&&UploadSkinPalettes(command)&&UploadInstanceStream(command,_ColorInstances);
    if(!_ChangedInstanceTransforms.empty()) {
        _InstanceTransformsDirty=true;
        _ChangedInstanceTransforms.clear();
    }
    return true;
}

bool Renderer::DrawPrepared(GRHI::IRenderPass& pass,const std::vector<PreparedDraw>& draws,const RenderView& view,
    const Mat4& viewProjection,GRHI::TextureFormat format,const std::vector<DirectionalLight>& lights,
    const RenderEnvironment::Uniform& environment,const EnvironmentGPU* reflections) {
    for(const auto& batch:_ColorInstanceDraws) {
        const auto& draw=draws[batch.First];
        if(draw.Shader) {
            if(!DrawShaderMaterial(pass,*draw.CachedMaterial,*draw.Shader,*draw.Mesh,draw.Material->Shader,
                draw.Section,*draw.Object,viewProjection,format))return false;
            ++_Statistics.DrawCalls;_Statistics.Triangles+=draw.Section.IndexCount/3;
            continue;
        }
        if(!DrawBuiltin(pass,draw,view,viewProjection,format,lights,environment,reflections,batch.IndexOffset,batch.Count))
            return false;
        if(batch.Count>1) {
            ++_Statistics.InstancedDrawCalls;_Statistics.InstancedInstances+=batch.Count;
            _Statistics.SavedDrawCalls+=batch.Count-1;
            if(draw.Object->Skinned) {
                ++_Statistics.SkinnedInstancedDrawCalls;
                _Statistics.SkinnedInstancedInstances+=batch.Count;
            }
        }
    }
    return true;
}
}
