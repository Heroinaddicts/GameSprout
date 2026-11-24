#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
TextureRenderData Cubemap(bool specular) {
    TextureRenderData data;
    data.Width=data.Height=specular?2:1;data.MipLevels=specular?2:1;
    data.Dimension=TextureDimension::Cube;data.Format=TexturePixelFormat::RGBA16Float;data.SRGB=false;
    constexpr std::uint16_t one=0x3c00,half=0x3800,quarter=0x3400,eighth=0x3000;
    for(std::uint32_t mip=0;mip<data.MipLevels;++mip)for(std::uint32_t layer=0;layer<6;++layer) {
        const auto size=data.Width>>mip;
        const auto offset=data.Pixels.size();
        const auto rowPitch=size*8;
        data.Subresources.push_back({mip,layer,size,size,rowPitch,offset});
        data.Pixels.resize(offset+size*rowPitch);
        std::array<std::uint16_t,4> texel{half,0,0,one};
        if(mip==1)texel={0,0,half,one};
        else if(layer==1)texel={half,eighth,quarter,one};
        else if(layer==4&&!specular)texel={eighth,quarter,half,one};
        for(std::uint32_t pixel=0;pixel<size*size;++pixel)
            std::memcpy(data.Pixels.data()+offset+pixel*sizeof(texel),texel.data(),sizeof(texel));
    }
    return data;
}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1,EnvironmentRevision=1,MaterialRevision=1;
    unsigned EnvironmentLoads=0,MaterialLoads=0,MeshLoads=0;
    Color Emission{1,0,0,1};
    float Metallic=0,Roughness=0;
    bool SpecularHighlights=true,EnvironmentReflections=true;
    std::uint64_t GetRevision() const override{return Revision;}
    std::uint64_t GetResourceRevision(const ResourceReference& reference) const override {
        if(reference.LocalID==3)return EnvironmentRevision;
        if(reference.LocalID==2)return MaterialRevision;
        return 1;
    }
    void ChangeMaterial(){++MaterialRevision;++Revision;}
    bool LoadMesh(const ResourceReference&,MeshRenderData& data,std::string&) override {
        ++MeshLoads;
        data={};data.VertexCount=3;data.VertexStride=32;data.IndexCount=3;data.IndexOffset=96;data.UVOffset=24;
        const float vertices[][8]={{-.7f,-.7f,0,0,0,1,0,0},{.7f,-.7f,0,0,0,1,1,0},{0,.7f,0,0,0,1,.5f,1}};
        const std::uint32_t indices[]={0,1,2};data.Bytes.resize(sizeof(vertices)+sizeof(indices));
        std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.LocalBounds={{0,0,0},{.7f,.7f,0}};return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&) override{return false;}
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string&) override {
        ++MaterialLoads;
        data={};data.ShadingModel=MaterialShadingModel::Lit;data.EmissionColor=Emission;
        data.Metallic=Metallic;data.Roughness=Roughness;
        data.SpecularHighlights=SpecularHighlights;data.EnvironmentReflections=EnvironmentReflections;return true;
    }
    bool LoadEnvironment(const ResourceReference&,EnvironmentRenderData& data,std::string&) override {
        ++EnvironmentLoads;
        data={};data.Sky=Cubemap(false);data.Specular=Cubemap(true);
        data.DiffuseSH[0]={.125f,0,.125f};data.DiffuseSH[2]={.125f,0,0};return true;
    }
};
std::vector<std::uint8_t> Pixels(IRuntimeViewport& viewport) {
    std::vector<std::uint8_t> pixels;std::string error;Check(viewport.ReadPixels(pixels,error),error);return pixels;
}
void Center(IRuntimeViewport& viewport,Vector3 expected,const std::string& message) {
    const auto pixels=Pixels(viewport);const auto& stats=viewport.GetStatistics();
    const auto offset=(std::size_t(stats.Height/2)*stats.Width+stats.Width/2)*4;
    const float colors[]={expected.X,expected.Y,expected.Z};
    for(std::size_t channel=0;channel<3;++channel)
        Check(std::abs(int(pixels[offset+channel])-int(std::lround(std::clamp(colors[channel],0.0f,1.0f)*255)))<=2,message);
}
IGameObject* Mesh(IScene& scene,const char* name) {
    auto* object=scene.CreateGameObject(name);object->AddComponent<IMeshComponent>()->SetMesh({"0123456789abcdef0123456789abcdef",1});
    Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({{"0123456789abcdef0123456789abcdef",2}}),"Material failed");return object;
}
}
int main(int argc,char** argv) {
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;Provider provider;
    try {
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        std::string error;viewport=runtime->CreateOffscreenRenderer(96,96,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Atmosphere");auto* object=Mesh(*scene,"Surface");
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};
        view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=2;
        const auto camera=[&](Vector3 position,Vector3 target,Vector3 up=Vector3{0,1,0}) {
            view.Camera.Position=position;Check(TryMakeLookAt(position,target,up,view.Camera.View),"Camera failed");
        };
        const auto render=[&]{Check(viewport->Render(view),viewport->GetLastError());};
        EnvironmentLightingSettings environment;auto& atmosphere=environment.Atmosphere;
        atmosphere.Enabled=true;atmosphere.DrawSky=false;atmosphere.FogStart=5;atmosphere.FogEnd=10;
        atmosphere.FogMaxOpacity=1;atmosphere.FogColor={0,.25f,.5f,1};
        Check(scene->SetEnvironmentLighting(environment),"Atmosphere failed");
        camera({0,0,3},{});render();Center(*viewport,{1,0,0},"Fog affected near surfaces");
        camera({0,0,15},{});render();Center(*viewport,{0,.25f,.5f},"Fog failed beyond end distance");
        atmosphere.FogMaxOpacity=.5f;scene->SetEnvironmentLighting(environment);render();
        Center(*viewport,{.5f,.125f,.25f},"Maximum fog opacity was ignored");
        atmosphere.Enabled=false;scene->SetEnvironmentLighting(environment);render();Center(*viewport,{1,0,0},"Disabled fog retained stale uniforms");

        atmosphere.Enabled=true;atmosphere.DrawSky=true;atmosphere.FogMaxOpacity=0;
        atmosphere.ZenithColor={.1f,.2f,.7f,1};atmosphere.HorizonColor={.6f,.7f,.8f,1};
        scene->SetEnvironmentLighting(environment);camera({0,0,3},{});render();
        Center(*viewport,{1,0,0},"Sky overwrote scene geometry or depth");
        object->SetActive(false);render();Center(*viewport,{.6f,.7f,.8f},"Horizontal sky ray did not use horizon color");
        Check(viewport->GetStatistics().DrawCalls==1,"Sky should use exactly one fullscreen draw");
        camera({0,0,0},{0,1,0},{0,0,1});render();Center(*viewport,{.1f,.2f,.7f},"Sky failed to track upward camera rotation");
        view.Camera.Projection.Projection=CameraProjection::Perspective;camera({0,0,0},{0,.3f,1});render();
        const auto beforeTranslation=Pixels(*viewport);
        camera({100,200,300},{100,200.3f,301});render();
        const auto afterTranslation=Pixels(*viewport);Check(beforeTranslation.size()==afterTranslation.size(),"Sky extent changed");
        for(std::size_t index=0;index<beforeTranslation.size();++index)
            Check(std::abs(int(beforeTranslation[index])-int(afterTranslation[index]))<=1,"Sky moved with camera translation");
        environment.HDREnabled=true;scene->SetEnvironmentLighting(environment);render();
        const auto hdrGradient=Pixels(*viewport);
        Check(afterTranslation.size()==hdrGradient.size(),"HDR resolve changed the viewport extent");
        for(std::size_t index=0;index<hdrGradient.size();++index)
            Check(std::abs(int(afterTranslation[index])-int(hdrGradient[index]))<=1,"Linear HDR resolve flipped or shifted the sky gradient");
        environment.HDREnabled=false;

        // The same atmosphere uniform is used for ordinary and instanced Lit.
        object->SetActive(true);object->GetTransform()->SetLocalPosition({-.5f,0,0});
        auto* sibling=Mesh(*scene,"Sibling");sibling->GetTransform()->SetLocalPosition({.5f,0,0});
        atmosphere.DrawSky=false;atmosphere.FogMaxOpacity=.65f;atmosphere.FogStart=1;atmosphere.FogEnd=10;
        scene->SetEnvironmentLighting(environment);view.Camera.Projection.Projection=CameraProjection::Orthographic;
        view.Camera.Projection.OrthographicHeight=3;camera({0,0,7},{});
        view.EnableGPUInstancing=false;render();const auto ordinary=Pixels(*viewport);
        view.EnableGPUInstancing=true;render();const auto instanced=Pixels(*viewport);
        Check(viewport->GetStatistics().InstancedInstances==2,"Atmosphere fixture failed to instance");
        Check(ordinary==instanced,"Instanced fog differs from ordinary Lit");

        object->GetTransform()->SetLocalPosition({});sibling->SetActive(false);
        view.Camera.Projection.OrthographicHeight=2;camera({0,0,3},{});
        environment={};environment.HDREnabled=true;environment.ExposureEV=-2;
        provider.Emission={4,.5f,.25f,1};provider.ChangeMaterial();
        Check(scene->SetEnvironmentLighting(environment),"Cannot enable HDR scene color");
        render();Center(*viewport,{1,.125f,.0625f},"Exposure clamped radiance before resolving HDR scene color");
        DebugDrawList gizmos;gizmos.Triangles.push_back({{-.5f,-.5f,-.1f},{.5f,-.5f,-.1f},{0,.5f,-.1f},{0,1,0,1},true});
        view.DebugDraw=&gizmos;render();Center(*viewport,{1,.125f,.0625f},"Depth-tested gizmo rendered through opaque geometry after HDR resolve");
        UIDebugDrawList overlay;overlay.Triangles.push_back({{20,20},{80,20},{50,80},{.3f,.6f,.9f,1}});
        view.UIScreenDebugDraw=&overlay;render();Center(*viewport,{.3f,.6f,.9f},"HDR exposure modified screen overlays");
        view.DebugDraw=nullptr;view.UIScreenDebugDraw=nullptr;
        environment.ExposureEV=0;environment.Tonemap=ToneMapping::Neutral;
        provider.Emission={.25f,.25f,.25f,1};provider.ChangeMaterial();
        scene->SetEnvironmentLighting(environment);render();Center(*viewport,{.21f,.21f,.21f},"Neutral tone mapping was not selected");
        environment.Tonemap=ToneMapping::ACES;provider.Emission={1,1,1,1};provider.ChangeMaterial();
        scene->SetEnvironmentLighting(environment);render();Center(*viewport,{.804f,.804f,.804f},"ACES tone mapping was not selected");

        environment={};environment.Atmosphere.Enabled=true;environment.Atmosphere.FogMaxOpacity=0;
        environment.SkyEnvironment={"0123456789abcdef0123456789abcdef",3};
        object->SetActive(false);camera({},{0,0,1});
        scene->SetEnvironmentLighting(environment);render();Center(*viewport,{.125f,.25f,.5f},"Cubemap did not sample world +Z");
        environment.SkyRotationDegrees=90;environment.SkyIntensity=.5f;
        scene->SetEnvironmentLighting(environment);render();Center(*viewport,{.25f,.0625f,.125f},"Cubemap yaw and brightness did not apply");
        Check(provider.EnvironmentLoads==1,"Changing sky settings reloaded a prepared cubemap");
        environment.Atmosphere.DrawSky=false;environment.SkyRotationDegrees=0;environment.ReflectionIntensity=0;
        environment.Mode=AmbientLightMode::SkyEnvironment;
        object->SetActive(true);camera({0,0,3},{});provider.Emission={0,0,0,1};provider.ChangeMaterial();
        scene->SetEnvironmentLighting(environment);render();Center(*viewport,{.24f,0,.12f},"Lit did not use the cubemap's diffuse coefficients");
        environment.SkyRotationDegrees=180;scene->SetEnvironmentLighting(environment);render();
        Center(*viewport,{0,0,.12f},"Diffuse environment rotation did not match cubemap rotation");
        environment.SkyRotationDegrees=0;environment.ReflectionIntensity=1;
        provider.Metallic=1;provider.SpecularHighlights=false;provider.ChangeMaterial();
        scene->SetEnvironmentLighting(environment);render();Center(*viewport,{.5f,0,0},"Metallic mirror did not select reflection mip zero");
        provider.Roughness=1;provider.ChangeMaterial();render();Center(*viewport,{0,0,.225f},"Rough reflection did not sample the prefiltered mip");
        provider.EnvironmentReflections=false;provider.ChangeMaterial();render();
        Center(*viewport,{},"Disabled material environment reflections retained metallic IBL");
        provider.Metallic=0;provider.ChangeMaterial();render();
        Center(*viewport,{.24f,0,.12f},"Disabling material environment reflections removed diffuse irradiance");
        const auto meshLoads=provider.MeshLoads,materialLoads=provider.MaterialLoads;
        ++provider.EnvironmentRevision;++provider.Revision;render();
        Check(provider.EnvironmentLoads==2&&provider.MeshLoads==meshLoads&&provider.MaterialLoads==materialLoads,
            "An environment asset edit did not selectively reload the cubemaps");
        auto* other=runtime->CreateScene("Unconfigured");view.Scene=other;render();Center(*viewport,{},"Atmosphere leaked across scenes");
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS atmosphere GPU: fog, gradient/cubemap sky, rotated diffuse lighting and rough reflections, FP16 exposure and tone mapping, depth-tested/2D overlays, scene isolation and instancing\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete viewport;delete runtime;return 1;}
}
