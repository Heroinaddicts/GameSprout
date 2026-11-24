#pragma once
#include "BuiltinComponent.h"
namespace GameSproutRuntime {
class SkinnedMeshRendererComponent final : public BuiltinComponent<ISkinnedMeshRendererComponent> {
public:
    explicit SkinnedMeshRendererComponent(IGameObject*);
    std::uint64_t GetAnimatorObjectID() const override {return _AnimatorID;}
    bool SetAnimatorObjectID(std::uint64_t) override;
    bool GetSkinMatrices(const std::vector<SkinBinding>&,std::vector<Mat4>&,std::string&) const override;
    bool GetPresentationSkinMatrices(const std::vector<SkinBinding>&,std::vector<Mat4>&,std::string&) const;
private:
    bool BuildSkinMatrices(const std::vector<SkinBinding>&,std::vector<Mat4>&,std::string&,bool presentation) const;
    std::uint64_t _AnimatorID=0;
};
}
