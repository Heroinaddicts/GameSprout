#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include "../../PreparedResources/IndirectLightingAssetFormat.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;

namespace {
const std::string FixtureGuid="ed952b8bcbe54054ab54e4c14354eb70";

void Check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}

class Provider final : public IRuntimeResourceProvider {
public:
    unsigned IndirectLoads=0;
    std::uint64_t Revision=1;
    float Green=.4f;

    std::uint64_t GetRevision()const override{return Revision;}
    std::uint64_t GetResourceRevision(const ResourceReference& reference)const override {
        return reference.LocalID==4?Revision:1;
    }
    bool LoadMesh(const ResourceReference&,MeshRenderData& data,std::string&)override {
        const float vertices[][6]={{-5,-5,0,0,0,1},{5,-5,0,0,0,1},
            {5,5,0,0,0,1},{-5,5,0,0,0,1}};
        const std::uint32_t indices[]={0,1,2,0,2,3};
        data={};data.VertexCount=4;data.VertexStride=24;data.IndexCount=6;
        data.IndexOffset=sizeof(vertices);data.LocalBounds={{},{5,5,0}};
        data.Bytes.resize(sizeof(vertices)+sizeof(indices));
        std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));
        std::memcpy(data.Bytes.data()+data.IndexOffset,indices,sizeof(indices));
        return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string&)override {
        data={};data.ShadingModel=MaterialShadingModel::Lit;data.DoubleSided=true;
        data.EnvironmentReflections=false;
        return true;
    }
    bool LoadIndirectLighting(const ResourceReference& reference,IndirectLightingRenderData& data,std::string&)override {
        ++IndirectLoads;
        data={};
        if(reference.LocalID==4) {
            data.Dimensions={1,1,1};data.Coefficients.resize(1);data.Coefficients[0][0]={0,Green,0};
            return true;
        }
        data.Dimensions={2,1,2};data.Coefficients.resize(4);
        for(unsigned index=0;index<4;++index) {
            const float x=float(index%2),z=float(index/2);
            data.Coefficients[index][0]={.1f+.2f*x+.3f*z,0,0};
            data.Coefficients[index][2]={.1f,0,0}; // World +Z linear SH term.
        }
        return true;
    }
};

void CheckGridEncoding() {
    using PreparedResources::IndirectLightingAssetFormat::Decode;
    std::vector<std::uint8_t> bytes(24+27*4);
    std::memcpy(bytes.data(),"GSIRR001",8);
    bytes[8]=bytes[12]=bytes[16]=1;
    bytes[26]=0x80;bytes[27]=0x3f; // Constant red coefficient = 1.0f.
    IndirectLightingRenderData grid;
    Check(Decode(bytes,grid)&&grid.Coefficients[0][0].X==1.f,"Valid irradiance asset did not decode");
    bytes[8]=33;Check(!Decode(bytes,grid),"Oversized irradiance grid accepted");
    bytes[8]=1;
    bytes[27]=0x7f;Check(!Decode(bytes,grid),"Nonfinite irradiance coefficient accepted");
    bytes[27]=0x3f;
    bytes.pop_back();Check(!Decode(bytes,grid),"Truncated irradiance grid accepted");
}

void Center(IRuntimeViewport& target,const RenderView& view,Vector3 expected,const char* context,int tolerance=3) {
    Check(target.Render(view),target.GetLastError());
    Check(target.GetLastError().empty(),target.GetLastError());
    std::vector<std::uint8_t> pixels;
    std::string error;
    Check(target.ReadPixels(pixels,error),error);
    const auto& stats=target.GetStatistics();
    const auto offset=(std::size_t(stats.Height/2)*stats.Width+stats.Width/2)*4;
    const float channels[]={expected.X,expected.Y,expected.Z};
    for(unsigned index=0;index<3;++index) {
        const int desired=int(std::lround(std::clamp(channels[index],0.f,1.f)*255));
        Check(std::abs(int(pixels[offset+index])-desired)<=tolerance,
            std::string(context)+": expected "+std::to_string(desired)+", got "+std::to_string(pixels[offset+index]));
    }
}
}

int main(int argc,char** argv) {
    IGameSproutRuntime* runtime=nullptr;
    IRuntimeViewport* first=nullptr;
    IRuntimeViewport* second=nullptr;
    Provider provider;
    try {
        CheckGridEncoding();
        Check(argc==2,"Pass Runtime binary directory");
        runtime=Export(argv[1]);
        Check(runtime,"Cannot load Runtime");
        std::string error;
        first=runtime->CreateOffscreenRenderer(64,64,&provider,error);
        if(!first){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        second=runtime->CreateOffscreenRenderer(64,64,&provider,error);
        Check(second,error);
        auto* scene=runtime->CreateScene("Baked diffuse fixture");
        auto* object=scene->CreateGameObject("Static receiver");
        object->AddComponent<IMeshComponent>()->SetMesh({FixtureGuid,1});
        Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({{FixtureGuid,2}}),"Lit material failed");
        RenderView view;
        view.Scene=scene;view.DrawGrid=false;
        view.Camera.Projection.Projection=CameraProjection::Orthographic;
        view.Camera.Projection.OrthographicHeight=1;
        const auto aim=[&](float x) {
            view.Camera.Position={x,0,3};
            Check(TryMakeLookAt(view.Camera.Position,{x,0,0},{0,1,0},view.Camera.View),"Camera failed");
        };
        EnvironmentLightingSettings settings;
        settings.Mode=AmbientLightMode::Color;
        settings.AmbientColor={0,0,.2f,1};
        settings.IndirectLightVolumes={{ {FixtureGuid,3},{},{4,4,2},1,.5f,0 }};
        Check(scene->SetEnvironmentLighting(settings),"Authoring an indirect volume failed");
        aim(0);
        Center(*first,view,{.432f,0,0},"Expected trilinear grid interpolation and directional irradiance");
        aim(1.75f);
        Center(*first,view,{.258f,0,.096f},"Expected a smooth fade into global ambient",5);
        aim(3);
        Center(*first,view,{0,0,.192f},"Indirect lighting leaked beyond authored bounds");
        aim(0);
        Center(*second,view,{.432f,0,0},"Sibling viewport did not share the irradiance atlas");
        Check(provider.IndirectLoads==1,"An unchanged volume reloaded its irradiance grid");
        settings.IndirectLightVolumes[0].Intensity=.5f;
        Check(scene->SetEnvironmentLighting(settings),"Set volume intensity");
        Center(*first,view,{.216f,0,0},"Volume intensity did not affect diffuse lighting");
        Check(provider.IndirectLoads==1,"Changing intensity reloaded the irradiance grid");
        settings.IndirectLightVolumes.push_back({{FixtureGuid,4},{},{4,4,2},1,0,40});
        std::reverse(settings.IndirectLightVolumes.begin(),settings.IndirectLightVolumes.end());
        Check(scene->SetEnvironmentLighting(settings),"Add overlapping volume");
        Center(*first,view,{0,.384f,0},"Priority did not select the overlapping volume");
        provider.Green=.2f;++provider.Revision;
        Center(*first,view,{0,.192f,0},"Changing a prepared grid did not replace its GPU data");
        view.EnableBakedIndirectLighting=false;
        Center(*first,view,{0,0,.192f},"Capture switch did not disable baked indirect lighting");
        settings.IndirectLightVolumes[0].Size.X=0;
        Check(!scene->SetEnvironmentLighting(settings),"Invalid size was accepted");
        delete second;delete first;delete runtime;
        std::cout<<"PASS baked indirect lighting: asset validation, trilinear SH, blending, priority, resource reuse and capture switch\n";
        return 0;
    } catch(const std::exception& failure) {
        delete second;delete first;delete runtime;
        std::cerr<<"FAIL "<<failure.what()<<'\n';
        return 1;
    }
}
