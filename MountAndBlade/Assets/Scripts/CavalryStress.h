#pragma once

#include "RuntimeScriptModule.h"
#include "RuntimeAnimationData.h"
#include "RuntimeResourceApi.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

class CavalryStress final : public GameSproutRuntime::ScriptBehaviour {
public:
    GameSproutRuntime::IGameObject* VisualTemplate = nullptr;
    GameSproutRuntime::ResourceReference HorseModel;
    GameSproutRuntime::ResourceReference KnightModel;
    GameSproutRuntime::ResourceReference HelmetModel;
    GameSproutRuntime::ResourceReference WalkClip;
    GameSproutRuntime::ResourceReference CanterClip;
    GameSproutRuntime::ResourceReference ScoutMaterial;
    std::uint32_t RiderCount = 1000;
    float CenterX = 101;
    float CenterZ = 0;
    float RadiusX = 62;
    float RadiusZ = 112;
    bool AnimateCavalry = true;
    bool SwapEquipment = true;
    bool DetailedShadows = false;
    bool ProfileMovement = false;

protected:
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override;
    void Update(GameSproutRuntime::IGameSproutRuntime*, float deltaSeconds) override;
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime*, float) override;
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override;

private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<CavalryStress>;
    explicit CavalryStress(GameSproutRuntime::IGameSproutRuntime*) {}

    static constexpr auto NoNode = std::numeric_limits<std::size_t>::max();
    enum PartSlot : std::size_t { HorseBody, Saddle, Mane, Tail, KnightBody, Chainmail, Face, Helmet, Kit, PartCount };

    struct MeshBone {
        std::size_t Node;
        GameSproutRuntime::Mat4 InverseBind;
    };
    struct Part {
        std::string Name;
        GameSproutRuntime::ResourceReference Mesh;
        std::vector<GameSproutRuntime::ResourceReference> Materials;
        GameSproutRuntime::Mat4 Local;
        GameSproutRuntime::Mat4 InverseLocal;
        std::vector<MeshBone> Bones;
        std::vector<GameSproutRuntime::Mat4> SeatedSkin;
    };
    enum Gait : std::size_t { Walk, Canter, Idle, GaitCount };
    struct Clip {
        float Duration = 0;
        bool AlignedKeys = false;
        // Poses are in the shared _HorseJoints order.
        std::vector<std::uint8_t> ConstantChannels;
        std::vector<GameSproutRuntime::AnimationFrame> Frames;
    };
    struct HorseJoint {
        std::size_t Parent;
        unsigned Tracks;
        GameSproutRuntime::Mat4 RestMatrix;
    };
    struct GroundPoint {
        float Height = 0;
        bool Walkable = false;
    };
    struct Rider {
        std::uint64_t ObjectID = 0;
        std::uint64_t SeatID = 0;
        std::array<std::uint64_t,PartCount> Parts{};
        std::uint32_t RandomState = 0;
        float X = 0;
        float Z = 0;
        float Speed = 0;
        float Heading = 0;
        float DesiredHeading = 0;
        float TurnAfter = 0;
        float GaitAfter = 0;
        float OutfitAfter = 0;
        float StridePhase = 0;
        float IdleTime = 0;
        float CanterWeight = 0;
        float IdleWeight = 0;
        float SpeedAtGait = 0;
        GameSproutRuntime::Vector3 VisualGroundNormal{0,1,0};
        bool Cantering = false;
        bool Scout = false;
    };

    bool CaptureVisual(GameSproutRuntime::IGameSproutRuntime&, std::string& error);
    bool SampleTerrain(GameSproutRuntime::IGameSproutRuntime&, std::string& error);
    bool Ground(float x, float z, GameSproutRuntime::Vector3& position, GameSproutRuntime::Vector3& normal) const;
    bool CreateRider(GameSproutRuntime::IGameObject& group, std::string& error);
    bool UpdateRiders(GameSproutRuntime::IGameSproutRuntime&, float delta, std::string& error);
    bool UpdateVisual(GameSproutRuntime::IGameSproutRuntime&, Rider&, std::string& error,
        const GameSproutRuntime::IGameObject* root = nullptr);
    bool ApplyOutfit(GameSproutRuntime::IGameSproutRuntime&, const Rider&, std::string& error);
    bool ConfigurePart(GameSproutRuntime::IGameObject&, const Part&, std::string& error);
    void SampleHorse(const Rider&);
    void Fail(GameSproutRuntime::IGameSproutRuntime&, const std::string& error);

    std::array<Clip,GaitCount> _Clips;
    std::array<Part,PartCount> _Parts;
    Part _ScoutKit;
    std::vector<HorseJoint> _HorseJoints;
    std::vector<GameSproutRuntime::Mat4> _HorseWorld;
    std::vector<GameSproutRuntime::Mat4> _PaletteScratch;
    GameSproutRuntime::Mat4 _HorseRig;
    GameSproutRuntime::Mat4 _Seat;
    std::size_t _SeatBone = NoNode;
    std::vector<GroundPoint> _Ground;
    std::vector<GameSproutRuntime::Vector2> _StartPositions;
    std::vector<Rider> _Riders;
    std::size_t _PoseReady = 0;
    std::size_t _GroundWidth = 0;
    std::size_t _GroundDepth = 0;
    std::uint64_t _GroupID = 0;
    float _MinX = 0;
    float _MinZ = 0;
    float _CellX = 0;
    float _CellZ = 0;
    double _MovementMilliseconds = 0;
    unsigned _MovementFrames = 0;
    unsigned _ClipChanges = 0;
    unsigned _OutfitChanges = 0;
    bool _ReportedReady = false;
    bool _ReportedSwaps = false;
};

GAMESPROUT_SCRIPT(CavalryStress)
