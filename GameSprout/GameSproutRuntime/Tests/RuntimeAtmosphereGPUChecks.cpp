#include "GameSproutRuntimeExport.h"
#include "RuntimeEnvironmentApi.h"
#include "RuntimeRenderApi.h"
#include "RuntimeWeatherApi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
TextureRenderData Cubemap(bool specular,std::uint64_t environment=3) {
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
        if(specular&&environment==4)texel=layer==0?std::array<std::uint16_t,4>{0,0,half,one}:
            std::array<std::uint16_t,4>{0,half,0,one};
        else if(specular&&environment==5)texel={0,0,half,one};
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
    bool LoadEnvironment(const ResourceReference& reference,EnvironmentRenderData& data,std::string&) override {
        ++EnvironmentLoads;
        data={};data.Sky=Cubemap(false);data.Specular=Cubemap(true,reference.LocalID);
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
        view.CaptureLinearColor=true;render();
        std::vector<std::uint8_t> halfPixels;
        Check(viewport->ReadLinearColorHalf(halfPixels,error)&&halfPixels.size()==96*96*8,"HDR readback did not return binary16 scene color");
        const std::size_t halfCenter=(48*96+48)*8;
        Check(halfPixels[halfCenter]==0&&halfPixels[halfCenter+1]==0x44,
            "Linear capture clamped or applied the -2 EV display setting to radiance 4.0");
        view.RequiredStaticFlags=StaticFlags::ContributeGI;render();
        Center(*viewport,{0,0,0},"Unmarked renderer was included in a static lighting capture");
        object->SetStaticFlags(StaticFlags::ContributeGI);render();
        Center(*viewport,{1,.125f,.0625f},"Marked renderer was omitted after static flag edit");
        view.CaptureLinearColor=false;view.RequiredStaticFlags=StaticFlags::None;render();
        Check(!viewport->ReadLinearColorHalf(halfPixels,error)&&halfPixels.empty(),"An ordinary frame exposed stale HDR capture pixels");
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

        // A local capture contains a different face colour from the global sky.
        // Move the capture origin away from the +Z ray so box projection points at +X.
        environment.Mode=AmbientLightMode::None;
        ReflectionProbeSettings probe;
        probe.Environment={environment.SkyEnvironment.Guid,4};
        probe.Size={2,2,2};probe.BlendDistance=.5f;
        probe.CapturePosition={-4,0,0};probe.BoxProjection=false;
        environment.ReflectionProbes={probe};
        provider.EnvironmentReflections=true;provider.Metallic=1;provider.Roughness=0;provider.ChangeMaterial();
        Check(scene->SetEnvironmentLighting(environment),"Reflection probe settings rejected");
        render();Center(*viewport,{0,.5f,0},"Local cubemap did not override global environment inside its box");
        Check(viewport->GetStatistics().ReflectionProbeDrawCalls==1,"Local probe draw was not reported");
        const auto captureLoads=provider.EnvironmentLoads;
        environment.ReflectionProbes.front().BoxProjection=true;
        Check(scene->SetEnvironmentLighting(environment),"Box projection settings rejected");render();
        Center(*viewport,{0,0,.5f},"Box projection did not reflect the capture position");
        Check(captureLoads==provider.EnvironmentLoads,"Editing a probe reloaded its cubemap");

        environment.ReflectionProbes.front().BoxProjection=false;
        auto priorityProbe=probe;
        priorityProbe.Environment.LocalID=5;priorityProbe.Priority=1;
        environment.ReflectionProbes.push_back(priorityProbe);
        Check(scene->SetEnvironmentLighting(environment),"Priority probe settings rejected");render();
        Center(*viewport,{0,0,.5f},"The higher priority overlapping probe did not win");
        environment.ReflectionProbes.back().CullingMask=2;
        Check(scene->SetEnvironmentLighting(environment),"Probe layer mask rejected");render();
        Center(*viewport,{0,.5f,0},"The local receiver layer mask was ignored");

        object->GetTransform()->SetLocalPosition({.75f,0,0});
        camera({.75f,0,3},{.75f,0,0});render();
        const auto blended=Pixels(*viewport);
        const auto spot=(48*96+48)*4;
        Check(blended[spot]>57&&blended[spot]<71&&blended[spot+1]>57&&blended[spot+1]<71&&blended[spot+2]<=2,
            "Local reflection did not blend back to the global sky near the box edge");
        view.EnableReflectionProbes=false;render();
        Center(*viewport,{.5f,0,0},"Disable local probes did not restore global environment");
        Check(viewport->GetStatistics().ReflectionProbeDrawCalls==0,"Disabled local probes retained draw statistics");
        view.EnableReflectionProbes=true;object->GetTransform()->SetLocalPosition({3,0,0});
        camera({3,0,3},{3,0,0});render();
        Center(*viewport,{.5f,0,0},"A surface outside the influence box retained a local reflection");

        environment.SkyEnvironment={};object->GetTransform()->SetLocalPosition({});camera({0,0,3},{});
        Check(scene->SetEnvironmentLighting(environment),"Local-only environment rejected");render();
        Center(*viewport,{0,.5f,0},"A local probe without a global environment was not bound");
        auto* other=runtime->CreateScene("Unconfigured");view.Scene=other;render();Center(*viewport,{},"Atmosphere leaked across scenes");

        auto* weather=QueryWeatherRuntime(*runtime);Check(weather,"Weather API unavailable");
        EnvironmentLightingSettings weatherEnvironment;
        weatherEnvironment.Atmosphere.Enabled=true;
        weatherEnvironment.Atmosphere.FogMaxOpacity=0;
        Check(other->SetEnvironmentLighting(weatherEnvironment),"Weather sky base rejected");
        view.Camera.Projection.Projection=CameraProjection::Perspective;
        camera({},{0,.8f,1});
        WeatherSettings settings;settings.Enabled=true;
        settings.Preset.StarIntensity=0;
        for(auto& layer:settings.Preset.Clouds)layer.Coverage=0;
        Check(weather->SetWeatherSettings(*other,settings),"Weather settings rejected");
        render();const auto clearDay=Pixels(*viewport);
        auto cloud=settings.Preset;
        cloud.Clouds[0].Coverage=.9f;cloud.Clouds[0].Density=1;
        Check(weather->TransitionWeather(*other,cloud,0),"First cloud layer rejected");
        render();const auto firstLayer=Pixels(*viewport);
        cloud.Clouds[0].Coverage=0;
        cloud.Clouds[1].Coverage=.9f;cloud.Clouds[1].Density=1;
        Check(weather->TransitionWeather(*other,cloud,0),"Second cloud layer rejected");
        render();const auto secondLayer=Pixels(*viewport);
        Check(firstLayer!=clearDay&&secondLayer!=clearDay&&firstLayer!=secondLayer,
            "The two cloud layers did not change the sky independently");
        cloud.Clouds[0].Coverage=.42f;
        cloud.Clouds[1].Coverage=.48f;
        Check(weather->TransitionWeather(*other,cloud,0),"Legacy skies rejected a two-layer preset");
        render();const auto twoLayers=Pixels(*viewport);
        auto* effects=QueryEnvironmentRuntime(*runtime);
        Check(effects,"Environment API unavailable");
        EnvironmentSettings additionalCloud;
        additionalCloud.Effects={{"upper-cloud","GameSprout.CloudLayer"}};
        EnvironmentLayer cloudLayer;
        cloudLayer.Id="upper-cloud-settings";
        cloudLayer.Overrides={{"upper-cloud","Coverage",.84f},{"upper-cloud","Density",.93f},{"upper-cloud","Scale",6.5f}};
        additionalCloud.Layers.push_back(cloudLayer);
        Check(effects->SetEnvironmentSettings(*other,additionalCloud),"Third cloud layer rejected");
        render();
        Check(Pixels(*viewport)!=twoLayers&&viewport->GetStatistics().DrawCalls==1,
            "The third cloud layer did not composite with the two legacy layers in one sky draw");
        Check(effects->SetEnvironmentSettings(*other,{}),"Third cloud layer could not be removed");
        render();Check(Pixels(*viewport)==twoLayers,"Removing the third layer altered the two legacy layers");
        Check(weather->TransitionWeather(*other,settings.Preset,0)&&weather->SetTimeOfDay(*other,23),
            "Night sky settings rejected");
        render();const auto noStars=Pixels(*viewport);
        auto stars=settings.Preset;stars.StarIntensity=4;stars.StarDensity=1;
        Check(weather->TransitionWeather(*other,stars,0),"Stars rejected");
        render();const auto night=Pixels(*viewport);
        const auto centre=(48*96+48)*4;
        Check(noStars[centre+2]+20<clearDay[centre+2]&&night!=noStars,
            "Day/night and star intensity did not change the sky");

        Mesh(*other,"Lightning receiver");
        provider.Metallic=0;provider.ChangeMaterial();camera({0,0,3},{});
        render();const auto darkReceiver=Pixels(*viewport);
        WeatherLightning strike;strike.Position={0,0,1.5f};strike.Height=1;strike.Radius=8;
        Check(weather->TriggerLightning(*other,strike),"Lightning rejected");
        render();const auto litReceiver=Pixels(*viewport);
        Check(litReceiver[centre+2]>darkReceiver[centre+2]+12,"Lightning did not illuminate the surface");
        Check(viewport->GetStatistics().SceneSnapshotRebuilds==0,"Lightning invalidated the render snapshot");

        auto* fogScene=runtime->CreateScene("Regional fog without legacy weather");
        view.Scene=fogScene;
        Mesh(*fogScene,"Fog receiver");
        provider.Emission={1,0,0,1};provider.ChangeMaterial();
        EnvironmentLightingSettings fogLighting;
        fogLighting.Atmosphere.FogStart=1;fogLighting.Atmosphere.FogEnd=2;fogLighting.Atmosphere.FogMaxOpacity=1;
        Check(fogScene->SetEnvironmentLighting(fogLighting),"Regional fog base rejected");
        EnvironmentSettings regionalFog;
        EnvironmentLayer moat;
        moat.Id="moat";
        moat.Region=EnvironmentRegion{{0,0,15},{1,1,1},2};
        moat.Overrides={{"","Fog.Strength",1.f},{"","Fog.Color",Color{.1f,.4f,.6f,1}}};
        regionalFog.Layers.push_back(moat);
        Check(effects->SetEnvironmentSettings(*fogScene,regionalFog),"Regional fog rejected");
        camera({0,0,20},{});render();Center(*viewport,{1,0,0},"Regional fog affected a view outside the influence box");
        camera({0,0,17},{});render();Center(*viewport,{.645f,.27f,.35f},"Regional fog did not blend at the box edge");
        camera({0,0,15},{});render();Center(*viewport,{.1f,.4f,.6f},"Regional fog remained disabled with legacy weather off");
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS atmosphere GPU: fog, gradient/cubemap sky, rotated diffuse lighting, rough and local box reflections, FP16 exposure and tone mapping, depth-tested/2D overlays, scene isolation and instancing\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete viewport;delete runtime;return 1;}
}
