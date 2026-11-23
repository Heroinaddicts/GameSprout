#include "PreparedResourceDecode.h"
#include "../Module/Shared/AnimationDefinition.h"

bool PreparedResources::ResourceProvider::LoadAnimatorController(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::AnimatorControllerData& output,std::string& error) {
    output={};Json::Value descriptor,body;std::vector<std::uint8_t> bytes;
    if(!Read(reference,eAssetType::AnimatorController,descriptor,bytes,error))return false;
    if(descriptor["Format"]!="AnimatorController3"||descriptor["Encoding"]!="BinaryValueLE1"||!ObjectDocumentBinary::Decode(bytes,body)
        ||!AnimationDefinition::ReadController(body,true,output,error)) {
        if(error.empty())error="Invalid compiled AnimatorController";
        return false;
    }
    return true;
}

#include "../Module/Shared/AvatarMaskDefinition.h"
bool PreparedResources::ResourceProvider::LoadAvatarMask(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::AvatarMaskData& output,std::string& error) {
    output={};Json::Value descriptor,body;std::vector<std::uint8_t> bytes;GameSproutRuntime::AvatarMaskData data;
    if(!Read(reference,eAssetType::AvatarMask,descriptor,bytes,error))return false;
    if(descriptor["Format"]!="AvatarMask2"||descriptor["Encoding"]!="BinaryValueLE1"||!ObjectDocumentBinary::Decode(bytes,body)
        ||!AvatarMaskDefinition::Read(body,true,data,error)) {
        if(error.empty())error="Invalid compiled AvatarMask";return false;
    }
    GameSproutRuntime::SkeletonData skeleton;
    if((data.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!LoadSkeleton(data.Skeleton,skeleton,error))||!AvatarMaskDefinition::Validate(data,skeleton,error))return false;
    output=std::move(data);return true;
}
