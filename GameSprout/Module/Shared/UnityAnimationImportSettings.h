#pragma once

#include "AnimationDefinition.h"
#include "AnimationImportSettingsJson.h"

namespace UnityAnimationImportSettings {
struct Binding {
    GameSproutRuntime::ResourceReference Skeleton;
    std::string RootPath;
    bool TransformOnly = false;
    std::map<std::string, GameSproutRuntime::Quaternion> RotationCorrections;
};

inline bool Read(const Json::Value& value, Binding& output, std::string& error) {
    Binding binding;
    if (!AnimationDefinition::Keys(value, {"Skeleton", "RootPath", "TransformOnly", "RotationCorrections"}, error))
        return false;
    const auto reject = [&error](const char* message) { error = message; return false; };
    if (!AnimationDefinition::Reference(value["Skeleton"], binding.Skeleton))
        return reject("Unity animation requires a target Skeleton");
    if (value.isMember("RootPath")) {
        if (!value["RootPath"].isString() || !AnimationImportSettingsJson::Text(value["RootPath"].asString(), 65536))
            return reject("Unity animation RootPath must identify a target Skeleton node");
        binding.RootPath = value["RootPath"].asString();
    }
    if (value.isMember("TransformOnly")) {
        if (!value["TransformOnly"].isBool()) return reject("Unity animation TransformOnly must be a boolean");
        binding.TransformOnly = value["TransformOnly"].asBool();
    }
    if (value.isMember("RotationCorrections")) {
        const auto& corrections = value["RotationCorrections"];
        if (!corrections.isArray() || corrections.size() > 4096)
            return reject("Invalid Unity animation RotationCorrections array");
        for (const auto& correction : corrections) {
            if (!AnimationDefinition::Keys(correction, {"Path", "Rotation"}, error)) return false;
            if (!correction["Path"].isString() || !AnimationImportSettingsJson::Text(correction["Path"].asString(), 65536))
                return reject("Invalid Unity relative path for a rotation correction");
            const auto& rotation = correction["Rotation"];
            if (!rotation.isArray() || rotation.size() != 4)
                return reject("Unity animation rotation correction requires four quaternion components");
            float components[4];
            for (Json::ArrayIndex i = 0; i < 4; ++i) {
                double number;
                if (!AnimationDefinition::Number(rotation[i], number) || std::fabs(number) > std::numeric_limits<float>::max())
                    return reject("Unity animation rotation correction is not finite");
                components[i] = static_cast<float>(number);
            }
            const GameSproutRuntime::Quaternion input{components[0], components[1], components[2], components[3]};
            GameSproutRuntime::Quaternion unit;
            if (!input.TryNormalize(unit) || !binding.RotationCorrections.emplace(correction["Path"].asString(), unit).second)
                return reject("Invalid or duplicate Unity animation rotation correction");
        }
    }
    output = std::move(binding);
    error.clear();
    return true;
}
}
