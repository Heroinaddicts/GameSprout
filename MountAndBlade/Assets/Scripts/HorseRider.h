#pragma once
#include "RuntimeScriptModule.h"

// Project gameplay only. The owner is the horse's upright physics wrapper,
// with its origin at the hoof floor. Visual is a separate +Z-facing child;
// an imported animated rig belongs below Visual, not on the physics wrapper.
class HorseRider final : public GameSproutRuntime::ScriptBehaviour {
public:
    GameSproutRuntime::IGameObject* Camera = nullptr;
    GameSproutRuntime::IGameObject* Visual = nullptr;
    GameSproutRuntime::IRigidbodyComponent* Body = nullptr;
    GameSproutRuntime::ICapsuleColliderComponent* Collider = nullptr;
    GameSproutRuntime::IAnimatorComponent* Animator = nullptr;
    GameSproutRuntime::ResourceReference IdleClip;
    GameSproutRuntime::ResourceReference WalkClip;
    GameSproutRuntime::ResourceReference CanterClip;

    // Optional fallback for a simple unrigged prototype. Each leg pivot is
    // placed at the hip, with its leg geometry extending downwards from it.
    GameSproutRuntime::IGameObject* FrontLeftLeg = nullptr;
    GameSproutRuntime::IGameObject* FrontRightLeg = nullptr;
    GameSproutRuntime::IGameObject* BackLeftLeg = nullptr;
    GameSproutRuntime::IGameObject* BackRightLeg = nullptr;
    GameSproutRuntime::IGameObject* RiderPivot = nullptr;

    float WalkSpeed = 3.6f;
    float CanterSpeed = 7.5f;
    float ReverseSpeed = 1.3f;
    float WalkClipSpeed = 1.312f;  // Metres per second at the clip's native cadence.
    float CanterClipSpeed = 4.8f;
    float Acceleration = 3.5f;
    float Braking = 5.5f;
    float TurnSpeed = 70.0f; // Degrees per second; slower at a canter.
    float Heading = 0.0f;   // World yaw: zero faces +Z.
    float MaxSlopeDegrees = 48.0f;
    float GroundSnapDistance = 0.18f; // Maximum short drop or separation followed while grounded.
    float CameraDistance = 4.4f;
    float CameraHeight = 1.65f;
    float CameraPitch = 15.0f;
    float MouseSensitivity = 0.16f;
    float CameraSharpness = 10.0f;
    std::uint32_t CollisionMask = 0xfffffffdu;

protected:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override;
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* runtime) override;
    void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float fixedDeltaSeconds) override;
    void Update(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override;

private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<HorseRider>;
    explicit HorseRider(GameSproutRuntime::IGameSproutRuntime* runtime);

    struct AuthoredPose {
        std::uint64_t ObjectID = 0;
        GameSproutRuntime::Vector3 Position;
        GameSproutRuntime::Quaternion Rotation;
    };
    static float Finite(float value, float fallback);
    static float Approach(float current, float target, float distance);
    static GameSproutRuntime::Vector3 Position(GameSproutRuntime::IGameObject* object);
    static GameSproutRuntime::Vector3 Point(const GameSproutRuntime::Mat4& matrix, GameSproutRuntime::Vector3 point);
    static GameSproutRuntime::Quaternion Multiply(GameSproutRuntime::Quaternion a, GameSproutRuntime::Quaternion b);
    static bool WriteWorldPosition(GameSproutRuntime::IGameObject* object, GameSproutRuntime::Vector3 position);
    static bool Capture(GameSproutRuntime::IGameObject* object, AuthoredPose& pose);
    GameSproutRuntime::PhysicsQuerySettings QuerySettings() const;
    bool Ground(GameSproutRuntime::Vector3 position, GameSproutRuntime::PhysicsRaycastHit& hit, float above, float below) const;
    void DriveBody(float step, GameSproutRuntime::Vector3 horizontalVelocity);
    void MoveWithoutBody(float deltaSeconds, GameSproutRuntime::Vector3 forward);
    void Animate(float deltaSeconds);
    void PoseVisual(float deltaSeconds);
    void PoseLeg(GameSproutRuntime::IGameObject* object, AuthoredPose& pose, float angle);
    void PlaceCamera(float deltaSeconds, bool snap);

    GameSproutRuntime::IGameSproutRuntime* const _Runtime;
    AuthoredPose _VisualPose, _LegPoses[4], _RiderPose;
    GameSproutRuntime::ResourceReference _PlayingClip;
    GameSproutRuntime::Vector3 _GroundNormal{0.0f, 1.0f, 0.0f};
    GameSproutRuntime::Vector3 _VisualGroundNormal{0.0f, 1.0f, 0.0f};
    bool _Grounded = false;
    GameSproutRuntime::Vector3 _CameraPosition;
    GameSproutRuntime::Vector3 _LastGaitPosition;
    float _StillTime = 0.0f;
    bool _HasGaitPosition = false;
    float _Throttle = 0.0f, _Steer = 0.0f, _Speed = 0.0f;
    float _Heading = 0.0f, _CameraYaw = 0.0f, _OrbitIdle = 0.0f, _GaitPhase = 0.0f;
    bool _Canter = false, _OrbitHeld = false, _HasCamera = false, _Dynamic = false;
};
GAMESPROUT_SCRIPT(HorseRider)
