#pragma once

#include "IEnvironmentAuthoring.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "ISceneManager.h"
#include <map>

class EnvironmentAuthoring final : public IEnvironmentAuthoring,private ISceneListener,private IPlayModeListener {
public:
    ~EnvironmentAuthoring() override;
    EnvironmentAuthoringSnapshot ReadEnvironment() const override;
    bool ApplySettings(std::uint64_t,const GameSproutRuntime::EnvironmentSettings&,std::string&) override;
    bool RegisterEnvironmentListener(IEnvironmentAuthoringListener*) override;
    void UnregisterEnvironmentListener(IEnvironmentAuthoringListener*) override;
    std::vector<GameSproutRuntime::EnvironmentParameterDefinition> ReadParameters() const override;
    std::vector<GameSproutRuntime::EnvironmentEffectDescriptor> ReadEffects() const override;
    std::vector<GameSproutRuntime::EnvironmentEventDescriptor> ReadEvents() const override;
    bool ParseValue(const std::string&,GameSproutRuntime::EnvironmentValue&,std::string&) const override;
    std::string FormatValue(const GameSproutRuntime::EnvironmentValue&) const override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    void OnActiveSceneChanged() override {Publish();}
    void OnSceneStateChanged() override {Publish();}
    void OnPlayModeStateChanged(const PlayModeState&) override {Publish();}
    void Publish();

    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    GameSproutRuntime::IEnvironmentRuntime* _Environment=nullptr;
    ISceneManager* _Scenes=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    bool _SceneSubscribed=false,_PlaySubscribed=false,_Publishing=false;
    std::map<std::uint64_t,IEnvironmentAuthoringListener*> _Listeners;
    std::uint64_t _NextListener=0;
    EnvironmentAuthoringSnapshot _Published;
};
