#include "CastleEnvironment.h"
#include "RuntimeBuiltinResources.h"
#include "RuntimeEnvironmentApi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

using namespace GameSproutRuntime;

namespace {
constexpr char AuroraType[] = "MountAndBlade.Aurora";
constexpr char CurvePrefix[] = "Castle.WeatherCycle.";
constexpr double StageSeconds = 5.0;
constexpr float TransitionSeconds = 1.4f;

struct Phase {
    const char* Name;
    float Hour;
    float LowCloud;
    float HighCloud;
    float ExtraCloud;
    float Wind;
    Color CloudTint;
    float Fog;
    Color FogTint;
    float Sun;
    float Ambient;
};

constexpr std::array Phases{
    Phase{"晴天", 10.f, .08f, .10f, .04f, .012f, {1.1f, 1.04f, .94f, 1}, .42f, {.43f, .52f, .62f, 1}, 1.38f, 1.12f},
    Phase{"雨天", 14.f, .89f, .77f, .61f, .06f, {.45f, .53f, .64f, 1}, 1.78f, {.28f, .36f, .45f, 1}, .23f, .72f},
    Phase{"黑夜", 23.f, .19f, .15f, .14f, .016f, {.52f, .63f, .94f, 1}, .90f, {.19f, .27f, .47f, 1}, .8f, 1.74f},
    Phase{"白天", 13.f, .44f, .32f, .24f, .024f, {1.f, 1.05f, 1.12f, 1}, .82f, {.35f, .46f, .61f, 1}, 1.2f, 1.f},
    Phase{"闪电", 17.92f, .72f, .63f, .46f, .082f, {.56f, .62f, .83f, 1}, 1.44f, {.24f, .33f, .48f, 1}, .56f, 1.32f},
    Phase{"黄昏", 17.66f, .33f, .28f, .18f, .019f, {1.7f, .89f, .62f, 1}, 1.15f, {.62f, .36f, .33f, 1}, 1.15f, 1.06f},
    Phase{"狂风天", 15.7f, .53f, .50f, .37f, .24f, {.69f, .77f, .85f, 1}, 1.37f, {.38f, .42f, .42f, 1}, .78f, .94f}
};

constexpr std::size_t RainPhase = 1;
constexpr std::size_t NightPhase = 2;
constexpr std::size_t LightningPhase = 4;
constexpr std::size_t WindPhase = 6;
constexpr std::array StrikeTimes{.54, 1.95, 3.16};
constexpr std::array StormMaterials{"082e26b6151045aab15516ae996754c0", "1adcf216e8d84fb19f4498272dd52379"};

float Ease(float value) {
    const float t = std::clamp(value, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float Daylight(float hour) {
    const auto elevation = std::cos((hour - 12.f) * std::numbers::pi_v<float> / 12.f);
    return Ease((elevation + .08f) / .28f);
}

WeatherPreset MakePreset(const WeatherPreset& authored, std::size_t index) {
    const auto& phase = Phases[index];
    auto preset = authored;
    preset.Clouds[0] = {phase.LowCloud, .76f, 2.2f, 118.f, phase.Wind};
    preset.Clouds[1] = {phase.HighCloud, .58f, 4.7f, 131.f, phase.Wind * .63f};
    preset.CloudColor = phase.CloudTint;
    preset.StarIntensity = index == NightPhase ? 2.1f : .8f;
    preset.FogStrength = phase.Fog;
    preset.DaylightMultiplier = phase.Sun;
    preset.AmbientMultiplier = phase.Ambient;
    return preset;
}

EnvironmentValue Blend(float a, float b, float weight) { return std::lerp(a, b, weight); }
EnvironmentValue Blend(Color a, Color b, float weight) {
    return Color{std::lerp(a.R, b.R, weight), std::lerp(a.G, b.G, weight),
                 std::lerp(a.B, b.B, weight), std::lerp(a.A, b.A, weight)};
}

// The last part of each five-second slot eases toward the next look, including the wrap back to sun.
template<class Sample>
EnvironmentCurve MakeCurve(std::string target, std::string field, int priority, Sample sample) {
    EnvironmentCurve curve;
    curve.Id = std::string(CurvePrefix) + target + '.' + field;
    curve.Target = std::move(target);
    curve.Parameter = std::move(field);
    curve.Loop = true;
    curve.Priority = priority;
    curve.Keys.push_back({0.f, sample(0)});
    for (std::size_t index = 0; index < Phases.size(); ++index) {
        const float boundary = static_cast<float>(StageSeconds * (index + 1));
        const auto current = sample(index);
        const auto next = sample((index + 1) % Phases.size());
        curve.Keys.push_back({boundary - TransitionSeconds, current});
        for (int step = 1; step <= 8; ++step) {
            const auto t = static_cast<float>(step) / 8.f;
            curve.Keys.push_back({boundary - TransitionSeconds * (1.f - t), Blend(current, next, Ease(t))});
        }
    }
    return curve;
}
}

bool CastleEnvironment::Initialize(IGameSproutRuntime* runtime) {
    auto* environment = QueryEnvironmentRuntime(*runtime);
    if (!environment) return true;

    EnvironmentEffectDescriptor aurora;
    aurora.Type = AuroraType;
    aurora.Stage = EnvironmentEffectStage::SkyOverlay;
    aurora.Material = {"7bc1d8284d2048b196e43d459902db02", 1};
    aurora.Parameters = {
        {"AuroraIntensity", 0.f, EnvironmentInterpolation::Linear},
        {"WeatherVisibility", 1.f, EnvironmentInterpolation::Linear},
        {"AuroraTint", Color{.08f, .78f, .46f, 1}, EnvironmentInterpolation::Linear}
    };
    _Registered = environment->RegisterEnvironmentEffect(aurora);
    if (!_Registered) GS_ERROR(runtime, "Could not register the castle aurora effect");
    return true;
}

void CastleEnvironment::LaunchFinished(IGameSproutRuntime* runtime) {
    auto* weather = QueryWeatherRuntime(*runtime);
    if (!weather || !QueryEnvironmentRuntime(*runtime)) return;
    for (std::size_t index = 0; index < runtime->GetSceneCount(); ++index) {
        auto* scene = runtime->GetScene(index);
        if (!scene || !scene->IsSimulationEnabled() || scene->GetName() != "Medieval Castle") continue;

        if (!weather->GetWeatherSettings(*scene, _InitialWeather)) return;
        auto settings = _InitialWeather;
        settings.Enabled = true;
        settings.DayLengthSeconds = 0;
        if (!weather->SetWeatherSettings(*scene, settings)) return;
        _SceneID = scene->GetInstanceID();
        weather->SetTimeOfDay(*scene, Phases.front().Hour);
        weather->TransitionWeather(*scene, MakePreset(_InitialWeather.Preset, 0), 0.f);
        InstallCurves(*runtime, *scene);
        GS_TRACE(runtime, "主城堡天气 %s（5 秒）", Phases.front().Name);
        return;
    }
}

void CastleEnvironment::InstallCurves(IGameSproutRuntime& runtime, IScene& scene) {
    auto* environment = QueryEnvironmentRuntime(runtime);
    EnvironmentSettings composition;
    if (!environment->GetEnvironmentSettings(scene, composition)) return;
    const auto hasEffect = [&composition](std::string_view id) {
        return std::any_of(composition.Effects.begin(), composition.Effects.end(),
            [id](const auto& effect) { return effect.Id == id; });
    };
    composition.Curves.push_back(MakeCurve("", "Sky.Daylight", 25,
        [](std::size_t index) { return Daylight(Phases[index].Hour); }));
    composition.Curves.push_back(MakeCurve("", "Fog.Color", 25,
        [](std::size_t index) { return Phases[index].FogTint; }));
    if (hasEffect("Castle.HighCloud")) {
        composition.Curves.push_back(MakeCurve("Castle.HighCloud", "Coverage", 25,
            [](std::size_t index) { return Phases[index].ExtraCloud; }));
        composition.Curves.push_back(MakeCurve("Castle.HighCloud", "WindSpeed", 25,
            [](std::size_t index) { return Phases[index].Wind * .44f; }));
    }
    if (hasEffect("Castle.Aurora"))
        composition.Curves.push_back(MakeCurve("Castle.Aurora", "WeatherVisibility", 25,
            [](std::size_t index) { return index == NightPhase ? 1.f : 0.f; }));
    if (!environment->SetEnvironmentSettings(scene, composition))
        GS_ERROR(&runtime, "主城堡天气过渡曲线加载失败");
}

void CastleEnvironment::SetStormOverlay(IGameSproutRuntime& runtime, IScene& scene, std::size_t index, float amount) {
    auto& id = _OverlayIDs[index];
    auto* overlay = id ? runtime.QueryGameObject(id) : nullptr;
    if (amount <= .001f) {
        if (overlay && overlay->IsActiveSelf()) overlay->SetActive(false);
        return;
    }
    auto* camera = scene.GetMainCamera();
    if (!camera) return;
    if (overlay && overlay->Parent() != camera->GetGameObject()) {
        scene.DestroyGameObject(overlay);
        overlay = nullptr;
        id = 0;
    }
    if (!overlay) {
        overlay = scene.CreateGameObject("Weather cycle overlay", camera->GetGameObject());
        if (!overlay) return;
        auto* mesh = overlay->AddComponent<IMeshComponent>();
        auto* renderer = overlay->AddComponent<IMeshRendererComponent>();
        if (!mesh || !renderer || !renderer->SetMaterials({{StormMaterials[index], 1}})) {
            scene.DestroyGameObject(overlay);
            return;
        }
        mesh->SetMesh(GetBuiltinMeshReference(PrimitiveType::Quad));
        renderer->SetShadowCastingMode(ShadowCastingMode::Off);
        renderer->SetReceiveShadows(false);
        id = overlay->GetInstanceID();
    }
    const float distance = std::max(1.f, camera->GetSettings().NearPlane * 2.f);
    overlay->GetTransform()->SetLocalPosition({0, 0, distance});
    overlay->GetTransform()->SetLocalScale({amount, 1, distance});
    if (!overlay->IsActiveSelf()) overlay->SetActive(true);
}

void CastleEnvironment::Strike(IGameSproutRuntime& runtime, IScene& scene) const {
    auto* camera = scene.GetMainCamera();
    if (!camera) return;
    const auto world = camera->GetGameObject()->GetTransform()->GetWorldMatrix();
    const auto origin = world * Vector4{0, 0, 0, 1};
    const auto direction = world * Vector4{0, 0, 1, 0};
    const float length = std::hypot(direction.X, direction.Z);
    const auto forward = length > 1e-4f ? Vector3{direction.X / length, 0, direction.Z / length} : Vector3{0, 0, 1};
    const Vector3 right{forward.Z, 0, -forward.X};
    constexpr std::array offsets{-7.f, 10.f, -2.f};
    const auto endpoint = Vector3{origin.X, origin.Y - 5.f, origin.Z} + forward * 55.f + right * offsets[_NextStrike];

    EnvironmentEvent event;
    event.Type = EnvironmentLightningEventType;
    event.Payload = {
        {"", "Position", endpoint}, {"", "Height", 33.f}, {"", "Intensity", 12.f},
        {"", "Radius", 115.f}, {"", "DurationSeconds", .50f},
        {"", "SeedLow16", static_cast<float>(1451 + 133 * _NextStrike)}
    };
    QueryEnvironmentRuntime(runtime)->EmitEnvironmentEvent(scene, event);
}

void CastleEnvironment::LateUpdate(IGameSproutRuntime* runtime, float deltaSeconds) {
    if (!_SceneID || deltaSeconds <= 0) return;
    auto* scene = dynamic_cast<IScene*>(runtime->QueryGameObject(_SceneID));
    if (!scene || !scene->IsSimulationEnabled()) return;

    _Elapsed = std::fmod(_Elapsed + deltaSeconds, StageSeconds * Phases.size());
    const auto stage = static_cast<std::size_t>(_Elapsed / StageSeconds);
    auto* weather = QueryWeatherRuntime(*runtime);
    if (stage != _Phase) {
        _Phase = stage;
        _NextStrike = 0;
        _TransitionStarted = false;
        weather->SetTimeOfDay(*scene, Phases[_Phase].Hour);
        GS_TRACE(runtime, "主城堡天气 %s（5 秒）", Phases[_Phase].Name);
    }
    const auto next = (_Phase + 1) % Phases.size();
    const auto inPhase = static_cast<float>(_Elapsed - StageSeconds * _Phase);
    const auto remaining = static_cast<float>(StageSeconds) - inPhase;
    if (!_TransitionStarted && remaining <= TransitionSeconds) {
        weather->TransitionWeather(*scene, MakePreset(_InitialWeather.Preset, next), remaining);
        _TransitionStarted = true;
    }
    const auto weight = Ease(1.f - remaining / TransitionSeconds);
    const auto strength = [&](std::size_t target) {
        return std::lerp(_Phase == target ? 1.f : 0.f, next == target ? 1.f : 0.f, weight);
    };
    const auto rain = strength(RainPhase);
    SetStormOverlay(*runtime, *scene, 0, rain);
    SetStormOverlay(*runtime, *scene, 1, strength(WindPhase));
    if (_Phase == LightningPhase && _NextStrike < StrikeTimes.size() &&
        inPhase >= StrikeTimes[_NextStrike]) {
        Strike(*runtime, *scene);
        ++_NextStrike;
    }
}

void CastleEnvironment::Release(IGameSproutRuntime* runtime) {
    auto* environment = QueryEnvironmentRuntime(*runtime);
    auto* scene = _SceneID ? dynamic_cast<IScene*>(runtime->QueryGameObject(_SceneID)) : nullptr;
    if (scene) {
        for (const auto id : _OverlayIDs)
            if (auto* overlay = id ? runtime->QueryGameObject(id) : nullptr) scene->DestroyGameObject(overlay);
        EnvironmentSettings composition;
        if (environment->GetEnvironmentSettings(*scene, composition)) {
            std::erase_if(composition.Curves, [](const auto& curve) { return curve.Id.starts_with(CurvePrefix); });
            environment->SetEnvironmentSettings(*scene, composition);
        }
        auto* weather = QueryWeatherRuntime(*runtime);
        weather->SetWeatherSettings(*scene, _InitialWeather);
        if (_InitialWeather.Enabled) {
            weather->SetTimeOfDay(*scene, _InitialWeather.TimeOfDayHours);
            weather->TransitionWeather(*scene, _InitialWeather.Preset, 0.f);
        }
    }
    if (_Registered) environment->UnregisterEnvironmentEffect(AuroraType);
    _Registered = false;
}
