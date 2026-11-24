#include "LightingBaker.h"
#include "IProject.h"
#include "RuntimeRenderApi.h"
#include "../../Tools/Environment/LightingBakeKernels.h"
#include "../../Tools/Environment/LightingCapture.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <bit>
#include <limits>

namespace {
using namespace GameSproutRuntime;
using namespace GameSproutLightingCapture;

bool ValidEdge(std::uint32_t edge) {return edge>=16&&edge<=512&&std::has_single_bit(edge);}

Vector3 GridPosition(const BakedIndirectVolume& volume,const std::array<std::uint32_t,3>& grid,std::uint32_t cell) {
    const auto fraction=[](std::uint32_t coordinate,std::uint32_t size) {
        return size==1?0.f:float(coordinate)/float(size-1)-.5f;
    };
    const auto x=cell%grid[0];cell/=grid[0];
    const auto y=cell%grid[1];const auto z=cell/grid[1];
    return {volume.Center.X+volume.Size.X*fraction(x,grid[0]),
            volume.Center.Y+volume.Size.Y*fraction(y,grid[1]),
            volume.Center.Z+volume.Size.Z*fraction(z,grid[2])};
}

bool SaveNewFile(const QString& path,const void* data,std::size_t bytes) {
    if(QFileInfo::exists(path)||bytes>std::uint64_t(std::numeric_limits<qint64>::max()))return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly)&&
        file.write(static_cast<const char*>(data),static_cast<qint64>(bytes))==static_cast<qint64>(bytes)&&file.commit();
}

struct FilterResult {
    SH9 SH{};
    std::vector<std::uint8_t> Bytes;
    std::string Error;
};
}

struct LightingBaker::Job {
    LightingBakeRequest Request;
    ReflectionProbeSettings Probe;
    BakedIndirectVolume Volume;
    LinearCubeCapture Cube;
    IndirectLightingRenderData Grid;
    std::uint64_t SceneRevision=0,ProviderRevision=0;
    std::uint32_t Face=0;
};

LightingBaker::LightingBaker()=default;
LightingBaker::~LightingBaker(){Release(nullptr);}
void LightingBaker::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    if(!editor)return;
    _Runtime=editor->_Runtime;
    _Project=editor->GetGameSproutModule<IProject>("Project");
    _Assets=editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Play=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");
    _Lighting=editor->GetGameSproutModule<ILightingAuthoring>("LightingAuthoring");
    _Tasks=api->_Foundation?api->_Foundation->TaskApi():nullptr;
}
bool LightingBaker::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    return _Runtime&&_Project&&_Assets&&_Scenes&&_Play&&_Host&&_Lighting&&_Tasks;
}
bool LightingBaker::Launch(GameSproutEditor::GameSproutModuleApi*) {
    _TaskOwner=_Tasks->RegisterTaskOwner();
    _AssetSubscribed=_Assets->RegisterAssetListener(this);
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);
    _PlaySubscribed=_Play->RegisterPlayModeListener(this);
    return _TaskOwner!=Foundation::InvalidTaskOwnerId&&_AssetSubscribed&&_SceneSubscribed&&_PlaySubscribed;
}
void LightingBaker::Release(GameSproutEditor::GameSproutModuleApi*) {
    StopCapture();
    if(_PlaySubscribed)_Play->UnregisterPlayModeListener(this);
    if(_SceneSubscribed)_Scenes->UnregisterSceneListener(this);
    if(_AssetSubscribed)_Assets->UnregisterAssetListener(this);
    _PlaySubscribed=_SceneSubscribed=_AssetSubscribed=false;
    if(_Tasks&&_TaskOwner!=Foundation::InvalidTaskOwnerId)_Tasks->UnregisterTaskOwner(_TaskOwner);
    _TaskID=Foundation::InvalidTaskId;_TaskOwner=Foundation::InvalidTaskOwnerId;
    _Viewport.reset();_Job.reset();_Listeners.clear();_State={};
}
bool LightingBaker::RegisterBakeListener(ILightingBakeListener* listener) {
    if(!listener||_ListenerSerial==std::numeric_limits<std::uint64_t>::max())return false;
    for(const auto& [_,existing]:_Listeners)if(existing==listener)return false;
    _Listeners.emplace(++_ListenerSerial,listener);return true;
}
void LightingBaker::UnregisterBakeListener(ILightingBakeListener* listener) {
    for(auto iter=_Listeners.begin();iter!=_Listeners.end();++iter)if(iter->second==listener){_Listeners.erase(iter);break;}
}
void LightingBaker::Publish(LightingBakePhase phase,std::string message) {
    _State.Phase=phase;_State.Message=std::move(message);
    const auto last=_ListenerSerial;
    _Notifying=true;
    for(auto iter=_Listeners.begin();iter!=_Listeners.end()&&iter->first<=last;) {
        const auto id=iter->first;
        try{iter->second->OnLightingBakeChanged(_State);}catch(...) {}
        iter=_Listeners.upper_bound(id);
    }
    _Notifying=false;
}

bool LightingBaker::BeginBake(const LightingBakeRequest& request,std::string& error) {
    error.clear();
    if(_State.IsActive()||_Notifying){error="已有光照烘焙在进行。";return false;}
    if(!_Host||!_Runtime||_Host->IsFrameActive()||_Runtime->IsExecuting()||
       _Play->GetState().State!=EditorPlayState::Edit||_Scenes->IsDocumentSessionActive()||
       _Scenes->HasTransformEdit()||_Scenes->HasPropertyPreview()) {
        error="请在主场景的编辑模式下启动烘焙。";return false;
    }
    auto* scene=_Scenes->GetActiveScene();
    if(!scene||scene->GetInstanceID()!=request.SceneID){error="烘焙目标场景已经切换。";return false;}
    const auto& settings=scene->GetEnvironmentLighting();
    auto job=std::make_unique<Job>();
    job->Request=request;job->Cube.Edge=request.FaceSize;
    const bool reflection=request.Kind==LightingBakeKind::ReflectionProbe;
    const auto samples=reflection?1u:BakedIndirectSampleCount(request.Grid);
    if(!ValidEdge(request.FaceSize)||!std::isfinite(request.CaptureDistance)||request.CaptureDistance<=.03f||
       !samples||(reflection&&(!ValidEdge(request.ReflectionSize)||request.ReflectionSize>request.FaceSize||
       request.ReflectionSamples<16||request.ReflectionSamples>512||!std::has_single_bit(request.ReflectionSamples)))) {
        error="检查烘焙分辨率、采样数、范围或体积网格。";return false;
    }
    if(reflection) {
        if(request.Index>=settings.ReflectionProbes.size()){error="找不到所选反射探针。";return false;}
        job->Probe=settings.ReflectionProbes[request.Index];
    }else {
        if(request.Kind!=LightingBakeKind::IndirectVolume||request.Index>=settings.IndirectLightVolumes.size()) {
            error="找不到所选间接光体积。";return false;
        }
        job->Volume=settings.IndirectLightVolumes[request.Index];
        job->Grid.Dimensions=request.Grid;
        job->Grid.Coefficients.reserve(samples);
    }
    auto* provider=_Runtime->GetResourceProvider();
    if(!provider){error="预处理资源服务尚未就绪。";return false;}
    job->SceneRevision=scene->GetAuthoringRevision();
    job->ProviderRevision=provider->GetRevision();
    auto viewport=std::unique_ptr<IRuntimeViewport>(_Runtime->CreateOffscreenRenderer(request.FaceSize,request.FaceSize,provider,error));
    if(!viewport)return false;
    if(!_Host->RegisterFrameListener(this)){error="无法订阅烘焙帧。";return false;}
    _FrameSubscribed=true;_Viewport=std::move(viewport);_Job=std::move(job);
    _State={};_State.TotalFaces=samples*6;
    Publish(LightingBakePhase::Capturing,"采集线性场景光照");
    return true;
}
void LightingBaker::StopCapture() {
    if(_FrameSubscribed){_Host->UnregisterFrameListener(this);_FrameSubscribed=false;}
}
void LightingBaker::Finish(LightingBakePhase phase,std::string message) {
    StopCapture();
    if(_TaskID!=Foundation::InvalidTaskId) {_Tasks->CancelTask(_TaskID);_TaskID=Foundation::InvalidTaskId;}
    _Viewport.reset();_Job.reset();_OutputPath.clear();_OutputGuid.clear();
    Publish(phase,std::move(message));
}
void LightingBaker::CancelBake() {
    if(!_State.IsActive()||_Notifying)return;
    const bool importedSourceWritten=_State.Phase==LightingBakePhase::Importing;
    Finish(LightingBakePhase::Idle,importedSourceWritten?"绑定已取消，烘焙资源已保存在 Assets。":"烘焙已取消");
}
void LightingBaker::OnActiveSceneChanged(){if(_State.IsActive())CancelBake();}
void LightingBaker::OnSceneStateChanged() {
    if((_State.Phase==LightingBakePhase::Capturing||_State.Phase==LightingBakePhase::Filtering)&&
       !IsCaptureCurrent())
        Finish(LightingBakePhase::Failed,"场景在采集期间发生修改，已停止烘焙。");
}
void LightingBaker::OnPlayModeStateChanged(const PlayModeState& state) {
    if(state.State!=EditorPlayState::Edit&&_State.IsActive())CancelBake();
}
void LightingBaker::OnRuntimeFrame(const RuntimeFrameEvent&) {
    if(_State.Phase!=LightingBakePhase::Capturing||!_Job)return;
    auto* scene=_Scenes->GetActiveScene();
    if(!IsCaptureCurrent()) {
        Finish(LightingBakePhase::Failed,"采集期间场景或资源已变化，请重新烘焙。");return;
    }
    auto& job=*_Job;
    const bool reflection=job.Request.Kind==LightingBakeKind::ReflectionProbe;
    const auto position=reflection?job.Probe.CapturePosition:
        GridPosition(job.Volume,job.Request.Grid,static_cast<std::uint32_t>(job.Grid.Coefficients.size()));
    const auto flag=!job.Request.StaticOnly?StaticFlags::None:
        reflection?StaticFlags::ReflectionProbeStatic:StaticFlags::ContributeGI;
    std::string error;
    try {
        if(!CaptureFace(*_Viewport,*scene,position,job.Face,flag,~0u,job.Request.CaptureDistance,job.Cube.Faces[job.Face],error)) {
            Finish(LightingBakePhase::Failed,error);return;
        }
        ++job.Face;++_State.CompletedFaces;
        if(job.Face==job.Cube.Faces.size())SubmitFilter();
        else Publish(LightingBakePhase::Capturing,"采集线性场景光照");
    }catch(const std::exception& ex){Finish(LightingBakePhase::Failed,ex.what());}
}
void LightingBaker::SubmitFilter() {
    StopCapture();
    auto cube=std::make_shared<LinearCubeCapture>(std::move(_Job->Cube));
    auto result=std::make_shared<FilterResult>();
    const auto request=_Job->Request;
    const bool reflection=request.Kind==LightingBakeKind::ReflectionProbe;
    const auto task=_Tasks->SubmitTask({_TaskOwner},
        [request,cube,result,reflection](Int64& code,Foundation::ITaskContext& context) {
            if(context.IsCancellationRequested())return false;
            const bool ok=reflection?BuildReflectionAsset(*cube,request.ReflectionSize,request.ReflectionSamples,
                result->Bytes,result->Error,[&]{return context.IsCancellationRequested();}):
                ProjectIrradiance(*cube,result->SH,result->Error);
            code=ok?0:-1;
            return ok;
        },[this,result,reflection](const Foundation::TaskCompletion& completion,bool ok,const Foundation::ITaskContext&) {
            if(completion._TaskId!=_TaskID||!_Job)return;
            _TaskID=Foundation::InvalidTaskId;
            if(!ok){Finish(LightingBakePhase::Failed,result->Error.empty()?"光照滤波失败。":result->Error);return;}
            if(!IsCaptureCurrent()) {
                Finish(LightingBakePhase::Failed,"滤波期间场景或资源已变化，请重新烘焙。");return;
            }
            std::string error;
            if(!reflection) {
                _Job->Grid.Coefficients.push_back(result->SH);
                if(_State.CompletedFaces<_State.TotalFaces) {
                    _Job->Face=0;_Job->Cube={};_Job->Cube.Edge=_Job->Request.FaceSize;
                    if(!_Host->RegisterFrameListener(this)){Finish(LightingBakePhase::Failed,"无法继续采集。");return;}
                    _FrameSubscribed=true;
                    Publish(LightingBakePhase::Capturing,"采集线性场景光照");
                    return;
                }
                if(!BuildIrradianceAsset(_Job->Grid,result->Bytes,error)){Finish(LightingBakePhase::Failed,error);return;}
            }
            _Viewport.reset();
            if(!AuthorOutput(result->Bytes,error)){Finish(LightingBakePhase::Failed,error);return;}
            Publish(LightingBakePhase::Importing,_OutputPath);
        });
    if(!task.Accepted()){Finish(LightingBakePhase::Failed,"无法提交光照滤波任务。");return;}
    _TaskID=task._TaskId;
    Publish(LightingBakePhase::Filtering,"预滤波线性光照");
}

bool LightingBaker::AuthorOutput(std::vector<std::uint8_t>& bytes,std::string& error) {
    const QDir assets(QString::fromStdString(_Project->GetProjectRoot())+QStringLiteral("/Assets"));
    const auto folder=QStringLiteral("Lighting/Baked");
    if(!assets.exists()||!assets.mkpath(folder)) {error="无法创建光照资产目录。";return false;}
    const QString base=QFileInfo(assets.absolutePath()).canonicalFilePath()+QLatin1Char('/');
    if(!QFileInfo(assets.filePath(folder)).canonicalFilePath().startsWith(base)) {
        error="烘焙目录必须位于项目的 Assets 内。";return false;
    }
    const auto uuid=QUuid::createUuid().toString(QUuid::Id128);
    const bool reflection=_Job->Request.Kind==LightingBakeKind::ReflectionProbe;
    const QString file=(reflection?QStringLiteral("Reflection-"):QStringLiteral("Indirect-"))+uuid+
        (reflection?QStringLiteral(".environment"):QStringLiteral(".irradiance"));
    const auto relative=folder+QLatin1Char('/')+file;
    const auto path=assets.filePath(relative);
    const auto guid=uuid.toStdString();
    Json::Value meta(Json::objectValue),main(Json::objectValue);
    meta["Version"]=1;meta["Guid"]=guid;meta["Path"]=relative.toStdString();
    meta["ImporterType"]=reflection?"Environment":"Irradiance";meta["ImportSettings"]=Json::Value(Json::objectValue);
    main["Type"]=static_cast<unsigned>(reflection?eAssetType::Environment:eAssetType::Irradiance);
    main["SourceKey"]="main";main["LocalID"]=Json::UInt64(1);
    main["Name"]=QFileInfo(file).completeBaseName().toStdString();
    meta["SubAssets"].append(std::move(main));
    const auto encoded=AssetArtifactIO::JsonText(meta);
    const auto metaPath=path+QStringLiteral(".meta");
    if(!SaveNewFile(metaPath,encoded.data(),encoded.size())){error="无法写入烘焙资产 Meta。";return false;}
    if(!SaveNewFile(path,bytes.data(),bytes.size())) {
        QFile::remove(metaPath);error="无法写入烘焙资产。";return false;
    }
    _OutputGuid=guid;_OutputPath=relative.toStdString();
    return true;
}
bool LightingBaker::IsCaptureCurrent() const {
    auto* provider=_Runtime->GetResourceProvider();
    return IsSelectedTargetCurrent()&&provider&&provider->GetRevision()==_Job->ProviderRevision;
}
bool LightingBaker::IsSelectedTargetCurrent() const {
    const auto* scene=_Scenes->GetActiveScene();
    if(!scene||!_Job||scene->GetInstanceID()!=_Job->Request.SceneID||
       scene->GetAuthoringRevision()!=_Job->SceneRevision||_Scenes->HasTransformEdit()||_Scenes->HasPropertyPreview())return false;
    const auto& settings=scene->GetEnvironmentLighting();
    const auto i=_Job->Request.Index;
    return _Job->Request.Kind==LightingBakeKind::ReflectionProbe?
        i<settings.ReflectionProbes.size()&&settings.ReflectionProbes[i]==_Job->Probe:
        i<settings.IndirectLightVolumes.size()&&settings.IndirectLightVolumes[i]==_Job->Volume;
}
void LightingBaker::OnAssetModify(const AssetEvent& event) {
    if(_State.Phase!=LightingBakePhase::Importing||event.GuidValue.GetGuidString()!=_OutputGuid)return;
    if(event.State==eAssetImportState::Failed){Finish(LightingBakePhase::Failed,event.Error);return;}
    if(event.State!=eAssetImportState::Ready)return;
    if(!IsSelectedTargetCurrent()) {
        Finish(LightingBakePhase::Complete,"烘焙资源已保存，目标已变化，请重新选择资源。");return;
    }
    auto* scene=_Scenes->GetActiveScene();
    auto settings=scene->GetEnvironmentLighting();
    const ResourceReference asset{_OutputGuid,1};
    const auto i=_Job->Request.Index;
    if(_Job->Request.Kind==LightingBakeKind::ReflectionProbe)settings.ReflectionProbes[i].Environment=asset;
    else settings.IndirectLightVolumes[i].Asset=asset;
    std::string error;
    if(!_Lighting->SetEnvironment(scene->GetInstanceID(),settings,error)) {
        Finish(LightingBakePhase::Failed,_OutputPath+" 已保存，场景绑定失败："+error);return;
    }
    Finish(LightingBakePhase::Complete,"已导入并应用 "+_OutputPath);
}
void LightingBaker::OnAssetRemove(const AssetEvent& event) {
    if(_State.Phase==LightingBakePhase::Importing&&event.GuidValue.GetGuidString()==_OutputGuid)
        Finish(LightingBakePhase::Failed,"烘焙产物在导入时被移除。");
}
