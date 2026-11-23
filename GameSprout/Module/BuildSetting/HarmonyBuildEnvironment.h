#pragma once

#include "IBuildSetting.h"
#include <functional>

struct HarmonyBuildEnvironment {
    HarmonyBuildStatus Status;
    std::string ToolsPath;
    std::string SigningConfig;
    std::string DeviceId;
};

HarmonyBuildEnvironment InspectHarmonyBuildEnvironment(const HarmonyBuildSettings&,
                                                       const HarmonyLocalBuildSettings&,
                                                       const std::function<bool()>& cancelled);
