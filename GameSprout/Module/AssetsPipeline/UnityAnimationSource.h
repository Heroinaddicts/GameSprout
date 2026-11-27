#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace UnityAnimationSource {
struct Key {
    double Time = 0;
    std::array<double,4> Value{}, InSlope{}, OutSlope{};
};
using Curve = std::vector<Key>;

struct Track {
    // The path is relative to the Unity Animator, before binding to native nodes.
    std::string Path;
    Curve Translation, Rotation, Scale;
};

struct Clip {
    std::string Name;
    double SampleRate = 60, Start = 0, Stop = 0;
    bool Loop = false;
    std::vector<Track> Tracks;
    std::vector<std::string> IgnoredCurves;
};

bool LooksLikeUnityClip(std::string_view source);
bool Parse(std::string_view source,Clip& clip,std::string& error);
std::array<double,4> Sample(const Curve& curve,double time,bool rotation=false);
}
