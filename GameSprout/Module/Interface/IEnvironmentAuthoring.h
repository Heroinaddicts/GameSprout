#pragma once

#include "GameSproutEditorApi.h"
#include "RuntimeEnvironmentApi.h"

struct EnvironmentAuthoringSnapshot {
    std::uint64_t SceneID=0;
    bool Editable=false;
    GameSproutRuntime::EnvironmentSettings Settings;
    bool operator==(const EnvironmentAuthoringSnapshot&) const = default;
};

class IEnvironmentAuthoringListener {
public:
    virtual ~IEnvironmentAuthoringListener()=default;
    virtual void OnEnvironmentChanged(const EnvironmentAuthoringSnapshot&)=0;
};

class IEnvironmentAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IEnvironmentAuthoring()=default;
    virtual EnvironmentAuthoringSnapshot ReadEnvironment() const=0;
    virtual bool ApplySettings(std::uint64_t sceneID,const GameSproutRuntime::EnvironmentSettings&,std::string& error)=0;
    virtual bool RegisterEnvironmentListener(IEnvironmentAuthoringListener*)=0;
    virtual void UnregisterEnvironmentListener(IEnvironmentAuthoringListener*)=0;
    virtual std::vector<GameSproutRuntime::EnvironmentParameterDefinition> ReadParameters() const=0;
    virtual std::vector<GameSproutRuntime::EnvironmentEffectDescriptor> ReadEffects() const=0;
    virtual std::vector<GameSproutRuntime::EnvironmentEventDescriptor> ReadEvents() const=0;
    virtual bool ParseValue(const std::string&,GameSproutRuntime::EnvironmentValue&,std::string& error) const=0;
    virtual std::string FormatValue(const GameSproutRuntime::EnvironmentValue&) const=0;
};
