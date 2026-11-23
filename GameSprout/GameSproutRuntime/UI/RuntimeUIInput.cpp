#include "RuntimeUILayout.h"
#include "UIComponents.h"
#include "GameObject.h"
#include "Scene.h"
#include <cmath>

namespace GameSproutRuntime {
ButtonComponent* Runtime::FindUIButton(std::uint64_t objectID, std::uint64_t componentID) const {
    auto* node = Find(objectID);
    if (!IsUsable(node)) return nullptr;
    for (const auto& component : node->Components)
        if (component.Serial == componentID && !component.PendingRemove)
            return dynamic_cast<ButtonComponent*>(component.Pointer);
    return nullptr;
}

void Runtime::DropUISubscriptions(std::uint64_t objectID, std::uint64_t componentID) noexcept {
    for (auto* button : _UIButtons) button->DropSubscriptions(objectID, componentID);
}

void Runtime::ResetUIPointer() {
    _UIPointerEdges.clear();
    _UIPointerSource = UIInputPointer::None;
    _UIPointerDevice = _UIPointerContact = 0;
    if (auto* button = FindUIButton(_UIPressedObjectID, _UIPressedComponentID)) button->SetPressed(false);
    _UIPressedObjectID = _UIPressedComponentID = 0;
}

void Runtime::QueueUIPointer(UIInputPointer source, InputDeviceId device, InputContactId contact,
                             Vector2 position, bool down, bool canceled) {
    if (!_UIInputSceneID) return;
    if (down) {
        if (_UIPointerSource != UIInputPointer::None) return;
        _UIPointerSource = source;
        _UIPointerDevice = device;
        _UIPointerContact = contact;
    } else {
        if (_UIPointerSource != source || _UIPointerDevice != device || _UIPointerContact != contact) return;
        _UIPointerSource = UIInputPointer::None;
        _UIPointerDevice = _UIPointerContact = 0;
    }
    _UIPointerEdges.push_back({position, down, canceled});
}

void Runtime::DispatchUIPointer() {
    if (_UIPressedObjectID) {
        auto* button = FindUIButton(_UIPressedObjectID, _UIPressedComponentID);
        auto* scene = button ? button->GetGameObject()->GetScene() : nullptr;
        if (!scene || scene->GetInstanceID() != _UIInputSceneID || !button->GetGameObject()->IsActiveInHierarchy()) {
            if (button) button->SetPressed(false);
            _UIPressedObjectID = _UIPressedComponentID = 0;
        }
    }
    if (_UIPointerEdges.empty()) return;
    std::vector<UIPointerEdge> batch;
    batch.swap(_UIPointerEdges);
    const auto sceneID = _UIInputSceneID;
    for (const auto& edge : batch) {
        auto* root = Find(sceneID);
        if (!_InputEnabled || !_Input.Focused || _UIInputSceneID != sceneID ||
            !IsInScene(root) || !root->SceneOwner->IsSimulationEnabled()) {
            ResetUIPointer();
            return;
        }

        ButtonComponent* hit = nullptr;
        if (!edge.Canceled && !_UIButtons.empty() && ContainsUIPoint(_InputViewport.ContentRect, edge.Position)) {
            const auto pixel = InputPointToRender(_InputViewport,edge.Position);
            const auto layout = BuildUIFrame(*this, *root->SceneOwner, _InputViewport.RenderSize);
            for (auto element = layout.rbegin(); element != layout.rend(); ++element) {
                auto* button = element->Object->GetComponent<IButtonComponent>();
                if (button && HitUIElement(*element, pixel)) {
                    hit = FindUIButton(element->Object->GetInstanceID(), button->GetComponentInstanceID());
                    if (hit) break;
                }
            }
        }
        auto* pressed = FindUIButton(_UIPressedObjectID, _UIPressedComponentID);
        if (edge.Down) {
            if (pressed) pressed->SetPressed(false);
            _UIPressedObjectID = _UIPressedComponentID = 0;
            if (hit && hit->GetInteractable()) {
                _UIPressedObjectID = hit->GetGameObject()->GetInstanceID();
                _UIPressedComponentID = hit->GetComponentInstanceID();
                hit->SetPressed(true);
            }
        } else {
            _UIPressedObjectID = _UIPressedComponentID = 0;
            if (!pressed) continue;
            const bool click = pressed == hit && pressed->IsPressed() && pressed->GetInteractable();
            pressed->SetPressed(false);
            if (click) {
                RuntimeOperation operation(*this);
                FrameComponent record{pressed->GetGameObject()->GetInstanceID(), pressed->GetComponentInstanceID()};
                const auto constructionDepth = _ConstructionDepth;
                pressed->DispatchClick();
                RecoverCallbackConstruction(record, *pressed, constructionDepth);
            }
        }
    }
}
}
