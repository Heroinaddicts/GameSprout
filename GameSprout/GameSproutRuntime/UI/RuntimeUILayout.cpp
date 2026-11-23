#include "RuntimeUILayout.h"
#include "Components/TransformComponent.h"
#include "GameSproutRuntime.h"
#include <algorithm>
#include <cmath>

namespace GameSproutRuntime {
namespace {
bool Finite(Vector2 value) {
    return std::isfinite(value.X) && std::isfinite(value.Y);
}

bool ValidRect(const Rect& rect) {
    return Finite(rect.Position) && Finite(rect.Size) && Finite(rect.Position + rect.Size);
}

Rect LayoutRect(const Rect& parent, const IRectTransformComponent& transform, float scale) {
    const auto minimum = transform.GetAnchorMin();
    const auto maximum = transform.GetAnchorMax();
    const auto pivot = transform.GetPivot();
    const auto offset = transform.GetAnchoredPosition() * scale;
    const auto delta = transform.GetSizeDelta() * scale;
    const float anchorLeft = parent.Position.X + parent.Size.X * minimum.X;
    const float anchorTop = parent.Position.Y + parent.Size.Y * (1.0f - maximum.Y);
    return {{anchorLeft + offset.X - pivot.X * delta.X,
             anchorTop - offset.Y - (1.0f - pivot.Y) * delta.Y},
            {std::max(0.0f, parent.Size.X * (maximum.X - minimum.X) + delta.X),
             std::max(0.0f, parent.Size.Y * (maximum.Y - minimum.Y) + delta.Y)}};
}

const ICanvasComponent* ParentCanvas(const IGameObject& object) {
    for (auto* parent = object.Parent(); parent; parent = parent->Parent())
        if (auto* canvas = parent->GetComponent<ICanvasComponent>()) return canvas;
    return nullptr;
}

struct LayoutNode {
    IGameObject* Object;
    Rect Parent;
    UILayoutTransform ParentTransform;
};

bool LayoutElement(const LayoutNode& node, const IGameObject& root, float scale, UIElement& element) {
    element = {};
    element.Object = node.Object;
    element.Rectangle = node.Parent;
    element.Scale = scale;
    element.ParentTransform = element.Transform = node.ParentTransform;
    const auto* rectangle = node.Object->GetComponent<IRectTransformComponent>();
    const bool isRoot = node.Object == &root;
    if (rectangle && !isRoot) element.Rectangle = LayoutRect(node.Parent, *rectangle, scale);
    if (!ValidRect(element.Rectangle)) return false;

    const auto pivot = rectangle && !isRoot ? rectangle->GetPivot() : Vector2{0.5f, 0.5f};
    element.Pivot = element.Rectangle.Position +
        Vector2{pivot.X * element.Rectangle.Size.X, (1.0f - pivot.Y) * element.Rectangle.Size.Y};
    if (isRoot || !rectangle) return true;

    const auto* transform = static_cast<const TransformComponent*>(node.Object->GetTransform());
    Quaternion rotation;
    transform->GetCurrentLocalRotation(rotation);
    const auto localScale = transform->GetLocalScale();
    // Use the Transform's twist about +Z for the screen plane. In layout
    // coordinates +Y is down, so a positive authored angle turns +X upward.
    const double lengthSquared = double(rotation.Z) * rotation.Z + double(rotation.W) * rotation.W;
    const float cosine = lengthSquared > 0 ? float((double(rotation.W) * rotation.W - double(rotation.Z) * rotation.Z) / lengthSquared) : 1.0f;
    const float sine = lengthSquared > 0 ? float((2.0 * rotation.W * rotation.Z) / lengthSquared) : 0.0f;
    const UILayoutTransform local{{}, {cosine * localScale.X, -sine * localScale.X}, {sine * localScale.Y, cosine * localScale.Y}};
    element.Transform.X = node.ParentTransform.Direction(local.X);
    element.Transform.Y = node.ParentTransform.Direction(local.Y);
    element.Transform.Offset = node.ParentTransform.Point(element.Pivot - local.Direction(element.Pivot));
    return Finite(element.Transform.X) && Finite(element.Transform.Y) && Finite(element.Transform.Offset);
}

struct CanvasLayout {
    Rect Bounds;
    float Scale = 0;
};

float CanvasScale(const ICanvasComponent& canvas, Vector2 size, float displayDpi) {
    const auto settings = canvas.GetScaleSettings();
    if (settings.Mode == CanvasScaleMode::ConstantPixelSize) return settings.ScaleFactor;
    if (settings.Mode == CanvasScaleMode::ConstantPhysicalSize) {
        constexpr float unitsPerInch[]{2.54f, 25.4f, 1.0f, 72.0f, 6.0f};
        const float dpi = displayDpi > 0 ? displayDpi : settings.FallbackDpi;
        return dpi / unitsPerInch[static_cast<std::size_t>(settings.PhysicalUnit)];
    }
    const auto reference = canvas.GetReferenceResolution();
    const float width = size.X / reference.X;
    const float height = size.Y / reference.Y;
    if (settings.MatchMode == CanvasScreenMatchMode::Expand) return std::min(width, height);
    if (settings.MatchMode == CanvasScreenMatchMode::Shrink) return std::max(width, height);
    return std::exp(std::lerp(std::log(width), std::log(height), settings.WidthHeightMatch));
}

CanvasLayout LayoutCanvas(const ICanvasComponent& canvas, Vector2 size, RuntimeInputViewportMetrics metrics) {
    if (!ValidRect({{},size}) || size.X <= 0 || size.Y <= 0) return {};
    CanvasLayout layout{{{},size}, CanvasScale(canvas,size,metrics.DisplayDpi)};
    if (canvas.GetScaleSettings().UseSafeArea && metrics.SafeArea.Size.X > 0 && metrics.SafeArea.Size.Y > 0) {
        metrics.RenderSize = size;
        const auto start = InputPointToRender(metrics,metrics.SafeArea.Position);
        const auto end = InputPointToRender(metrics,metrics.SafeArea.Position+metrics.SafeArea.Size);
        layout.Bounds = {start,end-start};
    }
    return layout;
}

bool LayoutInCanvas(IGameObject& object, IGameObject& root, const CanvasLayout& layout, UIElement& element) {
    if (!std::isfinite(layout.Scale) || layout.Scale <= 0 || !object.IsActiveInHierarchy()) return false;
    std::vector<IGameObject*> chain;
    for (auto* node = &object; node != &root; node = node->Parent()) chain.push_back(node);
    if (!LayoutElement({&root,layout.Bounds,{}},root,layout.Scale,element)) return false;
    for (auto node = chain.rbegin(); node != chain.rend(); ++node)
        if (!LayoutElement({*node,element.Rectangle,element.Transform},root,layout.Scale,element)) return false;
    return true;
}

void AppendCanvas(IGameObject& root, const CanvasLayout& layout, std::vector<UIElement>& elements, bool includeCanvas = false) {
    if (!root.IsActiveInHierarchy()) return;
    const float scale = layout.Scale;
    if (!std::isfinite(scale) || scale <= 0.0f) return;
    std::vector<LayoutNode> pending{{&root, layout.Bounds, {}}};
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        if (!node.Object || !node.Object->IsActiveSelf()) continue;
        if (const auto* nested = node.Object->GetComponent<ICanvasComponent>();
            nested && nested->GetRenderMode() != CanvasRenderMode::ScreenSpace) continue;

        UIElement element;
        if (!LayoutElement(node, root, scale, element)) continue;
        if (node.Object->GetComponent<IRectTransformComponent>() || (includeCanvas && node.Object == &root))
            elements.push_back(element);
        for (std::size_t child = node.Object->GetChildCount(); child > 0; --child)
            pending.push_back({node.Object->GetChild(child - 1), element.Rectangle, element.Transform});
    }
}

CanvasLayout LayoutCanvasPreview(const ICanvasComponent& canvas, Vector2 viewportSize, Vector2 gameViewportSize,
                                 const RuntimeInputViewportMetrics& metrics) {
    if (!ValidRect({{}, viewportSize}) || viewportSize.X <= 0 || viewportSize.Y <= 0) return {};
    if (!Finite(gameViewportSize) || gameViewportSize.X <= 0 || gameViewportSize.Y <= 0)
        gameViewportSize = metrics.RenderSize;
    if (!Finite(gameViewportSize) || gameViewportSize.X <= 0 || gameViewportSize.Y <= 0)
        gameViewportSize = canvas.GetReferenceResolution();
    // Preserve the Game target's aspect and leave the canvas outline visible.
    const float projection = 0.84f * std::min(viewportSize.X / gameViewportSize.X, viewportSize.Y / gameViewportSize.Y);
    const auto content = (viewportSize-gameViewportSize*projection)*0.5f;
    const auto layout = LayoutCanvas(canvas,gameViewportSize,metrics);
    return {{content+layout.Bounds.Position*projection,layout.Bounds.Size*projection},layout.Scale*projection};
}
}

bool UILayoutTransform::TryInversePoint(Vector2 value, Vector2& output) const {
    const double determinant = double(X.X) * Y.Y - double(X.Y) * Y.X;
    if (!std::isfinite(determinant) || determinant == 0.0) return false;
    const auto position = value - Offset;
    output = {float((double(position.X) * Y.Y - double(position.Y) * Y.X) / determinant),
              float((double(X.X) * position.Y - double(X.Y) * position.X) / determinant)};
    return Finite(output);
}

bool HitUIElement(const UIElement& element, Vector2 point) {
    Vector2 local;
    return element.Transform.TryInversePoint(point, local) && ContainsUIPoint(element.Rectangle, local);
}

Vector2 InputPointToRender(const RuntimeInputViewportMetrics& metrics, Vector2 position) {
    const auto offset = position-metrics.ContentRect.Position;
    const auto size = metrics.ContentRect.Size;
    return {offset.X/size.X*metrics.RenderSize.X,offset.Y/size.Y*metrics.RenderSize.Y};
}

bool BuildGameplayUIElement(IGameObject& object, const RuntimeInputViewportMetrics& metrics, UIElement& element) {
    auto* root = FindUIScreenCanvasRoot(object);
    if (!root) return false;
    const auto& canvas = *root->GetComponent<ICanvasComponent>();
    return LayoutInCanvas(object,*root,LayoutCanvas(canvas,metrics.RenderSize,metrics),element);
}

std::vector<UIElement> BuildUIFrame(const Runtime& runtime, IScene& scene, Vector2 viewportSize) {
    std::vector<UIElement> elements;
    const Rect viewport{{}, viewportSize};
    if (!ValidRect(viewport) || viewportSize.X <= 0 || viewportSize.Y <= 0) return elements;

    // Visit registered canvases and their subtrees. Large 3D-only parts of a
    // scene never enter UI layout. Separate roots use stable creation order.
    const auto metrics = runtime.InputViewportForScene(scene);
    for (const auto id : runtime.UICanvasObjects()) {
        auto* root = runtime.QueryGameObject(id);
        if (!root || root->GetScene() != &scene) continue;
        const auto* canvas = root->GetComponent<ICanvasComponent>();
        if (!canvas || canvas->GetRenderMode() != CanvasRenderMode::ScreenSpace) continue;
        const auto* parentCanvas = ParentCanvas(*root);
        if (parentCanvas && parentCanvas->GetRenderMode() == CanvasRenderMode::ScreenSpace) continue;

        AppendCanvas(*root,LayoutCanvas(*canvas,viewportSize,metrics),elements);
    }
    return elements;
}

UIPreviewFrame BuildUIPreview(const Runtime& runtime, IScene& scene, std::uint64_t objectID, Vector2 viewportSize,
                              Vector2 gameViewportSize) {
    UIPreviewFrame preview;
    auto* object = runtime.QueryGameObject(objectID);
    auto* root = object && object->GetScene() == &scene ? FindUIScreenCanvasRoot(*object) : nullptr;
    if (!root) return preview;

    const auto& canvas = *root->GetComponent<ICanvasComponent>();
    const auto layout = LayoutCanvasPreview(canvas,viewportSize,gameViewportSize,runtime.InputViewportForScene(scene));
    preview.Canvas = layout.Bounds;
    preview.IsCanvasSelected = object == root;
    AppendCanvas(*root,layout,preview.Elements,true);
    return preview;
}

std::uint64_t Runtime::PickUIScreenPreview(IScene* scene, std::uint64_t previewObjectID, Vector2 size, Vector2 pixel,
                                           Vector2 gameViewportSize) const {
    if (!IsInScene(Find(scene))) return 0;
    const auto preview = BuildUIPreview(*this, *scene, previewObjectID, size, gameViewportSize);
    if (!ContainsUIPoint(preview.Canvas, pixel)) return 0;
    for (auto element = preview.Elements.rbegin(); element != preview.Elements.rend(); ++element)
        if (HitUIElement(*element, pixel)) return element->Object->GetInstanceID();
    return 0;
}

bool Runtime::GetUIScreenPreviewGeometry(IScene* scene, std::uint64_t objectID, Vector2 size,
                                         UIScreenPreviewGeometry& output, Vector2 gameViewportSize) const {
    output = {};
    if (!IsInScene(Find(scene))) return false;
    auto* object = QueryGameObject(objectID);
    auto* root = object && object->GetScene() == scene ? FindUIScreenCanvasRoot(*object) : nullptr;
    if (!root || !object->IsActiveInHierarchy()) return false;
    const auto& canvas = *root->GetComponent<ICanvasComponent>();
    const auto layout = LayoutCanvasPreview(canvas,size,gameViewportSize,InputViewportForScene(*scene));
    const float scale = layout.Scale;
    UIElement element;
    if (!LayoutInCanvas(*object,*root,layout,element)) return false;

    const auto topLeft = element.Rectangle.Position;
    const auto rectangleSize = element.Rectangle.Size;
    const auto topRight = topLeft + Vector2{rectangleSize.X, 0};
    const auto bottomLeft = topLeft + Vector2{0, rectangleSize.Y};
    output.CanvasBounds = layout.Bounds;
    output.Corners = {element.Transform.Point(topLeft), element.Transform.Point(topRight),
        element.Transform.Point(topLeft + rectangleSize), element.Transform.Point(bottomLeft)};
    output.Pivot = element.Transform.Point(element.Pivot);
    output.ParentRight = element.ParentTransform.X * scale;
    output.ParentUp = element.ParentTransform.Y * -scale;
    output.Right = element.Transform.X * scale;
    output.Up = element.Transform.Y * -scale;
    output.Size = rectangleSize * (1.0f / scale);
    output.IsCanvasRoot = root == object;
    return Finite(output.Pivot) && Finite(output.Size) && Finite(output.ParentRight) && Finite(output.ParentUp) &&
        Finite(output.Right) && Finite(output.Up) && std::all_of(output.Corners.begin(), output.Corners.end(), Finite);
}
}
