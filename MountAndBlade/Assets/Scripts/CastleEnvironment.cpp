#include "CastleEnvironment.h"
#include "RuntimeEnvironmentApi.h"

using namespace GameSproutRuntime;

namespace {
constexpr char AuroraType[] = "MountAndBlade.Aurora";
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
        {"AuroraTint", Color{.08f, .78f, .46f, 1}, EnvironmentInterpolation::Linear}
    };
    _Registered = environment->RegisterEnvironmentEffect(aurora);
    if (!_Registered) GS_ERROR(runtime, "Could not register the castle aurora effect");
    return true;
}

void CastleEnvironment::Release(IGameSproutRuntime* runtime) {
    if (!_Registered) return;
    QueryEnvironmentRuntime(*runtime)->UnregisterEnvironmentEffect(AuroraType);
    _Registered = false;
}
