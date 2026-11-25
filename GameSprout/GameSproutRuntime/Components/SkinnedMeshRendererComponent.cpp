#include "SkinnedMeshRendererComponent.h"
#include "ComponentFactory.h"
#include "GameObject.h"
#include "RuntimeMath.h"
#include "Scene.h"
#include <algorithm>
#include <cstring>
namespace GameSproutRuntime {
namespace {
GameObjectState* OwnerState(IGameObject* owner) {
    const auto* access=dynamic_cast<const ObjectAccess*>(owner);
    return access?&access->State():nullptr;
}
}
SkinnedMeshRendererComponent::SkinnedMeshRendererComponent(IGameObject* object)
    : BuiltinComponent<ISkinnedMeshRendererComponent>(object,"GameSproutRuntime.SkinnedMeshRenderer"),_OwnerState(OwnerState(object)) {}
bool SkinnedMeshRendererComponent::SetAnimatorObjectID(std::uint64_t id){
    if(!_OwnerState)return false;
    auto& runtime=_OwnerState->Owner;
    if(id){auto* state=runtime.Find(id);if(!runtime.IsUsable(state)||state->GetScene()!=_GameObject->GetScene()||!state->Object->GetComponent<IAnimatorComponent>())return false;}
    if(_AnimatorID==id)return true;
    runtime.SetSkinAnimatorReference(_GameObject->GetInstanceID(),_AnimatorID,id);
    _AnimatorID=id;
    NotifyComponentChanged(_GameObject,this);
    return true;
}
bool SkinnedMeshRendererComponent::SetSkinPose(std::span<const Mat4> matrices) {
    if(matrices.size()>128||!_OwnerState||!_OwnerState->Owner.IsUsable(_OwnerState))return false;
    if(matrices.size()==_SkinPose.size()&&
       (matrices.empty()||!std::memcmp(matrices.data(),_SkinPose.data(),matrices.size_bytes())))return true;
    if(!std::all_of(matrices.begin(),matrices.end(),
        [](const Mat4& matrix){return RuntimeMath::IsFinite(matrix);}))return false;
    _SkinPose.assign(matrices.begin(),matrices.end());
    ++_SkinPoseRevision;
    if(auto* scene=_OwnerState->SceneOwner)scene->InvalidateSkinPose();
    return true;
}
bool SkinnedMeshRendererComponent::GetSkinMatrices(const std::vector<SkinBinding>& bones,std::vector<Mat4>& output,std::string& error) const {
    return BuildSkinMatrices(bones,output,error,false);
}
bool SkinnedMeshRendererComponent::GetPresentationSkinMatrices(const std::vector<SkinBinding>& bones,std::vector<Mat4>& output,std::string& error) const {
    return BuildSkinMatrices(bones,output,error,true);
}
bool SkinnedMeshRendererComponent::BuildSkinMatrices(const std::vector<SkinBinding>& bones,std::vector<Mat4>& output,std::string& error,bool presentation) const {
    output.clear();error.clear();
    if(!_OwnerState){error="SkinnedMeshRenderer has no live Runtime owner";return false;}
    auto& runtime=_OwnerState->Owner;
    if(!runtime.IsInScene(_OwnerState)){error="SkinnedMeshRenderer is not attached to a live scene";return false;}
    if(!_SkinPose.empty()) {
        if(_SkinPose.size()!=bones.size()){error="Runtime skin pose does not match the mesh bone count";return false;}
        output=_SkinPose;
        return true;
    }
    auto* state=runtime.Find(_AnimatorID);
    auto* animator=runtime.IsInScene(state)&&state->GetScene()==_GameObject->GetScene()?state->Object->GetComponent<IAnimatorComponent>():nullptr;
    if(!animator){error="SkinnedMeshRenderer Animator binding is missing";return false;}
    const auto worldMatrix=[presentation](const IGameObject& object) {
        const auto* transform=object.GetTransform();
        return presentation?transform->GetPresentationWorldMatrix():transform->GetWorldMatrix();
    };
    Mat4 inverse;if(!RuntimeMath::TryInverseAffine(worldMatrix(*_GameObject),inverse)){error="SkinnedMeshRenderer transform is singular";return false;}
    output.reserve(bones.size());
    for(const auto& bone:bones){std::uint64_t id=0;if(!animator->ResolveBone(bone.NodePath,id)){output.clear();error="Skin bone is missing: "+bone.NodePath;return false;}
        auto* node=runtime.Find(id);if(!runtime.IsUsable(node)){output.clear();error="Skin bone was destroyed";return false;}
        const Mat4 matrix=inverse*worldMatrix(*node->Object)*bone.InverseBindMatrix;
        if(!RuntimeMath::IsFinite(matrix)){output.clear();error="Skin matrix is not finite";return false;}output.push_back(matrix);
    }return true;
}
}
