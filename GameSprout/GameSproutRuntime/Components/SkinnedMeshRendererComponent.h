#pragma once
#include "BuiltinComponent.h"
namespace GameSproutRuntime {
class GameObjectState;

class SkinnedMeshRendererComponent final : public BuiltinComponent<ISkinnedMeshRendererComponent> {
public:
    explicit SkinnedMeshRendererComponent(IGameObject*);
    std::uint64_t GetAnimatorObjectID() const override {return _AnimatorID;}
    bool SetAnimatorObjectID(std::uint64_t) override;
    bool SetSkinPose(std::span<const Mat4>) override;
    bool GetSkinMatrices(const std::vector<SkinBinding>&,std::vector<Mat4>&,std::string&) const override;
    bool GetPresentationSkinMatrices(const std::vector<SkinBinding>&,std::vector<Mat4>&,std::string&) const;
private:
    friend class Renderer;
    bool BuildSkinMatrices(const std::vector<SkinBinding>&,std::vector<Mat4>&,std::string&,bool presentation) const;
    GameObjectState* const _OwnerState;
    std::uint64_t _AnimatorID=0;
    std::vector<Mat4> _SkinPose;
    std::uint64_t _SkinPoseRevision=0;
};
}
