#pragma once

#include "PreparedAssetType.h"
#include "JsonCpp/json.hpp"
#include <string>
#include <string_view>

namespace PreparedResources {
inline std::string JsonText(const Json::Value& value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    writer["emitUTF8"] = true;
    return Json::writeString(writer, value);
}

inline bool ValidGuid(std::string_view guid) {
    if (guid.size() != 32) return false;
    bool nonzero = false;
    for (const char digit : guid) {
        if (!(digit >= '0' && digit <= '9') && !(digit >= 'a' && digit <= 'f') &&
            !(digit >= 'A' && digit <= 'F')) return false;
        nonzero |= digit != '0';
    }
    return nonzero;
}

inline std::string CanonicalGuid(std::string_view guid) {
    std::string result(guid);
    for (char& digit : result) if (digit >= 'A' && digit <= 'F') digit += 'a' - 'A';
    return result;
}

inline std::string ArtifactFilename(std::string_view guid, std::uint64_t localID) {
    return CanonicalGuid(guid) + "_" + std::to_string(localID) + ".gsasset";
}
}
