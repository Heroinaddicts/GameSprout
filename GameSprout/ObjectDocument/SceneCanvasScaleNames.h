#pragma once
#include "RuntimeUIApi.h"
#include <array>
#include <utility>

namespace SceneCanvasScaleNames {
using GameSproutRuntime::CanvasPhysicalUnit;
using GameSproutRuntime::CanvasScaleMode;
using GameSproutRuntime::CanvasScreenMatchMode;

inline constexpr std::array Modes {
    std::pair {CanvasScaleMode::ConstantPixelSize, "ConstantPixelSize"},
    std::pair {CanvasScaleMode::ScaleWithScreenSize, "ScaleWithScreenSize"},
    std::pair {CanvasScaleMode::ConstantPhysicalSize, "ConstantPhysicalSize"}
};
inline constexpr std::array MatchModes {
    std::pair {CanvasScreenMatchMode::MatchWidthOrHeight, "MatchWidthOrHeight"},
    std::pair {CanvasScreenMatchMode::Expand, "Expand"},
    std::pair {CanvasScreenMatchMode::Shrink, "Shrink"}
};
inline constexpr std::array Units {
    std::pair {CanvasPhysicalUnit::Centimeters, "Centimeters"},
    std::pair {CanvasPhysicalUnit::Millimeters, "Millimeters"},
    std::pair {CanvasPhysicalUnit::Inches, "Inches"},
    std::pair {CanvasPhysicalUnit::Points, "Points"},
    std::pair {CanvasPhysicalUnit::Picas, "Picas"}
};
}
