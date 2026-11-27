#include "../AssetsImport/AssetTestSupport.h"
#include "AssetImportJob.h"
#include "../../Module/RuntimeAssets/LibraryRuntimeResources.h"
#include "../../Module/Shared/AnimationDefinition.h"
#include "../../Module/Shared/AnimationImportSettingsJson.h"
#include "../../Module/AssetsManager/ModelAssetFactory.hpp"
#include "../../Module/AssetsManager/AnimationClipAssetFactory.hpp"

namespace {
class Context final:public Foundation::ITaskContext {
public:
    UInt64 GetTaskId()const override{return 1;} bool IsCancellationRequested()const override{return false;}
    void* GetContext()const override{return nullptr;} bool SetOwnedContext(void*,Foundation::fTaskOwnedContextRelease)override{return false;}
};
class Resource final:public IAsset{
public:
    Resource(const Guid& guid,const AssetArtifact& artifact,std::string path):Identity(guid),Artifact(artifact),Path(std::move(path)){}
    Guid Identity;AssetArtifact Artifact;std::string Path;
    const Guid& GetGuid()const override{return Identity;}uint64_t GetLocalID()const override{return Artifact.LocalID;}
    eAssetType GetAssetType()const override{return Artifact.Type;}const std::string& GetName()const override{return Artifact.Name;}
    const std::string& GetArtifactPath()const override{return Path;}const std::string& GetDataDescriptorJson()const override{return Artifact.DescriptorJson;}
    uint32_t GetDataVersion()const override{return Artifact.DataVersion;}uint64_t GetDataSize()const override{return Artifact.DataSize;}
};
class Library final:public ILibraryManager {
public:
    std::map<std::string,Resource> Resources;std::map<std::string,AssetImportManifest> Manifests;std::string Root;
    void Add(const AssetImportJob& job){for(const auto& artifact:job.Result.Manifest.Assets){Resource resource(Guid(job.Result.Manifest.GuidString),artifact,QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(artifact.Path)).toStdString());
        const auto key=job.Result.Manifest.GuidString+"_"+std::to_string(artifact.LocalID);Resources.erase(key);Resources.emplace(key,std::move(resource));}Manifests[job.Result.Manifest.GuidString]=job.Result.Manifest;}
    IAsset* QueryAsset(const Guid& guid,uint64_t id)override{const auto found=Resources.find(guid.GetGuidString()+"_"+std::to_string(id));return found==Resources.end()?nullptr:&found->second;}
    ILDBSession* GetLDBSession()const override{return nullptr;}const std::string& GetLibraryRoot()const override{return Root;}
    std::vector<std::string> GetManifestGuids()const override{return {};}bool GetManifest(const Guid& guid,AssetImportManifest& output)const override{const auto found=Manifests.find(guid.GetGuidString());if(found==Manifests.end())return false;output=found->second;return true;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&)override{return false;}bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&)override{return false;}
    std::vector<std::string> FindDependents(const std::string&)const override{return {};}
protected:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
AssetImportRequest Request(QTemporaryDir& root,TestMeta& meta){AssetImportRequest request;request.AssetsRoot=root.path().toStdString()+"/Assets";
    request.LibraryRoot=root.path().toStdString()+"/Library";request.SourcePath=meta.Path;request.GuidString=meta._Guid.GetGuidString();request.Importer=meta._ImporterType;meta.SerializeMeta(request.MetaJson);return request;}
void Write(const QString& path,const Json::Value& value){std::string error;Check(AssetArtifactIO::Write(path,QByteArray::fromStdString(AssetArtifactIO::JsonText(value)),error),error);}
GameSproutRuntime::ResourceReference Ref(const AssetImportJob& job,eAssetType type){for(const auto& asset:job.Result.Manifest.Assets)if(asset.Type==type)return {job.Result.Manifest.GuidString,asset.LocalID};throw std::runtime_error("Missing expected artifact");}
void SchemaChecks(){
    GameSproutRuntime::AnimatorControllerData source;source.Layers.emplace_back();source.Layers[0].Name="Base Layer";source.Parameters.push_back({"Speed",GameSproutRuntime::AnimatorParameterType::Float,0});
    GameSproutRuntime::AnimatorState idle;idle.Name="Idle";idle.Clip={Guid(1,2,3,4).GetGuidString(),1};source.Layers[0].States.push_back(idle);
    auto walk=idle;walk.Name="Walk";source.Layers[0].States.push_back(walk);
    GameSproutRuntime::AnimatorTransition transition;transition.From=0;transition.To=1;transition.Conditions.push_back({"Speed",GameSproutRuntime::AnimatorCompare::Greater,0.1});source.Layers[0].Transitions.push_back(transition);
    auto document=AnimationDefinition::ControllerJson(source,false);GameSproutRuntime::AnimatorControllerData parsed;std::string error;
    Check(AnimationDefinition::ReadController(document,false,parsed,error),error);
    Check(AnimationDefinition::ReadController(AnimationDefinition::ControllerJson(parsed,true),true,parsed,error),error);
    Check(parsed.Layers[0].Transitions[0].From==0&&parsed.Layers[0].Transitions[0].To==1,"State graph was not compiled to indices");
    auto defaults=document;defaults["Layers"][0]["States"][0].removeMember("Speed");defaults["Layers"][0]["States"][0].removeMember("Loop");
    Check(AnimationDefinition::ReadController(defaults,false,parsed,error)&&parsed.Layers[0].States[0].Speed==1&&parsed.Layers[0].States[0].Loop,"Current authoring defaults failed");
    auto bad=document;bad["States"]=Json::Value(Json::arrayValue);Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"Old flat States silently accepted");
    bad=document;bad["Layers"][0]["Transitions"][0]["To"]="Missing";Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"Dangling transition accepted");
    bad=document;bad["Layers"][0]["Transitions"][0]["Conditions"][0]["Parameter"]="Missing";Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"Unknown parameter accepted");
    bad=document;bad["Layers"][0]["Transitions"][0]["Conditions"][0]["Compare"]="If";Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"Float/bool condition mismatch accepted");
    bad=document;bad["Layers"][0]["DefaultWeight"]=.5;Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"Base layer partial weight accepted");
    auto layered=document;auto upper=layered["Layers"][0];upper["Name"]="Upper";upper["BlendingMode"]="Additive";upper["DefaultWeight"]=.5;
    upper["ReferenceClip"]=upper["States"][0]["Clip"];upper["ReferenceTime"]=.25;layered["Layers"].append(upper);
    Check(AnimationDefinition::ReadController(layered,false,parsed,error)&&parsed.Layers.size()==2&&parsed.Layers[1].DefaultWeight==.5,error);
    Check(AnimationDefinition::ReadController(AnimationDefinition::ControllerJson(parsed,true),true,parsed,error),error);
    bad=layered;bad["Layers"][1]["Name"]=bad["Layers"][0]["Name"];Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"Duplicate Layer names accepted");
    bad=layered;bad["Layers"][1]["BlendingMode"]="Override";Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"Override reference pose accepted");
    bad=layered;bad["Layers"][1]["DefaultWeight"]=std::numeric_limits<double>::quiet_NaN();Check(!AnimationDefinition::ReadController(bad,false,parsed,error),"NaN layer weight accepted");
    bad=AnimationDefinition::ControllerJson(source,true);bad["Format"]="AnimatorController1";Check(!AnimationDefinition::ReadController(bad,true,parsed,error),"Previous prepared Controller format accepted");

}
void NativeChecks(){
    QTemporaryDir root;Check(root.isValid()&&QDir(root.path()).mkpath("Assets"),"Cannot create fixture");
    TestMeta meta(eAssetImporterType::AnimationClip,Guid(11,22,33,44),"Move.anim");Json::Value clip;clip["Version"]=1;clip["Duration"]=1;clip["SampleRate"]=1;
    clip["Tracks"]=Json::Value(Json::arrayValue);clip["Tracks"].append("/Root");clip["Frames"]=Json::Value(Json::arrayValue);
    for(unsigned frame=0;frame<2;++frame){Json::Value value,pose;value["Time"]=frame;pose["Translation"]=Json::Value(Json::arrayValue);
        pose["Translation"].append(frame);pose["Translation"].append(0);pose["Translation"].append(0);
        pose["Rotation"]=Json::Value(Json::arrayValue);for(int index=0;index<4;++index)pose["Rotation"].append(index==3?1:0);
        pose["Scale"]=Json::Value(Json::arrayValue);for(int index=0;index<3;++index)pose["Scale"].append(1);
        value["Poses"]=Json::Value(Json::arrayValue);value["Poses"].append(pose);clip["Frames"].append(value);}
    Write(root.path()+"/Assets/Move.anim",clip);AssetImportJob job(Request(root,meta));Context context;
    Check(RunAssetImport(job,context),"Native animation import: "+job.Result.Error);meta.Identities=job.Result.Manifest.SubAssetsJson;
    std::string metaJson;meta.SerializeMeta(metaJson);Json::Value metaValue;AssetArtifactIO::Parse(metaJson,metaValue);Write(root.path()+"/Assets/Move.anim.meta",metaValue);
    Library library;library.Add(job);LibraryRuntimeResources provider(nullptr,&library);GameSproutRuntime::AnimationClipData loaded;std::string error;
    const auto ref=Ref(job,eAssetType::AnimationClip);
    Check(provider.LoadAnimationClip(ref,loaded,error),error);
    Check(loaded.Frames.size()==2&&loaded.Frames[1].Poses[0].Position.X==1,"Native animation samples changed");
    GameSproutRuntime::AnimatorControllerData controller;controller.Layers.emplace_back();controller.Layers[0].Name="Base Layer";GameSproutRuntime::AnimatorState state;state.Name="Move";state.Clip=ref;controller.Layers[0].States.push_back(state);
    TestMeta controllerMeta(eAssetImporterType::AnimatorController,Guid(55,66,77,88),"Move.controller");Write(root.path()+"/Assets/Move.controller",AnimationDefinition::ControllerJson(controller,false));
    auto request=Request(root,controllerMeta);request.SourcePaths[ref.Guid]="Move.anim";AssetImportJob compiled(request);
    Check(RunAssetImport(compiled,context),"Controller import: "+compiled.Result.Error);library.Add(compiled);GameSproutRuntime::AnimatorControllerData graph;
    Check(provider.LoadAnimatorController(Ref(compiled,eAssetType::AnimatorController),graph,error),error);
    Check(graph.Layers[0].States.size()==1&&graph.Layers[0].States[0].Clip==ref,"Controller lost clip reference");
    GameSproutRuntime::AnimatorLayer synced;synced.Name="Synced";synced.SyncedLayerIndex=0;synced.SyncedLayerAffectsTiming=true;synced.IKPass=true;
    GameSproutRuntime::AnimatorMotionOverride overrideMotion;overrideMotion.State="Move";overrideMotion.Clip=ref;synced.MotionOverrides.push_back(overrideMotion);controller.Layers.push_back(synced);
    Write(root.path()+"/Assets/Move.controller",AnimationDefinition::ControllerJson(controller,false));AssetImportJob syncedJob(request);
    Check(RunAssetImport(syncedJob,context),"Sync controller compile: "+syncedJob.Result.Error);library.Add(syncedJob);
    Check(provider.LoadAnimatorController(Ref(syncedJob,eAssetType::AnimatorController),graph,error)&&graph.Layers[1].IKPass&&graph.Layers[1].SyncedLayerIndex==0&&graph.Layers[1].MotionOverrides[0].Clip==ref,error);

    clip["Frames"][1]["Time"]=0;Write(root.path()+"/Assets/Move.anim",clip);AssetImportJob invalid(Request(root,meta));
    Check(!RunAssetImport(invalid,context),"Invalid duplicate sample times accepted");
}
void UnityAnimationChecks() {
    QTemporaryDir root;
    Check(root.isValid()&&QDir(root.path()).mkpath("Assets")&&QDir(root.path()).mkpath("Library"),
        "Cannot create Unity animation fixture");
    TestMeta meta(eAssetImporterType::AnimationClip,Guid(31,32,33,34),"Unity.anim");
    const auto rig=Guid(41,42,43,44).GetGuidString();
    const GameSproutRuntime::ResourceReference skeleton{rig,20};
    const auto sourcePath=root.path()+"/Assets/Unity.anim";
    const QByteArray source=R"(%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!74 &7400000
AnimationClip:
  m_Name: 'Unity motion'
  m_RotationCurves: []
  m_PositionCurves:
  - curve:
      m_Curve:
      - time: 1
        value: {x: 0, y: 0, z: 0}
        outSlope: {x: 10, y: .inf, z: 0}
      - time: 3
        value: {x: 6, y: 10, z: 0}
        inSlope: {x: 2, y: 0, z: 0}
    path: >-
      Branch/Child
      Bone
  m_ScaleCurves: []
  m_FloatCurves:
  - curve:
      m_Curve:
      - time: 1
        value: 1
    attribute: blendShape.Expression
    path: Face
  m_SampleRate: 2
  m_AnimationClipSettings:
    m_StartTime: 1
    m_StopTime: 3
    m_LoopTime: 1
)";
    std::string error;
    Check(AssetArtifactIO::Write(sourcePath,source,error),error);
    Check(AssetArtifactIO::Write(root.path()+"/Assets/Rig.fbx",QByteArray("Fixture rig source"),error),error);
    Json::Value rigMeta;
    rigMeta["Version"]=1;
    rigMeta["Guid"]=rig;
    rigMeta["Path"]="Rig.fbx";
    rigMeta["ImporterType"]="Model";
    rigMeta["ImportSettings"]=Json::Value(Json::objectValue);
    Json::Value identity;
    identity["LocalID"]=Json::UInt64(skeleton.LocalID);
    identity["Type"]=static_cast<unsigned>(eAssetType::Skeleton);
    identity["SourceKey"]="skeleton";
    identity["Name"]="Skeleton";
    rigMeta["SubAssets"].append(identity);
    Write(root.path()+"/Assets/Rig.fbx.meta",rigMeta);

    const auto array=[](std::initializer_list<float> values) {
        Json::Value result(Json::arrayValue);
        for(auto value:values)result.append(value);
        return result;
    };
    Json::Value pose;
    pose["Translation"]=array({0,0,0});
    pose["Rotation"]=array({0,0,0,1});
    pose["Scale"]=array({1,2,3});
    const auto node=[&](const char* path,const char* name,int parent) {
        Json::Value result;
        result["Path"]=path;result["Name"]=name;
        result["Parent"]=parent;result["Transform"]=pose;
        return result;
    };
    constexpr auto rootNode="/#0/Rig#0";
    constexpr auto childNode="/#0/Rig#0/Branch#0/Child Bone#0";
    Json::Value rigBody;
    rigBody["Nodes"].append(node("/#0","",-1));
    rigBody["Nodes"].append(node(rootNode,"Rig",0));
    rigBody["Nodes"].append(node("/#0/Rig#0/Branch#0","Branch",1));
    rigBody["Nodes"].append(node(childNode,"Child Bone",2));
    Json::Value descriptor;
    descriptor["Encoding"]="BinaryValueLE1";
    const auto preparedPath=root.path()+"/Library/Rig.asset";
    auto request=Request(root,meta);
    request.SourcePaths[rig]="Rig.fbx";
    const auto publishSkeleton=[&] {
        QByteArray payload;
        Check(AssetValueBinary::Encode(rigBody,payload),"Cannot encode fixture Skeleton");
        Check(AssetArtifactIO::Write(preparedPath,payload,error),error);
        auto& resource=request.PreparedResources[rig+"_"+std::to_string(skeleton.LocalID)];
        resource.Type=eAssetType::Skeleton;
        resource.Path=preparedPath.toStdString();
        resource.DescriptorJson=AssetArtifactIO::JsonText(descriptor);
        resource.Hash=AssetArtifactIO::Hash(payload);
        resource.Size=static_cast<std::uint64_t>(payload.size());
        resource.Version=AssetArtifactIO::CurrentDataVersion(eAssetType::Skeleton);
    };
    publishSkeleton();

    Json::Value metaValue;
    Check(AssetArtifactIO::Parse(request.MetaJson,metaValue),"Cannot read fixture animation Meta");
    auto& binding=metaValue["ImportSettings"]["UnityClip"];
    binding["Skeleton"]=AnimationDefinition::Reference(skeleton);
    binding["RootPath"]=rootNode;
    request.MetaJson=AssetArtifactIO::JsonText(metaValue);
    Context context;
    AssetImportJob strict(request);
    Check(!RunAssetImport(strict,context)&&strict.Result.Error.find("TransformOnly")!=std::string::npos,
        "Unsupported source tracks were silently accepted");

    binding["TransformOnly"]=true;
    AnimationClipImportSettings settings;
    Json::Value roundTrip;
    Check(settings.ConvertFromJson(metaValue["ImportSettings"]) &&
        !settings.SetSourceAvatar({rig,21}) && settings.ConvertToJson(roundTrip) && roundTrip["UnityClip"]==binding,
        "Unity import settings did not survive the asset Meta round trip");
    request.MetaJson=AssetArtifactIO::JsonText(metaValue);
    AssetImportJob job(request);
    if(!RunAssetImport(job,context))throw std::runtime_error("Unity animation import: "+job.Result.Error);
    Check(job.Result.Diagnostics.find("m_FloatCurves")!=std::string::npos,"Transform-only import did not report ignored source curves");
    const auto ref=Ref(job,eAssetType::AnimationClip);
    Library library;
    library.Add(job);
    LibraryRuntimeResources provider(nullptr,&library);
    GameSproutRuntime::AnimationClipData clip;
    Check(provider.LoadAnimationClip(ref,clip,error),error);
    Check(clip.Tracks.size()==1&&clip.Tracks[0]==childNode&&clip.Frames.size()==5&&clip.Duration==2&&clip.SampleRate==2,
        "Unity clip did not retain its source timing and target Skeleton path");
    const auto& middle=clip.Frames[2];
    const auto& sampled=middle.Poses[0];
    Check(middle.Time==1&&std::abs(sampled.Position.X-5)<1e-5f&&sampled.Position.Y==0,
        "Unity clip did not preserve cubic or step interpolation");
    Check(sampled.Scale.X==1&&sampled.Scale.Y==2&&sampled.Scale.Z==3&&sampled.Rotation.W==1,
        "Unanimated channels did not use the Skeleton bind pose");
    Json::Value clipDescriptor;
    auto* preparedClip=library.QueryAsset(Guid(ref.Guid),ref.LocalID);
    Check(preparedClip&&AssetArtifactIO::Parse(preparedClip->GetDataDescriptorJson(),clipDescriptor)&&
        clipDescriptor["SourceFormat"]=="UnityYAML"&&clipDescriptor["SourceLoop"].asBool(),"Unity source loop was lost");
    QFile stored(sourcePath);
    Check(stored.open(QIODevice::ReadOnly)&&stored.readAll()==source,"Unity source file was changed during import");

    binding["RootPath"]="/#0";
    request.MetaJson=AssetArtifactIO::JsonText(metaValue);
    AssetImportJob wrongRoot(request);
    Check(!RunAssetImport(wrongRoot,context)&&wrongRoot.Result.Error.find("missing or ambiguous")!=std::string::npos,
        "Unity binding accepted a missing transform path");

    binding["RootPath"]=rootNode;
    request.MetaJson=AssetArtifactIO::JsonText(metaValue);
    rigBody["Nodes"].append(node("/#0/Rig#0/Branch#0/Child Bone#1","Child Bone",2));
    publishSkeleton();
    AssetImportJob ambiguous(request);
    Check(!RunAssetImport(ambiguous,context)&&ambiguous.Result.Error.find("missing or ambiguous")!=std::string::npos,
        "Unity binding guessed between duplicate sibling names");
}
void FBXChecks(const QString& file){
    QTemporaryDir root;Check(root.isValid()&&QDir(root.path()).mkpath("Assets"),"Cannot create FBX fixture");
    Check(QFile::copy(file,root.path()+"/Assets/Woman.fbx"),"Cannot copy FBX");TestMeta meta(eAssetImporterType::Model,Guid(71,72,73,74),"Woman.fbx");
    AssetImportJob job(Request(root,meta));Context context;Check(RunAssetImport(job,context),"FBX import: "+job.Result.Error);
    Library library;library.Add(job);LibraryRuntimeResources provider(nullptr,&library);GameSproutRuntime::ModelData model;std::string error;
    Check(provider.LoadModel(Ref(job,eAssetType::Model),model,error),error);GameSproutRuntime::SkeletonData skeleton;
    Check(provider.LoadSkeleton(model.Skeleton,skeleton,error),error);Check(model.Nodes.size()==skeleton.Nodes.size(),"Model/skeleton hierarchy differs");
    Check(model.Animations.size()==4,"Expected four Woman animation clips");std::size_t skinned=0;
    for(const auto& artifact:job.Result.Manifest.Assets){if(artifact.Type!=eAssetType::Mesh)continue;GameSproutRuntime::MeshRenderData mesh;
        Check(provider.LoadMesh({job.Result.Manifest.GuidString,artifact.LocalID},mesh,error),error);if(!mesh.Bones.empty()){++skinned;Check(mesh.Bones.size()==27,"Expected 27 bound bones");}}
    Check(skinned==1,"Expected one skinned Woman mesh");
    Check(job.Result.Diagnostics.find("3 vertices use more than four influences")!=std::string::npos,"Four-weight truncation was not diagnosed");
    for(const auto& ref:model.Animations){GameSproutRuntime::AnimationClipData clip;if(!provider.LoadAnimationClip(ref,clip,error)){for(const auto& artifact:job.Result.Manifest.Assets)if(artifact.LocalID==ref.LocalID)std::cerr<<artifact.DescriptorJson<<"\n";throw std::runtime_error(error);}
        Check(clip.Tracks.size()==skeleton.Nodes.size()&&clip.Frames.front().Time==0,"Clip binding/timing mismatch");}
    std::string rootPath;
    for(const auto& node:model.Nodes)if(node.Name=="Root_M")rootPath=node.Path;
    Check(!rootPath.empty(),"Cannot find stable Woman root-motion node");
    auto motionRequest=Request(root,meta);Json::Value motionMeta;AssetArtifactIO::Parse(motionRequest.MetaJson,motionMeta);
    AnimationClipImportOptions clipOptions;clipOptions.Name="WomanWalk";clipOptions.ExtractRootMotion=true;clipOptions.RootMotionNode=rootPath;
    clipOptions.Events={{0.5f,"Footstep","Left"}};
    motionMeta["ImportSettings"]["AnimationClips"]=Json::Value(Json::arrayValue);
    motionMeta["ImportSettings"]["AnimationClips"].append(AnimationImportSettingsJson::Write(clipOptions,true));
    motionRequest.MetaJson=AssetArtifactIO::JsonText(motionMeta);AssetImportJob motionJob(motionRequest);
    Check(RunAssetImport(motionJob,context),"FBX root-motion import: "+motionJob.Result.Error);
    Library motionLibrary;motionLibrary.Add(motionJob);LibraryRuntimeResources motionProvider(nullptr,&motionLibrary);
    bool checkedMotion=false;
    for(const auto& artifact:motionJob.Result.Manifest.Assets)if(artifact.Type==eAssetType::AnimationClip&&artifact.Name=="WomanWalk") {
        GameSproutRuntime::AnimationClipData clip;Check(motionProvider.LoadAnimationClip({motionJob.Result.Manifest.GuidString,artifact.LocalID},clip,error),error);
        Check(clip.RootMotionNodePath==rootPath&&clip.RootMotion.size()==clip.Frames.size()&&clip.Events.size()==1,
            "FBX settings did not produce root motion/events");checkedMotion=true;
    }
    Check(checkedMotion,"Configured FBX take was not produced");
    motionMeta["ImportSettings"]["AnimationClips"][0]["Name"]="MissingTake";
    motionRequest.MetaJson=AssetArtifactIO::JsonText(motionMeta);AssetImportJob missingTake(motionRequest);
    Check(!RunAssetImport(missingTake,context),"Unknown FBX animation Take configuration was ignored");
    std::cout<<"FBX: "<<model.Nodes.size()<<" nodes, "<<model.Animations.size()<<" clips, 27 palette bones, imported/read through Module provider\n";
}
}
#include "AnimationMotionAssetChecks.h"
#include "AvatarMaskAssetChecks.h"
#include "HumanoidAssetChecks.h"
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{SchemaChecks();NativeChecks();UnityAnimationChecks();CheckMotionAssets();CheckMaskSchema();CheckMaskImport();CheckHumanoidSchema();if(argc==3&&std::string(argv[1])=="--fbx"){FBXChecks(QString::fromUtf8(argv[2]));CheckHumanoidFBX(QString::fromUtf8(argv[2]));}
    std::cout<<"Animation asset/schema checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
