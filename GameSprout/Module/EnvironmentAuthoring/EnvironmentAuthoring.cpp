#include "EnvironmentAuthoring.h"
#include "SceneEnvironmentDocument.h"
#include <QDebug>
#include <algorithm>
#include <limits>
#include <sstream>

using namespace GameSproutRuntime;

EnvironmentAuthoring::~EnvironmentAuthoring(){Release(nullptr);}

void EnvironmentAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    if(!editor)return;
    _Runtime=editor->_Runtime;
    _Environment=_Runtime?QueryEnvironmentRuntime(*_Runtime):nullptr;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Play=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");
}
bool EnvironmentAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    return _Environment&&_Scenes&&_Play&&_Host;
}
bool EnvironmentAuthoring::Launch(GameSproutEditor::GameSproutModuleApi*) {
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);
    _PlaySubscribed=_Play->RegisterPlayModeListener(this);
    return _SceneSubscribed&&_PlaySubscribed;
}
EnvironmentAuthoringSnapshot EnvironmentAuthoring::ReadEnvironment() const {
    EnvironmentAuthoringSnapshot data;
    auto* scene=_Scenes->IsDocumentSessionActive()?nullptr:_Scenes->GetActiveScene();
    if(!scene||!_Environment->GetEnvironmentSettings(*scene,data.Settings))return data;
    data.SceneID=scene->GetInstanceID();
    data.Editable=_Play->GetState().State==EditorPlayState::Edit&&!_Scenes->IsPlaySessionActive();
    return data;
}
bool EnvironmentAuthoring::ApplySettings(std::uint64_t sceneID,const EnvironmentSettings& value,std::string& error) {
    const auto snapshot=ReadEnvironment();
    if(!sceneID||sceneID!=snapshot.SceneID||!snapshot.Editable||_Publishing||
        _Runtime->IsExecuting()||_Host->IsFrameActive()) {
        error="请在可编辑的当前场景中应用环境组合。";
        return false;
    }
    if(value==snapshot.Settings){error.clear();return true;}
    const auto operation=[environment=_Environment](const EnvironmentSettings& settings) {
        return [environment,settings](IGameObject& object,std::string& failure) {
            auto* scene=dynamic_cast<IScene*>(&object);
            if(scene&&environment->SetEnvironmentSettings(*scene,settings))return true;
            failure="无法应用环境组合。请检查 ID、目标、参数类型、权重和关键帧时间。";
            return false;
        };
    };
    return _Scenes->EditObject(sceneID,"修改环境组合",operation(value),operation(snapshot.Settings),error);
}
bool EnvironmentAuthoring::RegisterEnvironmentListener(IEnvironmentAuthoringListener* listener) {
    if(!listener||_NextListener==std::numeric_limits<std::uint64_t>::max()||
        std::any_of(_Listeners.begin(),_Listeners.end(),[listener](const auto& entry){return entry.second==listener;}))
        return false;
    _Listeners.emplace(++_NextListener,listener);
    return true;
}
void EnvironmentAuthoring::UnregisterEnvironmentListener(IEnvironmentAuthoringListener* listener) {
    std::erase_if(_Listeners,[listener](const auto& entry){return entry.second==listener;});
    if(_Listeners.empty())_Published={};
}
std::vector<EnvironmentParameterDefinition> EnvironmentAuthoring::ReadParameters() const {
    return _Environment->GetEnvironmentParameters();
}
std::vector<EnvironmentEffectDescriptor> EnvironmentAuthoring::ReadEffects() const {
    return _Environment->GetEnvironmentEffects();
}
std::vector<EnvironmentEventDescriptor> EnvironmentAuthoring::ReadEvents() const {
    return _Environment->GetEnvironmentEvents();
}
bool EnvironmentAuthoring::ParseValue(const std::string& text,EnvironmentValue& value,std::string& error) const {
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"]=true;
    builder["failIfExtra"]=true;
    builder["allowComments"]=false;
    builder["allowTrailingCommas"]=false;
    Json::Value data;
    std::istringstream stream(text);
    if(!Json::parseFromStream(builder,stream,&data,&error))return false;
    try {value=SceneEnvironmentDocument::ReadValue(data,"Value");}
    catch(const std::exception& exception){error=exception.what();return false;}
    return true;
}
std::string EnvironmentAuthoring::FormatValue(const EnvironmentValue& value) const {
    Json::StreamWriterBuilder builder;
    builder["indentation"]="";
    builder["precision"]=std::numeric_limits<float>::max_digits10;
    return Json::writeString(builder,SceneEnvironmentDocument::SerializeValue(value));
}
void EnvironmentAuthoring::Publish() {
    if(_Publishing||_Listeners.empty())return;
    const auto snapshot=ReadEnvironment();
    if(_Published==snapshot)return;
    _Published=snapshot;
    _Publishing=true;
    const auto last=_NextListener;
    for(auto current=_Listeners.begin();current!=_Listeners.end()&&current->first<=last;) {
        const auto id=current->first;
        try {current->second->OnEnvironmentChanged(snapshot);}
        catch(...) {qWarning("Environment authoring listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
    _Publishing=false;
}
void EnvironmentAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    _SceneSubscribed=false;_PlaySubscribed=false;_Publishing=false;
    _Listeners.clear();_Published={};_NextListener=0;
    _Runtime=nullptr;_Environment=nullptr;_Scenes=nullptr;_Play=nullptr;_Host=nullptr;
}
