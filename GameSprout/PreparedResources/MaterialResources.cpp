#include "PreparedResourceDecode.h"
#include "PreparedMaterialDefinition.h"
using PreparedResourceDecode::Unsigned;
using PreparedResourceDecode::FloatAt;
using PreparedResourceDecode::ColorValue;
using PreparedResourceDecode::ReferenceValue;
bool PreparedResources::ResourceProvider::LoadMaterial(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::MaterialRenderData& output, std::string& error) {
    output = {};
    ResetDependencies(reference);
    const auto fail = [&error]() { error = "Invalid prepared material data"; return false; };
    Json::Value body;
    GameSproutRuntime::ResourceReference current = reference;
    std::vector<GameSproutRuntime::ResourceReference> visited;
    // 提取材质后，场景仍可持有原 FBX 的 GUID + LocalID；产物中的映射转到独立 .mat。
    // 限制链长度并检测循环，错误映射不会无限递归或静默显示另一份材质。
    for (;;) {
        if (visited.size() >= 32 || std::find(visited.begin(), visited.end(), current) != visited.end()) {
            error = "Material remapping contains a cycle or exceeds 32 references";
            return false;
        }
        visited.push_back(current);
        AddDependency(reference,current);
        Json::Value descriptor;
        std::vector<std::uint8_t> bytes;
        if (!Read(current,eAssetType::Material,descriptor,bytes,error)) return false;
        if (descriptor["Encoding"] != "BinaryValueLE1" || !ObjectDocumentBinary::Decode(bytes,body) || !body.isObject()) return fail();
        if (body["Format"] == "GameSproutDocument") body = Json::Value(body["Data"]);
        if (!body.isObject()) return fail();
        if (!body.isMember("ExternalMaterial")) break;
        if (!ReferenceValue(body["ExternalMaterial"], current)) return fail();
    }
    GameSproutRuntime::MaterialRenderData data;
    for (const char* name : {"EnableGPUInstancing","SpecularHighlights","EnvironmentReflections","PlanarReflection"})
        if (body.isMember(name) && !body[name].isBool()) return fail();
    data.EnableGPUInstancing=body.get("EnableGPUInstancing",true).asBool();
    data.SpecularHighlights=body.get("SpecularHighlights",true).asBool();
    data.PlanarReflection=body.get("PlanarReflection",false).asBool();
    data.EnvironmentReflections=body.get("EnvironmentReflections",true).asBool();
    if (body.isMember("SortingPriority")) {
        if (!body["SortingPriority"].isInt() || body["SortingPriority"].asInt()<-50 || body["SortingPriority"].asInt()>50) return fail();
        data.SortingPriority=body["SortingPriority"].asInt();
    }
    if (body.isMember("BaseColor") && !ColorValue(body["BaseColor"],data.BaseColor)) return fail();
    if (body.isMember("BaseColorTextureST")) {
        const auto& transform = body["BaseColorTextureST"];
        if (!transform.isArray() || transform.size() != 4) return fail();
        float values[4];
        for (Json::ArrayIndex index = 0; index < 4; ++index) {
            if (!transform[index].isNumeric() || !std::isfinite(transform[index].asDouble())) return fail();
            values[index] = transform[index].asFloat();
            if (!std::isfinite(values[index])) return fail();
        }
        data.BaseColorTextureST = {values[0],values[1],values[2],values[3]};
    }
    if (body.isMember("DoubleSided")) {
        if (!body["DoubleSided"].isBool()) return fail();
        data.DoubleSided = body["DoubleSided"].asBool();
    }
    if (body.isMember("BaseColorTexture") && !body["BaseColorTexture"].isNull()
        && !ReferenceValue(body["BaseColorTexture"],data.BaseColorTexture)) return fail();
    if(body.isMember("Shader")&&body["Shader"].isObject()) {
        if(!ReferenceValue(body["Shader"],data.Shader))return fail();
        AddDependency(reference,data.Shader);
        Artifact shader;
        if(!LocateArtifact(data.Shader,eAssetType::Shader,shader,error)
            ||shader.Descriptor["Format"]!="GameSproutShader"||shader.Descriptor["ShaderVersion"]!=1) {error="Material references an unavailable graphics Shader bundle";return false;}
        const auto& descriptor=shader.Descriptor;
        Json::Value normalized=body;
        if(!MaterialDefinition::NormalizeValues(descriptor["Material"],normalized,error))return false;
        std::map<std::string,GameSproutRuntime::ShaderValueType> types;
        for(const auto& pass:descriptor["Passes"])for(const char* stage:{"Vertex","Fragment"})for(const auto& block:pass[stage]["Uniforms"])for(const auto& field:block["Members"]) {
            const auto name=field["Name"].asString(),type=field["Type"].asString();
            GameSproutRuntime::ShaderValueType value;
            if(type=="Float")value=GameSproutRuntime::ShaderValueType::Float;
            else if(type=="Int")value=GameSproutRuntime::ShaderValueType::Int;
            else if(type=="UInt")value=GameSproutRuntime::ShaderValueType::UInt;
            else if(type=="Float4")value=GameSproutRuntime::ShaderValueType::Float4;
            else {error="Unsupported material property type: "+type;return false;}
            const auto [found,inserted]=types.emplace(name,value);if(!inserted&&found->second!=value){error="Inconsistent material binding type: "+name;return false;}
        }
        for(const auto& [name,type]:types) {
            GameSproutRuntime::MaterialPropertyData property;property.Name=name;property.Type=type;
            const auto& value=normalized[name];
            if(type==GameSproutRuntime::ShaderValueType::Float) {
                if(!MaterialDefinition::FiniteNumber(value))return fail();property.Numbers[0]=value.asFloat();
            }else if(type==GameSproutRuntime::ShaderValueType::Float4) {
                if(!MaterialDefinition::VectorValue(value))return fail();for(unsigned i=0;i<4;++i)property.Numbers[i]=value[i].asFloat();
            }else if(type==GameSproutRuntime::ShaderValueType::Int) {
                if(value.isBool())property.Integer=value.asBool()?1:0;else if(value.isInt())property.Integer=value.asInt();else return fail();
            }else {
                if(value.isBool())property.Unsigned=value.asBool()?1:0;else if(value.isUInt())property.Unsigned=value.asUInt();else return fail();
            }
            data.Properties.push_back(std::move(property));
        }
        for(const auto& property:descriptor["Material"]["Properties"])if(property["Type"]=="Texture") {
            GameSproutRuntime::MaterialTextureData texture;texture.Name=property["Name"].asString();
            if(!normalized[texture.Name].isNull()&&!ReferenceValue(normalized[texture.Name],texture.Texture))return fail();
            data.Textures.push_back(std::move(texture));
        }
    }else if(body["Shader"]=="GameSprout/Lit") {
        Json::Value normalized=body;
        if(!MaterialDefinition::NormalizeValues(MaterialDefinition::LitSchema(),normalized,error))return false;
        data.ShadingModel=GameSproutRuntime::MaterialShadingModel::Lit;
        if(!ColorValue(normalized["BaseColor"],data.BaseColor)||!ColorValue(normalized["Emission"],data.EmissionColor))return fail();
        const auto readNumber=[&](const char* name,float& value) {
            if(!MaterialDefinition::FiniteNumber(normalized[name]))return false;
            value=normalized[name].asFloat();return value>=0;
        };
        if(!readNumber("Metallic",data.Metallic)||!readNumber("Roughness",data.Roughness)||!readNumber("NormalScale",data.NormalScale))return fail();
        if(!readNumber("Stylization",data.Stylization)||!readNumber("LightThreshold",data.LightThreshold)||
            !readNumber("LightSoftness",data.LightSoftness)||!readNumber("ColorSaturation",data.ColorSaturation)||
            !ColorValue(normalized["ShadowTint"],data.StylizedShadowTint))return fail();
        if(!normalized["NormalTexture"].isNull()&&!ReferenceValue(normalized["NormalTexture"],data.NormalTexture))return fail();
        const auto& transform=normalized["NormalTextureST"];
        if(!MaterialDefinition::VectorValue(transform))return fail();
        data.NormalTextureST={transform[0].asFloat(),transform[1].asFloat(),transform[2].asFloat(),transform[3].asFloat()};
        const auto addFloat4=[&](std::string name) {
            const auto& value=normalized[name];
            if(!MaterialDefinition::VectorValue(value))return false;
            GameSproutRuntime::MaterialPropertyData property;
            property.Name=std::move(name);
            property.Type=GameSproutRuntime::ShaderValueType::Float4;
            for(Json::ArrayIndex index=0;index<4;++index)property.Numbers[index]=value[index].asFloat();
            data.Properties.push_back(std::move(property));
            return true;
        };
        for(const char* name:{"ORMTexture","TransmissionTexture"}) {
            const auto& value=normalized[name];
            if(value.isNull())continue;
            GameSproutRuntime::MaterialTextureData texture;
            texture.Name=name;
            if(!ReferenceValue(value,texture.Texture)||!addFloat4(texture.Name+"ST"))return fail();
            AddDependency(reference,texture.Texture);
            data.Textures.push_back(std::move(texture));
        }
        if(!normalized["ORMTexture"].isNull()&&
            (!addFloat4("ORMTextureRemapScale")||!addFloat4("ORMTextureRemapBias")))return fail();
        float transmissionStrength=0;
        if(!readNumber("TransmissionStrength",transmissionStrength))return fail();
        if(transmissionStrength>0) {
            GameSproutRuntime::MaterialPropertyData property;
            property.Name="TransmissionStrength";
            property.Type=GameSproutRuntime::ShaderValueType::Float;
            property.Numbers[0]=transmissionStrength;
            data.Properties.push_back(std::move(property));
            if(!addFloat4("TransmissionTint"))return fail();
        }
    }else if(body.isMember("Shader")&&body["Shader"]!="GameSprout/Preview")return fail();
    output = std::move(data);
    return true;
}
