#pragma once

#include "Header.h"
#include "IAssetsManager.h"
#include "ILightingAuthoring.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "ISceneManager.h"
#include "ITaskApi.h"
#include <map>
#include <memory>

class IProject;
class LightingBaker final : public ILightingBaker, private IRuntimeFrameListener,
    private IAssetListener, private ISceneListener, private IPlayModeListener {
public:
    LightingBaker();
    ~LightingBaker() override;
    LightingBakeState GetBakeState() const override {return _State;}
    bool RegisterBakeListener(ILightingBakeListener*) override;
    void UnregisterBakeListener(ILightingBakeListener*) override;
    bool BeginBake(const LightingBakeRequest&,std::string&) override;
    void CancelBake() override;
private:
    struct Job;
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
    void OnAssetCreate(const AssetEvent& event) override {OnAssetModify(event);}
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
    void Publish(LightingBakePhase,std::string message={});
    void StopCapture();
    void Finish(LightingBakePhase,std::string);
    void SubmitFilter();
    bool AuthorOutput(std::vector<std::uint8_t>&,std::string&);
    bool IsCaptureCurrent() const;
    bool IsSelectedTargetCurrent() const;

    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    IProject* _Project=nullptr;
    IAssetsManager* _Assets=nullptr;
    ISceneManager* _Scenes=nullptr;
    IRuntimeHost* _Host=nullptr;
    IPlayMode* _Play=nullptr;
    ILightingAuthoring* _Lighting=nullptr;
    Foundation::ITaskApi* _Tasks=nullptr;
    UInt64 _TaskOwner=Foundation::InvalidTaskOwnerId;
    UInt64 _TaskID=Foundation::InvalidTaskId;
    std::unique_ptr<GameSproutRuntime::IRuntimeViewport> _Viewport;
    std::unique_ptr<Job> _Job;
    std::string _OutputGuid,_OutputPath;
    LightingBakeState _State;
    std::map<std::uint64_t,ILightingBakeListener*> _Listeners;
    std::uint64_t _ListenerSerial=0;
    bool _FrameSubscribed=false,_AssetSubscribed=false,_SceneSubscribed=false,_PlaySubscribed=false;
    bool _Notifying=false;
};
