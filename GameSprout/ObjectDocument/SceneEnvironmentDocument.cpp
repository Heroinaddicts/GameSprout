#include "SceneEnvironmentDocument.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

using namespace GameSproutRuntime;

namespace {
[[noreturn]] void Fail(const std::string& path,std::string_view error) {
    throw std::runtime_error(path+": "+std::string(error));
}

void Members(const Json::Value& value,const std::string& path,std::initializer_list<std::string_view> fields) {
    if(!value.isObject())Fail(path,"expected an object");
    for(const auto& name:value.getMemberNames())
        if(std::find(fields.begin(),fields.end(),name)==fields.end())Fail(path+"."+name,"unknown field");
}

float Number(const Json::Value& value,const std::string& path) {
    if(!value.isNumeric())Fail(path,"expected a finite number");
    const double number=value.asDouble();
    if(!std::isfinite(number)||std::abs(number)>std::numeric_limits<float>::max())
        Fail(path,"number is outside the float range");
    return static_cast<float>(number);
}

bool Integral(const Json::Value& value) {
    return value.type()==Json::intValue||value.type()==Json::uintValue;
}

std::string String(const Json::Value& value,const std::string& path,bool allowEmpty=false) {
    if(!value.isString())Fail(path,"expected a string");
    auto string=value.asString();
    if(!allowEmpty&&string.empty())Fail(path,"expected a nonempty string");
    return string;
}

std::string Target(const Json::Value& value,const std::string& path) {
    return value.isMember("Target")?String(value["Target"],path+".Target",true):std::string{};
}

Vector3 Vector(const Json::Value& value,const std::string& path) {
    if(!value.isArray()||value.size()!=3)Fail(path,"expected three coordinates");
    return {Number(value[0],path+"[0]"),Number(value[1],path+"[1]"),Number(value[2],path+"[2]")};
}

Json::Value Values(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);
    for(const auto value:values)result.append(value);
    return result;
}

Json::Value Vector(Vector3 value) {
    return Values({value.X,value.Y,value.Z});
}

template<class T,class Read>
void ReadArray(const Json::Value& owner,const char* field,const std::string& path,std::vector<T>& result,Read read) {
    if(!owner.isMember(field))return;
    const auto& values=owner[field];
    const auto member=path+"."+field;
    if(!values.isArray())Fail(member,"expected an array");
    result.reserve(values.size());
    for(Json::ArrayIndex i=0;i<values.size();++i)
        result.push_back(read(values[i],member+"["+std::to_string(i)+"]"));
}

void ReadBlend(const Json::Value& value,const std::string& path,int& priority,float& weight) {
    if(value.isMember("Priority")) {
        if(!Integral(value["Priority"])||!value["Priority"].isInt())Fail(path+".Priority","expected an integer");
        priority=value["Priority"].asInt();
    }
    if(value.isMember("Weight"))weight=Number(value["Weight"],path+".Weight");
}

EnvironmentOverride ReadOverride(const Json::Value& value,const std::string& path) {
    Members(value,path,{"Target","Parameter","Value"});
    return {Target(value,path),String(value["Parameter"],path+".Parameter"),
        SceneEnvironmentDocument::ReadValue(value["Value"],path+".Value")};
}

EnvironmentLayer ReadLayer(const Json::Value& value,const std::string& path) {
    Members(value,path,{"Id","Priority","Weight","Region","Overrides"});
    EnvironmentLayer result;
    result.Id=String(value["Id"],path+".Id");
    ReadBlend(value,path,result.Priority,result.Weight);
    if(value.isMember("Region")) {
        const auto& region=value["Region"];
        Members(region,path+".Region",{"Center","HalfSize","BlendDistance"});
        result.Region=EnvironmentRegion{Vector(region["Center"],path+".Region.Center"),
            Vector(region["HalfSize"],path+".Region.HalfSize"),
            region.isMember("BlendDistance")?Number(region["BlendDistance"],path+".Region.BlendDistance"):0};
    }
    ReadArray(value,"Overrides",path,result.Overrides,ReadOverride);
    return result;
}

EnvironmentCurve ReadCurve(const Json::Value& value,const std::string& path) {
    Members(value,path,{"Id","Target","Parameter","Priority","Weight","Loop","Keys"});
    EnvironmentCurve result;
    result.Id=String(value["Id"],path+".Id");
    result.Target=Target(value,path);
    result.Parameter=String(value["Parameter"],path+".Parameter");
    ReadBlend(value,path,result.Priority,result.Weight);
    if(value.isMember("Loop")) {
        if(!value["Loop"].isBool())Fail(path+".Loop","expected a boolean");
        result.Loop=value["Loop"].asBool();
    }
    ReadArray(value,"Keys",path,result.Keys,[](const Json::Value& key,const std::string& keyPath) {
        Members(key,keyPath,{"Seconds","Value"});
        return EnvironmentKeyframe{Number(key["Seconds"],keyPath+".Seconds"),
            SceneEnvironmentDocument::ReadValue(key["Value"],keyPath+".Value")};
    });
    return result;
}
}

namespace SceneEnvironmentDocument {
EnvironmentValue ReadValue(const Json::Value& value,const std::string& path) {
    if(value.isBool())return value.asBool();
    if(value.isNumeric())return Number(value,path);
    if(value.isArray()) {
        if(value.size()==3)return Vector(value,path);
        if(value.size()==4)return Color{Number(value[0],path+"[0]"),Number(value[1],path+"[1]"),
            Number(value[2],path+"[2]"),Number(value[3],path+"[3]")};
        Fail(path,"expected a three-component vector or four-component RGBA color");
    }
    if(value.isObject()) {
        Members(value,path,{"Guid","LocalID"});
        const auto guid=String(value["Guid"],path+".Guid",true);
        if(!Integral(value["LocalID"])||!value["LocalID"].isUInt64())
            Fail(path+".LocalID","expected an unsigned integer");
        const auto localID=value["LocalID"].asUInt64();
        if(guid.empty()&&localID==0)return ResourceReference{};
        if(guid.size()!=32||!std::all_of(guid.begin(),guid.end(),[](unsigned char c){return std::isxdigit(c);})||
            guid.find_first_not_of('0')==std::string::npos||!localID)
            Fail(path,"expected a nonzero 32-digit resource GUID and a nonzero LocalID");
        return ResourceReference{guid,localID};
    }
    Fail(path,"expected a parameter value");
}

Json::Value SerializeValue(const EnvironmentValue& value) {
    return std::visit([](const auto& entry)->Json::Value {
        using T=std::decay_t<decltype(entry)>;
        if constexpr(std::is_same_v<T,Color>)return Values({entry.R,entry.G,entry.B,entry.A});
        else if constexpr(std::is_same_v<T,Vector3>)return Vector(entry);
        else if constexpr(std::is_same_v<T,ResourceReference>) {
            Json::Value result(Json::objectValue);
            result["Guid"]=entry.Guid;
            result["LocalID"]=Json::UInt64(entry.LocalID);
            return result;
        } else return entry;
    },value);
}

EnvironmentSettings Read(const Json::Value& value,const std::string& path) {
    Members(value,path,{"Version","Effects","Layers","Curves"});
    if(!Integral(value["Version"])||!value["Version"].isInt()||value["Version"].asInt()!=1)
        Fail(path+".Version","unsupported environment configuration version");
    EnvironmentSettings settings;
    ReadArray(value,"Effects",path,settings.Effects,[](const Json::Value& effect,const std::string& effectPath) {
        Members(effect,effectPath,{"Id","Type"});
        return EnvironmentEffectInstance{String(effect["Id"],effectPath+".Id"),String(effect["Type"],effectPath+".Type")};
    });
    ReadArray(value,"Layers",path,settings.Layers,ReadLayer);
    ReadArray(value,"Curves",path,settings.Curves,ReadCurve);
    return settings;
}

Json::Value Serialize(const EnvironmentSettings& settings) {
    Json::Value value(Json::objectValue);
    value["Version"]=1;
    for(const auto& effect:settings.Effects) {
        Json::Value item(Json::objectValue);
        item["Id"]=effect.Id;
        item["Type"]=effect.Type;
        value["Effects"].append(std::move(item));
    }
    for(const auto& layer:settings.Layers) {
        Json::Value item(Json::objectValue);
        item["Id"]=layer.Id;
        item["Priority"]=layer.Priority;
        item["Weight"]=layer.Weight;
        if(const auto& region=layer.Region) {
            item["Region"]["Center"]=Vector(region->Center);
            item["Region"]["HalfSize"]=Vector(region->HalfSize);
            item["Region"]["BlendDistance"]=region->BlendDistance;
        }
        for(const auto& entry:layer.Overrides) {
            Json::Value overrideValue(Json::objectValue);
            if(!entry.Target.empty())overrideValue["Target"]=entry.Target;
            overrideValue["Parameter"]=entry.Parameter;
            overrideValue["Value"]=SerializeValue(entry.Value);
            item["Overrides"].append(std::move(overrideValue));
        }
        value["Layers"].append(std::move(item));
    }
    for(const auto& curve:settings.Curves) {
        Json::Value item(Json::objectValue);
        item["Id"]=curve.Id;
        if(!curve.Target.empty())item["Target"]=curve.Target;
        item["Parameter"]=curve.Parameter;
        item["Loop"]=curve.Loop;
        item["Priority"]=curve.Priority;
        item["Weight"]=curve.Weight;
        for(const auto& key:curve.Keys) {
            Json::Value keyframe(Json::objectValue);
            keyframe["Seconds"]=key.Seconds;
            keyframe["Value"]=SerializeValue(key.Value);
            item["Keys"].append(std::move(keyframe));
        }
        value["Curves"].append(std::move(item));
    }
    return value;
}
}
