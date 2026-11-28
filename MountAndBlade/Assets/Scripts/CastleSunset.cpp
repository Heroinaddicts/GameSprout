#include "CastleSunset.h"
#include "Math/TransformMath.h"
#include <algorithm>

using namespace GameSproutRuntime;

namespace {
constexpr Vector3 SunsetDirection{-.930f, .174f, .324f};
constexpr Color SunsetLight{1.f, .52f, .24f, 1.f};
constexpr float SunsetIntensity = 3.0f;

ILightComponent* LightOn(IGameSproutRuntime& runtime, std::uint64_t id) {
    auto* object = id ? runtime.QueryGameObject(id) : nullptr;
    return object ? object->GetComponent<ILightComponent>() : nullptr;
}
}

bool CastleSunset::Register(IEnvironmentRuntime& environment) {
    EnvironmentEffectDescriptor effect;
    effect.Type = EffectType;
    effect.Stage = EnvironmentEffectStage::SkyOverlay;
    effect.Material = {"0442560a892446c08d24531ddd371fe7", 1};
    effect.Parameters = {{WeightParameter, 0.f},
        {"SunsetDirection", Color{SunsetDirection.X, SunsetDirection.Y, SunsetDirection.Z, 0}}};
    _Registered = environment.RegisterEnvironmentEffect(effect);
    return _Registered;
}

bool CastleSunset::Bind(IGameSproutRuntime&, IScene& scene) {
    auto* dayObject = scene.FindChild("Castle afternoon sun");
    auto* day = dayObject ? dayObject->GetComponent<ILightComponent>() : nullptr;
    if (!_Registered || !day || day->GetLightType() != LightType::Directional) return false;

    Quaternion rotation;
    if (!RuntimeMath::TryMakeLookRotation(SunsetDirection * -1.f, {0, 1, 0}, rotation)) return false;
    auto* sunsetObject = scene.CreateGameObject("Castle sunset directional sun");
    if (!sunsetObject) return false;
    auto* sunset = sunsetObject->AddComponent<ILightComponent>();
    _DaySettings = day->GetSettings();
    _SunsetSettings = _DaySettings;
    _SunsetSettings.Color = SunsetLight;
    _SunsetSettings.Intensity = 0.f;
    if (!sunset || !sunsetObject->GetTransform()->SetLocalRotation(rotation) || !sunset->SetSettings(_SunsetSettings)) {
        scene.DestroyGameObject(sunsetObject);
        return false;
    }
    _DayID = dayObject->GetInstanceID();
    _SunsetID = sunsetObject->GetInstanceID();
    return true;
}

void CastleSunset::SetWeight(IGameSproutRuntime& runtime, float weight) {
    weight = std::clamp(weight, 0.f, 1.f);
    if (!_SunsetID || _Weight == weight) return;

    // Both light directions remain fixed. The renderer switches the primary
    // shadow light when the stronger contribution crosses over.
    if (auto* day = LightOn(runtime, _DayID)) day->SetIntensity(_DaySettings.Intensity * (1.f - weight));
    if (auto* sunset = LightOn(runtime, _SunsetID)) sunset->SetIntensity(SunsetIntensity * weight);
    _Weight = weight;
}

void CastleSunset::Release(IGameSproutRuntime& runtime, IScene* scene) {
    if (auto* day = LightOn(runtime, _DayID)) day->SetSettings(_DaySettings);
    auto* sunset = _SunsetID ? runtime.QueryGameObject(_SunsetID) : nullptr;
    if (scene && sunset && sunset->GetScene() == scene) scene->DestroyGameObject(sunset);
    if (auto* environment = QueryEnvironmentRuntime(runtime); environment && _Registered)
        environment->UnregisterEnvironmentEffect(EffectType);
    _Registered = false;
    _DayID = 0;
    _SunsetID = 0;
    _Weight = 0.f;
}
