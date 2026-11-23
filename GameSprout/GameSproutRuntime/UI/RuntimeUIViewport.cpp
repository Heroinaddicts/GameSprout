#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "RuntimeUILayout.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GameSproutRuntime {
namespace {
bool Finite(Vector2 value) { return std::isfinite(value.X) && std::isfinite(value.Y); }
bool HasArea(Rect rect) {
    return rect.Size.X>0 && rect.Size.Y>0 && Finite(rect.Position) && Finite(rect.Size) && Finite(rect.Position+rect.Size);
}
bool Same(Vector2 a, Vector2 b) { return a.X==b.X && a.Y==b.Y; }
bool Same(Rect a, Rect b) { return Same(a.Position,b.Position) && Same(a.Size,b.Size); }
bool Same(const RuntimeInputViewportMetrics& a, const RuntimeInputViewportMetrics& b) {
    return Same(a.ContentRect,b.ContentRect) && Same(a.RenderSize,b.RenderSize) && Same(a.SafeArea,b.SafeArea) &&
        a.PixelRatio==b.PixelRatio && a.DisplayDpi==b.DisplayDpi && a.Orientation==b.Orientation;
}

Rect ClipTo(Rect a, Rect b) {
    if (!HasArea(a)) return {};
    const auto endA=a.Position+a.Size, endB=b.Position+b.Size;
    const Vector2 start{std::max(a.Position.X,b.Position.X),std::max(a.Position.Y,b.Position.Y)};
    const Vector2 end{std::min(endA.X,endB.X),std::min(endA.Y,endB.Y)};
    const Rect result{start,end-start};
    return HasArea(result) ? result : Rect{};
}
}

void Runtime::SetUIInputViewport(IScene* scene, Vector2 size, Vector2 renderedSize) {
    RuntimeInputViewportMetrics metrics;
    metrics.ContentRect = {{},size};
    metrics.RenderSize = renderedSize;
    if (size.X>0 && renderedSize.X>0) metrics.PixelRatio=renderedSize.X/size.X;
    SetInputViewportMetrics(scene,metrics);
}

void Runtime::SetInputViewportMetrics(IScene* scene, const RuntimeInputViewportMetrics& value) {
    if (_ShuttingDown || _NotifyingInputViewport) return;
    auto* node=Find(scene);
    const auto id=IsInScene(node) && node->IsSceneRoot() && HasArea(value.ContentRect) ? node->ID : 0;
    auto metrics=id ? value : RuntimeInputViewportMetrics{};
    if (id) {
        if (!HasArea({{},metrics.RenderSize})) metrics.RenderSize=metrics.ContentRect.Size;
        metrics.SafeArea=ClipTo(metrics.SafeArea,metrics.ContentRect);
        if (!std::isfinite(metrics.PixelRatio) || metrics.PixelRatio<=0) metrics.PixelRatio=1.0f;
        if (!std::isfinite(metrics.DisplayDpi) || metrics.DisplayDpi<=0) metrics.DisplayDpi=0.0f;
        if (metrics.Orientation>InputViewportOrientation::PortraitFlipped) metrics.Orientation=InputViewportOrientation::Unknown;
    }
    if (id==_UIInputSceneID && Same(metrics,_InputViewport)) return;
    if (id!=_UIInputSceneID) {
        ResetUIPointer();
        CancelInputPointers();
    }
    _UIInputSceneID=id;
    metrics.Revision=_InputViewport.Revision+1;
    _InputViewport=metrics;
    const auto last=_NextInputViewportListener;
    _NotifyingInputViewport=true;
    for (auto current=_InputViewportListeners.begin(); current!=_InputViewportListeners.end() && current->first<=last;) {
        const auto key=current->first;
        try { current->second->OnInputViewportChanged(metrics); } catch (...) {}
        current=_InputViewportListeners.upper_bound(key);
    }
    _NotifyingInputViewport=false;
}

bool Runtime::RegisterInputViewportListener(IRuntimeInputViewportListener* listener) {
    if (!listener || _ShuttingDown || _NextInputViewportListener==std::numeric_limits<std::uint64_t>::max()) return false;
    for (const auto& entry:_InputViewportListeners) if (entry.second==listener) return false;
    _InputViewportListeners.emplace(++_NextInputViewportListener,listener);
    return true;
}

void Runtime::UnregisterInputViewportListener(IRuntimeInputViewportListener* listener) {
    std::erase_if(_InputViewportListeners,[listener](const auto& entry) { return entry.second==listener; });
}

bool Runtime::IsInputUIOwner(std::uint64_t id) const {
    auto* object=QueryGameObject(id);
    const auto* scene=object ? object->GetScene() : nullptr;
    return scene && scene->GetInstanceID()==_UIInputSceneID && object->IsActiveInHierarchy() && FindUIScreenCanvasRoot(*object);
}

bool Runtime::ScreenPointToUILocal(std::uint64_t id, Vector2 screen, Vector2& output) const {
    if (!IsInputUIOwner(id) || !Finite(screen)) return false;
    UIElement element;
    auto* object=QueryGameObject(id);
    if (!BuildGameplayUIElement(*object,_InputViewport,element)) return false;
    const auto pixel=InputPointToRender(_InputViewport,screen);
    Vector2 untransformed;
    if (!element.Transform.TryInversePoint(pixel,untransformed)) return false;
    const auto delta=untransformed-element.Pivot;
    const Vector2 local{delta.X/element.Scale,-delta.Y/element.Scale};
    if (!Finite(local)) return false;
    output=local;
    return true;
}

bool Runtime::UILocalPointToScreen(std::uint64_t id, Vector2 local, Vector2& output) const {
    if (!IsInputUIOwner(id) || !Finite(local)) return false;
    UIElement element;
    if (!BuildGameplayUIElement(*QueryGameObject(id),_InputViewport,element)) return false;
    Vector2 ignored;
    if (!element.Transform.TryInversePoint(element.Transform.Offset,ignored)) return false;
    const auto pixel=element.Transform.Point(element.Pivot+Vector2{local.X,-local.Y}*element.Scale);
    const auto& content=_InputViewport.ContentRect;
    const auto size=_InputViewport.RenderSize;
    const auto screen=content.Position+Vector2{pixel.X/size.X*content.Size.X,pixel.Y/size.Y*content.Size.Y};
    if (!Finite(screen)) return false;
    output=screen;
    return true;
}

bool Runtime::GetUIRectLocalBounds(std::uint64_t id, Rect& output) const {
    if (!IsInputUIOwner(id)) return false;
    UIElement element;
    if (!BuildGameplayUIElement(*QueryGameObject(id),_InputViewport,element)) return false;
    const auto& rect=element.Rectangle;
    const auto inverseScale=1.0f/element.Scale;
    const Rect local{{(rect.Position.X-element.Pivot.X)*inverseScale,
                      (element.Pivot.Y-rect.Position.Y-rect.Size.Y)*inverseScale},rect.Size*inverseScale};
    if (!Finite(local.Position) || !Finite(local.Size)) return false;
    output=local;
    return true;
}

bool Runtime::HitTestUIRect(std::uint64_t id, Vector2 screen) const {
    if (!IsInputUIOwner(id) || !ContainsUIPoint(_InputViewport.ContentRect,screen)) return false;
    UIElement element;
    return BuildGameplayUIElement(*QueryGameObject(id),_InputViewport,element) &&
        HitUIElement(element,InputPointToRender(_InputViewport,screen));
}
}
