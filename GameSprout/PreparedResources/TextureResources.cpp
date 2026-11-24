#include "PreparedResourceDecode.h"
#include "Internal/PreparedTextureLayout.h"

namespace {
using namespace GameSproutRuntime;
using PreparedResourceDecode::Unsigned;

bool DecodeTexture(const Json::Value& descriptor,std::uint64_t fileSize,TexturePrefetchSource& data) {
    if(descriptor["Encoding"]!="RawBytes"||descriptor["Format"]!="RGBA8"||
       !Unsigned(descriptor["Width"],data.Width)||!Unsigned(descriptor["Height"],data.Height))return false;
    const auto& mips=descriptor["Mips"];
    if(!mips.isArray()||mips.empty()||mips.size()>32)return false;
    data.MipLevels=mips.size();
    std::uint64_t end=0;
    for(Json::ArrayIndex level=0;level<mips.size();++level) {
        const auto& mip=mips[level];
        TextureSubresource sub;
        sub.MipLevel=level;
        if(!mip.isObject()||!Unsigned(mip["Width"],sub.Width)||!Unsigned(mip["Height"],sub.Height)||
           !Unsigned(mip["RowPitch"],sub.RowPitch)||!mip["Offset"].isUInt64()||!mip["Size"].isUInt64()||
           mip["Offset"].asUInt64()!=end||!sub.RowPitch||
           sub.Height>(fileSize-end)/sub.RowPitch||
           mip["Size"].asUInt64()!=std::uint64_t(sub.RowPitch)*sub.Height)return false;
        sub.Offset=end;
        end+=mip["Size"].asUInt64();
        data.Subresources.push_back(sub);
    }
    if(end!=fileSize||!PreparedTextureLayout::Validate(data,fileSize))return false;
    const auto& settings=descriptor["Settings"];
    if(!settings.isObject())return false;
    for(const char* key:{"ColorSpace","FilterMode","WrapU","WrapV"})
        if(settings.isMember(key)&&!settings[key].isString())return false;
    const auto space=settings.get("ColorSpace","SRGB").asString();
    if(space!="SRGB"&&space!="Linear")return false;
    data.SRGB=space=="SRGB";
    const auto filter=settings.get("FilterMode","Bilinear").asString();
    if(filter!="Point"&&filter!="Bilinear"&&filter!="Trilinear")return false;
    data.Sampler.MinFilter=data.Sampler.MagFilter=filter=="Point"?TextureFilter::Nearest:TextureFilter::Linear;
    data.Sampler.MipFilter=filter=="Trilinear"?TextureFilter::Linear:TextureFilter::Nearest;
    const auto aniso=settings.get("Anisotropy",1);
    if(!aniso.isUInt()||aniso.asUInt()<1||aniso.asUInt()>16)return false;
    data.Sampler.MaxAnisotropy=data.MipLevels>1&&filter!="Point"?aniso.asFloat():1.f;
    const auto wrap=[](const std::string& value,TextureAddressMode& mode) {
        if(value=="Repeat")mode=TextureAddressMode::Repeat;
        else if(value=="ClampToEdge")mode=TextureAddressMode::ClampToEdge;
        else if(value=="MirrorRepeat")mode=TextureAddressMode::MirroredRepeat;
        else return false;
        return true;
    };
    if(!wrap(settings.get("WrapU","Repeat").asString(),data.Sampler.AddressU)||
       !wrap(settings.get("WrapV","Repeat").asString(),data.Sampler.AddressV))return false;
    data.ArtifactSize=fileSize;
    return true;
}
}

bool PreparedResources::ResourceProvider::ResolveTexturePrefetch(const ResourceReference& reference,TexturePrefetchSource& output) {
    output={};
    Artifact artifact;
    std::ifstream file;
    std::string error;
    if(!OpenArtifact(reference,eAssetType::Texture,artifact,file,error))return false;
    TexturePrefetchSource source;
    if(!DecodeTexture(artifact.Descriptor,artifact.Size,source))return false;
    source.ArtifactPath=artifact.Path.string();
    output=std::move(source);
    return true;
}

bool PreparedResources::ResourceProvider::LoadTexture(const ResourceReference& reference,TextureRenderData& output,std::string& error) {
    output={};
    Artifact artifact;
    std::ifstream file;
    if(!OpenArtifact(reference,eAssetType::Texture,artifact,file,error))return false;
    TexturePrefetchSource source;
    if(!DecodeTexture(artifact.Descriptor,artifact.Size,source)||
       artifact.Size>std::uint64_t(std::numeric_limits<std::size_t>::max())||
       artifact.Size>std::uint64_t(std::numeric_limits<std::streamsize>::max())) {
        error="Invalid prepared texture mip layout or sampler settings";
        return false;
    }
    TextureRenderData data;
    PreparedTextureLayout::CopyDescriptor(data,source);
    data.Pixels.resize(static_cast<std::size_t>(artifact.Size));
    if(!file.read(reinterpret_cast<char*>(data.Pixels.data()),static_cast<std::streamsize>(data.Pixels.size()))) {
        MarkReadFailed(reference,true);
        error="Incomplete prepared texture";
        return false;
    }
    MarkReadFailed(reference,false);
    output=std::move(data);
    return true;
}
