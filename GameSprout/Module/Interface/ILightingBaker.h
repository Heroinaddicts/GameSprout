#pragma once

#include "GameSproutEditorApi.h"
#include <array>
#include <cstdint>
#include <string>

enum class LightingBakeKind : std::uint8_t { ReflectionProbe, IndirectVolume };
enum class LightingBakePhase : std::uint8_t { Idle, Capturing, Filtering, Importing, Complete, Failed };
struct LightingBakeRequest {
    std::uint64_t SceneID = 0;
    LightingBakeKind Kind = LightingBakeKind::ReflectionProbe;
    std::size_t Index = 0;
    std::uint32_t FaceSize = 64;
    std::uint32_t ReflectionSize = 64;
    std::uint32_t ReflectionSamples = 128;
    std::array<std::uint32_t,3> Grid{3,2,3};
    float CaptureDistance = 150;
    bool StaticOnly = true;
};
struct LightingBakeState {
    LightingBakePhase Phase = LightingBakePhase::Idle;
    std::uint32_t CompletedFaces = 0;
    std::uint32_t TotalFaces = 0;
    std::string Message;
    bool IsActive() const {
        return Phase==LightingBakePhase::Capturing||Phase==LightingBakePhase::Filtering||Phase==LightingBakePhase::Importing;
    }
};
class ILightingBakeListener {
public:
    virtual ~ILightingBakeListener() = default;
    virtual void OnLightingBakeChanged(const LightingBakeState&) = 0;
};
// Main-thread authoring service. Progress is pushed while a bake is active.
// Builds an independent project asset and assigns it in one undoable edit.
class ILightingBaker : public GameSproutEditor::IGameSproutModule {
public:
    virtual LightingBakeState GetBakeState() const = 0;
    virtual bool RegisterBakeListener(ILightingBakeListener*) = 0;
    virtual void UnregisterBakeListener(ILightingBakeListener*) = 0;
    virtual bool BeginBake(const LightingBakeRequest&,std::string&) = 0;
    virtual void CancelBake() = 0;
};
