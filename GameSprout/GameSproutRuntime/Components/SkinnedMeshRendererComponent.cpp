#include "SkinnedMeshRendererComponent.h"
#include "ComponentFactory.h"
#include "GameObject.h"
#include "RuntimeMath.h"
namespace GameSproutRuntime {
SkinnedMeshRendererComponent::SkinnedMeshRendererComponent(IGameObject* object):BuiltinComponent<ISkinnedMeshRendererComponent>(object,"GameSproutRuntime.SkinnedMeshRenderer"){}
bool SkinnedMeshRendererComponent::SetAnimatorObjectID(std::uint64_t id){
    auto* access=dynamic_cast<ObjectAccess*>(_GameObject);if(!access)return false;
    auto& runtime=access->State().Owner;
    if(id){auto* state=runtime.Find(id);if(!runtime.IsUsable(state)||state->GetScene()!=_GameObject->GetScene()||!state->Object->GetComponent<IAnimatorComponent>())return false;}
    if(_AnimatorID==id)return true;
    runtime.SetSkinAnimatorReference(_GameObject->GetInstanceID(),_AnimatorID,id);
    _AnimatorID=id;
    NotifyComponentChanged(_GameObject,this);
    return true;
}
bool SkinnedMeshRendererComponent::GetSkinMatrices(const std::vector<SkinBinding>& bones,std::vector<Mat4>& output,std::string& error) const {
    return BuildSkinMatrices(bones,output,error,false);
}
bool SkinnedMeshRendererComponent::GetPresentationSkinMatrices(const std::vector<SkinBinding>& bones,std::vector<Mat4>& output,std::string& error) const {
    return BuildSkinMatrices(bones,output,error,true);
}
bool SkinnedMeshRendererComponent::BuildSkinMatrices(const std::vector<SkinBinding>& bones,std::vector<Mat4>& output,std::string& error,bool presentation) const {
    output.clear();error.clear();auto* access=dynamic_cast<ObjectAccess*>(_GameObject);
    if(!access){error="SkinnedMeshRenderer has no live Runtime owner";return false;}
    auto& runtime=access->State().Owner;auto* state=runtime.Find(_AnimatorID);
    if(!runtime.IsInScene(&access->State())){error="SkinnedMeshRenderer is not attached to a live scene";return false;}
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
