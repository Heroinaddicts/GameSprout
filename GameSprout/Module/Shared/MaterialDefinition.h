#pragma once

#include "../../PreparedResources/PreparedMaterialDefinition.h"
#include "AssetArtifactIO.h"
#include "AssetValueBinary.h"
#include "ILibraryManager.h"

namespace MaterialDefinition {
// .shader remains valid HLSL: the declaration is a block comment ignored by DXC.
// Scan comments rather than searching arbitrary text, so strings/line comments
// mentioning the marker cannot accidentally replace the authoring schema.
inline bool ParseShaderSchema(const QByteArray & source, Json::Value & schema, std::string & error) {
    error.clear();
    Json::Value result;
    result["Name"]="Shader"; result["Properties"]=Json::Value(Json::arrayValue);
    bool found=false;
    const std::string text=source.toStdString();
    for (size_t i=0; i+1<text.size();) {
        if (text[i]=='\"' || text[i]=='\'') {
            const char quote=text[i++];
            while (i<text.size()) { if (text[i]=='\\') { i+=2; continue; } if (text[i++]==quote) break; }
            continue;
        }
        if (text[i]=='/' && text[i+1]=='/') {
            const size_t end=text.find('\n',i+2); i=end==std::string::npos ? text.size() : end+1; continue;
        }
        if (text[i]!='/' || text[i+1]!='*') { ++i; continue; }
        const size_t start=i+2;
        const size_t end=text.find("*/",start);
        const std::string comment=text.substr(start,end==std::string::npos ? text.size()-start : end-start);
        size_t offset=0;
        while (offset<comment.size() && std::isspace(static_cast<unsigned char>(comment[offset]))) ++offset;
        const std::string marker="GameSproutMaterial";
        if (comment.compare(offset,marker.size(),marker)==0) {
            offset+=marker.size();
            if (offset==comment.size() || std::isspace(static_cast<unsigned char>(comment[offset])) || comment[offset]=='{') {
                if (end==std::string::npos || found || comment.size()>256*1024) {
                    error="Shader requires one terminated GameSproutMaterial block, at most 256 KiB"; return false;
                }
                if (!AssetArtifactIO::Parse(comment.substr(offset),result) || !ValidateSchema(result,error)) {
                    if (error.empty()) error="Invalid JSON in GameSproutMaterial block";
                    return false;
                }
                found=true;
            }
        }
        i=end==std::string::npos ? text.size() : end+2;
    }
    schema=std::move(result);
    return true;
}
// Editor main-thread helpers. Workers instead resolve the Shader through their
// immutable source-path snapshot and read its HLSL using AssetImportJob.
inline bool ResolveSchema(ILibraryManager * library, const Json::Value & document, Json::Value & schema, std::string & error) {
    error.clear();
    if (!ShaderReference(document,error)) return false;
    if (!document.isMember("Shader") || document["Shader"].isString()) { schema=BuiltinSchema(document); return true; }
    const Json::Value & reference=document["Shader"];
    IAsset * shader=library ? library->QueryAsset(Guid(reference["Guid"].asString()),reference["LocalID"].asUInt64()) : nullptr;
    if (!shader || shader->GetAssetType()!=eAssetType::Shader) { error="Material Shader is missing or is not a graphics Shader"; return false; }
    Json::Value descriptor;
    if (!AssetArtifactIO::Parse(shader->GetDataDescriptorJson(),descriptor) || !descriptor.isObject()
        || !ValidateSchema(descriptor["Material"],error)) {
        if (error.empty()) error="Shader has no valid material property descriptor";
        return false;
    }
    schema=descriptor["Material"];
    return true;
}
inline bool ReadPreparedMaterial(ILibraryManager * library, const Guid & guid, uint64_t localID,
    Json::Value & document, std::string & error) {
    error.clear();
    IAsset * asset=library ? library->QueryAsset(guid,localID) : nullptr;
    if (!asset || asset->GetAssetType()!=eAssetType::Material) { error="Material resource does not exist"; return false; }
    if (asset->GetDataVersion()!=2 || asset->GetDataSize()>static_cast<uint64_t>(MaxDocumentBytes)) {
        error="Material prepared data version or size is unsupported"; return false;
    }
    Json::Value descriptor,body;
    if (!AssetArtifactIO::Parse(asset->GetDataDescriptorJson(),descriptor) || !descriptor.isObject()
        || descriptor["Encoding"]!="BinaryValueLE1") {
        error="Material has an invalid prepared data descriptor"; return false;
    }
    QFile file(QString::fromStdString(asset->GetArtifactPath()));
    if (!file.open(QIODevice::ReadOnly)) { error=file.errorString().toStdString(); return false; }
    if (file.size()<0 || file.size()>MaxDocumentBytes || static_cast<uint64_t>(file.size())!=asset->GetDataSize()) {
        error="Material prepared file size does not match its descriptor"; return false;
    }
    const QByteArray bytes=file.read(MaxDocumentBytes+1);
    if (file.error()!=QFileDevice::NoError || !file.atEnd() || bytes.size()>MaxDocumentBytes
        || static_cast<uint64_t>(bytes.size())!=asset->GetDataSize()
        || !AssetValueBinary::Decode(bytes,body) || !body.isObject()) {
        error="Cannot read prepared material values"; return false;
    }
    if (body["Format"]=="GameSproutDocument") body=Json::Value(body["Data"]);
    if (!body.isObject()) { error="Prepared material body must be an object"; return false; }
    document=std::move(body);
    return true;
}
}
