#include "HorseRider.h"
#include "Math/TransformMath.h"
#include <algorithm>
#include <cmath>

HorseRider::HorseRider(GameSproutRuntime::IGameSproutRuntime* runtime) : _Runtime(runtime) {}

void HorseRider::OnCreate(GameSproutRuntime::IGameSproutRuntime*) {
    _Throttle = _Steer = _Speed = _GaitPhase = _OrbitIdle = 0.0f;
    _Canter = _OrbitHeld = _HasCamera = _Dynamic = false;
    _PlayingClip = {};
    _HasGaitPosition = false;
    _StillTime = 0.0f;
    _Grounded = false;
    _GroundNormal = _VisualGroundNormal = {0.0f, 1.0f, 0.0f};
}

void HorseRider::OnInitialized(GameSproutRuntime::IGameSproutRuntime* runtime) {
    if (!Body) Body = _GameObject->GetComponent<GameSproutRuntime::IRigidbodyComponent>();
    if (!Collider) Collider = _GameObject->GetComponent<GameSproutRuntime::ICapsuleColliderComponent>();
    _Dynamic = Body && Collider && Body->GetGameObject() == _GameObject &&
        Collider->GetAttachedRigidbody() == Body && Body->IsEnabled() &&
        Body->GetMotionType() == GameSproutRuntime::PhysicsMotionType::Dynamic;
    if (_Dynamic) {
        // Physics owns the wrapper's translation. Steering rotates only Visual,
        // leaving the collision capsule upright and preserving gravity velocity.
        _Dynamic = Body->SetConstraints(Body->GetConstraints() |
            static_cast<std::uint32_t>(GameSproutRuntime::PhysicsConstraint::FreezeRotation));
        Body->SetContinuous(true);
    }
    if (!_Dynamic && Body && Body->IsEnabled()) {
        // Do not mix direct transform movement with a non-Dynamic live body.
        GS_ERROR(runtime, "HorseRider requires a Dynamic Rigidbody or no enabled Rigidbody");
    }
    _Heading = std::remainder(Finite(Heading, 0.0f), 360.0f);
    _CameraYaw = _Heading;
    Capture(Visual, _VisualPose);
    Capture(FrontLeftLeg, _LegPoses[0]); Capture(FrontRightLeg, _LegPoses[1]);
    Capture(BackLeftLeg, _LegPoses[2]); Capture(BackRightLeg, _LegPoses[3]);
    Capture(RiderPivot, _RiderPose);
    if (Animator) {
        // The physical wrapper is the sole locomotion owner. Imported root
        // motion is extracted from the visible skeleton without moving twice.
        Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ExtractOnly);
    }
    PoseVisual(0.0f);
    Animate(0.0f);
    PlaceCamera(0.0f, true);
}

void HorseRider::EarlyUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) {
    if (!runtime || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
    const auto& input = runtime->GetInputState();
    _Throttle = _Steer = 0.0f; _Canter = false;
    if (input.Focused) {
        _Throttle = (input.GetKey(GameSproutRuntime::InputKey::W).Held ? 1.0f : 0.0f) -
            (input.GetKey(GameSproutRuntime::InputKey::S).Held ? 1.0f : 0.0f);
        _Steer = (input.GetKey(GameSproutRuntime::InputKey::D).Held ? 1.0f : 0.0f) -
            (input.GetKey(GameSproutRuntime::InputKey::A).Held ? 1.0f : 0.0f);
        _Canter = input.GetKey(GameSproutRuntime::InputKey::Shift).Held;
        const auto orbit = input.GetMouseButton(GameSproutRuntime::InputMouseButton::Right);
        // The Game viewport owns focus/capture. Never steal the cursor or
        // consume Scene-editor controls; ignore the initial capture click delta.
        if (orbit.Held && _OrbitHeld && !orbit.Pressed) {
            const float sensitivity = std::clamp(Finite(MouseSensitivity, 0.16f), 0.0f, 2.0f);
            _CameraYaw += Finite(input.MouseDelta.X, 0.0f) * sensitivity;
            CameraPitch += Finite(input.MouseDelta.Y, 0.0f) * sensitivity;
        }
        _OrbitHeld = orbit.Held;
        if (std::fabs(input.WheelDelta.Y) > 0.0f)
            CameraDistance = std::clamp(Finite(CameraDistance, 4.4f) - input.WheelDelta.Y * 0.4f, 2.2f, 9.0f);
    } else {
        _OrbitHeld = false;
    }
    CameraPitch = std::clamp(Finite(CameraPitch, 15.0f), -12.0f, 58.0f);
    _CameraYaw = std::remainder(Finite(_CameraYaw, _Heading), 360.0f);
    _OrbitIdle = _OrbitHeld ? 0.0f : _OrbitIdle + deltaSeconds;
}

void HorseRider::FixedUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float fixedDeltaSeconds) {
    if (!runtime || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0f) return;
    const float step = std::min(fixedDeltaSeconds, 0.1f);
    if (!runtime->GetInputState().Focused) { _Throttle = _Steer = 0.0f; _Canter = false; }
    const float walk = std::clamp(Finite(WalkSpeed, 3.6f), 0.1f, 12.0f);
    const float canter = std::clamp(Finite(CanterSpeed, 7.5f), walk, 20.0f);
    const float reverse = std::clamp(Finite(ReverseSpeed, 1.3f), 0.0f, 3.0f);
    // S first decelerates forward movement and then backs up slowly. Releasing
    // the reins coasts to a stop; this sightseeing demo has no speed latch.
    const float target = _Throttle > 0.0f ? (_Canter ? canter : walk) : _Throttle < 0.0f ? -reverse : 0.0f;
    const bool braking = _Speed * target < 0.0f || std::fabs(target) < std::fabs(_Speed);
    const float rate = std::clamp(Finite(braking ? Braking : Acceleration, braking ? 5.5f : 3.5f), 0.1f, 30.0f);
    _Speed = Approach(_Speed, target, rate * step);
    const float turn = std::clamp(Finite(TurnSpeed, 70.0f), 5.0f, 180.0f);
    const float speedRatio = std::min(std::fabs(_Speed) / canter, 1.0f);
    const float turnScale = std::fabs(_Speed) < 0.1f ? 0.3f : 1.0f - speedRatio * 0.45f;
    _Heading = std::remainder(_Heading + _Steer * turn * turnScale * step * (_Speed < -0.1f ? -1.0f : 1.0f), 360.0f);
    const float yaw = GameSproutRuntime::DegreesToRadians(_Heading);
    const GameSproutRuntime::Vector3 forward{std::sin(yaw), 0.0f, std::cos(yaw)};
    if (_Dynamic && Body && Body->IsEnabled() && Body->GetMotionType() == GameSproutRuntime::PhysicsMotionType::Dynamic) {
        DriveBody(step, forward * _Speed);
    } else if (!Body || !Body->IsEnabled()) {
        _Grounded = false;
        MoveWithoutBody(step, forward);
    }
}

void HorseRider::Update(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) {
    if (!runtime || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
    PoseVisual(deltaSeconds);
    Animate(deltaSeconds);
}

void HorseRider::LateUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) {
    if (runtime && std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) PlaceCamera(deltaSeconds, false);
}

void HorseRider::OnRelease(GameSproutRuntime::IGameSproutRuntime*) {
    if (Animator) Animator->StopClip();
    _Throttle = _Steer = _Speed = 0.0f;
    _OrbitHeld = _HasCamera = false;
}

float HorseRider::Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
float HorseRider::Approach(float current, float target, float distance) {
    return current < target ? std::min(current + distance, target) : std::max(current - distance, target);
}
GameSproutRuntime::Vector3 HorseRider::Position(GameSproutRuntime::IGameObject* object) {
    if (!object) return {};
    const auto matrix = object->GetTransform()->GetWorldMatrix();
    return {matrix(0,3), matrix(1,3), matrix(2,3)};
}
GameSproutRuntime::Vector3 HorseRider::Point(const GameSproutRuntime::Mat4& matrix, GameSproutRuntime::Vector3 point) {
    const auto result = matrix * GameSproutRuntime::Vector4{point.X, point.Y, point.Z, 1.0f};
    return {result.X, result.Y, result.Z};
}
GameSproutRuntime::Quaternion HorseRider::Multiply(GameSproutRuntime::Quaternion a, GameSproutRuntime::Quaternion b) {
    return {a.W*b.X+a.X*b.W+a.Y*b.Z-a.Z*b.Y, a.W*b.Y-a.X*b.Z+a.Y*b.W+a.Z*b.X,
        a.W*b.Z+a.X*b.Y-a.Y*b.X+a.Z*b.W, a.W*b.W-a.X*b.X-a.Y*b.Y-a.Z*b.Z};
}
bool HorseRider::WriteWorldPosition(GameSproutRuntime::IGameObject* object, GameSproutRuntime::Vector3 position) {
    if (!object || object->GetTransform()->IsAnimationDriven()) return false;
    GameSproutRuntime::Mat4 inverse;
    if (auto* parent = object->Parent())
        if (!GameSproutRuntime::RuntimeMath::TryInverseAffine(parent->GetTransform()->GetWorldMatrix(), inverse)) return false;
    object->GetTransform()->SetLocalPosition(Point(inverse, position));
    return true;
}
bool HorseRider::Capture(GameSproutRuntime::IGameObject* object, AuthoredPose& pose) {
    if (!object || object->GetTransform()->IsAnimationDriven()) return false;
    if (pose.ObjectID != object->GetInstanceID()) {
        pose.ObjectID = object->GetInstanceID();
        pose.Position = object->GetTransform()->GetLocalPosition();
        pose.Rotation = object->GetTransform()->GetLocalRotation();
    }
    return true;
}

GameSproutRuntime::PhysicsQuerySettings HorseRider::QuerySettings() const {
    GameSproutRuntime::PhysicsQuerySettings query;
    query.CollisionMask = CollisionMask;
    query.IncludeTriggers = false;
    std::vector<GameSproutRuntime::IGameObject*> pending{_GameObject};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        auto* object = pending[index];
        query.IgnoredObjectIDs.push_back(object->GetInstanceID());
        for (std::size_t child = 0; child < object->GetChildCount(); ++child)
            if (auto* value = object->GetChild(child)) pending.push_back(value);
    }
    if (Camera) query.IgnoredObjectIDs.push_back(Camera->GetInstanceID());
    return query;
}

bool HorseRider::Ground(GameSproutRuntime::Vector3 position, GameSproutRuntime::PhysicsRaycastHit& hit, float above, float below) const {
    std::string error;
    if (!_Runtime->Raycast(_GameObject->GetScene(), {position + GameSproutRuntime::Vector3{0.0f, above, 0.0f}, {0.0f, -1.0f, 0.0f}},
        above + below, hit, QuerySettings(), error)) return false;
    return hit.Normal.Y >= std::cos(GameSproutRuntime::DegreesToRadians(std::clamp(Finite(MaxSlopeDegrees, 48.0f), 1.0f, 75.0f)));
}

void HorseRider::DriveBody(float step, GameSproutRuntime::Vector3 horizontalVelocity) {
    using namespace GameSproutRuntime;
    auto velocity = Body->GetLinearVelocity();
    velocity.X = horizontalVelocity.X;
    velocity.Z = horizontalVelocity.Z;

    constexpr Vector3 up{0, 1, 0};
    const bool wasGrounded = _Grounded;
    _Grounded = false;
    _GroundNormal = up;
    const auto& collider = Collider->GetColliderSettings();
    const auto shape = Collider->GetShape();
    const auto world = _GameObject->GetTransform()->GetWorldMatrix();
    const auto capsule = world * RuntimeMath::ComposeTRS(collider.Center, collider.Rotation, {1, 1, 1});
    const Vector3 capsuleUp{capsule(0, 1), capsule(1, 1), capsule(2, 1)};
    Vector3 axis;
    if (!collider.Enabled || collider.IsTrigger || shape.Axis != PhysicsAxis::Y ||
        !capsuleUp.TryNormalize(axis) || std::fabs(axis.Y) < 0.999f) {
        Body->SetLinearVelocity(velocity);
        return;
    }

    const Vector3 capsuleRight{capsule(0, 0), capsule(1, 0), capsule(2, 0)};
    const Vector3 capsuleForward{capsule(0, 2), capsule(1, 2), capsule(2, 2)};
    const float radius = shape.Radius * std::max(capsuleRight.Length(), capsuleForward.Length());
    const float halfHeight = std::max(shape.Height * capsuleUp.Length() * 0.5f, radius);
    const Vector3 sole{capsule(0, 3), capsule(1, 3) - halfHeight, capsule(2, 3)};
    const Vector3 travel = horizontalVelocity * step;
    const float slope = DegreesToRadians(std::clamp(Finite(MaxSlopeDegrees, 48.0f), 1.0f, 75.0f));
    const float minNormalY = std::cos(slope);
    const float snap = std::clamp(Finite(GroundSnapDistance, 0.18f), 0.0f, 0.5f);
    const float travelLength = travel.Length();
    const float stepRise = travelLength * std::tan(slope);
    constexpr float contactTolerance = 0.04f;
    const float probeAbove = stepRise + radius / minNormalY + snap;
    const auto query = QuerySettings();
    const auto probe = [&](Vector3 position, PhysicsRaycastHit& hit) {
        std::string error;
        return _Runtime->Raycast(_GameObject->GetScene(), {position + up * probeAbove, -up},
            probeAbove + stepRise + snap + contactTolerance, hit, query, error);
    };
    const auto supportedSole = [&](const PhysicsRaycastHit& hit) {
        // The spherical capsule end contacts a slope to one side of its centre.
        return hit.Position.Y + radius * (1.0f / hit.Normal.Y - 1.0f);
    };

    PhysicsRaycastHit current;
    if (probe(sole, current) && current.Normal.Y >= minNormalY) {
        const float gap = sole.Y - supportedSole(current);
        const float supportTolerance = wasGrounded ? std::max(contactTolerance, snap) : contactTolerance;
        if (std::fabs(gap) <= supportTolerance && gap >= -contactTolerance) {
            _Grounded = true;
            _GroundNormal = current.Normal;
            PhysicsRaycastHit ahead = current;
            // Drive toward support at the next position so an uphill velocity
            // cannot carry the horse above a nearby flat crest.
            if (travelLength < 0.0001f || probe(sole + travel, ahead)) {
                const float rise = ahead.Position.Y - current.Position.Y;
                const bool uphillWall = ahead.Normal.Y < minNormalY && ahead.Normal.Dot(travel) < 0;
                if (uphillWall || rise > stepRise + contactTolerance) {
                    velocity.X = velocity.Z = 0.0f;
                    velocity.Y = std::min(velocity.Y, 0.0f);
                } else if (ahead.Normal.Y >= minNormalY && rise >= -stepRise - snap) {
                    const float targetGap = supportedSole(ahead) - sole.Y;
                    velocity.Y = std::clamp(targetGap, -stepRise - snap, stepRise + contactTolerance) / step;
                } else {
                    // No nearby walkable continuation. Keep the physical
                    // vertical velocity so a genuine ledge can be ridden off.
                    _Grounded = false;
                }
            } else {
                _Grounded = false;
            }
        }
    }
    if (!_Grounded) _GroundNormal = up;
    Body->SetLinearVelocity(velocity);
}

void HorseRider::MoveWithoutBody(float deltaSeconds, GameSproutRuntime::Vector3 forward) {
    const auto start = Position(_GameObject);
    auto displacement = forward * (_Speed * deltaSeconds);
    const float length = displacement.Length();
    if (length > 0.0001f) {
        const auto direction = displacement * (1.0f / length);
        const auto side = GameSproutRuntime::Vector3{direction.Z, 0.0f, -direction.X} * 0.35f;
        const GameSproutRuntime::Vector3 offsets[] = {{}, side, -side};
        float allowed = length;
        for (float height : {0.45f, 1.2f}) for (const auto offset : offsets) {
            GameSproutRuntime::PhysicsRaycastHit hit;
            std::string error;
            if (_Runtime->Raycast(_GameObject->GetScene(), {start + offset + GameSproutRuntime::Vector3{0.0f,height,0.0f}, direction},
                length + 0.45f, hit, QuerySettings(), error)) {
                if (hit.Normal.Y < 0.65f) allowed = std::min(allowed, std::max(0.0f, hit.Distance - 0.45f));
            } else if (!error.empty()) return;
        }
        displacement = direction * allowed;
        if (allowed + 0.001f < length) _Speed = 0.0f;
    }
    auto candidate = start + displacement;
    GameSproutRuntime::PhysicsRaycastHit ground;
    // No blind Y=0 fallback: require real walkable collision geometry and
    // refuse cliffs/steps beyond the ray follower's modest 0.55m allowance.
    if (!Ground(candidate, ground, 1.5f, 2.0f) || std::fabs(ground.Position.Y - start.Y) > 0.55f) {
        _Speed = 0.0f;
        return;
    }
    candidate.Y = ground.Position.Y;
    _GroundNormal = ground.Normal;
    WriteWorldPosition(_GameObject, candidate);
}

void HorseRider::PoseVisual(float deltaSeconds) {
    if (!Visual || Visual == _GameObject || Visual->GetScene() != _GameObject->GetScene() || !Capture(Visual, _VisualPose)) return;
    const float yaw = GameSproutRuntime::DegreesToRadians(_Heading);
    const GameSproutRuntime::Vector3 forward{std::sin(yaw), 0.0f, std::cos(yaw)};
    const float normalBlend = deltaSeconds > 0.0f ? 1.0f - std::exp(-12.0f * deltaSeconds) : 1.0f;
    const auto blendedNormal = _VisualGroundNormal + (_GroundNormal - _VisualGroundNormal) * normalBlend;
    blendedNormal.TryNormalize(_VisualGroundNormal);
    auto slopeForward = forward;
    // Keep the visual heading aligned with XZ travel on cross slopes.
    if (_VisualGroundNormal.Y > 0.1f)
        slopeForward.Y = -forward.Dot(_VisualGroundNormal) / _VisualGroundNormal.Y;
    if (!slopeForward.TryNormalize(slopeForward)) slopeForward = forward;
    GameSproutRuntime::Mat4 inverse;
    if (auto* parent = Visual->Parent())
        if (!GameSproutRuntime::RuntimeMath::TryInverseAffine(parent->GetTransform()->GetWorldMatrix(), inverse)) return;
    const auto localForward = inverse * GameSproutRuntime::Vector4{slopeForward.X,slopeForward.Y,slopeForward.Z,0.0f};
    const auto localUp = inverse * GameSproutRuntime::Vector4{_VisualGroundNormal.X,_VisualGroundNormal.Y,_VisualGroundNormal.Z,0.0f};
    GameSproutRuntime::Quaternion rotation;
    if (GameSproutRuntime::RuntimeMath::TryMakeLookRotation({localForward.X,localForward.Y,localForward.Z}, {localUp.X,localUp.Y,localUp.Z}, rotation))
        Visual->GetTransform()->SetLocalRotation(Multiply(rotation, _VisualPose.Rotation));
    if (deltaSeconds <= 0.0f) return;
    // A real animation clip owns its skeleton; fallback bobbing is used only
    // for an unrigged placeholder, never superimposed on animated bones.
    if (!Animator || _PlayingClip.Guid.empty()) {
        auto position = _VisualPose.Position;
        position.Y += std::sin(_GaitPhase * 2.0f) * std::min(std::fabs(_Speed) / 4.0f, 1.0f) * 0.045f;
        Visual->GetTransform()->SetLocalPosition(position);
    }
}

void HorseRider::PoseLeg(GameSproutRuntime::IGameObject* object, AuthoredPose& pose, float angle) {
    if (!Capture(object, pose)) return;
    object->GetTransform()->SetLocalRotation(Multiply(pose.Rotation, {std::sin(angle * 0.5f),0.0f,0.0f,std::cos(angle * 0.5f)}));
}

void HorseRider::Animate(float deltaSeconds) {
    using namespace GameSproutRuntime;
    const auto presented = _GameObject->GetTransform()->GetPresentationWorldMatrix();
    const Vector3 position{presented(0,3),presented(1,3),presented(2,3)};
    const auto displacement = _HasGaitPosition ? position - _LastGaitPosition : Vector3{};
    _LastGaitPosition = position;
    _HasGaitPosition = true;
    const auto yaw = DegreesToRadians(_Heading);
    Vector3 tangent{std::sin(yaw),0,std::cos(yaw)};
    if (_GroundNormal.Y > 0.1f) tangent.Y = -tangent.Dot(_GroundNormal) / _GroundNormal.Y;
    Vector3 direction;
    const float maxTravel = std::max(0.0f, deltaSeconds) * std::max(1.0f, Finite(CanterSpeed,7.5f)) * 2.5f;
    // Read presented displacement to follow actual motion, including a blocked
    // body and frames without a physics tick. Ignore teleports and airborne drift.
    const float distance = (_Grounded || !_Dynamic) && displacement.Length() <= maxTravel &&
        tangent.TryNormalize(direction) ? displacement.Dot(direction) : 0.f;
    const float speed = deltaSeconds > 0 ? std::fabs(distance) / deltaSeconds : 0;
    _StillTime = speed > 0.12f ? 0.0f : _StillTime + deltaSeconds;
    ResourceReference clip;
    if (speed < 0.12f && (_StillTime >= .09f || std::fabs(_Speed) < .12f)) clip = IdleClip;
    else if (distance >= 0 && speed > std::max(0.3f, Finite(WalkSpeed, 3.6f)) * 1.2f && !CanterClip.Guid.empty()) clip = CanterClip;
    else if (speed < 0.12f && !_PlayingClip.Guid.empty()) clip = _PlayingClip;
    else clip = WalkClip;
    if (Animator) {
        if (clip.Guid.empty()) clip = !IdleClip.Guid.empty() ? IdleClip : Animator->GetDefaultClip();
        if (!clip.Guid.empty()) {
            const auto previous = Animator->GetClipPlaybackState();
            const bool moving = clip == WalkClip || clip == CanterClip;
            const bool switched = _PlayingClip != clip || !previous.Active;
            const bool previousMoving = _PlayingClip == WalkClip || _PlayingClip == CanterClip;
            const auto phase = previousMoving && previous.Duration > 0 ? previous.Time / previous.Duration : 0;
            if (switched && Animator->PlayClip(clip,0.0f,moving ? 0.0f : 1.0f,true)) _PlayingClip = clip;
            if (moving && _PlayingClip == clip) {
                const auto playback = Animator->GetClipPlaybackState();
                const float nativeSpeed = clip == CanterClip ? std::max(.01f,Finite(CanterClipSpeed,4.8f)) :
                    std::max(.01f,Finite(WalkClipSpeed,1.312f));
                if (playback.Duration > 0 && (switched || std::fabs(distance) > 1e-6f)) {
                    const float time = (switched ? phase * playback.Duration : playback.Time) + distance / nativeSpeed;
                    const auto wrapped = time - std::floor(time / playback.Duration) * playback.Duration;
                    Animator->SeekClip(wrapped);
                }
            }
            return;
        }
    }
    const float rate = speed < 0.1f ? 0.0f : 2.5f + speed * 0.9f;
    _GaitPhase = std::remainder(_GaitPhase + rate * deltaSeconds, 6.28318530718f);
    const float amplitude = std::min(speed / 3.6f, 1.0f) * 0.48f;
    const float swing = std::sin(_GaitPhase) * amplitude;
    PoseLeg(FrontLeftLeg, _LegPoses[0], swing); PoseLeg(BackRightLeg, _LegPoses[3], swing);
    PoseLeg(FrontRightLeg, _LegPoses[1], -swing); PoseLeg(BackLeftLeg, _LegPoses[2], -swing);
    if (Capture(RiderPivot, _RiderPose)) {
        auto position = _RiderPose.Position;
        position.Y += std::sin(_GaitPhase * 2.0f) * std::min(speed / 4.0f, 1.0f) * 0.035f;
        RiderPivot->GetTransform()->SetLocalPosition(position);
    }
}

void HorseRider::PlaceCamera(float deltaSeconds, bool snap) {
    if (!Camera || Camera == _GameObject || Camera->GetScene() != _GameObject->GetScene()) return;
    auto* component = Camera->GetComponent<GameSproutRuntime::ICameraComponent>();
    if (!component || Camera->GetTransform()->IsAnimationDriven()) return;
    // After sightseeing with RMB, gently settle behind a moving horse.
    if (!_OrbitHeld && _OrbitIdle > 2.5f && std::fabs(_Speed) > 0.5f) {
        const float difference = std::remainder(_Heading - _CameraYaw, 360.0f);
        _CameraYaw += difference * (1.0f - std::exp(-1.5f * deltaSeconds));
    }
    const auto presented = _GameObject->GetTransform()->GetPresentationWorldMatrix();
    const GameSproutRuntime::Vector3 focus{presented(0,3),presented(1,3) + Finite(CameraHeight,1.65f),presented(2,3)};
    const float yaw = GameSproutRuntime::DegreesToRadians(_CameraYaw);
    const float pitch = GameSproutRuntime::DegreesToRadians(std::clamp(Finite(CameraPitch,15.0f),-12.0f,58.0f));
    const GameSproutRuntime::Vector3 forward{std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const auto desired = focus - forward * std::clamp(Finite(CameraDistance,4.4f),2.2f,9.0f);
    const float sharpness = std::clamp(Finite(CameraSharpness,10.0f),0.0f,40.0f);
    const float blend = snap || !_HasCamera || sharpness == 0.0f ? 1.0f : 1.0f - std::exp(-sharpness * deltaSeconds);
    const auto candidate = _HasCamera ? _CameraPosition + (desired - _CameraPosition) * blend : desired;
    const auto offset = candidate - focus;
    GameSproutRuntime::Vector3 back;
    if (!offset.TryNormalize(back)) back = -forward;
    float allowed = offset.Length();
    GameSproutRuntime::Vector3 right;
    if (!GameSproutRuntime::Vector3{0,1,0}.Cross(-back).TryNormalize(right)) right = {1,0,0};
    const auto up = (-back).Cross(right);
    const float padding = std::max(0.2f, component->GetSettings().NearPlane);
    const GameSproutRuntime::Vector3 ring[] = {{},right*padding,-right*padding,up*padding,-up*padding};
    const auto query = QuerySettings();
    for (const auto shift : ring) {
        GameSproutRuntime::PhysicsRaycastHit hit;
        std::string error;
        if (_Runtime->Raycast(_GameObject->GetScene(), {focus+shift,back}, offset.Length()+padding, hit, query, error))
            allowed = std::min(allowed,std::max(0.0f,hit.Distance-padding));
        else if (!error.empty()) return;
    }
    const auto position = focus + back * allowed;
    if (!WriteWorldPosition(Camera,position)) return;
    if (!component->LookAt(allowed > 0.001f ? focus : position-back)) return;
    _CameraPosition = position;
    _HasCamera = true;
}
