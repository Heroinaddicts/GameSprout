#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

static void Check(bool condition,const std::string& message) {if(!condition) throw std::runtime_error(message);}
static void UInt(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    for(int shift=0;shift<32;shift+=8) bytes.push_back(static_cast<std::uint8_t>(value>>shift));
}
static void Float(std::vector<std::uint8_t>& bytes,float value) {
    std::uint32_t bits;std::memcpy(&bits,&value,4);UInt(bytes,bits);
}
class SkinProvider final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    std::uint32_t BoneCount=2;
    std::uint32_t ActiveJoint=1;
    float VertexOffsetX=0;
    bool Color=false,UV=false,BadIndex=false,BadWeight=false,DiagonalNormal=false,MixedWeights=false,UseEveryBone=false;
    int MeshLoads=0;
    std::uint64_t GetRevision() const override {return Revision;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string&) override {
        ++MeshLoads;data={};data.VertexCount=UseEveryBone?BoneCount:3;data.VertexStride=56+(Color?16:0)+(UV?8:0);
        data.ColorOffset=Color?24:-1;data.UVOffset=UV?(Color?40:24):-1;
        data.BoneIndicesOffset=24+(Color?16:0)+(UV?8:0);data.BoneWeightsOffset=data.BoneIndicesOffset+16;
        const float positions[3][3]={{-.14f,-.14f,0},{.14f,-.14f,0},{0,.14f,0}};
        for(std::uint32_t vertex=0;vertex<data.VertexCount;++vertex) {
            const auto& position=positions[vertex%3];
            Float(data.Bytes,position[0]+VertexOffsetX);Float(data.Bytes,position[1]);Float(data.Bytes,position[2]);
            Float(data.Bytes,DiagonalNormal?1:0);Float(data.Bytes,DiagonalNormal?1:0);Float(data.Bytes,DiagonalNormal?0:1);
            if(Color) for(int component=0;component<4;++component) Float(data.Bytes,1);
            if(UV) {Float(data.Bytes,.5f);Float(data.Bytes,.5f);}
            const auto joint=UseEveryBone?vertex:ActiveJoint;
            for(int influence=0;influence<4;++influence) UInt(data.Bytes,BadIndex?BoneCount:influence==0?joint:0);
            Float(data.Bytes,BadWeight?-.5f:MixedWeights?.50025f:1);
            Float(data.Bytes,MixedWeights?.50025f:0);Float(data.Bytes,0);Float(data.Bytes,0);
        }
        data.IndexOffset=data.Bytes.size();data.IndexCount=data.VertexCount;
        for(std::uint32_t index=0;index<data.IndexCount;++index) UInt(data.Bytes,index);
        for(std::uint32_t index=0;index<BoneCount;++index) data.Bones.push_back({index==1?"Bone":"Unused"+std::to_string(index),{}});
        data.Sections={{0,data.IndexCount,0}};data.LocalBounds={{VertexOffsetX,0,0},{.14f,.14f,0}};return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&) override {return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData& data,std::string&) override {data={};return true;}
};
struct Character {
    GameSproutRuntime::IGameObject* Root=nullptr;
    GameSproutRuntime::IGameObject* Bone=nullptr;
    GameSproutRuntime::IGameObject* UnusedBone=nullptr;
    GameSproutRuntime::IGameObject* Mesh=nullptr;
};
static Character CreateCharacter(GameSproutRuntime::IScene& scene,float x) {
    Character character;character.Root=scene.CreateGameObject("Character");
    character.Root->GetTransform()->SetLocalPosition({x,0,0});
    character.UnusedBone=scene.CreateGameObject("Unused",character.Root);
    character.UnusedBone->GetTransform()->SetLocalPosition({-10,0,0});
    character.Bone=scene.CreateGameObject("Bone",character.Root);
    character.Mesh=scene.CreateGameObject("Mesh",character.Root);
    auto* animator=character.Root->AddComponent<GameSproutRuntime::IAnimatorComponent>();Check(animator,"Animator factory failed");
    Check(animator->SetNodeBindings({{"Unused0",character.UnusedBone->GetInstanceID()},{"Bone",character.Bone->GetInstanceID()}}),"Cannot bind the palette to this character");
    character.Mesh->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"0123456789abcdef0123456789abcdef",1});
    character.Mesh->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    auto* skin=character.Mesh->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();Check(skin,"Skin renderer factory failed");
    Check(skin->SetAnimatorObjectID(character.Root->GetInstanceID()),"Cannot bind skin to Animator");return character;
}
static int Pixel(const std::vector<std::uint8_t>& pixels,float worldX,float worldY=0) {
    // A Unity camera on +Z looking at the origin has world -X as screen-right.
    const int x=static_cast<int>((.5f-worldX*.5f)*256),y=static_cast<int>((.5f-worldY*.5f)*256);
    return pixels[(std::size_t(y)*256+std::size_t(x))*4];
}

class SkinParityDigest {
public:
    void Byte(std::uint8_t value) { Value=(Value^value)*1099511628211ULL; }
    void Word(std::uint64_t value) {
        for(unsigned shift=0;shift<64;shift+=8)Byte(static_cast<std::uint8_t>(value>>shift));
    }
    void Vector(GameSproutRuntime::Vector3 value) {
        Word(std::bit_cast<std::uint32_t>(value.X));
        Word(std::bit_cast<std::uint32_t>(value.Y));
        Word(std::bit_cast<std::uint32_t>(value.Z));
    }
    std::uint64_t Value=14695981039346656037ULL;
};

static std::uint64_t CheckSkinBoundsParity(GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IRuntimeViewport& viewport,SkinProvider& provider,GameSproutRuntime::RenderView view) {
    using namespace GameSproutRuntime;
    constexpr std::size_t skinCount=257,boneCount=60;
    constexpr std::size_t runLength=69,secondRunStart=128;
    constexpr std::size_t skinBytes=boneCount*12*sizeof(float),offsetBytes=skinCount*sizeof(std::uint32_t);
    enum Phase { Warm, AllDirty, Unchanged, Disjoint, Single, Invalid, PhaseCount };
    constexpr std::array<std::size_t,PhaseCount> expectedUploads{
        skinCount*skinBytes+offsetBytes,skinCount*skinBytes,0,2*runLength*skinBytes,skinBytes,
        (skinCount-2)*skinBytes+offsetBytes};
    provider.BadWeight=provider.DiagonalNormal=provider.UV=provider.Color=false;
    provider.UseEveryBone=true;
    provider.BoneCount=boneCount;
    ++provider.Revision;

    view.Scene=runtime.CreateScene("Direct skin bounds parity");
    Check(view.Scene,"Cannot create skin bounds parity scene");
    struct SkinInstance { IMeshComponent* Mesh;ISkinnedMeshRendererComponent* Skin; };
    std::array<SkinInstance,skinCount> instances;
    for(std::size_t i=0;i<instances.size();++i) {
        auto* object=view.Scene->CreateGameObject("Direct skin "+std::to_string(i));
        Check(object,"Cannot create parity skin");
        const float x=-.8f+static_cast<float>(i%23)*.07f;
        const float y=-.4f+static_cast<float>(i/23)*.07f;
        object->GetTransform()->SetLocalPosition({x,y,0});
        object->GetTransform()->SetLocalScale({.12f,.12f,.12f});
        auto* mesh=object->AddComponent<IMeshComponent>();
        auto* renderer=object->AddComponent<IMeshRendererComponent>();
        auto* skin=object->AddComponent<ISkinnedMeshRendererComponent>();
        Check(mesh && renderer && skin,"Cannot add parity skin components");
        mesh->SetMesh({"0123456789abcdef0123456789abcdef",1});
        instances[i]={mesh,skin};
    }

    SkinParityDigest digest;
    std::string error;
    std::vector<std::uint8_t> pixels;
    std::array<std::size_t,skinCount> posePhases{};
    const int meshLoads=provider.MeshLoads;
    // Warm the bindings, then exercise full, unchanged, and partial uploads
    // without advancing Runtime. The last phase checks ordered failures.
    for(std::size_t phase=0;phase<PhaseCount;++phase) {
        std::uint32_t visible=0;
        for(std::size_t i=0;i<instances.size();++i) {
            const bool inDirtyRun=i<runLength || (i>=secondRunStart && i<secondRunStart+runLength);
            const bool dirty=phase==Warm || phase==AllDirty || phase==Invalid ||
                (phase==Disjoint && inDirtyRun) || (phase==Single && i==skinCount-1);
            if(dirty)posePhases[i]=phase;
            const auto posePhase=posePhases[i];
            const bool outside=(i+posePhase)%3==0;
            const bool badSize=phase==Invalid && i==skinCount/2;
            const bool overflow=phase==Invalid && i==skinCount-1;
            visible+=!outside && !badSize && !overflow;
            if(!dirty)continue;
            std::array<Mat4,boneCount> palette;
            const float x=(outside?24.f:0.f)+static_cast<float>(i+posePhase)/1024.f;
            const float y=static_cast<float>(posePhase+i%7)/64.f;
            for(std::size_t joint=0;joint<palette.size();++joint) {
                auto& matrix=palette[joint];
                matrix(0,0)=1.f+static_cast<float>((i+joint+posePhase)%9)/64.f;
                matrix(0,3)=x;
                matrix(1,3)=y;
                if(overflow)matrix(0,0)=matrix(0,3)=std::numeric_limits<float>::max();
            }
            const auto count=badSize?palette.size()-1:palette.size();
            Check(instances[i].Skin->SetSkinPose(std::span(palette.data(),count)),"Cannot set parity override pose");
        }
        if(!viewport.Render(view))throw std::runtime_error(viewport.GetLastError());
        Check(viewport.ReadPixels(pixels,error),error);
        const auto& stats=viewport.GetStatistics();
        Check(stats.VisibleObjects==visible && stats.Triangles==visible*(boneCount/3) && stats.DrawCalls>0,
            "Parity render lost or retained incorrect skinned instances");
        if(stats.InstanceSkinPaletteUploadBytes!=expectedUploads[phase])
            throw std::runtime_error("Wrong parity palette upload bytes in phase "+std::to_string(phase)+
                ": expected "+std::to_string(expectedUploads[phase])+", got "+std::to_string(stats.InstanceSkinPaletteUploadBytes));
        Check(provider.MeshLoads==meshLoads+1,"Parity instances did not share the prepared mesh");
        const auto& diagnostic=viewport.GetLastError();
        if(phase!=Invalid)Check(diagnostic.empty(),diagnostic);
        else {
            const auto invalidSize=diagnostic.find("Runtime skin pose does not match the mesh bone count");
            const auto invalidBounds=diagnostic.find("Skinned vertex is outside finite range");
            Check(invalidSize!=std::string::npos && invalidBounds!=std::string::npos && invalidSize<invalidBounds,
                "Parity diagnostics were lost or reordered: "+diagnostic);
        }

        digest.Word(phase);
        for(const auto& instance:instances) {
            const auto bounds=instance.Mesh->GetLocalBounds();
            digest.Vector(bounds.Center);
            digest.Vector(bounds.Extents);
        }
        for(const auto value:{stats.Width,stats.Height,stats.VisibleObjects,stats.SkippedObjects,stats.DrawCalls,
            stats.Triangles,stats.InstancedDrawCalls,stats.InstancedInstances})digest.Word(value);
        digest.Word(stats.InstanceSkinPaletteUploadBytes);
        digest.Word(pixels.size());
        for(const auto value:pixels)digest.Byte(value);
        digest.Word(diagnostic.size());
        for(const auto value:diagnostic)digest.Byte(static_cast<std::uint8_t>(value));
    }
    return digest.Value;
}

int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    GameSproutRuntime::IRuntimeViewport* renderer=nullptr;
    SkinProvider provider;std::string error;
    try {
        Check(argc==2,"Pass SDK output directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        renderer=runtime->CreateOffscreenRenderer(256,256,&provider,error);
        if(!renderer){delete runtime;std::cout<<"SKIP Runtime GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Skinned instances");auto first=CreateCharacter(*scene,-.5f),second=CreateCharacter(*scene,.5f);
        GameSproutRuntime::RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};
        view.Camera.Position={0,0,3};view.Camera.Projection.Projection=GameSproutRuntime::CameraProjection::Orthographic;
        view.Camera.Projection.OrthographicHeight=2;
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot make test camera");
        std::vector<std::uint8_t> pixels;
        Check(renderer->Render(view),renderer->GetLastError());Check(renderer->ReadPixels(pixels,error),error);
        Check(Pixel(pixels,-.5f)>100 && Pixel(pixels,.5f)>100,"Integer palette input failed to render the second bone");
        Check(provider.MeshLoads==1,"Identical meshes were loaded per instance");
        first.Bone->GetTransform()->SetLocalPosition({.3f,0,0});
        Check(renderer->Render(view),renderer->GetLastError());Check(renderer->ReadPixels(pixels,error),error);
        Check(Pixel(pixels,-.2f)>100 && Pixel(pixels,-.5f)<5 && Pixel(pixels,.5f)>100,"Per-instance palette moved a shared mesh or failed to move its vertex data");
        const int loads=provider.MeshLoads;Check(renderer->Render(view) && provider.MeshLoads==loads,"Changing pose reloaded the mesh artifact");
        GameSproutRuntime::SceneRaycastHit hit;
        Check(renderer->Raycast(scene,{{-.2f,0,3},{0,0,-1}},hit) && hit.ObjectID==first.Mesh->GetInstanceID(),"Picking did not use the current skinned pose");
        Check(!renderer->Raycast(scene,{{-.5f,0,3},{0,0,-1}},hit),"Picking still hit the old bind position");
        GameSproutRuntime::Bounds bounds;
        Check(renderer->GetWorldBounds(scene,first.Mesh->GetInstanceID(),bounds) && std::fabs(bounds.Center.X+.2f)<.001f,"Bounds ignored current bone translation");
        GameSproutRuntime::DebugDrawList outline;
        Check(renderer->AppendSelectionOutline(scene,first.Mesh->GetInstanceID(),outline,{1,1,0,1},false) && outline.Lines.size()==3,"Skinned selection outline missing");
        for(const auto& line:outline.Lines) Check(line.From.X>-.35f && line.From.X<-.05f && line.To.X>-.35f && line.To.X<-.05f,"Outline retained bind pose vertices");
        const auto* meshComponent=first.Mesh->GetComponent<GameSproutRuntime::IMeshComponent>();
        const auto singleJoint=meshComponent->GetLocalBounds();
        Check(singleJoint.Min().X>.15f && singleJoint.Max().X<.45f,"A zero-weight joint expanded the rendered bounds");
        {
            std::unique_ptr<GameSproutRuntime::IRuntimeViewport> sibling(runtime->CreateOffscreenRenderer(256,256,&provider,error));
            Check(sibling!=nullptr,error);
            auto* skin=first.Mesh->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
            GameSproutRuntime::Mat4 palette[2];
            const auto renderPixels=[&](GameSproutRuntime::IRuntimeViewport& viewport) {
                if(!viewport.Render(view))throw std::runtime_error(viewport.GetLastError());
                Check(viewport.ReadPixels(pixels,error),error);
            };

            palette[1](0,3)=2.f;
            Check(skin->SetSkinPose(palette),"Cannot set the offscreen override pose");
            renderPixels(*renderer);
            Check(Pixel(pixels,-.2f)<5 && Pixel(pixels,.5f)>100,"First viewport retained the Animator pose");
            renderPixels(*sibling);
            Check(Pixel(pixels,-.2f)<5 && Pixel(pixels,.5f)>100,"Second viewport retained the Animator pose");

            // Change the override without advancing the Runtime and render in reverse order.
            palette[1](0,3)=.55f;
            Check(skin->SetSkinPose(palette),"Cannot change the override pose in the same frame");
            auto invalid=std::to_array(palette);
            invalid[0](3,0)=std::numeric_limits<float>::quiet_NaN();
            Check(!skin->SetSkinPose(invalid),"Accepted a skin matrix with a NaN coefficient");
            invalid[0]=palette[0];
            invalid[1](3,3)=std::numeric_limits<float>::infinity();
            Check(!skin->SetSkinPose(invalid),"Accepted a skin matrix with an infinite coefficient");
            renderPixels(*sibling);
            Check(Pixel(pixels,.05f)>100,"Second viewport retained the old override bounds");
            renderPixels(*renderer);
            Check(Pixel(pixels,.05f)>100,"First viewport retained the old override bounds");

            Check(first.Mesh->RemoveComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>(),"Cannot remove a cached direct skin binding");
            skin=first.Mesh->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
            Check(skin && skin->SetAnimatorObjectID(first.Root->GetInstanceID()),"Cannot replace the cached skin binding");
            palette[1](0,3)=.7f;
            Check(skin->SetSkinPose(palette),"Cannot set the replacement skin override");
            renderPixels(*renderer);
            Check(Pixel(pixels,.24f)>100 && Pixel(pixels,.05f)<5,"First viewport used a removed skin binding");
            renderPixels(*sibling);
            Check(Pixel(pixels,.24f)>100 && Pixel(pixels,.05f)<5,"Second viewport used a removed skin binding");

            palette[1](0,3)=2.f;
            Check(skin->SetSkinPose(palette),"Cannot restore the offscreen override pose");
            Check(renderer->Render(view) && sibling->Render(view),"Cannot render both viewports before a mesh reload");
            Check(provider.MeshLoads==loads,"An additional viewport or pose reloaded a prepared mesh");

            // Keep the pose and bone count unchanged while reimporting the mesh into view.
            provider.VertexOffsetX=-1.6f;++provider.Revision;
            renderPixels(*sibling);
            Check(Pixel(pixels,-.1f)>100,"Second viewport retained bounds from before the mesh reload");
            renderPixels(*renderer);
            Check(Pixel(pixels,-.1f)>100,"First viewport retained bounds from before the mesh reload");
            Check(provider.MeshLoads==loads+1,"Both viewports did not share the reloaded prepared mesh");

            provider.VertexOffsetX=0;++provider.Revision;
            Check(skin->SetSkinPose({}),"Cannot clear the override pose");
            renderPixels(*renderer);
            Check(Pixel(pixels,-.2f)>100,"Clearing the override did not restore the Animator pose");
            renderPixels(*sibling);
            Check(Pixel(pixels,-.2f)>100,"Clearing the override did not restore the second viewport's Animator pose");
        }
        provider.MixedWeights=true;++provider.Revision;
        first.Bone->GetTransform()->SetLocalPosition({10,0,0});
        first.UnusedBone->GetTransform()->SetLocalPosition({10,0,0});
        Check(renderer->Render(view),renderer->GetLastError());
        Check(renderer->GetWorldBounds(scene,first.Mesh->GetInstanceID(),bounds),"Cannot read exact mixed-weight bounds");
        const auto mixed=meshComponent->GetLocalBounds();
        Check(mixed.Min().X-.5f<=bounds.Min().X && mixed.Max().X-.5f>=bounds.Max().X &&
            mixed.Min().Y<=bounds.Min().Y && mixed.Max().Y>=bounds.Max().Y,
            "Rendered bounds clipped the exact pose when blend weights summed slightly over one");
        provider.MixedWeights=false;
        first.Bone->GetTransform()->SetLocalPosition({.3f,0,0});
        first.UnusedBone->GetTransform()->SetLocalPosition({-10,0,0});
        for(int variant=0;variant<4;++variant) {
            provider.Color=(variant&1)!=0;provider.UV=(variant&2)!=0;++provider.Revision;
            Check(renderer->Render(view),renderer->GetLastError());Check(renderer->ReadPixels(pixels,error),error);
            Check(Pixel(pixels,-.2f)>100,"A skinned UV/color shader variant used incorrect vertex locations");
        }
        provider.DiagonalNormal=true;++provider.Revision;
        first.Bone->GetTransform()->SetLocalScale({1,.2f,1});
        Check(renderer->Render(view),renderer->GetLastError());Check(renderer->ReadPixels(pixels,error),error);
        Check(Pixel(pixels,-.2f)>190,"Skin normals did not apply inverse transpose under nonuniform bone scale");
        first.Bone->GetTransform()->SetLocalScale({1,1,1});
        provider.BoneCount=2;provider.BadIndex=true;++provider.Revision;
        Check(renderer->Render(view) && renderer->GetStatistics().DrawCalls==0 && !renderer->GetLastError().empty(),"Out-of-range skin index reached GPU");
        provider.BadIndex=false;provider.BadWeight=true;++provider.Revision;
        Check(renderer->Render(view) && renderer->GetStatistics().DrawCalls==0 && !renderer->GetLastError().empty(),"Invalid skin weights reached GPU");
        const auto parityDigest=CheckSkinBoundsParity(*runtime,*renderer,provider,view);
        std::cout<<"Skin bounds parity digest: "<<std::hex<<parityDigest<<std::dec<<'\n'<<std::flush;
        provider.UseEveryBone=false;
        auto* leftSkin=first.Mesh->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
        auto* rightSkin=second.Mesh->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
        for(const std::uint32_t count:{129u,529u}) {
            provider.BoneCount=count;provider.ActiveJoint=count-1;++provider.Revision;
            std::vector<GameSproutRuntime::Mat4> pose(count);
            pose.back()(0,3)=.30f;
            Check(leftSkin->SetSkinPose(pose),"Cannot set a skin override larger than the uniform palette");
            pose.back()(0,3)=-.30f;
            Check(rightSkin->SetSkinPose(pose),"Cannot set the second large skin override");
            for(const bool instancing:{false,true,false}) {
                view.EnableGPUInstancing=instancing;
                if(!renderer->Render(view))throw std::runtime_error(renderer->GetLastError());
                Check(renderer->GetLastError().empty(),renderer->GetLastError());
                Check(renderer->ReadPixels(pixels,error),error);
                Check(Pixel(pixels,-.2f)>100 && Pixel(pixels,.2f)>100 &&
                    Pixel(pixels,-.5f)<5 && Pixel(pixels,.5f)<5,
                    "A high bone index did not deform the rendered mesh");
                const auto& stats=renderer->GetStatistics();
                Check(stats.InstancedDrawCalls==(instancing?1u:0u),"Large palettes changed draw batching semantics");
                Check(renderer->Raycast(scene,{{-.2f,0,3},{0,0,-1}},hit)&&hit.ObjectID==first.Mesh->GetInstanceID(),
                    "Picking did not use the large skin palette");
            }
        }
        delete renderer;delete runtime;
        std::cout<<"PASS actual GPU skinning, shared instances and viewport pose bounds, picking/bounds/outline, variants and invalid palettes\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';delete renderer;delete runtime;return 1;
    }
}
