#pragma once
#include "../Module/Shared/AnimationDefinition.h"
#include "../Module/Shared/AvatarMaskDefinition.h"
#include "HumanoidValidation.h"

namespace HumanoidDefinition {
inline Json::Value Numbers(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);for(float value:values)result.append(value);return result;
}
inline Json::Value Pose(const GameSproutRuntime::TransformPose& pose) {
    Json::Value value;value["Translation"]=Numbers({pose.Position.X,pose.Position.Y,pose.Position.Z});
    value["Rotation"]=Numbers({pose.Rotation.X,pose.Rotation.Y,pose.Rotation.Z,pose.Rotation.W});
    value["Scale"]=Numbers({pose.Scale.X,pose.Scale.Y,pose.Scale.Z});return value;
}
inline bool Values(const Json::Value& source,float* values,unsigned count) {
    if(!source.isArray()||source.size()!=count)return false;
    for(unsigned index=0;index<count;++index) {
        double number;if(!AnimationDefinition::Number(source[index],number)||std::fabs(number)>1e20)return false;
        values[index]=static_cast<float>(number);
    }
    return true;
}
inline bool Pose(const Json::Value& source,GameSproutRuntime::TransformPose& output) {
    float position[3],rotation[4],scale[3];
    if(!Values(source["Translation"],position,3)||!Values(source["Rotation"],rotation,4)||!Values(source["Scale"],scale,3))return false;
    output.Position={position[0],position[1],position[2]};output.Rotation={rotation[0],rotation[1],rotation[2],rotation[3]};
    output.Scale={scale[0],scale[1],scale[2]};
    return output.Rotation.TryNormalize(output.Rotation)&&scale[0]>0&&scale[1]>0&&scale[2]>0;
}
inline bool Bone(const Json::Value& value,GameSproutRuntime::HumanBone& output) {
    if(!value.isString())return false;
    for(unsigned index=0;index<static_cast<unsigned>(GameSproutRuntime::HumanBone::Count);++index)
        if(value.asString()==GameSproutRuntime::HumanBoneName(static_cast<GameSproutRuntime::HumanBone>(index))) {
            output=static_cast<GameSproutRuntime::HumanBone>(index);return true;
        }
    return false;
}
inline Json::Value WriteAvatar(const GameSproutRuntime::HumanoidAvatarData& avatar) {
    Json::Value value;value["Format"]="HumanoidAvatar1";value["Skeleton"]=AnimationDefinition::Reference(avatar.Skeleton);
    value["HumanScale"]=avatar.HumanScale;value["Bones"]=Json::Value(Json::arrayValue);value["ReferencePose"]=Json::Value(Json::arrayValue);
    for(const auto& mapping:avatar.Bones) {
        Json::Value bone;bone["Bone"]=GameSproutRuntime::HumanBoneName(mapping.Bone);bone["Path"]=mapping.Path;
        bone["Axis"]=Numbers({mapping.Axis.X,mapping.Axis.Y,mapping.Axis.Z,mapping.Axis.W});value["Bones"].append(bone);
    }
    for(const auto& pose:avatar.ReferencePose)value["ReferencePose"].append(Pose(pose));return value;
}
inline bool ReadAvatar(const Json::Value& source,GameSproutRuntime::HumanoidAvatarData& output,std::string& error) {
    output={};error.clear();GameSproutRuntime::HumanoidAvatarData avatar;double number;
    if(!AnimationDefinition::Keys(source,{"Format","Skeleton","HumanScale","Bones","ReferencePose"},error))return false;
    if(source["Format"]!="HumanoidAvatar1"||!AnimationDefinition::Reference(source["Skeleton"],avatar.Skeleton)
        ||!AnimationDefinition::Number(source["HumanScale"],number)||number<=0||number>100000
        ||!source["Bones"].isArray()||source["Bones"].size()>static_cast<unsigned>(GameSproutRuntime::HumanBone::Count)
        ||!source["ReferencePose"].isArray()||source["ReferencePose"].empty()||source["ReferencePose"].size()>100000) {
        error="Invalid prepared Humanoid Avatar";return false;
    }
    avatar.HumanScale=static_cast<float>(number);std::set<unsigned> roles;std::set<std::string> paths;
    for(const auto& value:source["Bones"]) {
        GameSproutRuntime::HumanBoneMapping mapping;float axis[4];
        if(!AnimationDefinition::Keys(value,{"Bone","Path","Axis"},error))return false;
        if(!Bone(value["Bone"],mapping.Bone)||!value["Path"].isString()||!AvatarMaskDefinition::Path(value["Path"].asString())
            ||!roles.insert(static_cast<unsigned>(mapping.Bone)).second||!paths.insert(value["Path"].asString()).second||!Values(value["Axis"],axis,4)){error="Invalid prepared Human bone mapping";return false;}
        mapping.Path=value["Path"].asString();mapping.Axis={axis[0],axis[1],axis[2],axis[3]};if(!mapping.Axis.TryNormalize(mapping.Axis)){error="Invalid Human bone axis";return false;}avatar.Bones.push_back(mapping);
    }
    for(const auto& value:source["ReferencePose"]) {
        GameSproutRuntime::TransformPose pose;if(!AnimationDefinition::Keys(value,{"Translation","Rotation","Scale"},error)||!Pose(value,pose)){error="Invalid prepared Human reference pose";return false;}
        avatar.ReferencePose.push_back(pose);
    }
    output=std::move(avatar);return true;
}
}
