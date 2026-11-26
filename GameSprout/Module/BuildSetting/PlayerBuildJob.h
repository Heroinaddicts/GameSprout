#pragma once

#include "IBuildSetting.h"
#include "IAssetsPipeline.h"
#include "IScriptCompiler.h"
#include "RuntimeProjectSettings.h"
#include <functional>

struct PlayerBuildArtifact {
    std::string Guid;
    std::string SourcePath;
    std::string ArtifactPath;
    AssetArtifact Data;
};

struct PlayerBuildJob {
    BuildOperation Operation = BuildOperation::Idle;
    BuildPlatform Platform = BuildPlatform::MacOS;
    BuildProfile Profile;
    HarmonyLocalBuildSettings HarmonyLocal;
    std::uint64_t ShaderRevision = 0;
    bool Run = false;
    bool IOSSimulator = false;
    bool Published = false;
    bool Launched = false;
    std::string ProjectRoot;
    std::string CacheRoot;
    std::string Output;
    std::string Template;
    std::string Error;
    std::string RunError;
    std::string RunOutput;
    std::vector<GameSproutRuntime::ResourceReference> Scenes;
    std::vector<PlayerBuildArtifact> Artifacts;
    ScriptCompileRequest Scripts;
    GameSproutRuntime::RuntimeProjectSettings ProjectSettings;
};

eAssetBuildProfile AssetProfile(const PlayerBuildJob&);
const char* PlayerBuildPlatformKey(BuildPlatform);
bool IsMacPlayerBuild(BuildPlatform);
bool IsLocalPlayerBuild(BuildPlatform);
bool ExecutePlayerBuild(PlayerBuildJob&, const IAssetsPipeline&, const IScriptCompiler*,
                        const std::function<bool()>& cancelled);
