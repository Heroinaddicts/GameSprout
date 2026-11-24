#include "LightingBakeKernels.h"
#include "EnvironmentAssetFormat.h"
#include "IndirectLightingAssetFormat.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace GameSproutLightingCapture;
using namespace GameSproutRuntime;

namespace {
void Check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}

constexpr std::array<std::uint8_t,8> Pixel{0x00,0x40,0x00,0x38,0x00,0x30,0x00,0x3c};
LinearCubeCapture ConstantCube() {
    LinearCubeCapture cube;
    cube.Edge=8;
    for(auto& face:cube.Faces)
        for(auto index=0u;index<cube.Edge*cube.Edge;++index)
            face.insert(face.end(),Pixel.begin(),Pixel.end());
    return cube;
}
}

int main() {
    try {
        auto cube=ConstantCube();
        SH9 sh;
        std::string error;
        Check(ProjectIrradiance(cube,sh,error),error);
        Check(std::fabs(sh[0].X-2.f)<1e-6f&&std::fabs(sh[0].Y-.5f)<1e-6f&&
            std::fabs(sh[0].Z-.125f)<1e-6f,"Diffuse SH of a constant sphere must preserve its radiance");
        for(std::size_t coefficient=1;coefficient<sh.size();++coefficient)
            Check(sh[coefficient].Length()<1e-5f,"A constant capture should have no directional SH");

        std::vector<std::uint8_t> artifact;
        Check(BuildReflectionAsset(cube,8,64,artifact,error),error);
        EnvironmentRenderData environment;
        using namespace PreparedResources;
        Check(EnvironmentAssetFormat::Describe(artifact,environment)&&environment.Specular.MipLevels==4,
            "Baker did not produce a complete prepared environment");
        for(std::size_t index=EnvironmentAssetFormat::HeaderBytes;index<artifact.size();index+=Pixel.size())
            Check(std::equal(Pixel.begin(),Pixel.end(),artifact.begin()+index),
                "A reflection mip lost linear HDR color");
        const auto latest=artifact;
        unsigned polls=0;
        Check(!BuildReflectionAsset(cube,8,64,artifact,error,[&]{return ++polls==6;})&&artifact==latest,
            "Cancelled native bake replaced the previous result");

        // A single +X emissive face must project on the x basis, with no y/z tilt.
        for(auto face=1u;face<cube.Faces.size();++face)
            std::fill(cube.Faces[face].begin(),cube.Faces[face].end(),std::uint8_t(0));
        Check(ProjectIrradiance(cube,sh,error),error);
        Check(sh[3].X>.3f&&std::fabs(sh[1].X)<1e-6f&&std::fabs(sh[2].X)<1e-6f,
            "Cubemap face orientation did not match the public world-space SH basis");
        IndirectLightingRenderData irradiance;
        irradiance.Dimensions={2,1,1};
        irradiance.Coefficients={sh,{}};
        Check(BuildIrradianceAsset(irradiance,artifact,error),error);
        IndirectLightingRenderData imported;
        Check(IndirectLightingAssetFormat::Decode(artifact,imported)&&imported.Dimensions==irradiance.Dimensions&&
            imported.Coefficients[0][3].X==sh[3].X&&imported.Coefficients[1][3].X==0,
            "Irradiance file did not preserve signed coefficients and sample order");
        cube.Faces.back().pop_back();
        Check(!ProjectIrradiance(cube,sh,error),"A truncated face must be rejected");
        std::cout<<"PASS native bake kernels: radiance, cube orientation, all reflection mips, cancellation and prepared formats\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
