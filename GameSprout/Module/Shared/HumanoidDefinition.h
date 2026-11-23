#pragma once
#include "../../PreparedResources/PreparedHumanoidDefinition.h"
#include "HumanoidImportSettings.h"
#include "HumanoidValidation.h"

namespace HumanoidDefinition {
inline Json::Value SettingsJson(const ModelHumanoidImportSettings& settings) {
    Json::Value value;value["HumanScale"]=settings.HumanScale;value["HumanBones"]=Json::Value(Json::arrayValue);
    value["HumanReferencePose"]=Json::Value(Json::arrayValue);
    for(const auto& mapping:settings.HumanBones) {
        Json::Value bone;bone["Bone"]=GameSproutRuntime::HumanBoneName(mapping.Bone);bone["Path"]=mapping.Path;value["HumanBones"].append(bone);
    }
    for(const auto& pose:settings.ReferencePose) {
        Json::Value entry=Pose(pose.Pose);entry["Path"]=pose.Path;value["HumanReferencePose"].append(entry);
    }
    return value;
}
inline bool ReadSettings(const Json::Value& value,ModelHumanoidImportSettings& output,std::string& error) {
    output={};error.clear();const auto fail=[&](const char* message){error=message;return false;};
    if(!value.isObject())return fail("Humanoid settings must be an object");
    ModelHumanoidImportSettings settings;double number;
    if(value.isMember("HumanScale")) {
        if(!AnimationDefinition::Number(value["HumanScale"],number)||number<0||number>100000)return fail("HumanScale must be zero (automatic) or positive metres");
        settings.HumanScale=static_cast<float>(number);
    }
    if(value.isMember("HumanBones")&&(!value["HumanBones"].isArray()||value["HumanBones"].size()>static_cast<unsigned>(GameSproutRuntime::HumanBone::Count)))return fail("Invalid HumanBones mapping");
    std::set<unsigned> bones;std::set<std::string> paths;
    for(const auto& entry:value["HumanBones"]) {
        GameSproutRuntime::HumanBoneMapping mapping;
        if(!AnimationDefinition::Keys(entry,{"Bone","Path"},error))return false;
        if(!Bone(entry["Bone"],mapping.Bone)||!entry["Path"].isString()||!AvatarMaskDefinition::Path(entry["Path"].asString())
            ||!bones.insert(static_cast<unsigned>(mapping.Bone)).second||!paths.insert(entry["Path"].asString()).second)return fail("Human bones must map unique roles to unique Skeleton paths");
        mapping.Path=entry["Path"].asString();settings.HumanBones.push_back(mapping);
    }
    if(value.isMember("HumanReferencePose")&&(!value["HumanReferencePose"].isArray()||value["HumanReferencePose"].size()>100000))return fail("Invalid HumanReferencePose");
    paths.clear();for(const auto& entry:value["HumanReferencePose"]) {
        HumanoidReferencePose pose;
        if(!AnimationDefinition::Keys(entry,{"Path","Translation","Rotation","Scale"},error))return false;
        if(!entry["Path"].isString()||!AvatarMaskDefinition::Path(entry["Path"].asString())
            ||!paths.insert(entry["Path"].asString()).second||!Pose(entry,pose.Pose))return fail("Reference poses require unique Skeleton paths and valid positive-scale TRS");
        pose.Path=entry["Path"].asString();settings.ReferencePose.push_back(pose);
    }
    output=std::move(settings);return true;
}
inline bool Prepare(const ModelHumanoidImportSettings& settings,const GameSproutRuntime::SkeletonData& skeleton,
    const GameSproutRuntime::ResourceReference& reference,GameSproutRuntime::HumanoidAvatarData& output,std::string& error) {
    output={};GameSproutRuntime::HumanoidAvatarData avatar;avatar.Skeleton=reference;avatar.Bones=settings.HumanBones;
    std::map<std::string,std::size_t> paths;
    for(const auto& node:skeleton.Nodes){paths.emplace(node.Path,avatar.ReferencePose.size());avatar.ReferencePose.push_back(node.Transform);}
    for(const auto& pose:settings.ReferencePose) {
        const auto found=paths.find(pose.Path);if(found==paths.end()){error="Reference pose path no longer exists: "+pose.Path;return false;}
        avatar.ReferencePose[found->second]=pose.Pose;
    }
    if(!GameSproutRuntime::BuildHumanAvatarAxes(skeleton,avatar,error))return false;
    if(settings.HumanScale>0)avatar.HumanScale=settings.HumanScale;
    if(!std::isfinite(avatar.HumanScale)||avatar.HumanScale<=0||avatar.HumanScale>100000){error="Humanoid body scale exceeds supported metre range";return false;}
    if(!GameSproutRuntime::ValidateHumanAvatar(skeleton,avatar,error))return false;
    output=std::move(avatar);return true;
}
}
