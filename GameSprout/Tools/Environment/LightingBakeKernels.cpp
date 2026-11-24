#include "LightingBakeKernels.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>

namespace GameSproutLightingCapture {
namespace {

using GameSproutRuntime::Vector3;
constexpr std::uint32_t MaxCaptureEdge=2048;
constexpr float HalfMaximum=65504.f;

float DecodeHalf(std::uint16_t value) {
    const auto exponent=(value>>10)&31u;
    const auto fraction=value&1023u;
    const auto sign=std::uint32_t(value&0x8000u)<<16;
    if(exponent==0)return (value&0x8000u?-1.f:1.f)*std::ldexp(float(fraction),-24);
    if(exponent==31)return std::numeric_limits<float>::infinity();
    return std::bit_cast<float>(sign|((exponent+112u)<<23)|(fraction<<13));
}

std::uint16_t EncodeHalf(float value) {
    const auto bits=std::bit_cast<std::uint32_t>(value);
    const auto sign=(bits>>16)&0x8000u;
    const auto exponent=(bits>>23)&0xffu;
    const auto fraction=bits&0x7fffffu;
    // Inputs have already been clamped to the finite half-float range.
    if(exponent<102u)return static_cast<std::uint16_t>(sign);
    if(exponent<113u) {
        const auto shift=126u-exponent;
        const auto mantissa=fraction|0x800000u;
        return static_cast<std::uint16_t>(sign|
            ((mantissa+(1u<<(shift-1))-1u+((mantissa>>shift)&1u))>>shift));
    }
    const auto rounded=(fraction+0xfffu+((fraction>>13)&1u))>>13;
    return static_cast<std::uint16_t>(sign|(((exponent-112u)<<10)+rounded));
}

void AppendWord(std::vector<std::uint8_t>& bytes,std::uint32_t value,unsigned byteCount) {
    for(unsigned index=0;index<byteCount;++index)
        bytes.push_back(static_cast<std::uint8_t>(value>>(index*8)));
}
void AppendVector(std::vector<std::uint8_t>& bytes,Vector3 value) {
    for(float component:{value.X,value.Y,value.Z})AppendWord(bytes,std::bit_cast<std::uint32_t>(component),4);
}
void AppendPixel(std::vector<std::uint8_t>& bytes,Vector3 value) {
    for(float component:{value.X,value.Y,value.Z})
        AppendWord(bytes,EncodeHalf(std::clamp(component,0.f,HalfMaximum)),2);
    AppendWord(bytes,0x3c00,2);
}

struct CubeRGB {
    std::uint32_t Edge=0;
    std::array<std::vector<Vector3>,6> Faces;
};

bool DecodeCube(const LinearCubeCapture& input,CubeRGB& output,std::string& error) {
    if(!input.Edge||input.Edge>MaxCaptureEdge) {
        error="Lighting capture face edge must be in 1–"+std::to_string(MaxCaptureEdge);
        return false;
    }
    output.Edge=input.Edge;
    const std::size_t count=std::size_t(input.Edge)*input.Edge;
    for(std::size_t face=0;face<input.Faces.size();++face) {
        const auto& bytes=input.Faces[face];
        if(bytes.size()!=count*8) {
            error="Lighting capture requires six square RGBA16F faces";
            return false;
        }
        auto& pixels=output.Faces[face];
        pixels.resize(count);
        for(std::size_t index=0;index<count;++index) {
            const auto channel=[&](unsigned component) {
                const auto offset=index*8+component*2;
                return DecodeHalf(std::uint16_t(bytes[offset])|(std::uint16_t(bytes[offset+1])<<8));
            };
            Vector3 pixel{channel(0),channel(1),channel(2)};
            if(!std::isfinite(pixel.X)||!std::isfinite(pixel.Y)||!std::isfinite(pixel.Z)) {
                error="Lighting capture contains nonfinite RGB";
                return false;
            }
            pixels[index]={std::max(0.f,pixel.X),std::max(0.f,pixel.Y),std::max(0.f,pixel.Z)};
        }
    }
    return true;
}

Vector3 CubeDirection(unsigned face,float s,float t) {
    const Vector3 axes[]={{1,-t,-s},{-1,-t,s},{s,1,t},{s,-1,-t},{s,-t,1},{-s,-t,-1}};
    return axes[face]*(1.f/std::sqrt(1.f+s*s+t*t));
}

SH9 ProjectDiffuse(const CubeRGB& cube) {
    // Cubemap differential solid angle integrated over each actual texel.
    const auto edge=cube.Edge;
    const auto area=[](double s,double t){return std::atan2(s*t,std::sqrt(1+s*s+t*t));};
    const auto coordinate=[edge](std::uint32_t index){return 2.*index/edge-1.;};
    std::vector<double> corners(std::size_t(edge+1)*(edge+1));
    for(std::uint32_t row=0;row<=edge;++row)
        for(std::uint32_t column=0;column<=edge;++column)
            corners[std::size_t(row)*(edge+1)+column]=area(coordinate(column),coordinate(row));

    constexpr double normalization[]={.282094792,.488602512,.488602512,.488602512,
        1.092548431,1.092548431,.315391565,1.092548431,.546274215};
    constexpr double lambertBands[]={1,2./3,2./3,2./3,.25,.25,.25,.25,.25};
    double sums[9][3]{};
    for(unsigned face=0;face<6;++face) {
        for(std::uint32_t row=0;row<edge;++row)for(std::uint32_t column=0;column<edge;++column) {
            const auto corner=std::size_t(row)*(edge+1)+column;
            const double weight=corners[corner+edge+2]-corners[corner+edge+1]-corners[corner+1]+corners[corner];
            const auto normal=CubeDirection(face,float(coordinate(column)+1./edge),float(coordinate(row)+1./edge));
            const double x=normal.X,y=normal.Y,z=normal.Z;
            const double basis[]={1,y,z,x,x*y,y*z,3*z*z-1,x*z,x*x-y*y};
            const auto color=cube.Faces[face][std::size_t(row)*edge+column];
            for(unsigned coefficient=0;coefficient<9;++coefficient) {
                const auto scale=weight*basis[coefficient];
                sums[coefficient][0]+=scale*color.X;
                sums[coefficient][1]+=scale*color.Y;
                sums[coefficient][2]+=scale*color.Z;
            }
        }
    }
    SH9 output;
    for(unsigned coefficient=0;coefficient<9;++coefficient) {
        const auto scale=normalization[coefficient]*normalization[coefficient]*lambertBands[coefficient];
        output[coefficient]={float(sums[coefficient][0]*scale),float(sums[coefficient][1]*scale),
                             float(sums[coefficient][2]*scale)};
    }
    return output;
}

struct FilterRay {
    Vector3 Direction;
    float Weight;
};
struct ReflectionFilter {
    std::vector<FilterRay> Rays;
    float WeightSum=0;
};

ReflectionFilter GGXRays(std::uint32_t samples,float roughness) {
    ReflectionFilter output;
    output.Rays.reserve(samples);
    const float alpha=roughness*roughness;
    const auto bits=static_cast<unsigned>(std::bit_width(samples)-1);
    for(std::uint32_t index=0;index<samples;++index) {
        std::uint32_t reverse=0;
        for(unsigned bit=0;bit<bits;++bit)reverse|=((index>>bit)&1u)<<(bits-bit-1);
        const auto xi=float(reverse)/samples;
        const float cosSquared=(1.f-xi)/(1.f+(alpha*alpha-1.f)*xi);
        const float z=2.f*cosSquared-1.f;
        if(z<=0)continue;
        const float azimuth=2.f*std::numbers::pi_v<float>*index/samples;
        const float radial=2.f*std::sqrt(cosSquared*(1.f-cosSquared));
        output.Rays.push_back({{std::cos(azimuth)*radial,std::sin(azimuth)*radial,z},z});
        output.WeightSum+=z;
    }
    return output;
}

Vector3 Lerp(Vector3 left,Vector3 right,float weight) {
    return left*(1.f-weight)+right*weight;
}

Vector3 SampleCube(const CubeRGB& cube,Vector3 direction) {
    const auto x=direction.X,y=direction.Y,z=direction.Z;
    const auto ax=std::fabs(x),ay=std::fabs(y),az=std::fabs(z);
    unsigned face;
    float s,t,dimension;
    if(ax>=ay&&ax>=az) {face=x>=0?0:1;s=x>=0?-z:z;t=-y;dimension=ax;}
    else if(ay>=az) {face=y>=0?2:3;s=x;t=y>=0?z:-z;dimension=ay;}
    else {face=z>=0?4:5;s=z>=0?x:-x;t=-y;dimension=az;}
    const auto edge=cube.Edge;
    const float u=std::clamp((s/dimension+1.f)*edge*.5f-.5f,0.f,float(edge-1));
    const float v=std::clamp((t/dimension+1.f)*edge*.5f-.5f,0.f,float(edge-1));
    const auto x0=static_cast<std::uint32_t>(u),y0=static_cast<std::uint32_t>(v);
    const auto x1=std::min(x0+1,edge-1),y1=std::min(y0+1,edge-1);
    const auto& pixels=cube.Faces[face];
    const auto row0=std::size_t(y0)*edge,row1=std::size_t(y1)*edge;
    return Lerp(Lerp(pixels[row0+x0],pixels[row0+x1],u-x0),
                Lerp(pixels[row1+x0],pixels[row1+x1],u-x0),v-y0);
}

Vector3 FilterReflection(const CubeRGB& cube,Vector3 normal,const ReflectionFilter& filter) {
    Vector3 tangent;
    const Vector3 axis=std::fabs(normal.Y)>.99f?Vector3{1,0,0}:Vector3{0,1,0};
    axis.Cross(normal).TryNormalize(tangent);
    const auto bitangent=normal.Cross(tangent);
    Vector3 sum;
    for(const auto& ray:filter.Rays) {
        const auto& r=ray.Direction;
        const auto direction=tangent*r.X+bitangent*r.Y+normal*r.Z;
        sum=sum+SampleCube(cube,direction)*ray.Weight;
    }
    return sum*(1.f/filter.WeightSum);
}

}

bool ProjectIrradiance(const LinearCubeCapture& cube,SH9& output,std::string& error) {
    error.clear();
    CubeRGB decoded;
    if(!DecodeCube(cube,decoded,error))return false;
    output=ProjectDiffuse(decoded);
    return true;
}

bool BuildReflectionAsset(const LinearCubeCapture& cube,std::uint32_t reflectionEdge,std::uint32_t samples,
    std::vector<std::uint8_t>& output,std::string& error,const std::function<bool()>& cancelled) {
    error.clear();
    const auto stopped=[&]{
        if(!cancelled||!cancelled())return false;
        error="Reflection bake cancelled";
        return true;
    };
    if(stopped())return false;
    const auto skyEdge=cube.Edge;
    if(!std::has_single_bit(skyEdge)||!std::has_single_bit(reflectionEdge)||reflectionEdge>1024||
       !std::has_single_bit(samples)||samples<2||samples>1024) {
        error="Environment face sizes and sample count must be supported powers of two";
        return false;
    }
    CubeRGB decoded;
    if(!DecodeCube(cube,decoded,error))return false;
    const auto diffuse=ProjectDiffuse(decoded);
    if(stopped())return false;
    const auto levels=static_cast<std::uint32_t>(std::bit_width(reflectionEdge));
    std::size_t texelsPerFace=std::size_t(skyEdge)*skyEdge;
    for(auto edge=reflectionEdge;edge>0;edge>>=1)texelsPerFace+=std::size_t(edge)*edge;
    std::vector<std::uint8_t> encoded;
    encoded.reserve(24+9*12+texelsPerFace*6*8);
    constexpr std::string_view magic="GSENV001";
    encoded.insert(encoded.end(),magic.begin(),magic.end());
    for(auto word:{skyEdge,reflectionEdge,levels,0u})AppendWord(encoded,word,4);
    for(const auto& coefficient:diffuse)AppendVector(encoded,coefficient);
    for(const auto& face:decoded.Faces) {
        if(stopped())return false;
        for(const auto& pixel:face)AppendPixel(encoded,pixel);
    }
    for(unsigned mip=0;mip<levels;++mip) {
        const auto edge=reflectionEdge>>mip;
        const auto filter=mip?GGXRays(samples,float(mip)/float(levels-1)):ReflectionFilter{};
        for(unsigned face=0;face<6;++face)
            for(std::uint32_t row=0;row<edge;++row) {
                if(stopped())return false;
                for(std::uint32_t column=0;column<edge;++column) {
                    const auto direction=CubeDirection(face,(2.f*column+1.f)/edge-1.f,(2.f*row+1.f)/edge-1.f);
                    const auto filtered=mip?FilterReflection(decoded,direction,filter):SampleCube(decoded,direction);
                    AppendPixel(encoded,filtered);
                }
            }
    }
    output.swap(encoded);
    return true;
}

bool BuildIrradianceAsset(const GameSproutRuntime::IndirectLightingRenderData& data,
    std::vector<std::uint8_t>& output,std::string& error) {
    error.clear();
    if(!GameSproutRuntime::IsValidIndirectLightingGrid(data)) {
        error="Invalid irradiance grid dimensions, cell count or spherical harmonics coefficients";
        return false;
    }
    std::vector<std::uint8_t> encoded;
    encoded.reserve(24+data.Coefficients.size()*9*12);
    constexpr std::string_view magic="GSIRR001";
    encoded.insert(encoded.end(),magic.begin(),magic.end());
    for(auto dimension:data.Dimensions)AppendWord(encoded,dimension,4);
    AppendWord(encoded,0,4);
    for(const auto& cell:data.Coefficients)for(const auto& coefficient:cell)AppendVector(encoded,coefficient);
    output.swap(encoded);
    return true;
}

}
