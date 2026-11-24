#include "Components/TransformComponent.h"
#include "GameObject.h"
#include "PhysicsConversions.h"
#include "RuntimePhysicsSystem.h"
#include "Scene.h"
#include <algorithm>

namespace GameSproutRuntime {
namespace {
Quaternion InterpolateRotation(Quaternion previous,Quaternion current,float alpha) {
    float dot=previous.X*current.X+previous.Y*current.Y+previous.Z*current.Z+previous.W*current.W;
    if(dot<0) {
        dot=-dot;
        current={-current.X,-current.Y,-current.Z,-current.W};
    }
    float left=1-alpha,right=alpha;
    if(dot<0.9995f) {
        const float angle=std::acos(std::min(dot,1.f));
        const float denominator=std::sin(angle);
        left=std::sin(left*angle)/denominator;
        right=std::sin(right*angle)/denominator;
    }
    return {left*previous.X+right*current.X,left*previous.Y+right*current.Y,
            left*previous.Z+right*current.Z,left*previous.W+right*current.W};
}
}
bool RuntimePhysicsSystem::PublishTransforms(SceneWorld &world,bool completedStep,std::string &error) {
    if(world.DynamicBodies.empty())return true;
    std::vector<std::pair<std::size_t, uint64_t>> ordered;
    ordered.reserve(world.DynamicBodies.size());
    for (const auto id : world.DynamicBodies) {
        auto *object = _Runtime.QueryGameObject(id);
        std::size_t depth = 0;
        for (auto *parent = object ? object->Parent() : nullptr; parent; parent = parent->Parent())
            ++depth;
        ordered.emplace_back(depth, id);
    }
    // Parent world poses must be published before converting a child body's
    // world pose back to local TRS.
    std::sort(ordered.begin(), ordered.end());
    for (const auto &[depth, id] : ordered) {
        auto &record = world.Bodies.at(id);
        auto *rigid = dynamic_cast<RigidbodyComponent *>(ResolveComponent(id, record.ComponentID));
        if (!rigid)
            continue;
        GPhysics::BodyState body;
        if (!world.Backend.GetBodyState(id, body))
            continue;
        auto *object = rigid->GetGameObject();
        auto *transform = dynamic_cast<TransformComponent *>(object->GetTransform());
        Mat4 local = RuntimeMath::ComposeTRS(RuntimeVector(body.Transform.Position),
                                             RuntimeRotation(body.Transform.Rotation), record.WorldScale);
        if (object->Parent()) {
            Mat4 inverse;
            if (!RuntimeMath::TryInverseAffine(object->Parent()->GetTransform()->GetWorldMatrix(), inverse)) {
                error = "Cannot publish physics pose through a singular parent transform";
                return false;
            }
            local = inverse * local;
        }
        if (!transform->SetPhysicsPose(local, rigid->Settings.MotionType == PhysicsMotionType::Dynamic)) {
            error = "Physics result cannot be represented by this node's local TRS";
            return false;
        }
        rigid->LinearVelocity = RuntimeVector(body.LinearVelocity);
        rigid->AngularVelocity = RuntimeVector(body.AngularVelocity);
        rigid->Sleeping = !body.Active;
        record.LastWorld = transform->GetWorldMatrix();
        record.TransformRevision = transform->GetAuthoringRevision();
        if(rigid->Settings.Interpolate) {
            auto& samples=world.Presentation.at(id);
            if(completedStep||!samples.Initialized) {
                samples.Previous=samples.Initialized?samples.Current:body.Transform;
                samples.Current=body.Transform;
                samples.Initialized=true;
                samples.TransformRevision=record.TransformRevision;
            }
        }
    }
    return true;
}
void RuntimePhysicsSystem::PublishPresentation(float alpha,bool collapseHistory) {
    for(const auto& [sceneID,world]:_Worlds) {
        if(!world)continue;
        for(auto& [id,samples]:world->Presentation) {
            const auto* node=_Runtime.Find(id);
            if(!_Runtime.IsInScene(node)||node->SceneOwner->GetInstanceID()!=sceneID)continue;
            auto* transform=static_cast<TransformComponent*>(node->Object->GetTransform());
            auto* rigid=static_cast<RigidbodyComponent*>(node->Object->GetComponent<IRigidbodyComponent>());
            if(collapseHistory)samples.Previous=samples.Current;
            if(!samples.Initialized||alpha>=1.f||samples.Previous==samples.Current||
               !node->IsActiveInHierarchy()||transform->IsAnimationDriven()||
               !rigid||!rigid->IsEnabled()||!rigid->GetInterpolate()||rigid->HasTeleport||
               samples.ConfigurationRevision!=rigid->Revision||
               samples.TransformRevision!=transform->GetAuthoringRevision()) {
                transform->SetPresentationPhysicsPose(nullptr);
                continue;
            }
            const auto& a=samples.Previous.Position;
            const auto& b=samples.Current.Position;
            const Vector3 position{std::lerp(a.X,b.X,alpha),std::lerp(a.Y,b.Y,alpha),std::lerp(a.Z,b.Z,alpha)};
            const auto rotation=InterpolateRotation(RuntimeRotation(samples.Previous.Rotation),RuntimeRotation(samples.Current.Rotation),alpha);
            const auto matrix=RuntimeMath::ComposeTRS(position,rotation,world->Bodies.at(id).WorldScale);
            transform->SetPresentationPhysicsPose(&matrix,rotation);
        }
    }
}
void RuntimePhysicsSystem::ResetPresentation(uint64_t scene,const GameObjectState* subtree) {
    const auto found=_Worlds.find(scene);
    if(found==_Worlds.end()||!found->second)return;
    for(auto& [id,samples]:found->second->Presentation) {
        const auto* node=_Runtime.Find(id);
        if(!_Runtime.IsInScene(node)||node->SceneOwner->GetInstanceID()!=scene)continue;
        if(subtree) {
            const auto* ancestor=node;
            while(ancestor&&ancestor!=subtree)ancestor=ancestor->Parent;
            if(!ancestor)continue;
        }
        samples.Initialized=false;
        static_cast<TransformComponent*>(node->Object->GetTransform())->SetPresentationPhysicsPose(nullptr);
    }
}
} // namespace GameSproutRuntime
