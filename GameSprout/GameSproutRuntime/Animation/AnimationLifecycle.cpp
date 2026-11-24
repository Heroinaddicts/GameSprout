#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/AnimatorComponent.h"
#include <algorithm>

namespace GameSproutRuntime {
namespace {
template<typename Index>
void RemoveReference(Index& index,std::uint64_t target,std::uint64_t owner) {
    const auto found=index.find(target);
    if(found==index.end())return;
    found->second.erase(owner);
    if(found->second.empty())index.erase(found);
}

template<typename Index>
bool AddReference(Index& index,std::uint64_t target,std::uint64_t owner) {
    auto [entry,created]=index.try_emplace(target);
    try {
        return entry->second.insert(owner).second;
    } catch(...) {
        if(created)index.erase(entry);
        throw;
    }
}
}

void Runtime::SetAnimationBindingOwners(std::uint64_t owner,const std::vector<AnimationNodeBinding>& oldBindings,
                                        const std::set<std::uint64_t>& newTargets) {
    std::vector<std::uint64_t> inserted;
    inserted.reserve(newTargets.size());
    try {
        for(const auto target:newTargets)
            if(AddReference(_AnimatorsByBone,target,owner))inserted.push_back(target);
    } catch(...) {
        for(const auto target:inserted)RemoveReference(_AnimatorsByBone,target,owner);
        throw;
    }
    for(const auto& binding:oldBindings)
        if(binding.ObjectID&&!newTargets.contains(binding.ObjectID))
            RemoveReference(_AnimatorsByBone,binding.ObjectID,owner);
}

void Runtime::SetSkinAnimatorReference(std::uint64_t owner,std::uint64_t oldAnimator,std::uint64_t newAnimator) {
    if(oldAnimator==newAnimator)return;
    if(newAnimator)AddReference(_SkinsByAnimator,newAnimator,owner);
    if(oldAnimator)RemoveReference(_SkinsByAnimator,oldAnimator,owner);
}

bool Runtime::AffectsAnimationBindings(const GameObjectState& root,bool descendants) const {
    if(_AnimatorsByBone.empty())return false;
    const auto referencedInScene=[&](std::uint64_t id) {
        const auto found=_AnimatorsByBone.find(id);
        return found!=_AnimatorsByBone.end()&&std::any_of(found->second.begin(),found->second.end(),[&](auto owner) {
            const auto* node=Find(owner);
            return node&&node->SceneOwner==root.SceneOwner;
        });
    };
    if(referencedInScene(root.ID))return true;
    if(!descendants)return false;
    std::vector<const GameObjectState*> pending(root.Children.begin(),root.Children.end());
    while(!pending.empty()) {
        const auto* child=pending.back();
        pending.pop_back();
        if(referencedInScene(child->ID))return true;
        pending.insert(pending.end(),child->Children.begin(),child->Children.end());
    }
    return false;
}

void Runtime::ForgetObjectAnimationBindings(std::uint64_t id) {
    ForgetAnimationReferences(id,true);
}

void Runtime::ForgetAnimatorReferences(std::uint64_t id) {
    ForgetAnimationReferences(id,false);
}

void Runtime::ForgetAnimationReferences(std::uint64_t target,bool forgetBoneBindings) {
    if(_ShuttingDown)return;
    // Detach the entries before setters remove the same links. Preserve owner
    // ordering, including an owner that references this ID in both roles.
    const auto bones=forgetBoneBindings?_AnimatorsByBone.extract(target):AnimationReferenceIndex::node_type{};
    auto skins=_SkinsByAnimator.extract(target);
    if(bones.empty()&&skins.empty())return;
    const std::set<std::uint64_t> empty;
    const auto& boneOwners=bones.empty()?empty:bones.mapped();
    const auto& skinOwners=skins.empty()?empty:skins.mapped();
    auto bone=boneOwners.begin(),skin=skinOwners.begin();
    while(bone!=boneOwners.end()||skin!=skinOwners.end()) {
        const auto owner=skin==skinOwners.end()||(bone!=boneOwners.end()&&*bone<*skin)?*bone:*skin;
        const bool boundBone=bone!=boneOwners.end()&&*bone==owner;
        const bool boundSkin=skin!=skinOwners.end()&&*skin==owner;
        if(boundBone)++bone;
        if(boundSkin)++skin;
        auto* node=Find(owner);
        if(!IsUsable(node))continue;
        if(boundBone)
            if(auto* animator=dynamic_cast<AnimatorComponent*>(node->Object->GetComponent<IAnimatorComponent>()))
                animator->ForgetObject(target);
        if(boundSkin) {
            skins.mapped().erase(owner);
            if(auto* mesh=node->Object->GetComponent<ISkinnedMeshRendererComponent>();mesh&&mesh->GetAnimatorObjectID()==target)
                mesh->SetAnimatorObjectID(0);
        }
    }
    // A component removal keeps its node alive. Keep links from temporarily
    // inaccessible owners so a later Animator replacement can still be found.
    if(!forgetBoneBindings&&!skins.empty()&&!skins.mapped().empty())
        _SkinsByAnimator.insert(std::move(skins));
}
}
