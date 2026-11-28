#include "CastleRainImpacts.h"
#include "Math/TransformMath.h"
#include "RuntimeBuiltinResources.h"
#include "RuntimePhysicsData.h"
#include "RuntimeResourceApi.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace GameSproutRuntime;

namespace {
constexpr std::uint64_t MoatFileID = 1772;
constexpr char GroundMaterial[] = "70d0619abfca444e9cfe1d72a49548b8";
constexpr char WaterMaterial[] = "4020c28de6ab472fbcea1b9fbaee4072";
constexpr float GroundSeconds = .33f;
constexpr float WaterSeconds = .64f;
constexpr float ImpactsPerSecond = 58.f;
constexpr Vector3 Up{0, 1, 0};

Vector3 XYZ(Vector4 value) { return {value.X, value.Y, value.Z}; }

float Side(float x, float z, Vector3 start, Vector3 end) {
    return (x - start.X) * (end.Z - start.Z) - (z - start.Z) * (end.X - start.X);
}
}

bool CastleRainImpacts::InitializePool(IGameSproutRuntime& runtime, IScene& scene) {
    _Initialized = true;
    auto* root = scene.CreateGameObject("Rain impacts");
    if (!root) return false;
    _PoolID = root->GetInstanceID();

    const auto prepare = [&](auto& impacts, const char* material, const char* name) {
        for (auto& impact : impacts) {
            auto* object = scene.CreateGameObject(name, root);
            if (!object) return false;
            object->SetActive(false);
            auto* mesh = object->AddComponent<IMeshComponent>();
            auto* renderer = object->AddComponent<IMeshRendererComponent>();
            if (!mesh || !renderer || !renderer->SetMaterials({{material, 1}})) return false;
            mesh->SetMesh(GetBuiltinMeshReference(PrimitiveType::Quad));
            renderer->SetShadowCastingMode(ShadowCastingMode::Off);
            renderer->SetReceiveShadows(false);
            impact.ObjectID = object->GetInstanceID();
        }
        return true;
    };
    if (!prepare(_Ground, GroundMaterial, "Rain ground splash") || !prepare(_Water, WaterMaterial, "Moat ripple")) {
        scene.DestroyGameObject(root);
        _PoolID = 0;
        return false;
    }
    CacheWaterSurface(runtime, scene);
    return true;
}

void CastleRainImpacts::CacheWaterSurface(IGameSproutRuntime& runtime, IScene& scene) {
    const auto& plane = scene.GetEnvironmentLighting().PlanarReflection;
    auto* water = runtime.QuerySceneObjectByFileID(&scene, MoatFileID);
    const auto* mesh = water && water->GetName() == "Moat water" ? water->GetComponent<IMeshComponent>() : nullptr;
    auto* resources = runtime.GetResourceProvider();
    if (!mesh || !resources || !plane.Enabled) return;

    MeshRenderData geometry;
    std::string error;
    if (!resources->LoadMesh(mesh->GetMesh(), geometry, error) || geometry.VertexStride < 3 * sizeof(float) ||
        geometry.PositionOffset > geometry.VertexStride - 3 * sizeof(float) || geometry.IndexOffset > geometry.Bytes.size() ||
        geometry.VertexCount > geometry.IndexOffset / geometry.VertexStride ||
        geometry.IndexCount > (geometry.Bytes.size() - geometry.IndexOffset) / sizeof(std::uint32_t)) return;

    const auto world = water->GetTransform()->GetWorldMatrix();
    std::vector<Vector3> positions;
    positions.reserve(geometry.VertexCount);
    for (std::size_t vertex = 0; vertex < geometry.VertexCount; ++vertex) {
        std::array<float, 3> position;
        const auto offset = vertex * geometry.VertexStride + geometry.PositionOffset;
        std::memcpy(position.data(), geometry.Bytes.data() + offset, sizeof(position));
        positions.push_back(XYZ(world * Vector4{position[0], position[1], position[2], 1}));
    }

    _WaterHeight = plane.Height;
    _WaterTriangles.reserve(geometry.IndexCount / 3);
    for (std::size_t face = 0; face + 2 < geometry.IndexCount; face += 3) {
        std::array<std::uint32_t, 3> indices;
        std::memcpy(indices.data(), geometry.Bytes.data() + geometry.IndexOffset + face * sizeof(std::uint32_t), sizeof(indices));
        if (std::any_of(indices.begin(), indices.end(), [&](auto index) { return index >= positions.size(); })) continue;
        const auto a = positions[indices[0]], b = positions[indices[1]], c = positions[indices[2]];
        if (std::abs(a.Y - _WaterHeight) > .1f || std::abs(b.Y - _WaterHeight) > .1f || std::abs(c.Y - _WaterHeight) > .1f ||
            std::abs(Side(c.X, c.Z, a, b)) < 1e-6f) continue;
        _WaterTriangles.push_back({a, b, c});
    }
}

bool CastleRainImpacts::IsOverWater(float x, float z) const {
    return std::any_of(_WaterTriangles.begin(), _WaterTriangles.end(), [&](const auto& triangle) {
        const auto a = triangle.A, b = triangle.B, c = triangle.C;
        if (x < std::min({a.X, b.X, c.X}) || x > std::max({a.X, b.X, c.X}) ||
            z < std::min({a.Z, b.Z, c.Z}) || z > std::max({a.Z, b.Z, c.Z})) return false;
        const float ab = Side(x, z, a, b), bc = Side(x, z, b, c), ca = Side(x, z, c, a);
        return (ab >= 0 && bc >= 0 && ca >= 0) || (ab <= 0 && bc <= 0 && ca <= 0);
    });
}

float CastleRainImpacts::Random() {
    _RandomState = _RandomState * 1664525u + 1013904223u;
    return static_cast<float>(_RandomState >> 8) * (1.0f / 16777216.0f);
}

void CastleRainImpacts::Spawn(IGameSproutRuntime& runtime, IScene& scene, Vector3 eye, Vector3 forward) {
    const float distance = std::lerp(5.5f, 40.f, Random());
    const Vector3 right{forward.Z, 0, -forward.X};
    const auto spot = eye + forward * distance + right * ((Random() - .5f) * distance * 1.16f);
    const Vector3 origin{spot.X, eye.Y + 19.f, spot.Z};
    PhysicsRaycastHit hit;
    std::string error;
    const bool solid = runtime.Raycast(&scene, {origin, -Up}, 95.f, hit, {}, error);
    const bool onWater = (!solid || hit.Position.Y < _WaterHeight - .015f) && origin.Y > _WaterHeight &&
        IsOverWater(spot.X, spot.Z);
    if (!onWater && (!solid || hit.Normal.Y < .45f)) return;

    const auto place = [&](auto& impacts, Vector3 location, Quaternion rotation, float width, float height, float duration) {
        auto free = std::min_element(impacts.begin(), impacts.end(),
            [](const auto& first, const auto& second) { return first.Remaining < second.Remaining; });
        auto* object = runtime.QueryGameObject(free->ObjectID);
        if (!object || free->Remaining > 0) return;
        auto* transform = object->GetTransform();
        if (!transform->SetLocalRotation(rotation)) return;
        transform->SetLocalPosition(location);
        transform->SetLocalScale({width, height, .05f});
        object->SetActive(true);
        free->Width = width;
        free->Height = height;
        free->Remaining = duration;
    };

    if (onWater) {
        constexpr float halfSqrt = .70710678f;
        const float size = std::lerp(.78f, 1.27f, Random());
        place(_Water, {spot.X, _WaterHeight + .026f, spot.Z}, {-halfSqrt, 0, 0, halfSqrt}, size, size, WaterSeconds);
        return;
    }

    Quaternion facing;
    auto towards = eye - hit.Position;
    towards.Y = 0;
    if (!RuntimeMath::TryMakeLookRotation(towards, Up, facing)) return;
    const float scale = std::lerp(.78f, 1.23f, Random());
    place(_Ground, hit.Position + hit.Normal * .025f + Up * (.17f * scale), facing,
        .64f * scale, .36f * scale, GroundSeconds);
}

void CastleRainImpacts::Update(IGameSproutRuntime& runtime, IScene& scene, float density, float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0 || !std::isfinite(density)) return;
    density = std::clamp(density, 0.f, 1.f);
    if (!_Initialized && (density <= .02f || !InitializePool(runtime, scene))) return;
    if (!_PoolID) return;

    const auto age = [&](auto& impacts, float duration) {
        for (auto& impact : impacts) {
            if (impact.Remaining <= 0) continue;
            impact.Remaining = std::max(0.f, impact.Remaining - deltaSeconds);
            auto* object = runtime.QueryGameObject(impact.ObjectID);
            if (!object) continue;
            if (impact.Remaining == 0) object->SetActive(false);
            else object->GetTransform()->SetLocalScale({impact.Width, impact.Height, 1.05f - impact.Remaining / duration});
        }
    };
    age(_Ground, GroundSeconds);
    age(_Water, WaterSeconds);

    const auto* camera = scene.GetMainCamera();
    if (density <= .02f || !camera) {
        _EmitRemainder = 0;
        return;
    }
    const auto world = camera->GetGameObject()->GetTransform()->GetWorldMatrix();
    auto forward = XYZ(world * Vector4{0, 0, 1, 0});
    forward.Y = 0;
    if (!forward.TryNormalize(forward)) return;
    const auto eye = XYZ(world * Vector4{0, 0, 0, 1});
    _EmitRemainder = std::min(5.f, _EmitRemainder + deltaSeconds * density * ImpactsPerSecond);
    while (_EmitRemainder >= 1.f) {
        _EmitRemainder -= 1.f;
        Spawn(runtime, scene, eye, forward);
    }
}

void CastleRainImpacts::Release(IGameSproutRuntime& runtime, IScene& scene) {
    if (auto* root = _PoolID ? runtime.QueryGameObject(_PoolID) : nullptr) scene.DestroyGameObject(root);
    _PoolID = 0;
    _Initialized = false;
    _Ground = {};
    _Water = {};
    _WaterTriangles.clear();
    _EmitRemainder = 0;
}
