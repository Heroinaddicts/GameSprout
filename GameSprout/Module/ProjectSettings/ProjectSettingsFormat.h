#pragma once
#include "../../PreparedResources/PreparedProjectSettings.h"
#include <QByteArray>

namespace ProjectSettingsFormat {
inline bool Parse(const QByteArray& bytes, Json::Value& value, std::string& error) {
    return Parse(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())),value,error);
}
inline QByteArray Encode(const GameSproutRuntime::RuntimeProjectSettings& data) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    writer["emitUTF8"] = true;
    return QByteArray::fromStdString(Json::writeString(writer, EncodeDocument(data)) + "\n");
}
inline bool Decode(const QByteArray& bytes, GameSproutRuntime::RuntimeProjectSettings& output, std::string& error) {
    return Decode(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())),output,error);
}
}
