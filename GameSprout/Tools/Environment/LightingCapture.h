#pragma once

#include "RuntimeRenderApi.h"
#include <array>
#include <cmath>

namespace GameSproutLightingCapture {
using namespace GameSproutRuntime;

// Cube faces in the GRHI sampler order +X, -X, +Y, -Y, +Z, -Z.
// The caller owns a square offscreen viewport and the scene. Capture never
// drives game simulation or changes the scene, camera components or settings.
inline bool CaptureFace(IRuntimeViewport& viewport,IScene& scene,Vector3 position,unsigned face,
    StaticFlags requiredFlags,std::uint32_t mask,float farPlane,std::vector<std::uint8_t>& pixels,std::string& error) {
    constexpr std::array<Vector3,6> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    constexpr std::array<Vector3,6> up{{{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}}};
    pixels.clear();error.clear();
    if(face>=directions.size()||!std::isfinite(farPlane)||farPlane<=.03f) {
        error="Invalid lighting capture face or range";return false;
    }
    RenderView view;
    view.Scene=&scene;view.DrawGrid=false;view.DrawUI=false;view.CaptureLinearColor=true;
    view.EnableReflectionProbes=false;view.EnablePlanarReflections=false;view.EnableBakedIndirectLighting=false;
    view.RequiredStaticFlags=requiredFlags;
    view.Camera.Position=position;
    view.Camera.Projection.VerticalFovRadians=1.57079632679f;
    view.Camera.Projection.NearPlane=.03f;
    view.Camera.Projection.FarPlane=farPlane;
    view.Camera.Projection.CullingMask=mask;
    if(!TryMakeLookAt(position,position+directions[face],up[face],view.Camera.View)) {
        error="Cannot build lighting capture camera";return false;
    }
    if(!viewport.Render(view)||!viewport.GetLastError().empty()) {
        error=viewport.GetLastError();return false;
    }
    const auto& stats=viewport.GetStatistics();
    if(!stats.Width||stats.Width!=stats.Height) {
        error="Lighting capture target must be square";return false;
    }
    return viewport.ReadLinearColorHalf(pixels,error);
}
}
