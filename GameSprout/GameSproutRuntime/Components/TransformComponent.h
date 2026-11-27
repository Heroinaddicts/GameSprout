#pragma once
#include "BuiltinComponent.h"
#include <memory>

namespace GameSproutRuntime {
class GameObjectState;
class Runtime;
class TransformComponent final : public BuiltinComponent<ITransformComponent> {
public:
    explicit TransformComponent(IGameObject* owner);
    ~TransformComponent() override;
    Vector3 GetLocalPosition() const override { return _HasPhysicsPose ? _PhysicsPosition : _Position; }
    void SetLocalPosition(Vector3 value) override;
    Quaternion GetLocalRotation() const override { return _HasPhysicsPose ? _PhysicsRotation : _Rotation; }
    bool SetLocalRotation(Quaternion value) override;
    Vector3 GetLocalScale() const override { return _Scale; }
    void SetLocalScale(Vector3 value) override;
    bool IsAnimationDriven() const override {return _PoseOwner!=0;}
    Mat4 GetLocalMatrix() const override;
    Mat4 GetWorldMatrix() const override;
    Mat4 GetPresentationWorldMatrix() const override;
    bool SetLocalMatrix(const Mat4& value);
    // Evaluation changes the displayed pose, never the serialized authoring TRS.
    // Keep its actual rotation separately: signed/zero scale and root-motion
    // shear make the original quaternion impossible to recover from the matrix.
    bool SetEvaluatedPose(std::uint64_t animatorID, const Mat4& matrix, Quaternion rotation);
    bool CanSetEvaluatedPose(std::uint64_t animatorID) const {return animatorID&&!IsSceneRoot()&&!(_HasPhysicsPose&&_PhysicsDynamic)&&(!_PoseOwner||_PoseOwner==animatorID);}
    void ClearEvaluatedPose(std::uint64_t animatorID);
    // Simulation poses are visible to Runtime/scripts but never emit authoring
    // ComponentChanged events. Resetting gameplay restores the authored TRS.
    bool SetPhysicsPose(const Mat4& local,bool dynamic);
    void ClearPhysicsPose();
    void SetPresentationPhysicsPose(const Mat4* world, Quaternion rotation={});
    std::uint64_t GetAuthoringRevision() const { return _AuthoringRevision; }
    // Accumulated animation deltas use the authored baseline, not a physics
    // result that already contains the previous frame's animation movement.
    Vector3 GetAuthoringLocalPosition() const { return _Position; }
    Quaternion GetAuthoringLocalRotation() const { return _Rotation; }
    // Orientation of the effective pose, without authored scale/reflection.
    bool GetCurrentLocalRotation(Quaternion& output) const;
    static bool GetCurrentWorldRotation(const IGameObject&,Mat4& output,bool presentation);

private:
    static constexpr char TypeName[]="GameSproutRuntime.Transform";
    bool IsSceneRoot() const;
    void PrepareAuthoringChange();
    void InvalidateLocalMatrix();
    void NotifyAuthoringChange();
    const Mat4& ResolveBuiltinWorldMatrix() const;
    const Mat4& ResolveWorldFromParent(const Mat4& world,std::uint64_t parentChange,std::uint64_t epoch) const;
    GameObjectState* _OwnerState=nullptr;
    mutable bool _LocalMatrixKnown=false;
    mutable Mat4 _LocalMatrixCache,_WorldMatrixCache;
    mutable std::uint64_t _WorldMatrixEpoch=0;
    mutable std::uint64_t _WorldMatrixChangeEpoch=0;
    mutable std::uint64_t _ParentMatrixChangeEpoch=0;
    Vector3 _Position;
    Quaternion _Rotation;
    Vector3 _Scale{1.0f, 1.0f, 1.0f};
    std::uint64_t _PoseOwner = 0;
    Mat4 _EvaluatedMatrix;
    Quaternion _EvaluatedRotation;
    bool _HasPhysicsPose=false,_PhysicsDynamic=false;
    Vector3 _PhysicsPosition;
    Quaternion _PhysicsRotation;
    Mat4 _PhysicsMatrix;
    struct PresentationPhysicsPose {
        Mat4 World;
        Quaternion Rotation;
    };
    std::unique_ptr<PresentationPhysicsPose> _PhysicsPresentation;
    bool _HasPresentationPhysicsWorld=false;
    std::uint64_t _AuthoringRevision=0;
    friend class Runtime;
    friend class Scene;
    friend class Renderer;
};
}
