#pragma once
#include "PreparedResourceFormat.h"
#include <cctype>
#include <cmath>
#include <limits>
#include <set>

// Authoring metadata is deliberately separate from GPU binding reflection.
// Values live in the .mat document's root; the schema lives with its Shader.
namespace MaterialDefinition {
inline constexpr std::int64_t MaxDocumentBytes=16*1024*1024;
inline Json::Value Vector(double x, double y, double z, double w) {
    Json::Value value(Json::arrayValue);
    for (double component : {x,y,z,w}) value.append(component);
    return value;
}
inline bool Identifier(const std::string & name) {
    if (name.empty() || name.size()>128) return false;
    const auto alpha=[](unsigned char c) { return (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_'; };
    if (!alpha(static_cast<unsigned char>(name.front()))) return false;
    for (unsigned char c : name) if (!alpha(c) && !(c>='0' && c<='9')) return false;
    return true;
}
inline bool FiniteNumber(const Json::Value & value) {
    return value.isNumeric() && std::isfinite(value.asDouble())
        && std::abs(value.asDouble())<=static_cast<double>(std::numeric_limits<float>::max());
}
inline bool VectorValue(const Json::Value & value) {
    if (!value.isArray() || value.size()!=4) return false;
    for (const auto & component : value) if (!FiniteNumber(component)) return false;
    return true;
}
inline bool Reference(const Json::Value & value) {
    return value.isObject() && value["Guid"].isString() && PreparedResources::ValidGuid(value["Guid"].asString())
        && value["LocalID"].isUInt64() && value["LocalID"].asUInt64()!=0;
}
inline bool ShaderReference(const Json::Value & document, std::string & error) {
    error.clear();
    if (!document.isObject()) { error="Material must be an object"; return false; }
    if (!document.isMember("Shader") || (document["Shader"].isString() && (document["Shader"].asString()=="GameSprout/Preview" || document["Shader"].asString()=="GameSprout/Lit"))
        || Reference(document["Shader"])) return true;
    error="Shader must be GameSprout/Preview, GameSprout/Lit or a valid Shader GUID + LocalID reference";
    return false;
}
inline bool PropertyValue(const Json::Value & property, const Json::Value & value) {
    const std::string type=property["Type"].asString();
    if (type=="Color" || type=="Vector") {
        if (!VectorValue(value)) return false;
        if (type=="Color" && property.get("ColorSpace","")=="Linear") {
            for (Json::ArrayIndex index=0;index<3;++index)
                if (value[index].asDouble()<0 || (!property.get("HDR",false).asBool() && value[index].asDouble()>1)) return false;
            if (value[3].asDouble()<0 || value[3].asDouble()>1) return false;
        }
        return true;
    }
    if (type=="Texture") return value.isNull() || Reference(value);
    if (type=="Toggle") return value.isBool();
    if (type=="Int") return value.isInt();
    if (type=="Enum") return value.isUInt() && value.asUInt()<property["Options"].size();
    if (!FiniteNumber(value)) return false;
    if (type=="Range") return value.asDouble()>=property["Min"].asDouble() && value.asDouble()<=property["Max"].asDouble();
    return type=="Float" && (!property.isMember("Min") || value.asDouble()>=property["Min"].asDouble());
}
inline bool ValidateSchema(const Json::Value & schema, std::string & error) {
    error.clear();
    if (!schema.isObject() || !schema["Name"].isString() || schema["Name"].asString().empty()
        || !schema["Properties"].isArray() || schema["Properties"].size()>512) {
        error="Material schema requires Name and a Properties array (at most 512 entries)"; return false;
    }
    const std::set<std::string> types{"Float","Int","Range","Color","Vector","Texture","Toggle","Enum"};
    const std::set<std::string> reserved{"Version","Name","Shader","ExternalMaterial","Textures","UnresolvedTextures",
        "EnableGPUInstancing","SpecularHighlights","EnvironmentReflections","SortingPriority"};
    std::set<std::string> names;
    std::set<std::string> textureTransforms;
    for (const auto & property : schema["Properties"]) {
        if (!property.isObject() || !property["Name"].isString() || !Identifier(property["Name"].asString())
            || reserved.count(property["Name"].asString()) || !names.insert(property["Name"].asString()).second
            || !property["Type"].isString() || !types.count(property["Type"].asString())
            || !property["DisplayName"].isString() || property["DisplayName"].asString().empty()
            || !property.isMember("Default")) {
            error="Material property requires a unique identifier, DisplayName, supported Type and Default"; return false;
        }
        const std::string name=property["Name"].asString();
        const std::string type=property["Type"].asString();
        for (const char * key : {"Group","Tooltip"}) if (property.isMember(key) && !property[key].isString()) {
            error="Material property "+name+": "+key+" must be text"; return false;
        }
        if (property.isMember("ColorSpace") && (type!="Color" || !property["ColorSpace"].isString()
            || (property["ColorSpace"]!="Linear" && property["ColorSpace"]!="SRGB"))) {
            error="Material color ColorSpace must be Linear or SRGB"; return false;
        }
        for (const char * key : {"Hidden","HDR"}) if (property.isMember(key) && !property[key].isBool()) {
            error="Material property "+name+": "+key+" must be boolean"; return false;
        }
        if (type=="Range" && (!FiniteNumber(property["Min"]) || !FiniteNumber(property["Max"])
            || property["Min"].asDouble()>property["Max"].asDouble())) {
            error="Material Range "+name+" requires finite Min <= Max"; return false;
        }
        if(type=="Float" && property.isMember("Min") && !FiniteNumber(property["Min"])) {error="Material Float Min must be finite";return false;}
        if (type=="Enum") {
            if (!property["Options"].isArray() || property["Options"].empty() || property["Options"].size()>256) {
                error="Material Enum "+name+" requires a nonempty Options array"; return false;
            }
            std::set<std::string> options;
            for (const auto & option : property["Options"]) if (!option.isString() || option.asString().empty()
                || !options.insert(option.asString()).second) {
                error="Material Enum "+name+" options must be unique text labels"; return false;
            }
        }
        if (!PropertyValue(property,property["Default"])) {
            error="Invalid default value for material property "+name; return false;
        }
        if (type=="Texture") textureTransforms.insert(name+"ST");
    }
    for (const auto & name : textureTransforms) if (names.count(name)) {
        error="Material texture transform "+name+" is implicit and cannot also be declared as a property"; return false;
    }
    return true;
}
inline Json::Value PreviewSchema() {
    Json::Value schema(Json::objectValue);
    schema["Name"]="GameSprout/Preview";
    schema["Properties"]=Json::Value(Json::arrayValue);
    const auto add=[&](const char * name,const char * display,const char * type,const Json::Value & value) {
        Json::Value property;
        property["Name"]=name; property["DisplayName"]=display; property["Type"]=type; property["Default"]=value;
        if (std::string(type)=="Range") { property["Min"]=0.0; property["Max"]=1.0; }
        schema["Properties"].append(property);
    };
    add("BaseColor","基础颜色","Color",Vector(1,1,1,1));
    add("BaseColorTexture","基础贴图","Texture",Json::Value());
    add("Metallic","金属度","Range",0.0);
    add("Roughness","粗糙度","Range",0.5);
    add("DoubleSided","双面渲染","Toggle",false);
    return schema;
}

// Built-in Lit stores Color values in linear RGB, matching Runtime Color.
// The picker converts display sRGB; texture import settings control texture decoding.
inline Json::Value LitSchema() {
    Json::Value schema(Json::objectValue);schema["Name"]="GameSprout/Lit";
    schema["Properties"]=Json::Value(Json::arrayValue);
    const auto add=[&](const char* name,const char* display,const char* type,const Json::Value& value,const char* group) {
        Json::Value property;property["Name"]=name;property["DisplayName"]=display;
        property["Type"]=type;property["Default"]=value;property["Group"]=group;
        if(std::string(type)=="Range"){property["Min"]=0.0;property["Max"]=1.0;}
        if(std::string(type)=="Color")property["ColorSpace"]="Linear";
        schema["Properties"].append(property);
    };
    add("BaseColor","基础颜色","Color",Vector(1,1,1,1),"表面");
    add("BaseColorTexture","基础贴图","Texture",Json::Value(),"表面");
    add("Metallic","金属度","Range",0.0,"表面");
    add("Roughness","粗糙度","Range",0.5,"表面");
    add("NormalTexture","法线贴图","Texture",Json::Value(),"法线");
    schema["Properties"][4]["Tooltip"]="法线贴图须以 Linear 颜色空间导入，不应进行 sRGB 解码。";
    add("NormalScale","法线强度","Float",1.0,"法线");
    schema["Properties"][5]["Min"]=0.0;
    add("Emission","自发光","Color",Vector(0,0,0,1),"自发光");
    schema["Properties"][6]["HDR"]=true;
    add("DoubleSided","双面渲染","Toggle",false,"渲染");
    add("Stylization","风格化强度","Range",0.0,"风格化");
    schema["Properties"][8]["Tooltip"]="0 保留物理光照；1 使用柔和明暗分区和彩色阴影。";
    add("ShadowTint","阴影色调","Color",Vector(.65,.75,1,1),"风格化");
    add("LightThreshold","明暗分界","Range",.35,"风格化");
    add("LightSoftness","明暗过渡","Range",.25,"风格化");
    add("ColorSaturation","颜色饱和度","Float",1.0,"风格化");
    schema["Properties"][12]["Min"]=0.0;
    return schema;
}
inline Json::Value BuiltinSchema(const Json::Value& document) {
    return document["Shader"]=="GameSprout/Lit"?LitSchema():PreviewSchema();
}

inline bool ValidateValues(const Json::Value & schema, const Json::Value & document, std::string & error) {
    if (!ValidateSchema(schema,error) || !ShaderReference(document,error)) return false;
    // Engine rendering options are independent of Shader author properties and
    // reflection. A user Shader cannot claim their names as material uniforms.
    for (const char* name : {"EnableGPUInstancing","SpecularHighlights","EnvironmentReflections"}) {
        if (document.isMember(name) && !document[name].isBool()) {
            error=std::string("Material ")+name+" must be boolean";return false;
        }
    }
    if (document.isMember("SortingPriority") && (!document["SortingPriority"].isInt()
        || document["SortingPriority"].asInt()<-50 || document["SortingPriority"].asInt()>50)) {
        error="Material SortingPriority must be an integer in [-50,50]";return false;
    }
    for (const auto & property : schema["Properties"]) {
        const std::string name=property["Name"].asString();
        if (document.isMember(name) && !PropertyValue(property,document[name])) {
            error="Invalid material value for "+name; return false;
        }
        if (property["Type"]=="Texture" && document.isMember(name+"ST") && !VectorValue(document[name+"ST"])) {
            error="Texture transform "+name+"ST must contain four finite numbers"; return false;
        }
    }
    return true;
}
inline bool NormalizeValues(const Json::Value & schema, Json::Value & document, std::string & error) {
    if (!ValidateValues(schema,document,error)) return false;
    Json::Value normalized=document;
    if (!normalized.isMember("Shader")) normalized["Shader"]="GameSprout/Preview";
    if (!normalized.isMember("EnableGPUInstancing")) normalized["EnableGPUInstancing"]=true;
    if (!normalized.isMember("SortingPriority")) normalized["SortingPriority"]=0;
    if (normalized["Shader"]=="GameSprout/Lit") {
        for (const char* name : {"SpecularHighlights","EnvironmentReflections"})
            if (!normalized.isMember(name)) normalized[name]=true;
    }
    for (const auto & property : schema["Properties"]) {
        const std::string name=property["Name"].asString();
        if (!normalized.isMember(name)) normalized[name]=property["Default"];
        if (property["Type"]=="Texture" && !normalized.isMember(name+"ST")) normalized[name+"ST"]=Vector(1,1,0,0);
    }
    document=std::move(normalized);
    return true;
}

}
