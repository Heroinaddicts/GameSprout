#include "SceneWeatherDocument.h"
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace GameSproutRuntime;

namespace {
void Members(const Json::Value& value,const std::string& path,std::initializer_list<std::string_view> allowed) {
    if(!value.isObject())throw std::runtime_error(path+": expected an object");
    for(const auto& name:value.getMemberNames())
        if(std::find(allowed.begin(),allowed.end(),name)==allowed.end())
            throw std::runtime_error(path+"."+name+": unknown field");
}

float Number(const Json::Value& value,const std::string& path) {
    if(!value.isNumeric())throw std::runtime_error(path+": expected a finite number");
    const double number=value.asDouble();
    if(!std::isfinite(number)||std::abs(number)>std::numeric_limits<float>::max())
        throw std::runtime_error(path+": expected a finite number");
    return static_cast<float>(number);
}

void ReadNumber(const Json::Value& value,const std::string& path,const char* key,float& destination) {
    if(value.isMember(key))destination=Number(value[key],path+"."+key);
}

Color ReadColor(const Json::Value& value,const std::string& path) {
    if(!value.isArray()||value.size()!=4)throw std::runtime_error(path+": expected four RGBA values");
    return {Number(value[0],path+"[0]"),Number(value[1],path+"[1]"),
        Number(value[2],path+"[2]"),Number(value[3],path+"[3]")};
}

Json::Value WriteValues(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);
    for(const auto value:values)result.append(value);
    return result;
}

WeatherCloudLayer ReadCloud(const Json::Value& value,const std::string& path,WeatherCloudLayer layer) {
    Members(value,path,{"Coverage","Density","Scale","WindDirectionDegrees","WindSpeed"});
    ReadNumber(value,path,"Coverage",layer.Coverage);
    ReadNumber(value,path,"Density",layer.Density);
    ReadNumber(value,path,"Scale",layer.Scale);
    ReadNumber(value,path,"WindDirectionDegrees",layer.WindDirectionDegrees);
    ReadNumber(value,path,"WindSpeed",layer.WindSpeed);
    return layer;
}

WeatherPreset ReadPreset(const Json::Value& value,const std::string& path) {
    Members(value,path,{"Clouds","StarIntensity","StarDensity","StarRotationDegrees","CloudColor",
        "FogStrength","DaylightMultiplier","AmbientMultiplier"});
    WeatherPreset preset;
    if(value.isMember("Clouds")) {
        const auto& clouds=value["Clouds"];
        if(!clouds.isArray()||clouds.size()!=preset.Clouds.size())
            throw std::runtime_error(path+".Clouds: expected two cloud layers");
        for(Json::ArrayIndex index=0;index<clouds.size();++index)
            preset.Clouds[index]=ReadCloud(clouds[index],path+".Clouds["+std::to_string(index)+"]",preset.Clouds[index]);
    }
    ReadNumber(value,path,"StarIntensity",preset.StarIntensity);
    ReadNumber(value,path,"StarDensity",preset.StarDensity);
    ReadNumber(value,path,"StarRotationDegrees",preset.StarRotationDegrees);
    ReadNumber(value,path,"FogStrength",preset.FogStrength);
    ReadNumber(value,path,"DaylightMultiplier",preset.DaylightMultiplier);
    ReadNumber(value,path,"AmbientMultiplier",preset.AmbientMultiplier);
    if(value.isMember("CloudColor"))preset.CloudColor=ReadColor(value["CloudColor"],path+".CloudColor");
    return preset;
}

WeatherLightning ReadLightning(const Json::Value& value,const std::string& path) {
    Members(value,path,{"Position","Height","Tint","Intensity","DurationSeconds","Radius","Seed"});
    WeatherLightning lightning;
    if(value.isMember("Position")) {
        const auto& position=value["Position"];
        if(!position.isArray()||position.size()!=3)throw std::runtime_error(path+".Position: expected three coordinates");
        lightning.Position={Number(position[0],path+".Position[0]"),Number(position[1],path+".Position[1]"),
            Number(position[2],path+".Position[2]")};
    }
    ReadNumber(value,path,"Height",lightning.Height);
    ReadNumber(value,path,"Intensity",lightning.Intensity);
    ReadNumber(value,path,"DurationSeconds",lightning.DurationSeconds);
    ReadNumber(value,path,"Radius",lightning.Radius);
    if(value.isMember("Tint"))lightning.Tint=ReadColor(value["Tint"],path+".Tint");
    if(value.isMember("Seed")) {
        if(!value["Seed"].isUInt())throw std::runtime_error(path+".Seed: expected an unsigned 32-bit integer");
        lightning.Seed=value["Seed"].asUInt();
    }
    return lightning;
}
}

namespace SceneWeatherDocument {
WeatherSettings Read(const Json::Value& value,const std::string& path) {
    Members(value,path,{"Enabled","TimeOfDayHours","DayLengthSeconds","Preset","Lightning"});
    WeatherSettings settings;
    if(value.isMember("Enabled")) {
        if(!value["Enabled"].isBool())throw std::runtime_error(path+".Enabled: expected a boolean");
        settings.Enabled=value["Enabled"].asBool();
    }
    ReadNumber(value,path,"TimeOfDayHours",settings.TimeOfDayHours);
    ReadNumber(value,path,"DayLengthSeconds",settings.DayLengthSeconds);
    if(value.isMember("Preset"))settings.Preset=ReadPreset(value["Preset"],path+".Preset");
    if(value.isMember("Lightning"))settings.Lightning=ReadLightning(value["Lightning"],path+".Lightning");
    if(!IsValidWeatherSettings(settings))throw std::runtime_error(path+": weather parameters are outside their supported ranges");
    return settings;
}

Json::Value Serialize(const WeatherSettings& settings) {
    if(!IsValidWeatherSettings(settings))throw std::runtime_error("Invalid authored weather settings");
    Json::Value value(Json::objectValue);
    value["Enabled"]=settings.Enabled;
    value["TimeOfDayHours"]=settings.TimeOfDayHours;
    value["DayLengthSeconds"]=settings.DayLengthSeconds;
    auto& preset=value["Preset"];
    preset["Clouds"]=Json::Value(Json::arrayValue);
    for(const auto& layer:settings.Preset.Clouds) {
        Json::Value cloud(Json::objectValue);
        cloud["Coverage"]=layer.Coverage;
        cloud["Density"]=layer.Density;
        cloud["Scale"]=layer.Scale;
        cloud["WindDirectionDegrees"]=layer.WindDirectionDegrees;
        cloud["WindSpeed"]=layer.WindSpeed;
        preset["Clouds"].append(std::move(cloud));
    }
    preset["StarIntensity"]=settings.Preset.StarIntensity;
    preset["StarDensity"]=settings.Preset.StarDensity;
    preset["StarRotationDegrees"]=settings.Preset.StarRotationDegrees;
    const auto& cloudColor=settings.Preset.CloudColor;
    preset["CloudColor"]=WriteValues({cloudColor.R,cloudColor.G,cloudColor.B,cloudColor.A});
    preset["FogStrength"]=settings.Preset.FogStrength;
    preset["DaylightMultiplier"]=settings.Preset.DaylightMultiplier;
    preset["AmbientMultiplier"]=settings.Preset.AmbientMultiplier;
    auto& lightning=value["Lightning"];
    lightning["Position"]=WriteValues({settings.Lightning.Position.X,settings.Lightning.Position.Y,settings.Lightning.Position.Z});
    lightning["Height"]=settings.Lightning.Height;
    const auto& tint=settings.Lightning.Tint;
    lightning["Tint"]=WriteValues({tint.R,tint.G,tint.B,tint.A});
    lightning["Intensity"]=settings.Lightning.Intensity;
    lightning["DurationSeconds"]=settings.Lightning.DurationSeconds;
    lightning["Radius"]=settings.Lightning.Radius;
    lightning["Seed"]=Json::UInt(settings.Lightning.Seed);
    return value;
}
}
