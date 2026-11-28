#pragma once

#include "RuntimeWeatherApi.h"
#include <JsonCpp/json.hpp>

namespace SceneWeatherDocument {
GameSproutRuntime::WeatherSettings Read(const Json::Value&,const std::string& path="Scene.Weather");
Json::Value Serialize(const GameSproutRuntime::WeatherSettings&);
}
