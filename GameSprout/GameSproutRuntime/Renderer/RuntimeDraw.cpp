#include "RuntimeRenderer.h"
#include "RuntimeEnvironment.h"
#include <algorithm>

namespace GameSproutRuntime {
bool Renderer::DrawBuiltin(GRHI::IRenderPass& pass,const PreparedDraw& draw,const RenderView& view,
    const Mat4& viewProjection,GRHI::TextureFormat format,const std::vector<DirectionalLight>& lights,
    const RenderEnvironment::Uniform& environment,const EnvironmentGPU* reflections,
    std::size_t firstInstance,std::uint32_t instanceCount) {
    const auto& data=draw.Mesh->Data;
    PipelineKey key;key.Format=format;key.Stride=data.VertexStride;key.Position=data.PositionOffset;key.Normal=data.NormalOffset;
    key.Color=draw.Object->Skinned&&!draw.Mesh->SkinHasNonWhiteVertexColor?-1:data.ColorOffset;
    key.UV=data.UVOffset;key.DoubleSided=draw.Material->DoubleSided;key.Negative=draw.Object->Negative;
    key.Lit=draw.Material->ShadingModel==MaterialShadingModel::Lit;key.Instanced=instanceCount>1;
    const bool skyReflection=key.Lit&&draw.Material->EnvironmentReflections&&reflections;
    key.SpatialLighting=key.Lit&&(draw.ReflectionProbeIndex||UsesPlanarReflection(draw)||UsesIndirectLighting(draw));
    key.EnvironmentReflections=skyReflection&&!key.SpatialLighting;
    auto* vertices=draw.Object->Skinned?SkinVertexInput(key,*draw.Mesh):draw.Mesh->Buffer;
    auto* pipeline=Pipeline(key);if(!pipeline)return false;
    DrawUniform uniform;uniform.ViewProjection=viewProjection;
    if(!key.Instanced){uniform.Model=draw.Object->Model;uniform.Normal=draw.Object->Normal;}
    uniform.BaseColor=draw.Material->BaseColor;uniform.TextureST=draw.Material->BaseColorTextureST;
    pass.SetPipeline(pipeline);pass.SetVertexBuffer(0,vertices,0);
    if(key.Instanced) {
        pass.SetVertexBuffer(1,_ColorInstances.Buffer,firstInstance*sizeof(std::uint32_t));
        pass.SetVertexStorageBuffer(0,_InstanceTransformsBuffer);
        if(draw.Object->Skinned) {
            pass.SetVertexStorageBuffer(1,_SkinPaletteOffsetsBuffer);
            pass.SetVertexStorageBuffer(2,_SkinPaletteBuffer);
        }
    }
    auto* skinIndices=draw.Object->Skinned?draw.Mesh->SkinColorIndices.get():nullptr;
    if(draw.Object->Skinned&&!draw.Transparent&&draw.Mesh->SkinOrderedColorIndices)
        skinIndices=draw.Mesh->SkinOrderedColorIndices.get();
    pass.SetIndexBuffer(skinIndices?skinIndices:draw.Mesh->Buffer,GRHI::IndexFormat::UInt32,
        skinIndices?0:static_cast<std::size_t>(data.IndexOffset));
    pass.SetVertexUniform(0,&uniform,sizeof(uniform));
    if(draw.Object->Skinned&&!key.Instanced) {
        SkinUniform skin;
        std::copy(draw.Object->SkinMatrices.begin(),draw.Object->SkinMatrices.end(),skin.Bones);
        pass.SetVertexUniform(1,&skin,sizeof(skin));
    }
    pass.SetFragmentTextureSampler(0,draw.Texture?draw.Texture->Texture:_White,draw.Texture?draw.Texture->Sampler:_DefaultSampler);
    if(key.Lit) {
        auto lightUniform=MakeLitUniform(view,draw,lights);
        if(key.SpatialLighting) {
            auto spatialEnvironment=environment;
            if(!BindSpatialEnvironment(pass,draw,view.Scene->GetEnvironmentLighting(),
                skyReflection?reflections:nullptr,spatialEnvironment))return false;
            BindPlanarReflection(pass,draw,lightUniform);
            BindIndirectLighting(pass,draw,spatialEnvironment);
            pass.SetFragmentUniform(1,&spatialEnvironment,sizeof(spatialEnvironment));
        } else {
            pass.SetFragmentUniform(1,&environment,sizeof(environment));
            if(key.EnvironmentReflections)pass.SetFragmentTextureSampler(5,reflections->Specular.get(),EnvironmentSampler());
        }
        pass.SetFragmentUniform(0,&lightUniform,sizeof(lightUniform));
        const auto& shadowSource=_PlanarShadowSource?*_PlanarShadowSource:*this;
        pass.SetFragmentUniform(2,&shadowSource._ShadowUniform,sizeof(_ShadowUniform));
        pass.SetFragmentUniform(3,&shadowSource._SpotShadowUniform,sizeof(_SpotShadowUniform));
        auto* shadowAtlas=shadowSource._ShadowAtlas?shadowSource._ShadowAtlas:_White;
        auto* shadowSampler=shadowSource._ShadowSampler?shadowSource._ShadowSampler:_DefaultSampler;
        pass.SetFragmentTextureSampler(2,shadowAtlas,shadowSampler);
        pass.SetFragmentTextureSampler(3,shadowSource._DynamicShadowAtlas?shadowSource._DynamicShadowAtlas:shadowAtlas,shadowSampler);
        pass.SetFragmentTextureSampler(4,shadowSource._SpotShadowCount?shadowSource._SpotShadowAtlas:_White,shadowSampler);
        pass.SetFragmentTextureSampler(1,draw.NormalTexture?draw.NormalTexture->Texture:_White,
            draw.NormalTexture?draw.NormalTexture->Sampler:_DefaultSampler);
    }
    pass.DrawIndexed(draw.Section.IndexCount,instanceCount,draw.Section.FirstIndex,0,0);
    ++_Statistics.DrawCalls;_Statistics.Triangles+=std::uint64_t(draw.Section.IndexCount/3)*instanceCount;
    if(draw.ReflectionProbeIndex)++_Statistics.ReflectionProbeDrawCalls;
    return true;
}
}
