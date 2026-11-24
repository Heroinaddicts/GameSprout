#include "SceneEnvironmentLightingDocument.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
[[noreturn]] void Fail(const std::string& path,const char* reason){throw std::runtime_error(path+": "+reason);}
float Number(const Json::Value& value,const std::string& path,bool signedValue=false) {
    if(!value.isNumeric()||!std::isfinite(value.asDouble())||
       std::fabs(value.asDouble())>std::numeric_limits<float>::max()||(!signedValue&&value.asDouble()<0))
        Fail(path,signedValue?"expected finite float":"expected nonnegative finite float");
    return value.asFloat();
}
GameSproutRuntime::Color Color(const Json::Value& data,const std::string& path) {
    if(!data.isArray()||data.size()!=4)Fail(path,"expected four linear RGBA values");
    GameSproutRuntime::Color color;
    color.R=Number(data[0],path+"[0]");color.G=Number(data[1],path+"[1]");
    color.B=Number(data[2],path+"[2]");color.A=Number(data[3],path+"[3]");
    if(color.A>1)Fail(path+"[3]","alpha must be between zero and one");
    return color;
}
Json::Value Color(GameSproutRuntime::Color color) {
    Json::Value data(Json::arrayValue);data.append(color.R);data.append(color.G);data.append(color.B);data.append(color.A);return data;
}
GameSproutRuntime::ResourceReference ReadResource(const Json::Value& value,const std::string& path) {
    if(value.isNull())return {};
    if(!value.isObject()||value.size()!=2||!value["Guid"].isString()||!value["LocalID"].isUInt64()||
       value["LocalID"].asUInt64()==0)Fail(path,"expected a resource Guid and positive LocalID");
    std::string guid=value["Guid"].asString();
    if(guid.size()!=32)Fail(path+".Guid","expected 32 hexadecimal digits");
    bool nonzero=false;
    for(char& digit:guid) {
        if(digit>='A'&&digit<='F')digit=static_cast<char>(digit-'A'+'a');
        if(!((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')))Fail(path+".Guid","invalid hexadecimal digit");
        nonzero|=digit!='0';
    }
    if(!nonzero)Fail(path+".Guid","zero GUID is not a resource reference");
    return {std::move(guid),value["LocalID"].asUInt64()};
}
GameSproutRuntime::Vector3 Vector(const Json::Value& value,const std::string& path) {
    if(!value.isArray()||value.size()!=3)Fail(path,"expected three coordinates");
    return {Number(value[0],path+"[0]",true),Number(value[1],path+"[1]",true),Number(value[2],path+"[2]",true)};
}
Json::Value Vector(GameSproutRuntime::Vector3 value) {
    Json::Value result(Json::arrayValue);
    result.append(value.X);result.append(value.Y);result.append(value.Z);
    return result;
}
bool Boolean(const Json::Value& value,const std::string& path) {
    if(!value.isBool())Fail(path,"expected boolean");
    return value.asBool();
}
std::uint32_t Unsigned(const Json::Value& value,const std::string& path) {
    if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isUInt())
        Fail(path,"expected an unsigned 32-bit integer");
    return value.asUInt();
}
std::int32_t Integer(const Json::Value& value,const std::string& path) {
    if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isInt())
        Fail(path,"expected a signed 32-bit integer");
    return value.asInt();
}
Json::Value Resource(const GameSproutRuntime::ResourceReference& reference,const std::string& path) {
    if(reference.Guid.empty())return {};
    Json::Value result(Json::objectValue);
    result["Guid"]=reference.Guid;
    result["LocalID"]=Json::UInt64(reference.LocalID);
    result["Guid"]=ReadResource(result,path).Guid;
    return result;
}
void ReadSpatial(const Json::Value& root,const std::string& path,GameSproutRuntime::EnvironmentLightingSettings& settings) {
    if(root.isMember("PlanarReflection")) {
        const auto& value=root["PlanarReflection"];const auto key=path+".PlanarReflection";
        if(!value.isObject())Fail(key,"expected object");
        auto& planar=settings.PlanarReflection;
        for(const auto& name:value.getMemberNames()) {
            const auto member=key+"."+name;
            if(name=="Enabled")planar.Enabled=Boolean(value[name],member);
            else if(name=="Height")planar.Height=Number(value[name],member,true);
            else if(name=="ResolutionScale")planar.ResolutionScale=Number(value[name],member);
            else if(name=="MaxResolution")planar.MaxResolution=Unsigned(value[name],member);
            else if(name=="Strength")planar.Strength=Number(value[name],member);
            else if(name=="MaxDistance")planar.MaxDistance=Number(value[name],member);
            else if(name=="LayerMask")planar.LayerMask=Unsigned(value[name],member);
            else Fail(member,"unknown field");
        }
        if(!GameSproutRuntime::IsValidPlanarReflection(planar))Fail(key,"invalid planar reflection settings");
    }
    if(root.isMember("ReflectionProbes")) {
        const auto& values=root["ReflectionProbes"];const auto key=path+".ReflectionProbes";
        if(!values.isArray())Fail(key,"expected array");
        for(Json::ArrayIndex i=0;i<values.size();++i) {
            const auto& value=values[i];const auto item=key+"["+std::to_string(i)+"]";
            if(!value.isObject())Fail(item,"expected object");
            auto& probe=settings.ReflectionProbes.emplace_back();
            for(const auto& name:value.getMemberNames()) {
                const auto member=item+"."+name;
                if(name=="Enabled")probe.Enabled=Boolean(value[name],member);
                else if(name=="BoxProjection")probe.BoxProjection=Boolean(value[name],member);
                else if(name=="Environment")probe.Environment=ReadResource(value[name],member);
                else if(name=="CapturePosition")probe.CapturePosition=Vector(value[name],member);
                else if(name=="Center")probe.Center=Vector(value[name],member);
                else if(name=="Size")probe.Size=Vector(value[name],member);
                else if(name=="BlendDistance")probe.BlendDistance=Number(value[name],member);
                else if(name=="Intensity")probe.Intensity=Number(value[name],member);
                else if(name=="Priority")probe.Priority=Integer(value[name],member);
                else if(name=="CullingMask")probe.CullingMask=Unsigned(value[name],member);
                else Fail(member,"unknown field");
            }
            if(!GameSproutRuntime::IsValidReflectionProbe(probe))Fail(item,"invalid reflection probe settings");
        }
    }
    if(root.isMember("IndirectLightVolumes")) {
        const auto& values=root["IndirectLightVolumes"];const auto key=path+".IndirectLightVolumes";
        if(!values.isArray()||values.size()>GameSproutRuntime::MaxBakedIndirectVolumes)
            Fail(key,"expected an array of at most eight irradiance volumes");
        for(Json::ArrayIndex i=0;i<values.size();++i) {
            const auto& value=values[i];const auto item=key+"["+std::to_string(i)+"]";
            if(!value.isObject())Fail(item,"expected object");
            auto& volume=settings.IndirectLightVolumes.emplace_back();
            for(const auto& name:value.getMemberNames()) {
                const auto member=item+"."+name;
                if(name=="Asset")volume.Asset=ReadResource(value[name],member);
                else if(name=="Center")volume.Center=Vector(value[name],member);
                else if(name=="Size")volume.Size=Vector(value[name],member);
                else if(name=="BlendDistance")volume.BlendDistance=Number(value[name],member);
                else if(name=="Intensity")volume.Intensity=Number(value[name],member);
                else if(name=="Priority")volume.Priority=Unsigned(value[name],member);
                else Fail(member,"unknown field");
            }
            if(!GameSproutRuntime::IsValidBakedIndirectVolume(volume))Fail(item,"invalid irradiance volume settings");
        }
    }
}
void SerializeSpatial(const GameSproutRuntime::EnvironmentLightingSettings& settings,Json::Value& result) {
    const auto& plan=settings.PlanarReflection;
    if(plan!=GameSproutRuntime::PlanarReflectionSettings{}) {
        auto& value=result["PlanarReflection"];
        value["Enabled"]=plan.Enabled;value["Height"]=plan.Height;value["Strength"]=plan.Strength;
        value["ResolutionScale"]=plan.ResolutionScale;value["MaxResolution"]=plan.MaxResolution;
        value["MaxDistance"]=plan.MaxDistance;value["LayerMask"]=plan.LayerMask;
    }
    for(const auto& probe:settings.ReflectionProbes) {
        auto& value=result["ReflectionProbes"].append(Json::Value(Json::objectValue));
        value["Enabled"]=probe.Enabled;value["BoxProjection"]=probe.BoxProjection;
        value["Environment"]=Resource(probe.Environment,"Scene.EnvironmentLighting.ReflectionProbes.Environment");
        value["CapturePosition"]=Vector(probe.CapturePosition);value["Center"]=Vector(probe.Center);value["Size"]=Vector(probe.Size);
        value["BlendDistance"]=probe.BlendDistance;value["Intensity"]=probe.Intensity;
        value["Priority"]=probe.Priority;value["CullingMask"]=probe.CullingMask;
    }
    for(const auto& volume:settings.IndirectLightVolumes) {
        auto& value=result["IndirectLightVolumes"].append(Json::Value(Json::objectValue));
        value["Asset"]=Resource(volume.Asset,"Scene.EnvironmentLighting.IndirectLightVolumes.Asset");
        value["Center"]=Vector(volume.Center);value["Size"]=Vector(volume.Size);
        value["BlendDistance"]=volume.BlendDistance;value["Intensity"]=volume.Intensity;value["Priority"]=volume.Priority;
    }
}
}
namespace SceneEnvironmentLightingDocument {
GameSproutRuntime::EnvironmentLightingSettings Read(const Json::Value& data,const std::string& path) {
    if(!data.isObject())Fail(path,"expected object");
    for(const auto& name:data.getMemberNames())
        if(name!="Mode"&&name!="AmbientColor"&&name!="SkyColor"&&name!="EquatorColor"&&name!="GroundColor"&&name!="Intensity"&&
           name!="SHCoefficients"&&name!="Atmosphere"&&name!="SkyEnvironment"&&name!="SkyRotationDegrees"&&name!="SkyIntensity"&&
           name!="ReflectionIntensity"&&name!="HDREnabled"&&name!="ExposureEV"&&name!="Tonemap"&&name!="SpotShadows"&&
           name!="PlanarReflection"&&name!="ReflectionProbes"&&name!="IndirectLightVolumes")
            Fail(path+"."+name,"unknown field");
    GameSproutRuntime::EnvironmentLightingSettings result;
    if(data.isMember("Mode")) {
        if(!data["Mode"].isString())Fail(path+".Mode","expected ambient mode string");
        const auto mode=data["Mode"].asString();
        if(mode=="None")result.Mode=GameSproutRuntime::AmbientLightMode::None;
        else if(mode=="Color")result.Mode=GameSproutRuntime::AmbientLightMode::Color;
        else if(mode=="Gradient")result.Mode=GameSproutRuntime::AmbientLightMode::Gradient;
        else if(mode=="SphericalHarmonics")result.Mode=GameSproutRuntime::AmbientLightMode::SphericalHarmonics;
        else if(mode=="SkyEnvironment")result.Mode=GameSproutRuntime::AmbientLightMode::SkyEnvironment;
        else Fail(path+".Mode","unknown ambient mode");
    }
    for(const auto& entry:{std::pair{"AmbientColor",&result.AmbientColor},std::pair{"SkyColor",&result.SkyColor},
        std::pair{"EquatorColor",&result.EquatorColor},std::pair{"GroundColor",&result.GroundColor}})
        if(data.isMember(entry.first))*entry.second=Color(data[entry.first],path+"."+entry.first);
    if(data.isMember("Intensity"))result.Intensity=Number(data["Intensity"],path+".Intensity");
    if(data.isMember("SHCoefficients")) {
        const auto& coefficients=data["SHCoefficients"];
        if(!coefficients.isArray()||coefficients.size()!=9)Fail(path+".SHCoefficients","expected nine RGB coefficients");
        for(Json::ArrayIndex index=0;index<9;++index) {
            const auto& coefficient=coefficients[index];const auto key=path+".SHCoefficients["+std::to_string(index)+"]";
            if(!coefficient.isArray()||coefficient.size()!=3)Fail(key,"expected three signed RGB values");
            result.SHCoefficients[index]={Number(coefficient[0],key+"[0]",true),Number(coefficient[1],key+"[1]",true),Number(coefficient[2],key+"[2]",true)};
        }
    }
    if(data.isMember("Atmosphere")) {
        const auto& atmosphere=data["Atmosphere"];const auto key=path+".Atmosphere";
        if(!atmosphere.isObject())Fail(key,"expected object");
        for(const auto& name:atmosphere.getMemberNames())
            if(name!="Enabled"&&name!="DrawSky"&&name!="ZenithColor"&&name!="HorizonColor"&&name!="FogColor"&&
               name!="FogStart"&&name!="FogEnd"&&name!="FogMaxOpacity")Fail(key+"."+name,"unknown field");
        auto& settings=result.Atmosphere;
        for(const auto& entry:{std::pair{"Enabled",&settings.Enabled},std::pair{"DrawSky",&settings.DrawSky}})
            if(atmosphere.isMember(entry.first)) {
                if(!atmosphere[entry.first].isBool())Fail(key+"."+entry.first,"expected boolean");
                *entry.second=atmosphere[entry.first].asBool();
            }
        for(const auto& entry:{std::pair{"ZenithColor",&settings.ZenithColor},std::pair{"HorizonColor",&settings.HorizonColor},
            std::pair{"FogColor",&settings.FogColor}})
            if(atmosphere.isMember(entry.first))*entry.second=Color(atmosphere[entry.first],key+"."+entry.first);
        for(const auto& entry:{std::pair{"FogStart",&settings.FogStart},std::pair{"FogEnd",&settings.FogEnd},
            std::pair{"FogMaxOpacity",&settings.FogMaxOpacity}})
            if(atmosphere.isMember(entry.first))*entry.second=Number(atmosphere[entry.first],key+"."+entry.first);
        if(settings.FogEnd<=settings.FogStart)Fail(key+".FogEnd","must be greater than FogStart");
        if(settings.FogMaxOpacity>1)Fail(key+".FogMaxOpacity","must be between zero and one");
    }
    if(data.isMember("SkyEnvironment"))
        result.SkyEnvironment=ReadResource(data["SkyEnvironment"],path+".SkyEnvironment");
    if(data.isMember("SkyRotationDegrees"))
        result.SkyRotationDegrees=Number(data["SkyRotationDegrees"],path+".SkyRotationDegrees",true);
    if(data.isMember("SkyIntensity"))result.SkyIntensity=Number(data["SkyIntensity"],path+".SkyIntensity");
    if(data.isMember("ReflectionIntensity"))
        result.ReflectionIntensity=Number(data["ReflectionIntensity"],path+".ReflectionIntensity");
    if(data.isMember("HDREnabled")) {
        if(!data["HDREnabled"].isBool())Fail(path+".HDREnabled","expected boolean");
        result.HDREnabled=data["HDREnabled"].asBool();
    }
    if(data.isMember("ExposureEV")) {
        result.ExposureEV=Number(data["ExposureEV"],path+".ExposureEV",true);
        if(result.ExposureEV< -16||result.ExposureEV>16)Fail(path+".ExposureEV","must be between -16 and 16");
    }
    if(data.isMember("Tonemap")) {
        const auto& tone=data["Tonemap"];
        if(tone=="None")result.Tonemap=GameSproutRuntime::ToneMapping::None;
        else if(tone=="Neutral")result.Tonemap=GameSproutRuntime::ToneMapping::Neutral;
        else if(tone=="ACES")result.Tonemap=GameSproutRuntime::ToneMapping::ACES;
        else Fail(path+".Tonemap","expected None, Neutral or ACES");
    }
    if(data.isMember("SpotShadows")) {
        const auto& shadows=data["SpotShadows"];const auto key=path+".SpotShadows";
        if(!shadows.isObject())Fail(key,"expected object");
        for(const auto& name:shadows.getMemberNames()) {
            const auto& value=shadows[name];
            if(name=="MaxLights") {
                if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isUInt()||value.asUInt()>4)
                    Fail(key+".MaxLights","expected integer between 0 and 4");
                result.SpotShadows.MaxLights=value.asUInt();
            }else if(name=="MaxResolution") {
                if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isUInt()||
                    !GameSproutRuntime::IsValidLightShadowResolution(value.asUInt()))
                    Fail(key+".MaxResolution","expected a power of two between 256 and 4096");
                result.SpotShadows.MaxResolution=value.asUInt();
            }else Fail(key+"."+name,"unknown field");
        }
    }
    ReadSpatial(data,path,result);
    return result;
}
Json::Value Serialize(const GameSproutRuntime::EnvironmentLightingSettings& settings) {
    if(!GameSproutRuntime::IsValidEnvironmentLighting(settings))Fail("Scene.EnvironmentLighting","invalid Runtime environment lighting");
    Json::Value result(Json::objectValue);
    switch(settings.Mode) {
    case GameSproutRuntime::AmbientLightMode::None:result["Mode"]="None";break;
    case GameSproutRuntime::AmbientLightMode::Color:result["Mode"]="Color";break;
    case GameSproutRuntime::AmbientLightMode::Gradient:result["Mode"]="Gradient";break;
    case GameSproutRuntime::AmbientLightMode::SphericalHarmonics:result["Mode"]="SphericalHarmonics";break;
    case GameSproutRuntime::AmbientLightMode::SkyEnvironment:result["Mode"]="SkyEnvironment";break;
    }
    result["AmbientColor"]=Color(settings.AmbientColor);result["SkyColor"]=Color(settings.SkyColor);
    result["EquatorColor"]=Color(settings.EquatorColor);result["GroundColor"]=Color(settings.GroundColor);
    result["Intensity"]=settings.Intensity;
    result["SHCoefficients"]=Json::Value(Json::arrayValue);
    for(const auto& coefficient:settings.SHCoefficients) {
        Json::Value value(Json::arrayValue);value.append(coefficient.X);value.append(coefficient.Y);value.append(coefficient.Z);
        result["SHCoefficients"].append(std::move(value));
    }
    const auto& atmosphere=settings.Atmosphere;auto& data=result["Atmosphere"];
    data["Enabled"]=atmosphere.Enabled;data["DrawSky"]=atmosphere.DrawSky;
    data["ZenithColor"]=Color(atmosphere.ZenithColor);data["HorizonColor"]=Color(atmosphere.HorizonColor);
    data["FogColor"]=Color(atmosphere.FogColor);data["FogStart"]=atmosphere.FogStart;
    data["FogEnd"]=atmosphere.FogEnd;data["FogMaxOpacity"]=atmosphere.FogMaxOpacity;
    if(!settings.SkyEnvironment.Guid.empty()) {
        auto& sky=result["SkyEnvironment"];
        sky["Guid"]=settings.SkyEnvironment.Guid;
        sky["LocalID"]=Json::UInt64(settings.SkyEnvironment.LocalID);
        sky["Guid"]=ReadResource(sky,"Scene.EnvironmentLighting.SkyEnvironment").Guid;
    }
    result["SkyRotationDegrees"]=settings.SkyRotationDegrees;
    result["SkyIntensity"]=settings.SkyIntensity;
    result["ReflectionIntensity"]=settings.ReflectionIntensity;
    result["HDREnabled"]=settings.HDREnabled;
    result["ExposureEV"]=settings.ExposureEV;
    switch(settings.Tonemap) {
    case GameSproutRuntime::ToneMapping::None:result["Tonemap"]="None";break;
    case GameSproutRuntime::ToneMapping::Neutral:result["Tonemap"]="Neutral";break;
    case GameSproutRuntime::ToneMapping::ACES:result["Tonemap"]="ACES";break;
    }
    if(settings.SpotShadows!=GameSproutRuntime::SpotShadowBudgetSettings{}) {
        auto& shadows=result["SpotShadows"];
        shadows["MaxLights"]=Json::UInt(settings.SpotShadows.MaxLights);
        shadows["MaxResolution"]=Json::UInt(settings.SpotShadows.MaxResolution);
    }
    SerializeSpatial(settings,result);
    return result;
}
}
