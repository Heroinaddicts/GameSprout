#include "CastleLightning.h"
#include "Math/TransformMath.h"
#include "RuntimeBuiltinResources.h"
#include "RuntimeEnvironmentApi.h"
#include "RuntimeLightingApi.h"
#include "RuntimePhysicsApi.h"
#include <algorithm>
#include <cmath>

using namespace GameSproutRuntime;

namespace {
constexpr float BoltDuration = .74f;
constexpr float GroundDuration = .82f;
const ResourceReference BranchMaterial{"fe35fefdb6bb42c2955b8615e7e7fa75", 1};
const ResourceReference ContactMaterial{"6b4e5fc7c3974277b66cbf631d860372", 1};

IGameObject* CreateQuad(IScene& scene, IGameObject& parent, const char* name, const ResourceReference& material) {
    auto* quad = scene.CreateGameObject(name, &parent);
    if (!quad) return nullptr;
    auto* mesh = quad->AddComponent<IMeshComponent>();
    auto* renderer = quad->AddComponent<IMeshRendererComponent>();
    if (!mesh || !renderer || !renderer->SetMaterials({material})) return nullptr;
    mesh->SetMesh(GetBuiltinMeshReference(PrimitiveType::Quad));
    renderer->SetShadowCastingMode(ShadowCastingMode::Off);
    renderer->SetReceiveShadows(false);
    return quad;
}

bool FindContact(IGameSproutRuntime& runtime, IScene& scene, Vector3 origin, Vector3 forward, Vector3 right,
                 unsigned index, PhysicsRaycastHit& contact) {
    constexpr std::array distances{26.f, 20.f, 34.f, 15.f, 42.f};
    constexpr std::array offsets{-5.f, 6.f, -1.5f};
    const auto& water = scene.GetEnvironmentLighting().PlanarReflection;
    std::string error;
    PhysicsQuerySettings query;
    for (const auto distance : distances) {
        for (const auto side : {offsets[index % offsets.size()], 0.f}) {
            const auto start = origin + forward * distance + right * side + Vector3{0, 28, 0};
            PhysicsRaycastHit hit;
            if (!runtime.Raycast(&scene, {start, {0, -1, 0}}, 87, hit, query, error) || hit.Normal.Y < .46f ||
                (water.Enabled && hit.Position.Y < water.Height + .10f)) continue;

            const Vector3 delta = hit.Position + hit.Normal * .18f - origin;
            Vector3 direction;
            if (!delta.TryNormalize(direction)) continue;
            PhysicsRaycastHit obstacle;
            if (runtime.Raycast(&scene, {origin, direction}, std::max(.01f, delta.Length() - .30f), obstacle, query, error)) continue;
            contact = hit;
            return true;
        }
    }
    return false;
}

float LightningNoise(unsigned point, unsigned axis, std::uint32_t seed) {
    auto hash = point * 0x9e3779b9u ^ axis * 0x85ebca6bu ^ seed;
    hash = (hash ^ (hash >> 16)) * 0x7feb352du;
    return static_cast<float>((hash ^ (hash >> 15)) & 65535u) / 65535.f;
}

Vector3 BoltVertex(const WeatherLightning& strike, unsigned point) {
    // The Runtime trunk uses twelve segments. Attach the authored branches to
    // its matching world-space vertices and leave the trunk/light to Runtime.
    const float height = strike.Height * static_cast<float>(point) / 12.f;
    const float range = height * .20f;
    return strike.Position + Vector3{(LightningNoise(point, 2, strike.Seed) - .5f) * range, height,
        (LightningNoise(point, 3, strike.Seed) - .5f) * range};
}
}

bool CastleLightning::CreateMeshes(IScene& scene) {
    auto* group = scene.CreateGameObject("Weather lightning contact");
    if (!group) return false;

    auto* contact = CreateQuad(scene, *group, "Lightning impact and sparks", ContactMaterial);
    if (!contact) {
        scene.DestroyGameObject(group);
        return false;
    }
    for (auto& branch : _Branches) {
        auto* quad = CreateQuad(scene, *group, "Lightning branch", BranchMaterial);
        if (!quad) {
            scene.DestroyGameObject(group);
            return false;
        }
        branch.ObjectID = quad->GetInstanceID();
    }
    _GroupID = group->GetInstanceID();
    _ContactID = contact->GetInstanceID();
    return true;
}

void CastleLightning::Strike(IGameSproutRuntime& runtime, IScene& scene, unsigned index) {
    const auto* camera = scene.GetMainCamera();
    auto* environment = QueryEnvironmentRuntime(runtime);
    if (!camera || !environment) return;
    const auto world = camera->GetGameObject()->GetTransform()->GetWorldMatrix();
    const Vector3 origin{world(0, 3), world(1, 3), world(2, 3)};
    Vector3 forward;
    if (!Vector3{world(0, 2), 0, world(2, 2)}.TryNormalize(forward)) return;
    const Vector3 right{forward.Z, 0, -forward.X};
    PhysicsRaycastHit contact;
    if (!FindContact(runtime, scene, origin, forward, right, index, contact)) return;

    WeatherLightning strike;
    strike.Position = contact.Position + contact.Normal * .09f;
    const auto& projection = camera->GetSettings();
    const float pitch = std::atan2(world(1, 2), std::hypot(world(0, 2), world(2, 2)));
    const float upper = std::clamp(pitch + projection.VerticalFovRadians * .43f, -.15f, .73f);
    const float distance = std::max(1.f, (contact.Position - origin).Dot(forward));
    strike.Height = std::clamp(origin.Y + std::tan(upper) * distance - strike.Position.Y, 11.f, 42.f);
    strike.Seed = 1451u + 133u * index;

    EnvironmentEvent event;
    event.Type = EnvironmentLightningEventType;
    event.Payload = {
        {"", "Position", strike.Position}, {"", "Height", strike.Height}, {"", "Intensity", 13.5f},
        {"", "Radius", 75.f}, {"", "DurationSeconds", BoltDuration},
        {"", "Tint", Color{.66f, .80f, 1.f, 1.f}}, {"", "SeedLow16", static_cast<float>(strike.Seed & 65535u)}
    };
    if (!environment->EmitEnvironmentEvent(scene, event)) return;
    if (!_GroupID && !CreateMeshes(scene)) return;
    auto* group = runtime.QueryGameObject(_GroupID);
    auto* ground = runtime.QueryGameObject(_ContactID);
    if (!group || !ground) return;

    Quaternion groundRotation;
    RuntimeMath::TryMakeLookRotation(contact.Normal, forward, groundRotation);
    ground->GetTransform()->SetLocalPosition(strike.Position + contact.Normal * .06f);
    ground->GetTransform()->SetLocalRotation(groundRotation);
    ground->GetTransform()->SetLocalScale({1.8f, 1.8f, .012f});

    constexpr std::array junctions{3u, 5u, 8u, 10u};
    for (std::size_t fork = 0; fork < junctions.size(); ++fork) {
        const float side = (fork + index) % 2 == 0 ? -1.f : 1.f;
        const float span = strike.Height * (.15f + .045f * static_cast<float>(fork));
        const auto start = BoltVertex(strike, junctions[fork]);
        const auto elbow = start + right * (side * span * .58f) + Vector3{0, -span * .29f, 0};
        const auto end = start + right * (side * span) + Vector3{0, -span * .88f, 0};
        const std::array points{start, elbow, end};
        for (std::size_t section = 0; section < 2; ++section) {
            auto& branch = _Branches[fork * 2 + section];
            const auto middle = (points[section] + points[section + 1]) * .5f;
            const auto line = points[section + 1] - points[section];
            Vector3 up;
            Quaternion orientation;
            if (!line.TryNormalize(up)) continue;
            const auto view = middle - origin;
            if (!RuntimeMath::TryMakeLookRotation(view - up * view.Dot(up), up, orientation)) continue;
            branch.Width = section == 0 ? .24f : .16f;
            branch.Length = line.Length() + .13f;
            if (auto* quad = runtime.QueryGameObject(branch.ObjectID)) {
                auto* transform = quad->GetTransform();
                transform->SetLocalPosition(middle);
                transform->SetLocalRotation(orientation);
                transform->SetLocalScale({branch.Width, branch.Length, 1.f});
            }
        }
    }
    group->SetActive(true);
    _Age = 0;
    _Active = true;
}

void CastleLightning::Update(IGameSproutRuntime& runtime, float deltaSeconds) {
    if (!_Active || deltaSeconds <= 0) return;
    _Age += deltaSeconds;
    if (_Age >= GroundDuration) {
        if (auto* group = runtime.QueryGameObject(_GroupID)) group->SetActive(false);
        _Active = false;
        return;
    }
    const float bolt = _Age / BoltDuration;
    const float echo = std::max(0.f, 1.f - std::abs(bolt - .28f) / .12f);
    const float intensity = std::max(.001f, (std::exp(-4.f * bolt) + .45f * echo) * std::max(0.f, 1.f - bolt));
    for (const auto& branch : _Branches)
        if (auto* quad = runtime.QueryGameObject(branch.ObjectID))
            quad->GetTransform()->SetLocalScale({branch.Width, branch.Length, intensity});

    const float groundAge = _Age / GroundDuration;
    const float size = std::lerp(1.8f, 5.4f, 1.f - (1.f - groundAge) * (1.f - groundAge));
    if (auto* ground = runtime.QueryGameObject(_ContactID))
        ground->GetTransform()->SetLocalScale({size, size, std::max(.012f, groundAge)});
}

void CastleLightning::Release(IGameSproutRuntime& runtime, IScene& scene) {
    if (_GroupID)
        if (auto* group = runtime.QueryGameObject(_GroupID)) scene.DestroyGameObject(group);
    _GroupID = _ContactID = 0;
    _Active = false;
}
