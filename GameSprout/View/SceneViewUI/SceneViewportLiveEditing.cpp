#include "SceneViewportEditing.h"
#include "SceneViewport.h"
#include <QMetaObject>

namespace {
SceneTransformState Read(const GameSproutRuntime::ITransformComponent& transform) {
    return {transform.GetLocalPosition(),transform.GetLocalRotation(),transform.GetLocalScale()};
}

bool Equal(GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b) {
    return a.X==b.X&&a.Y==b.Y&&a.Z==b.Z;
}

bool Equal(GameSproutRuntime::Quaternion a,GameSproutRuntime::Quaternion b) {
    const float sign=a.X*b.X+a.Y*b.Y+a.Z*b.Z+a.W*b.W<0?-1.0f:1.0f;
    return a.X==sign*b.X&&a.Y==sign*b.Y&&a.Z==sign*b.Z&&a.W==sign*b.W;
}

ISceneManager::fSceneEditOperation SetTransform(SceneTransformState value) {
    return [value](GameSproutRuntime::IGameObject& object,std::string& error) {
        auto* transform=object.GetTransform();
        if(!transform||transform->IsAnimationDriven()) {
            error="节点的变换已失效或由动画驱动。";
            return false;
        }
        const auto current=Read(*transform);
        if(!Equal(current.Rotation,value.Rotation)&&!transform->SetLocalRotation(value.Rotation)) {
            error="Runtime 拒绝该旋转。";
            return false;
        }
        if(!Equal(current.Position,value.Position))transform->SetLocalPosition(value.Position);
        if(!Equal(current.Scale,value.Scale))transform->SetLocalScale(value.Scale);
        return true;
    };
}
}

bool SceneViewportEditing::BeginLiveDrag(GameSproutRuntime::IGameObject& object,const char* label,std::string& error) {
    for(auto* node=&object;node&&node!=_ActiveScene;node=node->Parent()) {
        if(node->GetTransform()->IsAnimationDriven()) {
            error="节点或父级由动画驱动，不能拖拽该变换。";
            return false;
        }
    }
    auto* transform=object.GetTransform();
    const ObjectEditTarget target{_SceneID,_ObjectID,transform->GetComponentInstanceID()};
    const auto before=Read(*transform);
    if(!_ObjectEditing->BeginContinuousEdit(target,label,SetTransform(before),error))return false;
    _LiveDragTarget=target;
    _LiveDragLast=before;
    return true;
}

void SceneViewportEditing::QueueLiveDragPreview() {
    if(_LiveDragPreviewQueued)return;
    _LiveDragPreviewQueued=true;
    // Live property writes and physics synchronization need the owner loop outside RuntimeHost's frame.
    QMetaObject::invokeMethod(&_Viewport,[this] {
        _LiveDragPreviewQueued=false;
        ApplyPendingLiveDrag();
    },Qt::QueuedConnection);
}

void SceneViewportEditing::ApplyPendingLiveDrag() {
    if(!_PendingDrag||!_LiveDragTarget.ComponentID)return;
    const auto value=*_PendingDrag;
    _PendingDrag.reset();
    if(!_ObjectEditing->IsContinuousEditFor(_LiveDragTarget)) {
        Cancel();
        return;
    }
    if(Equal(_LiveDragLast.Position,value.Position)&&Equal(_LiveDragLast.Rotation,value.Rotation)&&
        Equal(_LiveDragLast.Scale,value.Scale))return;
    std::string error;
    if(!_ObjectEditing->PreviewContinuousEdit(SetTransform(value),SetTransform(_LiveDragLast),error)) {
        Cancel();
        Report(error);
        return;
    }
    _LiveDragLast=value;
    _DragChanged=true;
    _Viewport.RequestRender();
}

void SceneViewportEditing::FinishLiveDrag(bool commit) {
    const auto target=_LiveDragTarget;
    _LiveDragTarget={};
    if(!_ObjectEditing||!_ObjectEditing->IsContinuousEditFor(target))return;
    std::string error;
    if(commit&&_DragChanged) {
        if(_ObjectEditing->CommitContinuousEdit(SetTransform(_LiveDragLast),error))return;
        if(_ObjectEditing->IsContinuousEditFor(target)) {
            std::string ignored;
            _ObjectEditing->CancelContinuousEdit(ignored);
        }
        Report(error);
    } else if(!_ObjectEditing->CancelContinuousEdit(error))Report(error);
}
