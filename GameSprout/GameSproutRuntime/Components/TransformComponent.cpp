#include "TransformComponent.h"
#include "ComponentFactory.h"
#include "RuntimeMath.h"
#include "GameObject.h"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <vector>
#if defined(_WIN32) && (defined(_M_X64) || defined(__SSE2__))
#include <emmintrin.h>
#endif

namespace {
GameSproutRuntime::Mat4 MultiplyWorldMatrix(const GameSproutRuntime::Mat4& parent,
    const GameSproutRuntime::Mat4& local) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__SSE2__))
    const auto parentX=_mm_loadu_ps(parent.Data);
    const auto parentY=_mm_loadu_ps(parent.Data+4);
    const auto parentZ=_mm_loadu_ps(parent.Data+8);
    const auto parentW=_mm_loadu_ps(parent.Data+12);
    GameSproutRuntime::Mat4 world;
    for(std::size_t i=0;i<16;i+=4) {
        auto column=_mm_add_ps(_mm_setzero_ps(),_mm_mul_ps(parentX,_mm_set1_ps(local.Data[i])));
        column=_mm_add_ps(column,_mm_mul_ps(parentY,_mm_set1_ps(local.Data[i+1])));
        column=_mm_add_ps(column,_mm_mul_ps(parentZ,_mm_set1_ps(local.Data[i+2])));
        column=_mm_add_ps(column,_mm_mul_ps(parentW,_mm_set1_ps(local.Data[i+3])));
        _mm_storeu_ps(world.Data+i,column);
    }
    return world;
#else
    return parent*local;
#endif
}

bool UnitRotation(GameSproutRuntime::Quaternion value,GameSproutRuntime::Quaternion& unit) {
    const double squared=double(value.X)*value.X+double(value.Y)*value.Y+double(value.Z)*value.Z+double(value.W)*value.W;
    // Float单位四元数本身有舍入误差。保留此范围内的分量，使getter值写回/撤销恢复精确幂等。
    if(std::isfinite(squared) && std::fabs(squared-1.0)<=8.0*std::numeric_limits<float>::epsilon()) unit=value;
    else if(!value.TryNormalize(unit)) return false;
    // q/-q规范为同一表示，包括W==0的180度旋转。
    if(unit.W<0 || (unit.W==0 && (unit.Z<0 || (unit.Z==0 && (unit.Y<0 || (unit.Y==0 && unit.X<0))))))
        unit={-unit.X,-unit.Y,-unit.Z,-unit.W};
    return true;
}
}

namespace GameSproutRuntime {
TransformComponent::TransformComponent(IGameObject* owner)
    : BuiltinComponent<ITransformComponent>(owner, TypeName) {
    if(auto* access=dynamic_cast<ObjectAccess*>(owner))_OwnerState=&access->State();
}
TransformComponent::~TransformComponent() {
    if(_OwnerState&&_HasPresentationPhysicsWorld)_OwnerState->Owner.TrackPresentationOverride(false);
}

void TransformComponent::InvalidateLocalMatrix() {
    _LocalMatrixKnown=false;
    _WorldMatrixChangeEpoch=0;
    if (_OwnerState) {
        _OwnerState->Owner.InvalidateTransforms();
        _OwnerState->Owner.InvalidatePhysicsTransform(*_OwnerState);
    }
}

bool TransformComponent::IsSceneRoot() const {
    if(_OwnerState&&!_OwnerState->IsSceneRoot())return false;
    return _GameObject&&static_cast<IGameObject*>(_GameObject->GetScene())==_GameObject;
}

void TransformComponent::NotifyAuthoringChange() {
    if(_OwnerState)
        _OwnerState->Owner.NotifyTransformChange(*_OwnerState);
}

void TransformComponent::SetLocalPosition(Vector3 value) {
    if (IsSceneRoot() || !RuntimeMath::IsFinite(value)) return;
    const auto current=GetLocalPosition();
    if(value.X==current.X&&value.Y==current.Y&&value.Z==current.Z)return;
    PrepareAuthoringChange();
    _Position = value;
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyAuthoringChange();
}

bool TransformComponent::SetLocalRotation(Quaternion value) {
    Quaternion unit;
    if (!UnitRotation(value,unit)) return false;
    if (IsSceneRoot()) return unit.X == 0.0f && unit.Y == 0.0f && unit.Z == 0.0f;
    const auto current=GetLocalRotation();
    if (unit.X == current.X && unit.Y == current.Y && unit.Z == current.Z && unit.W == current.W) return true;
    PrepareAuthoringChange();
    _Rotation = unit;
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyAuthoringChange();
    return true;
}

void TransformComponent::SetLocalScale(Vector3 value) {
    if (IsSceneRoot() || !RuntimeMath::IsFinite(value) ||
        (value.X == _Scale.X && value.Y == _Scale.Y && value.Z == _Scale.Z)) return;
    PrepareAuthoringChange();
    _Scale = value;
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyAuthoringChange();
}

Mat4 TransformComponent::GetLocalMatrix() const {
    if(!_LocalMatrixKnown) {
        if(_HasPhysicsPose&&_PhysicsDynamic)_LocalMatrixCache=_PhysicsMatrix;
        else if(_PoseOwner)_LocalMatrixCache=_EvaluatedMatrix;
        else if(_HasPhysicsPose)_LocalMatrixCache=_PhysicsMatrix;
        else _LocalMatrixCache=RuntimeMath::ComposeTRS(_Position,_Rotation,_Scale);
        _LocalMatrixKnown=true;
    }
    return _LocalMatrixCache;
}

bool TransformComponent::GetCurrentLocalRotation(Quaternion& output) const {
    if (_PoseOwner && !(_HasPhysicsPose && _PhysicsDynamic)) {
        output=_EvaluatedRotation;
        return true;
    }
    output=GetLocalRotation();
    return true;
}
bool TransformComponent::GetCurrentWorldRotation(const IGameObject& object,Mat4& output,bool presentation) {
    Mat4 result;
    for(const IGameObject* node=&object;node;node=node->Parent()) {
        auto* local=node->GetTransform();
        if(!local)return false;
        Quaternion rotation;
        const auto* transform=dynamic_cast<const TransformComponent*>(local);
        if(presentation&&transform&&transform->_HasPresentationPhysicsWorld) {
            result=RuntimeMath::ComposeTRS({},transform->_PhysicsPresentation->Rotation,{1,1,1})*result;
            break;
        }
        if(transform) {
            if(!transform->GetCurrentLocalRotation(rotation))return false;
            // A separately simulated child body has an independent world pose.
            if(transform->_HasPhysicsPose)presentation=false;
        }else rotation=local->GetLocalRotation();
        result=RuntimeMath::ComposeTRS({},rotation,{1,1,1})*result;
    }
    output=result;
    return true;
}

bool TransformComponent::SetEvaluatedPose(std::uint64_t animatorID,const Mat4& matrix,Quaternion rotation) {
    Quaternion unit;
    if(!animatorID || IsSceneRoot() || (_HasPhysicsPose&&_PhysicsDynamic) || !RuntimeMath::IsFinite(matrix) ||
       (_PoseOwner && _PoseOwner!=animatorID) || !UnitRotation(rotation,unit)) return false;
    bool changed=_PoseOwner!=animatorID || unit.X!=_EvaluatedRotation.X || unit.Y!=_EvaluatedRotation.Y ||
        unit.Z!=_EvaluatedRotation.Z || unit.W!=_EvaluatedRotation.W;
    for(unsigned index=0;index<16&&!changed;++index)changed=_EvaluatedMatrix.Data[index]!=matrix.Data[index];
    _EvaluatedMatrix=matrix;_EvaluatedRotation=unit;_PoseOwner=animatorID;
    SetPresentationPhysicsPose(nullptr);
    if(changed){InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}
    return true;
}
void TransformComponent::ClearEvaluatedPose(std::uint64_t animatorID) {if(_PoseOwner==animatorID){_PoseOwner=0;InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}}

Mat4 TransformComponent::GetWorldMatrix() const {
    if(!_OwnerState) {
        Mat4 world=GetLocalMatrix();
        for(IGameObject* parent=_GameObject->Parent();parent;parent=parent->Parent())
            if(auto* transform=parent->GetTransform())world=transform->GetLocalMatrix()*world;
        return world;
    }
    return ResolveBuiltinWorldMatrix();
}
const Mat4& TransformComponent::ResolveBuiltinWorldMatrix() const {
    const auto epoch=_OwnerState->Owner.GetTransformEpoch();
    if(_WorldMatrixEpoch==epoch)return _WorldMatrixCache;
    if(const auto* parent=_OwnerState->Parent) {
        if(const auto* transform=parent->BuiltinTransform;transform&&transform->_WorldMatrixEpoch==epoch)
            return ResolveWorldFromParent(transform->_WorldMatrixCache,transform->_WorldMatrixChangeEpoch,epoch);
    } else return ResolveWorldFromParent(Mat4{},0,epoch);
    // A new epoch asks for verification. Only a local change or a changed
    // parent matrix requires recomputing the cached result.
    std::array<const TransformComponent*,16> chain;
    std::optional<std::vector<const TransformComponent*>> deepChain;
    std::size_t count=0;
    const TransformComponent* current=this;
    Mat4 world;
    std::uint64_t parentChange=0;
    while(current) {
        if(current->_WorldMatrixEpoch==epoch) {
            world=current->_WorldMatrixCache;
            parentChange=current->_WorldMatrixChangeEpoch;
            break;
        }
        if(count<chain.size())chain[count]=current;
        else {
            if(!deepChain)deepChain.emplace();
            deepChain->push_back(current);
        }
        ++count;
        const auto* parent=current->_OwnerState->Parent;
        current=parent?parent->BuiltinTransform:nullptr;
        if(parent&&!current) {
            if(const auto* transform=parent->Object->GetTransform()) {
                world=transform->GetWorldMatrix();
                parentChange=epoch;
            }
        }
    }
    while(count>0) {
        --count;
        const auto* item=count<chain.size()?chain[count]:(*deepChain)[count-chain.size()];
        world=item->ResolveWorldFromParent(world,parentChange,epoch);
        parentChange=item->_WorldMatrixChangeEpoch;
    }
    return _WorldMatrixCache;
}
const Mat4& TransformComponent::ResolveWorldFromParent(const Mat4& world,std::uint64_t parentChange,std::uint64_t epoch) const {
    if(!_WorldMatrixChangeEpoch||_ParentMatrixChangeEpoch!=parentChange) {
        _WorldMatrixCache=MultiplyWorldMatrix(world,GetLocalMatrix());
        _WorldMatrixChangeEpoch=epoch;
        _ParentMatrixChangeEpoch=parentChange;
    }
    _WorldMatrixEpoch=epoch;
    return _WorldMatrixCache;
}
Mat4 TransformComponent::GetPresentationWorldMatrix() const {
    if(_HasPresentationPhysicsWorld)return _PhysicsPresentation->World;
    if(!_OwnerState||!_OwnerState->Owner.HasPresentationOverrides())return GetWorldMatrix();
    // Only a branch below an opted-in body needs a separate matrix. Reuse
    // the usual world cache for unrelated branches and independent bodies.
    auto* node=this;
    while(node&&!node->_HasPresentationPhysicsWorld) {
        if(node->_HasPhysicsPose)return GetWorldMatrix();
        const auto* parent=node->_OwnerState->Parent;
        node=parent?parent->BuiltinTransform:nullptr;
        if(parent&&!node)
            node=dynamic_cast<const TransformComponent*>(parent->Object->GetTransform());
    }
    if(!node)return GetWorldMatrix();
    Mat4 local;
    for(const auto* child=_OwnerState;child!=node->_OwnerState;child=child->Parent) {
        const auto* native=child->BuiltinTransform;
        local=(native?native->GetLocalMatrix():child->Object->GetTransform()->GetLocalMatrix())*local;
    }
    return node->_PhysicsPresentation->World*local;
}

bool TransformComponent::SetLocalMatrix(const Mat4& value) {
    if (IsSceneRoot()) return false;
    Vector3 position, scale;
    Quaternion rotation;
    if (!RuntimeMath::TryDecomposeTRS(value, position, rotation, scale)) return false;
    Quaternion unit;
    if(!UnitRotation(rotation,unit)) return false;
    _Position = position;
    _Rotation = unit;
    _Scale = scale;
    _HasPhysicsPose=false;
    SetPresentationPhysicsPose(nullptr);
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyAuthoringChange();
    return true;
}
void TransformComponent::PrepareAuthoringChange() {
    if(_HasPresentationPhysicsWorld)SetPresentationPhysicsPose(nullptr);
    if(!_HasPhysicsPose)return;
    _Position=_PhysicsPosition;
    _Rotation=_PhysicsRotation;
    _HasPhysicsPose=false;
}
bool TransformComponent::SetPhysicsPose(const Mat4& local,bool dynamic) {
    Vector3 position,scale;Quaternion rotation;
    if(IsSceneRoot()||!RuntimeMath::TryDecomposeTRS(local,position,rotation,scale))return false;
    bool changed=!_HasPhysicsPose||_PhysicsDynamic!=dynamic;
    for(unsigned index=0;index<16&&!changed;++index)changed=_PhysicsMatrix.Data[index]!=local.Data[index];
    _PhysicsPosition=position;_PhysicsRotation=rotation;_PhysicsMatrix=local;
    _HasPhysicsPose=true;_PhysicsDynamic=dynamic;
    if(changed){InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}
    return true;
}
void TransformComponent::ClearPhysicsPose(){
    const bool changed=_HasPhysicsPose;
    _HasPhysicsPose=false;_PhysicsDynamic=false;
    SetPresentationPhysicsPose(nullptr);
    if(changed){InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}
}
void TransformComponent::SetPresentationPhysicsPose(const Mat4* world,Quaternion rotation) {
    const bool available=world!=nullptr;
    if(_HasPresentationPhysicsWorld==available&&
       (!available||std::equal(std::begin(world->Data),std::end(world->Data),std::begin(_PhysicsPresentation->World.Data))))return;
    if(available) {
        if(!_PhysicsPresentation)_PhysicsPresentation=std::make_unique<PresentationPhysicsPose>();
        *_PhysicsPresentation={*world,rotation};
    }
    if(_OwnerState&&_HasPresentationPhysicsWorld!=available)_OwnerState->Owner.TrackPresentationOverride(available);
    _HasPresentationPhysicsWorld=available;
    NotifyRuntimePropertiesChanged(_GameObject,false);
}
}
