#pragma once

#include "Math/TransformMath.h"
#include "RuntimeAnimationApi.h"
#include "RuntimeResourceApi.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace CavalryPoseCapture {
using namespace GameSproutRuntime;

struct Part {
    const char* Name;
    const char* File;
    std::uint64_t LowMesh = 0;
};

// The named parts use the existing player rig, materials, and Rider Seat transform.
inline constexpr std::array Parts{
    Part{"Horse Body Geometry", "horse", 6964467865960709011ULL},
    Part{"Saddle Geometry", "saddle", 5553654483487323484ULL},
    Part{"Horse Tail Low Geometry", "tail"},
    Part{"Horse Mane1 Low Geometry", "mane"},
    Part{"Knight_base_LOD0 Geometry", "knight"},
    Part{"Shoulders_LOD0 Geometry", "shoulders"},
    Part{"Chainmail_LOD0 Geometry", "mail"},
    Part{"KnightFace_LOD0 Geometry", "face"},
    Part{"Helmet_LOD0 Geometry", "helmet"},
    Part{"VisorPivot_LOD0 Geometry", "visorPivot"},
    Part{"Visiere_LOD0 Geometry", "visor"},
};
inline constexpr unsigned PhaseCount = 8;

inline IGameObject* Find(IGameObject& object, std::string_view name) {
    if (object.GetName() == name) return &object;
    for (std::size_t index = 0; index < object.GetChildCount(); ++index) {
        if (auto* found = Find(*object.GetChild(index), name)) return found;
    }
    return nullptr;
}

inline void Require(bool success, const std::string& error) {
    if (!success) throw std::runtime_error(error);
}

template<class Value>
Value Read(const std::uint8_t* data) {
    Value value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

template<class Value>
void Write(std::ostream& stream, const Value& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline Mat4 NormalMatrix(const Mat4& matrix) {
    Mat4 inverse;
    Require(RuntimeMath::TryInverseAffine(matrix, inverse), "Invalid part normal transform");
    Mat4 result;
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column) result(row, column) = inverse(column, row);
    }
    return result;
}

inline Vector3 XYZ(Vector4 value) { return {value.X, value.Y, value.Z}; }

inline void WritePart(const std::filesystem::path& file, const MeshRenderData& mesh,
    IGameObject& object, const Mat4& inverseRoot) {
    const Mat4 relative = inverseRoot * object.GetTransform()->GetWorldMatrix();
    const Mat4 normalRelative = NormalMatrix(relative);
    std::vector<Mat4> joints;
    if (!mesh.Bones.empty()) {
        const auto* skin = object.GetComponent<ISkinnedMeshRendererComponent>();
        Require(skin, object.GetName() + " has no skin component");
        std::string error;
        const bool resolved = skin->GetSkinMatrices(mesh.Bones, joints, error);
        Require(resolved, object.GetName() + ": " + error);
    }
    Require(mesh.UVOffset >= 0, object.GetName() + " has no texture coordinates");
    std::ofstream stream(file, std::ios::binary);
    Write(stream, mesh.VertexCount);
    Write(stream, mesh.IndexCount);
    for (std::uint32_t vertex = 0; vertex < mesh.VertexCount; ++vertex) {
        const auto* data = mesh.Bytes.data() + vertex * std::size_t(mesh.VertexStride);
        Vector3 point = Read<Vector3>(data + mesh.PositionOffset);
        Vector3 normal = Read<Vector3>(data + mesh.NormalOffset);
        if (!joints.empty()) {
            const auto indices = Read<std::array<std::uint32_t, 4>>(data + mesh.BoneIndicesOffset);
            const auto weights = Read<std::array<float, 4>>(data + mesh.BoneWeightsOffset);
            Vector3 posedPoint{}, posedNormal{};
            float totalWeight = 0.0f;
            for (unsigned weight = 0; weight < 4; ++weight) {
                if (weights[weight] == 0.0f) continue;
                const Mat4& joint = joints.at(indices[weight]);
                posedPoint = posedPoint + XYZ(joint * Vector4{point.X, point.Y, point.Z, 1}) * weights[weight];
                posedNormal = posedNormal + XYZ(joint * Vector4{normal.X, normal.Y, normal.Z, 0}) * weights[weight];
                totalWeight += weights[weight];
            }
            if (totalWeight > 0.0f) {
                point = posedPoint * (1.0f / totalWeight);
                normal = posedNormal * (1.0f / totalWeight);
            }
        }
        point = XYZ(relative * Vector4{point.X, point.Y, point.Z, 1});
        normal = XYZ(normalRelative * Vector4{normal.X, normal.Y, normal.Z, 0});
        if (!normal.TryNormalize(normal)) normal = {0, 1, 0};
        Write(stream, std::array{point.X, point.Y, point.Z, normal.X, normal.Y, normal.Z});
        Write(stream, Read<std::array<float, 2>>(data + mesh.UVOffset));
    }
    const auto relativeX = XYZ(relative * Vector4{1, 0, 0, 0});
    const auto relativeY = XYZ(relative * Vector4{0, 1, 0, 0});
    const auto relativeZ = XYZ(relative * Vector4{0, 0, 1, 0});
    const bool mirrored = relativeX.Cross(relativeY).Dot(relativeZ) < 0.0f;
    for (std::uint32_t index = 0; index < mesh.IndexCount; index += 3) {
        auto triangle = Read<std::array<std::uint32_t, 3>>(mesh.Bytes.data() + mesh.IndexOffset + index * 4);
        if (mirrored) std::swap(triangle[1], triangle[2]);
        Write(stream, triangle);
    }
    Require(stream.good(), "Unable to write " + file.string());
}

// Call with the project's normal prepared resource provider and instantiated MedievalCastle scene.
inline void Capture(IGameObject& mountVisual, IRuntimeResourceProvider& resources,
    const std::filesystem::path& output) {
    const ResourceReference canter{"0b5174ef62cb5b759bcd1d41a44d6f3e", 1};
    auto* horseObject = Find(mountVisual, "Warhorse Rig");
    auto* riderObject = Find(mountVisual, "Mounted Knight");
    Require(horseObject && riderObject, "Missing horse or mounted rider in source hierarchy");
    auto* horse = horseObject->GetComponent<IAnimatorComponent>();
    auto* rider = riderObject->GetComponent<IAnimatorComponent>();
    Require(horse && rider, "Missing a source Animator");
    const bool horsePlaying = horse->PlayClip(canter);
    Require(horsePlaying, "Cannot sample Canter: " + horse->GetLastError());
    const bool riderPlaying = rider->PlayClip(rider->GetDefaultClip());
    Require(riderPlaying, "Cannot sample MountedPose: " + rider->GetLastError());

    std::array<MeshRenderData, Parts.size()> source;
    std::array<IGameObject*, Parts.size()> objects;
    for (std::size_t index = 0; index < Parts.size(); ++index) {
        const auto& part = Parts[index];
        objects[index] = Find(mountVisual, part.Name);
        Require(objects[index], std::string("Missing source part ") + part.Name);
        const auto* mesh = objects[index]->GetComponent<IMeshComponent>();
        Require(mesh, std::string("Missing mesh component on ") + part.Name);
        auto reference = mesh->GetMesh();
        if (part.LowMesh) reference.LocalID = part.LowMesh;
        std::string error;
        const bool loaded = resources.LoadMesh(reference, source[index], error);
        Require(loaded, std::string("Cannot load ") + part.Name + ": " + error);
    }

    const float duration = horse->GetClipPlaybackState().Duration;
    Require(duration > 0.0f, "Canter must have a positive duration");
    std::filesystem::create_directories(output);
    for (unsigned phase = 0; phase < PhaseCount; ++phase) {
        const bool sampled = horse->SeekClip(duration * phase / PhaseCount);
        Require(sampled, horse->GetLastError());
        Mat4 inverseRoot;
        Require(RuntimeMath::TryInverseAffine(mountVisual.GetTransform()->GetWorldMatrix(), inverseRoot),
            "Invalid Mount Visual transform");
        for (std::size_t index = 0; index < Parts.size(); ++index) {
            const auto phaseFile = output / (std::to_string(phase) + "_" + Parts[index].File + ".raw");
            WritePart(phaseFile, source[index], *objects[index], inverseRoot);
        }
    }
}
}
