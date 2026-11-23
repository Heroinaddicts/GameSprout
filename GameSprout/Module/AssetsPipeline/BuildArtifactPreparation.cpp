#include "AssetsPipeline.h"
#include "../Shared/AssetArtifactIO.h"
#include <algorithm>
#include <limits>

namespace {
constexpr uint64_t MaximumShaderCodeSize = 64u * 1024u * 1024u;

struct ShaderSlice {
    uint64_t SourceOffset;
    uint64_t TargetOffset;
    uint64_t Size;
    std::string Format;
};

const char* ProfileName(eAssetBuildProfile profile) {
    switch (profile) {
    case eAssetBuildProfile::MetalMacOS: return "macOS Metal";
    case eAssetBuildProfile::MetalIOS: return "iOS Metal";
    case eAssetBuildProfile::MetalIOSSimulator: return "iOS Simulator Metal";
    case eAssetBuildProfile::Vulkan: return "Vulkan";
    case eAssetBuildProfile::Headless: return "Server";
    }
    return nullptr;
}

int VariantPriority(const Json::Value& variant, eAssetBuildProfile profile) {
    const auto& format = variant["Format"];
    const auto& platform = variant["Platform"];
    if (profile == eAssetBuildProfile::Vulkan)
        return format == "SPIRV" && platform == "Vulkan" ? 1 : 0;
    if (format == "MSL" && platform == "Apple") return 1;
    if (format != "MetalLib") return 0;
    return ((profile == eAssetBuildProfile::MetalMacOS && platform == "macOS") ||
            (profile == eAssetBuildProfile::MetalIOS && platform == "iOS") ||
            (profile == eAssetBuildProfile::MetalIOSSimulator && platform == "iOSSimulator")) ? 2 : 0;
}

bool SelectShaderVariant(Json::Value& program, const AssetBuildArtifactRequest& request,
                         std::vector<ShaderSlice>& slices, uint64_t& payloadSize, std::string& error) {
    const auto& variants = program["Variants"];
    if (!variants.isArray() || variants.empty() || variants.size() > 16) {
        error = "Missing or invalid shader variants";
        return false;
    }
    Json::Value selected;
    int best = 0;
    for (const auto& variant : variants) {
        if (!variant.isObject()) {
            error = "Invalid shader variant descriptor";
            return false;
        }
        const int priority = VariantPriority(variant, request.Profile);
        if (priority > best) {
            best = priority;
            selected = variant;
        }
    }
    if (!best) {
        error = std::string("No shader variant for ") + ProfileName(request.Profile);
        return false;
    }
    if (!selected["Offset"].isUInt64() || !selected["Size"].isUInt64() ||
        !selected["EntryPoint"].isString() || selected["EntryPoint"].asString().empty() ||
        selected["EntryPoint"].asString().find('\0') != std::string::npos) {
        error = "Invalid selected shader variant descriptor";
        return false;
    }
    const auto offset = selected["Offset"].asUInt64();
    const auto size = selected["Size"].asUInt64();
    if (offset % 4 || !size || size > MaximumShaderCodeSize || offset > request.Artifact.DataSize ||
        size > request.Artifact.DataSize - offset || std::any_of(slices.begin(), slices.end(), [&](const auto& slice) {
            return offset < slice.SourceOffset + slice.Size && slice.SourceOffset < offset + size;
        })) {
        error = "Selected shader variants have invalid or overlapping byte ranges";
        return false;
    }
    const uint64_t targetOffset = (payloadSize + 3u) & ~uint64_t{3};
    slices.push_back({offset, targetOffset, size, selected["Format"].asString()});
    payloadSize = targetOffset + size;
    selected["Offset"] = Json::UInt64(targetOffset);
    program["Variants"] = Json::Value(Json::arrayValue);
    program["Variants"].append(std::move(selected));
    return true;
}

bool ReadPayload(const QString& path, uint64_t size, QByteArray& bytes) {
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || size > uint64_t(std::numeric_limits<qsizetype>::max()) ||
        info.size() < 0 || uint64_t(info.size()) != size) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    bytes = file.readAll();
    return file.error() == QFileDevice::NoError && uint64_t(bytes.size()) == size;
}

bool ValidShaderCode(const QByteArray& bytes, const std::string& format) {
    if (format == "MSL") return bytes.endsWith('\0') && !bytes.chopped(1).contains('\0');
    if (format == "MetalLib") return bytes.startsWith("MTLB");
    return bytes.size() >= 20 && bytes.size() % 4 == 0 && bytes.startsWith(QByteArray::fromHex("03022307"));
}
}

bool AssetsPipeline::PrepareBuildArtifact(const AssetBuildArtifactRequest& request,
    AssetBuildArtifactResult& result, std::string& error) const {
    result = {};
    error.clear();
    const auto cancelled = [&] {
        if (!request.IsCancellationRequested || !request.IsCancellationRequested()) return false;
        error = "Build asset preparation cancelled";
        return true;
    };
    if (cancelled()) return false;
    const QString source = QString::fromStdString(request.SourceArtifactPath);
    if (!ProfileName(request.Profile) || !QDir::isAbsolutePath(source) || !request.Artifact.LocalID) {
        error = "Invalid target artifact request";
        return false;
    }
    const bool graphics = request.Artifact.Type == eAssetType::Shader;
    if (request.Profile == eAssetBuildProfile::Headless || (!graphics && request.Artifact.Type != eAssetType::ComputeShader)) {
        result = {request.Artifact, request.SourceArtifactPath};
        return true;
    }

    Json::Value descriptor;
    if (request.Artifact.DataVersion != AssetArtifactIO::CurrentDataVersion(request.Artifact.Type) ||
        !AssetArtifactIO::Parse(request.Artifact.DescriptorJson, descriptor) || !descriptor.isObject() ||
        descriptor["Encoding"] != "RawBytes" ||
        (graphics && (descriptor["Format"] != "GameSproutShader" || descriptor["ShaderVersion"] != 1 ||
                      !descriptor["Passes"].isArray() || descriptor["Passes"].size() != 1)) ||
        (!graphics && descriptor["Stage"] != "compute")) {
        error = "Invalid imported shader descriptor";
        return false;
    }
    std::vector<ShaderSlice> slices;
    uint64_t payloadSize = 0;
    if (graphics) {
        auto& pass = descriptor["Passes"][0u];
        for (const char* stage : {"Vertex", "Fragment"}) {
            if (!pass.isObject() || !pass[stage].isObject() || pass[stage]["Stage"] != stage) {
                error = "Invalid graphics shader stage";
                return false;
            }
            if (!SelectShaderVariant(pass[stage], request, slices, payloadSize, error)) return false;
        }
    } else if (!SelectShaderVariant(descriptor, request, slices, payloadSize, error)) return false;

    auto output = request.Artifact;
    output.DataSize = payloadSize;
    output.DescriptorJson = AssetArtifactIO::JsonText(descriptor);
    if (output.DescriptorJson == request.Artifact.DescriptorJson && payloadSize == request.Artifact.DataSize) {
        result = {request.Artifact, request.SourceArtifactPath};
        return true;
    }

    const QDir cache(QString::fromStdString(request.TargetCacheRoot));
    if (!cache.isAbsolute() || QFileInfo(cache.absolutePath()).isSymLink() || !QDir().mkpath(cache.absolutePath())) {
        error = "Cannot open build artifact cache";
        return false;
    }
    const auto key = AssetArtifactIO::Hash(QByteArray::fromStdString(
        "GameSproutTargetShader1\n" + request.Artifact.Hash + "\n" + output.DescriptorJson));
    const QString filename = cache.filePath(QString::fromStdString(key) + ".gsasset");
    const QString hashFilename = cache.filePath(QString::fromStdString(key) + ".sha256");
    if (QFileInfo(filename).isSymLink() || QFileInfo(hashFilename).isSymLink()) {
        error = "Build artifact cache entries must be regular files";
        return false;
    }
    QByteArray bytes, expectedHash;
    if (ReadPayload(hashFilename, 64, expectedHash) && ReadPayload(filename, payloadSize, bytes)) {
        output.Hash = AssetArtifactIO::Hash(bytes);
        if (expectedHash == QByteArray::fromStdString(output.Hash)) {
            if (cancelled()) return false;
            result = {std::move(output), filename.toStdString()};
            return true;
        }
    }
    if (cancelled()) return false;
    QByteArray original;
    if (!ReadPayload(source, request.Artifact.DataSize, original) || AssetArtifactIO::Hash(original) != request.Artifact.Hash) {
        error = "Imported shader bytes changed before target preparation";
        return false;
    }
    bytes.clear();
    bytes.reserve(qsizetype(payloadSize));
    for (const auto& slice : slices) {
        if (cancelled()) return false;
        const auto code = QByteArray::fromRawData(original.constData() + slice.SourceOffset, qsizetype(slice.Size));
        if (!ValidShaderCode(code, slice.Format)) {
            error = "Invalid compiled " + slice.Format + " shader bytes";
            return false;
        }
        bytes.append(qsizetype(slice.TargetOffset) - bytes.size(), '\0');
        bytes.append(code);
    }
    if (cancelled()) return false;
    output.Hash = AssetArtifactIO::Hash(bytes);
    if (!AssetArtifactIO::Write(filename, bytes, error) ||
        !AssetArtifactIO::Write(hashFilename, QByteArray::fromStdString(output.Hash), error)) return false;
    result = {std::move(output), filename.toStdString()};
    return true;
}
