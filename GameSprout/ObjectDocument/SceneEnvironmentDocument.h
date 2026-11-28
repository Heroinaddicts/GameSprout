#pragma once
#include "RuntimeEnvironmentApi.h"
#include <JsonCpp/json.hpp>

namespace SceneEnvironmentDocument {
GameSproutRuntime::EnvironmentSettings Read(const Json::Value&,const std::string& path="Scene.Environment");
Json::Value Serialize(const GameSproutRuntime::EnvironmentSettings&);
GameSproutRuntime::EnvironmentValue ReadValue(const Json::Value&,const std::string& path);
Json::Value SerializeValue(const GameSproutRuntime::EnvironmentValue&);
}
