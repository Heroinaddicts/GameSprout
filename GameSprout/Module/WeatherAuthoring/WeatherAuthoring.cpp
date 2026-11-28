#include "WeatherAuthoring.h"
#include <QDebug>
#include <QThread>
#include <algorithm>
#include <limits>

using namespace GameSproutRuntime;

namespace {
WeatherPreset Look(WeatherLook look,const WeatherPreset& original) {
    auto preset=original;
    switch(look) {
    case WeatherLook::Clear:
        preset.Clouds[0].Coverage=.08f;preset.Clouds[0].Density=.45f;
        preset.Clouds[1].Coverage=.05f;preset.Clouds[1].Density=.3f;
        preset.CloudColor={1,1,1,1};preset.DaylightMultiplier=1;preset.AmbientMultiplier=1;preset.FogStrength=.75f;
        break;
    case WeatherLook::Cloudy:
        preset.Clouds[0].Coverage=.46f;preset.Clouds[0].Density=.7f;
        preset.Clouds[1].Coverage=.26f;preset.Clouds[1].Density=.5f;
        preset.CloudColor={.91f,.94f,1,1};preset.DaylightMultiplier=.72f;preset.AmbientMultiplier=.9f;preset.FogStrength=1;
        break;
    case WeatherLook::Overcast:
        preset.Clouds[0].Coverage=.93f;preset.Clouds[0].Density=.97f;
        preset.Clouds[1].Coverage=.75f;preset.Clouds[1].Density=.82f;
        preset.CloudColor={.39f,.44f,.53f,1};preset.DaylightMultiplier=.27f;preset.AmbientMultiplier=.78f;preset.FogStrength=1.32f;
        break;
    }
    return preset;
}
}

WeatherAuthoring::~WeatherAuthoring(){Release(nullptr);}
void WeatherAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    if(!editor)return;
    _Runtime=editor->_Runtime;
    _Weather=_Runtime?QueryWeatherRuntime(*_Runtime):nullptr;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Play=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");
}
bool WeatherAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();
    return _Weather&&_Scenes&&_Play&&_Host;
}
bool WeatherAuthoring::Launch(GameSproutEditor::GameSproutModuleApi*) {
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);
    _PlaySubscribed=_Play->RegisterPlayModeListener(this);
    return _SceneSubscribed&&_PlaySubscribed;
}

bool WeatherAuthoring::ReadWeather(WeatherAuthoringSnapshot& snapshot,std::string& error) const {
    error.clear();
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread||!_Weather) {
        error="天气编辑服务只能在所属主线程读取。";return false;
    }
    snapshot={};
    auto* scene=_Scenes->IsDocumentSessionActive()?nullptr:_Scenes->GetActiveScene();
    if(!scene||!_Weather->GetWeatherSettings(*scene,snapshot.Settings))return true;
    snapshot.SceneID=scene->GetInstanceID();
    const auto state=_Play->GetState().State;
    snapshot.Editable=state==EditorPlayState::Edit&&!_Scenes->IsPlaySessionActive();
    snapshot.PreviewAvailable=snapshot.Settings.Enabled&&(snapshot.Editable||
        state==EditorPlayState::Playing||state==EditorPlayState::Paused);
    snapshot.PreviewPlaying=_PreviewPlaying&&_PreviewSceneID==snapshot.SceneID;
    return true;
}
IScene* WeatherAuthoring::Resolve(std::uint64_t sceneID,WeatherAuthoringSnapshot& snapshot,std::string& error) const {
    if(!ReadWeather(snapshot,error))return nullptr;
    if(!sceneID||sceneID!=snapshot.SceneID||_Publishing||_Runtime->IsExecuting()||_Host->IsFrameActive()) {
        error="场景已切换或正在执行其他操作。";return nullptr;
    }
    return _Scenes->GetActiveScene();
}
bool WeatherAuthoring::ApplySettings(std::uint64_t sceneID,const WeatherSettings& value,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    if(!Resolve(sceneID,snapshot,error))return false;
    if(!snapshot.Editable||!IsValidWeatherSettings(value)) {
        error="请在编辑模式下使用有效的天气参数。";return false;
    }
    if(value==snapshot.Settings)return true;
    const auto operation=[weather=_Weather](WeatherSettings settings) {
        return [weather,settings](IGameObject& object,std::string& failure) {
            auto* scene=dynamic_cast<IScene*>(&object);
            if(scene&&weather->SetWeatherSettings(*scene,settings))return true;
            failure="无法修改当前场景的天气设置。";return false;
        };
    };
    const bool applied=_Scenes->EditObject(sceneID,"修改天气设置",operation(value),operation(snapshot.Settings),error);
    if(applied&&!value.Enabled){StopClock();Publish();}
    return applied;
}
bool WeatherAuthoring::ApplyLook(std::uint64_t sceneID,WeatherLook look,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    if(!Resolve(sceneID,snapshot,error))return false;
    auto value=snapshot.Settings;
    value.Enabled=true;
    value.Preset=Look(look,value.Preset);
    return ApplySettings(sceneID,value,error);
}
bool WeatherAuthoring::BeginClock(std::uint64_t sceneID,float seconds,std::string& error) {
    if(!_FrameSubscribed) {
        if(!_Host->RegisterFrameListener(this)){error="无法开始天气预览。";return false;}
        _FrameSubscribed=true;_PreviewTimer.start();
    }
    _PreviewSceneID=sceneID;
    _PreviewRemaining=std::max(_PreviewRemaining,seconds);
    return true;
}
bool WeatherAuthoring::PreviewLook(std::uint64_t sceneID,WeatherLook look,float seconds,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    auto* scene=Resolve(sceneID,snapshot,error);
    if(!scene)return false;
    if(!snapshot.PreviewAvailable||!std::isfinite(seconds)||seconds<0) {error="天气预览不可用。";return false;}
    if(snapshot.Editable&&seconds>0&&!BeginClock(sceneID,seconds,error))return false;
    if(_Weather->TransitionWeather(*scene,Look(look,snapshot.Settings.Preset),seconds))return true;
    error="Runtime 拒绝了天气过渡。";return false;
}
bool WeatherAuthoring::PreviewTime(std::uint64_t sceneID,float hours,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    auto* scene=Resolve(sceneID,snapshot,error);
    if(!scene)return false;
    if(snapshot.PreviewAvailable&&_Weather->SetTimeOfDay(*scene,hours))return true;
    error="无法预览指定时间。";return false;
}
bool WeatherAuthoring::SetPreviewPlaying(std::uint64_t sceneID,bool enabled,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    if(!Resolve(sceneID,snapshot,error))return false;
    if(!snapshot.Editable||!snapshot.PreviewAvailable) {error="请先启用编辑场景的天气。";return false;}
    if(enabled&&!BeginClock(sceneID,0,error))return false;
    _PreviewPlaying=enabled;
    if(!enabled&&_PreviewRemaining<=0)StopClock();
    Publish();return true;
}
bool WeatherAuthoring::ResetPreview(std::uint64_t sceneID,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    auto* scene=Resolve(sceneID,snapshot,error);
    if(!scene)return false;
    if(!snapshot.PreviewAvailable) {error="天气预览不可用。";return false;}
    if(_Weather->SetTimeOfDay(*scene,snapshot.Settings.TimeOfDayHours)&&
       _Weather->TransitionWeather(*scene,snapshot.Settings.Preset,0)) {
        if(snapshot.Editable)StopClock();
        Publish();return true;
    }
    error="无法恢复场景天气。";return false;
}
bool WeatherAuthoring::TriggerLightning(std::uint64_t sceneID,const WeatherLightning& lightning,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    auto* scene=Resolve(sceneID,snapshot,error);
    if(!scene)return false;
    if(!snapshot.PreviewAvailable||!IsValidWeatherLightning(lightning)) {
        error="请启用天气并检查闪电参数。";return false;
    }
    if(snapshot.Editable&&!BeginClock(sceneID,lightning.DurationSeconds,error))return false;
    if(_Weather->TriggerLightning(*scene,lightning))return true;
    error="Runtime 拒绝了闪电。";return false;
}
void WeatherAuthoring::OnRuntimeFrame(const RuntimeFrameEvent& event) {
    auto* scene=event.GameplayActive?nullptr:dynamic_cast<IScene*>(_Runtime->QueryGameObject(_PreviewSceneID));
    const auto elapsed=static_cast<float>(std::clamp(_PreviewTimer.nsecsElapsed()*1e-9,0.,.1));
    _PreviewTimer.restart();
    // Generic animation playback already advances the primary scene once.
    const bool alreadyAdvanced=scene&&scene->IsSimulationEnabled()&&event.DeltaSeconds>0;
    if(!scene||(!alreadyAdvanced&&!_Weather->AdvanceWeatherPreview(*scene,elapsed))) {
        StopClock();Publish();return;
    }
    _PreviewRemaining=std::max(0.f,_PreviewRemaining-(alreadyAdvanced?event.DeltaSeconds:elapsed));
    if(!_PreviewPlaying&&_PreviewRemaining==0)StopClock();
}
void WeatherAuthoring::StopClock() {
    if(_FrameSubscribed&&_Host)_Host->UnregisterFrameListener(this);
    _FrameSubscribed=false;_PreviewPlaying=false;_PreviewRemaining=0;_PreviewSceneID=0;_PreviewTimer.invalidate();
}
void WeatherAuthoring::OnActiveSceneChanged(){StopClock();Publish();}
void WeatherAuthoring::OnPlayModeStateChanged(const PlayModeState& state) {
    if(state.State!=EditorPlayState::Edit)StopClock();
    Publish();
}
bool WeatherAuthoring::RegisterWeatherListener(IWeatherAuthoringListener* listener) {
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread||!listener||
       _NextListener==std::numeric_limits<std::uint64_t>::max())return false;
    for(const auto& entry:_Listeners)if(entry.second==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void WeatherAuthoring::UnregisterWeatherListener(IWeatherAuthoringListener* listener) {
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread)return;
    for(auto current=_Listeners.begin();current!=_Listeners.end();) {
        if(current->second==listener)current=_Listeners.erase(current);
        else ++current;
    }
}
void WeatherAuthoring::Publish() {
    WeatherAuthoringSnapshot snapshot;std::string error;
    if(_Publishing||!ReadWeather(snapshot,error)||_Published==snapshot)return;
    _Published=snapshot;_Publishing=true;
    const auto last=_NextListener;
    for(auto current=_Listeners.begin();current!=_Listeners.end()&&current->first<=last;) {
        const auto id=current->first;
        try {current->second->OnWeatherAuthoringChanged(snapshot);}
        catch(...) {qWarning("Weather authoring listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
    _Publishing=false;
}
void WeatherAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    StopClock();
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    _SceneSubscribed=false;_PlaySubscribed=false;_Listeners.clear();_Published.reset();_NextListener=0;
    _Weather=nullptr;_Runtime=nullptr;_Scenes=nullptr;_Play=nullptr;_Host=nullptr;_OwnerThread=nullptr;
}
