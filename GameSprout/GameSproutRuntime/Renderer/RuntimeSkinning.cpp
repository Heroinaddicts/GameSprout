#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "RuntimeMath.h"
#include "Components/SkinnedMeshRendererComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace GameSproutRuntime {
std::optional<std::span<const Mat4>> Renderer::SkinMatrices(IGameObject& object,const MeshGPU& mesh,
    std::vector<Mat4>& matrices,SkinnedMeshRendererComponent** overrideSkin,
    SkinnedMeshRendererComponent* snapshotSkin) {
    matrices.clear();
    if(overrideSkin)*overrideSkin=nullptr;
    auto* skin=snapshotSkin?snapshotSkin:object.GetComponent<ISkinnedMeshRendererComponent>();
    if(!skin) return std::span<const Mat4>{};
    if(!mesh.HasCompleteSkinBindings()) {
        Diagnostic("Skinned mesh has no complete prepared bone bindings: "+object.GetName());return std::nullopt;
    }
    auto* builtin=snapshotSkin?snapshotSkin:dynamic_cast<SkinnedMeshRendererComponent*>(skin);
    if(builtin&&!builtin->_SkinPose.empty()&&_Runtime.IsInScene(builtin->_OwnerState)) {
        if(builtin->_SkinPose.size()!=mesh.Data.Bones.size()) {
            Diagnostic("Cannot bind skinned mesh '"+object.GetName()+"': Runtime skin pose does not match the mesh bone count");
            return std::nullopt;
        }
        // Borrow the component and its validated override only for this call site.
        if(overrideSkin)*overrideSkin=builtin;
        return std::span<const Mat4>(builtin->_SkinPose);
    }
    std::string error;
    const bool sampled=builtin?builtin->GetPresentationSkinMatrices(mesh.Data.Bones,matrices,error):
        skin->GetSkinMatrices(mesh.Data.Bones,matrices,error);
    if(!sampled || matrices.size()!=mesh.Data.Bones.size()) {
        Diagnostic("Cannot bind skinned mesh '"+object.GetName()+"': "+error);matrices.clear();return std::nullopt;
    }
    // The built-in component checks finiteness when supplying a pose or
    // constructing its Animator palette. Keep validation for other sources.
    if(!builtin)for(const auto& matrix:matrices) if(!RuntimeMath::IsFinite(matrix)) {
        Diagnostic("Skinned mesh has a nonfinite pose matrix: "+object.GetName());matrices.clear();return std::nullopt;
    }
    return std::span<const Mat4>(matrices);
}

bool Renderer::TryConservativeSkinPoseBounds(const MeshGPU& mesh,std::span<const Mat4> matrices,Bounds& bounds) noexcept {
    if(matrices.empty()) {
        bounds={mesh.CullMinimum*.5f+mesh.CullMaximum*.5f,mesh.CullMaximum*.5f-mesh.CullMinimum*.5f};
        return true;
    }

    constexpr auto infinity=std::numeric_limits<double>::infinity();
    double minimum[3]={infinity,infinity,infinity};
    double maximum[3]={-infinity,-infinity,-infinity};
    double magnitude[3]={};
#if defined(__SSE2__) || defined(_M_X64)
    auto minimumXY=_mm_set1_pd(infinity),maximumXY=_mm_set1_pd(-infinity),magnitudeXY=_mm_setzero_pd();
    const auto sign=_mm_set1_pd(-0.0);
#endif
    // Each weighted source point is inside its influencing joint's box.
    // Nonnegative weights make the posed point, divided by its total weight,
    // a convex combination inside the union AABB of the transformed boxes.
    // Geometry validation prepared the source boxes once. SkinMatrices has
    // already validated the palette, and only the first three rows affect the
    // positions written by linear skinning.
    const auto* jointBounds=mesh.JointBounds.data();
    const auto* palette=matrices.data();
    for(std::size_t joint=0;joint<mesh.JointBounds.size();++joint) {
        const auto& source=jointBounds[joint];
        const auto& matrix=palette[source.Joint].Data;
        const auto accumulateAxis=[&](std::size_t axis) {
            const double x=matrix[axis],y=matrix[axis+4],z=matrix[axis+8],translation=matrix[axis+12];
            const double ax=std::fabs(x),ay=std::fabs(y),az=std::fabs(z);
            const double center=translation+x*source.Center[0]+y*source.Center[1]+z*source.Center[2];
            const double radius=ax*source.Extent[0]+ay*source.Extent[1]+az*source.Extent[2];
            const double arithmeticMagnitude=std::fabs(translation)+ax*source.Magnitude[0]+ay*source.Magnitude[1]+az*source.Magnitude[2];
            if(const auto low=center-radius;low<minimum[axis])minimum[axis]=low;
            if(const auto high=center+radius;high>maximum[axis])maximum[axis]=high;
            if(arithmeticMagnitude>magnitude[axis])magnitude[axis]=arithmeticMagnitude;
        };
#if defined(__SSE2__) || defined(_M_X64)
        // Compute X and Y together with the scalar operation order and precision.
        const auto x=_mm_cvtps_pd(_mm_loadu_ps(matrix)),y=_mm_cvtps_pd(_mm_loadu_ps(matrix+4));
        const auto z=_mm_cvtps_pd(_mm_loadu_ps(matrix+8)),translation=_mm_cvtps_pd(_mm_loadu_ps(matrix+12));
        const auto ax=_mm_andnot_pd(sign,x),ay=_mm_andnot_pd(sign,y),az=_mm_andnot_pd(sign,z);
        const auto dot=[](__m128d vx,__m128d vy,__m128d vz,const double* weights,__m128d base) {
            auto value=_mm_add_pd(base,_mm_mul_pd(vx,_mm_set1_pd(weights[0])));
            value=_mm_add_pd(value,_mm_mul_pd(vy,_mm_set1_pd(weights[1])));
            return _mm_add_pd(value,_mm_mul_pd(vz,_mm_set1_pd(weights[2])));
        };
        const auto center=dot(x,y,z,source.Center,translation);
        const auto radius=_mm_add_pd(_mm_add_pd(_mm_mul_pd(ax,_mm_set1_pd(source.Extent[0])),
            _mm_mul_pd(ay,_mm_set1_pd(source.Extent[1]))),_mm_mul_pd(az,_mm_set1_pd(source.Extent[2])));
        const auto size=dot(ax,ay,az,source.Magnitude,_mm_andnot_pd(sign,translation));
        // MINPD/MAXPD select the second operand on ties, as do the scalar updates.
        minimumXY=_mm_min_pd(_mm_sub_pd(center,radius),minimumXY);
        maximumXY=_mm_max_pd(_mm_add_pd(center,radius),maximumXY);
        magnitudeXY=_mm_max_pd(size,magnitudeXY);
        accumulateAxis(2);
#else
        for(std::size_t axis=0;axis<3;++axis)accumulateAxis(axis);
#endif
    }
#if defined(__SSE2__) || defined(_M_X64)
    _mm_storeu_pd(minimum,minimumXY);
    _mm_storeu_pd(maximum,maximumXY);
    _mm_storeu_pd(magnitude,magnitudeXY);
#endif

    constexpr auto floatLimit=std::numeric_limits<float>::max();
    constexpr double roundingGuard=32.0*std::numeric_limits<float>::epsilon();
    float center[3],extent[3];
    for(std::size_t axis=0;axis<3;++axis) {
        // Validation allows a small normalization error. Include the actual
        // interval of per-vertex weight sums and guard float arithmetic.
        const double scaledMagnitude=magnitude[axis]*mesh.MaximumSkinWeightSum;
        const double padding=roundingGuard*scaledMagnitude;
        const double low=minimum[axis]*(minimum[axis]<0?mesh.MaximumSkinWeightSum:mesh.MinimumSkinWeightSum)-padding;
        const double high=maximum[axis]*(maximum[axis]>0?mesh.MaximumSkinWeightSum:mesh.MinimumSkinWeightSum)+padding;
        if(scaledMagnitude>floatLimit*.5||!(low<=high))return false;
        center[axis]=static_cast<float>((low+high)*.5);
        extent[axis]=std::nextafter(static_cast<float>(std::max(center[axis]-low,high-center[axis])),
            std::numeric_limits<float>::infinity());
    }
    bounds={{center[0],center[1],center[2]},{extent[0],extent[1],extent[2]}};
    return true;
}

bool Renderer::SkinPoseBounds(const MeshGPU& mesh,std::span<const Mat4> matrices,Bounds& bounds) {
    if(TryConservativeSkinPoseBounds(mesh,matrices,bounds))return true;
    // Very large finite inputs can overflow a conservative box even when the
    // real vertices are valid. Retain the exact path and its diagnostics then.
    std::vector<Vector3> positions;
    if(!PosedPositions(mesh,matrices,positions))return false;
    Vector3 low=positions.front(),high=low;
    for(const auto& point:positions) {
        low={std::min(low.X,point.X),std::min(low.Y,point.Y),std::min(low.Z,point.Z)};
        high={std::max(high.X,point.X),std::max(high.Y,point.Y),std::max(high.Z,point.Z)};
    }
    bounds={low*.5f+high*.5f,high*.5f-low*.5f};
    return true;
}

bool Renderer::PosedPositions(IGameObject& object,const MeshGPU& mesh,std::vector<Vector3>& positions) {
    positions.clear();
    std::vector<Mat4> matrices;
    const auto palette=SkinMatrices(object,mesh,matrices);
    return palette&&PosedPositions(mesh,*palette,positions);
}

bool Renderer::PosedPositions(const MeshGPU& mesh,std::span<const Mat4> matrices,std::vector<Vector3>& positions) {
    positions.clear();
    if(matrices.empty()) {positions=mesh.Positions;return true;}
    positions.reserve(mesh.Positions.size());
    for(std::size_t index=0;index<mesh.Positions.size();++index) {
        const auto& vertex=mesh.Skin[index];
        const auto& source=mesh.Positions[index];
        Vector3 position;
        // Identical linear skinning to the vertex shader; only called for bounds/editor queries.
        // The shared Mesh cache always retains bind positions, never a particular instance pose.
        for(std::size_t influence=0;influence<4;++influence) {
            const auto value=matrices[vertex.Bones[influence]]*Vector4{source.X,source.Y,source.Z,1};
            position=position+Vector3{value.X,value.Y,value.Z}*vertex.Weights[influence];
        }
        if(!RuntimeMath::IsFinite(position)) {positions.clear();Diagnostic("Skinned vertex is outside finite range");return false;}
        positions.push_back(position);
    }
    return true;
}
}
