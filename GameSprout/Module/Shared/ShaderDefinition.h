#pragma once
#include "MaterialDefinition.h"
#include "../../PreparedResources/PreparedShaderDefinition.h"

namespace ShaderDefinition {
inline bool Parse(const QByteArray& source,Json::Value& output,std::string& error) {
    output={};error.clear();bool found=false;const std::string text=source.toStdString();
    for(std::size_t i=0;i+1<text.size();) {
        if(text[i]=='\"'||text[i]=='\'') {const char quote=text[i++];while(i<text.size()){if(text[i]=='\\'){i+=2;continue;}if(text[i++]==quote)break;}continue;}
        if(text[i]=='/'&&text[i+1]=='/'){const auto end=text.find('\n',i+2);i=end==std::string::npos?text.size():end+1;continue;}
        if(text[i]!='/'||text[i+1]!='*'){++i;continue;}
        const auto start=i+2,end=text.find("*/",start);const auto comment=text.substr(start,end==std::string::npos?text.size()-start:end-start);
        std::size_t offset=0;while(offset<comment.size()&&std::isspace(static_cast<unsigned char>(comment[offset])))++offset;
        const std::string marker="GameSproutShader";
        if(comment.compare(offset,marker.size(),marker)==0&&(offset+marker.size()==comment.size()||std::isspace(static_cast<unsigned char>(comment[offset+marker.size()]))||comment[offset+marker.size()]=='{')) {
            if(found||end==std::string::npos||comment.size()>256*1024){error="Shader requires one terminated GameSproutShader declaration, at most 256 KiB";return false;}
            if(!AssetArtifactIO::Parse(comment.substr(offset+marker.size()),output)){error="Invalid JSON in GameSproutShader declaration";return false;}found=true;
        }
        i=end==std::string::npos?text.size():end+2;
    }
    if(!found){error="Graphics .shader requires a GameSproutShader declaration with Vertex and Fragment entries";return false;}
    if(!Keys(output,{"Name","Passes"},error))return false;
    if(!output.isMember("Name"))output["Name"]="Shader";
    if(!output["Name"].isString()||output["Name"].asString().empty()||!output["Passes"].isArray()||output["Passes"].size()!=1){error="Shader requires Name and exactly one Forward Pass in the current renderer";return false;}
    auto& pass=output["Passes"][0];
    if(!Keys(pass,{"Name","LightMode","Vertex","Fragment","Cull","Blend","DepthTest","DepthWrite","DepthCompare"},error))return false;
    for(const char* entry:{"Name","Vertex","Fragment"})if(!pass[entry].isString()||!MaterialDefinition::Identifier(pass[entry].asString())){error=std::string("Shader Pass requires an identifier: ")+entry;return false;}
    if(pass.get("LightMode","Forward")!="Forward"){error="Only Forward Shader LightMode is currently supported";return false;}pass["LightMode"]="Forward";
    for(const auto& setting:std::initializer_list<std::tuple<const char*,const char*,std::set<std::string>>>{{"Cull","Back",{"Off","Front","Back"}},{"Blend","Opaque",{"Opaque","Alpha","PremultipliedAlpha"}},{"DepthCompare","LessEqual",{"Never","Less","Equal","LessEqual","Greater","NotEqual","GreaterEqual","Always"}}}) {
        const auto& [key,defaultValue,values]=setting;if(!pass.isMember(key))pass[key]=defaultValue;
        if(!pass[key].isString()||!values.contains(pass[key].asString())){error=std::string("Unsupported Shader Pass state: ")+key;return false;}
    }
    for(const char* key:{"DepthTest","DepthWrite"}) {if(!pass.isMember(key))pass[key]=true;if(!pass[key].isBool()){error=std::string("Shader Pass state must be boolean: ")+key;return false;}}
    return true;
}
}
