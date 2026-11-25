#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
constexpr const char* guid="2aed0fdaab9f4ba0812effa9968eb613";
void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}

class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision() const override{return 1;}
    bool LoadMesh(const ResourceReference& reference,MeshRenderData& data,std::string&) override {
        const float floor[]={-4,0,-5,0,1,0, -4,0,5,0,1,0, 4,0,-5,0,1,0, 4,0,5,0,1,0};
        const float card[]={-.45f,-.45f,0,0,0,-1, -.45f,.45f,0,0,0,-1,
            .45f,-.45f,0,0,0,-1, .45f,.45f,0,0,0,-1};
        const std::uint32_t indices[]={0,1,2,2,1,3};
        data={};data.VertexCount=4;data.VertexStride=24;data.IndexCount=6;data.IndexOffset=sizeof(floor);
        data.LocalBounds=reference.LocalID==1?Bounds{{},{4,0,5}}:Bounds{{},{.45f,.45f,0}};
        data.Bytes.resize(sizeof(floor)+sizeof(indices));
        std::memcpy(data.Bytes.data(),reference.LocalID==1?floor:card,sizeof(floor));
        std::memcpy(data.Bytes.data()+data.IndexOffset,indices,sizeof(indices));
        return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&) override{return false;}
    bool LoadMaterial(const ResourceReference& reference,MaterialRenderData& data,std::string&) override {
        data={};data.ShadingModel=MaterialShadingModel::Lit;data.EnvironmentReflections=false;data.Roughness=.1f;
        if(reference.LocalID==3) {
            data.PlanarReflection=true;
            data.Metallic=1;
        } else {
            data.EmissionColor=reference.LocalID==4?Color{.75f,0,0,1}:
                reference.LocalID==5?Color{0,.75f,0,1}:Color{0,0,.75f,1};
        }
        return true;
    }
};

IGameObject* CreateMesh(IScene& scene,const char* name,std::uint64_t geometry,std::uint64_t material,Vector3 position) {
    auto* object=scene.CreateGameObject(name);
    object->GetTransform()->SetLocalPosition(position);
    object->AddComponent<IMeshComponent>()->SetMesh({guid,geometry});
    Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({{guid,material}}),"Cannot set fixture material");
    return object;
}

std::array<std::uint8_t,3> ProjectPixel(const std::vector<std::uint8_t>& image,const RenderedCamera& camera,Vector3 point) {
    const Vector4 clip=camera.ViewProjection*Vector4{point.X,point.Y,point.Z,1};
    const auto x=std::lround((.5f+.5f*clip.X/clip.W)*camera.Width);
    const auto y=std::lround((.5f-.5f*clip.Y/clip.W)*camera.Height);
    Check(x>=0&&x<camera.Width&&y>=0&&y<camera.Height,"Test point outside viewport");
    const auto offset=(std::size_t(y)*camera.Width+x)*4;
    return {image[offset],image[offset+1],image[offset+2]};
}
}

int main(int argc,char** argv) {
    try {
        Check(argc==2,"Pass Runtime binary directory");
        std::unique_ptr<IGameSproutRuntime> runtime(Export(argv[1]));
        Check(bool(runtime),"Cannot load Runtime");
        Provider provider;
        std::string error;
        std::unique_ptr<IRuntimeViewport> viewport(runtime->CreateOffscreenRenderer(160,120,&provider,error));
        if(!viewport){std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Mirror");
        auto* floor=CreateMesh(*scene,"Water",1,3,{});
        auto* red=CreateMesh(*scene,"Above plane red",2,4,{-1,1,0});
        auto* green=CreateMesh(*scene,"Above plane green",2,5,{1,1,0});
        auto* greenParent=scene->CreateGameObject("Parent outside the reflection mask");
        Check(greenParent&&green->SetParent(greenParent,true),"Cannot parent the reflected card");
        CreateMesh(*scene,"Submerged blue",2,6,{0,-.9f,-2});
        red->SetLayer(2);
        greenParent->SetLayer(2);
        green->SetLayer(3);
        RenderView view;
        view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};
        view.Camera.Position={0,2,-6};
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot orient camera");
        EnvironmentLightingSettings settings;
        settings.HDREnabled=true;settings.ExposureEV=-1;
        settings.PlanarReflection.Enabled=true;
        Check(scene->SetEnvironmentLighting(settings),"Cannot set water plane");
        auto render=[&]{
            Check(viewport->Render(view),viewport->GetLastError());
            std::vector<std::uint8_t> image;
            Check(viewport->ReadPixels(image,error),error);
            return image;
        };
        const auto image=render();
        const auto stats=viewport->GetStatistics();
        Check(stats.PlanarReflectionWidth==80&&stats.PlanarReflectionHeight==60&&
            stats.PlanarReflectionDrawCalls==2,"Mirror should render the two visible cards at half resolution");
        RenderedCamera camera;
        Check(viewport->GetRenderedCamera(camera)&&camera.Camera.Position.Y==2,"Reflection replaced the main camera");
        const auto redPixel=ProjectPixel(image,camera,{-1,-1,0});
        const auto greenPixel=ProjectPixel(image,camera,{1,-1,0});
        Check(redPixel[0]>85&&redPixel[1]<8&&greenPixel[1]>85&&greenPixel[0]<8,
            "Water did not reflect geometry in the correct horizontal position");
        for(std::size_t offset=2;offset<image.size();offset+=4)
            Check(image[offset]==0,"A submerged object escaped the reflection clip plane");
        UIDebugDrawList hud;
        hud.Triangles.push_back({{8,8},{32,8},{20,32},{0,0,.75f,1}});
        view.UIScreenDebugDraw=&hud;
        const auto withHUD=render();
        Check(withHUD[(16*160+20)*4+2]>180&&viewport->GetStatistics().PlanarReflectionDrawCalls==2,
            "HDR reflection changed the screen-space overlay or skipped water");
        view.UIScreenDebugDraw=nullptr;

        settings.PlanarReflection.LayerMask=1u<<3;
        Check(scene->SetEnvironmentLighting(settings),"Cannot restrict reflection layers");
        const auto masked=render();
        Check(ProjectPixel(masked,camera,{-1,-1,0})[0]<8&&ProjectPixel(masked,camera,{1,-1,0})[1]>85,
            "Reflected layer filter was ignored or modified the main camera mask");
        Check(ProjectPixel(masked,camera,{-1,1,0})[0]>85,"Layer filter hid geometry in the main view");

        greenParent->GetTransform()->SetLocalPosition({.85f,0,0});
        const auto moved=render();
        Check(ProjectPixel(moved,camera,{1,-1,0})[1]<8&&ProjectPixel(moved,camera,{1.85f,-1,0})[1]>85,
            "Shared reflection snapshot did not follow the masked parent's transform");
        auto* greenRenderer=green->GetComponent<IMeshRendererComponent>();
        Check(greenRenderer->SetMaterial(0,{guid,6}),"Cannot replace reflected material");
        const auto recolored=render();
        Check(ProjectPixel(recolored,camera,{1.85f,-1,0})[2]>85&&ProjectPixel(recolored,camera,{1.85f,-1,0})[1]<8,
            "Shared reflection snapshot did not follow the material replacement");
        Check(greenRenderer->SetMaterial(0,{guid,5}),"Cannot restore reflected material");
        greenParent->GetTransform()->SetLocalPosition({});

        view.EnablePlanarReflections=false;
        const auto disabled=render();
        Check(viewport->GetStatistics().PlanarReflectionDrawCalls==0&&
            ProjectPixel(disabled,camera,{1,-1,0})[1]<8,"Disabled capture left stale reflection content");
        view.EnablePlanarReflections=true;
        view.Camera.Projection.Projection=CameraProjection::Orthographic;
        view.Camera.Projection.OrthographicHeight=4;
        const auto ortho=render();
        Check(viewport->GetRenderedCamera(camera)&&ProjectPixel(ortho,camera,{1,-1,0})[1]>85,
            "Oblique clipping or reflection mapping failed for an orthographic camera");
        floor->SetActive(false);
        render();
        Check(!viewport->GetStatistics().PlanarReflectionWidth,"An invisible receiver still rendered a planar view");
        std::cout<<"PASS planar reflection GPU: mirror alignment, HDR, layer filtering, near-plane clip, orthographic projection and visibility\n";
        return 0;
    } catch(const std::exception& failure) {
        std::cerr<<"FAIL "<<failure.what()<<'\n';
        return 1;
    }
}
