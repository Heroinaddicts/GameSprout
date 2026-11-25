#include "CavalryStress.h"

#include "Math/TransformMath.h"
#include "RuntimeAnimationApi.h"
#include "RuntimePhysicsApi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <string_view>
#include <unordered_map>
#include <utility>

using namespace GameSproutRuntime;

namespace {
constexpr float Turn = 2 * std::numbers::pi_v<float>;
constexpr std::size_t RidersPerFrame = 24;
constexpr std::uint8_t CavalryRenderLayer = 8;
// Forward speed implied by the ground-contact hoof tracks of the source
// in-place clips, in metres per second at normal playback.
constexpr float WalkSpeed = 1.312f;
constexpr float CanterSpeed = 4.8f;

float RandomUnit(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(state >> 8) / 16777216.0f;
}

Vector3 HeadingDirection(float heading) {
    return {std::sin(heading), 0, std::cos(heading)};
}

bool PlaceRider(ITransformComponent& transform, Vector3 position, Vector3 normal, Vector3 forward) {
    // Lift the XZ movement direction into the ground tangent. Orthogonal
    // projection would also change heading on a slope with crossfall.
    forward.Y = -(forward.X * normal.X + forward.Z * normal.Z) / normal.Y;
    Quaternion rotation;
    if (!RuntimeMath::TryMakeLookRotation(forward, normal, rotation))
        return false;
    transform.SetLocalPosition(position);
    return transform.SetLocalRotation(rotation);
}

Mat4 Matrix(const TransformPose& pose) {
    return RuntimeMath::ComposeTRS(pose.Position,pose.Rotation,pose.Scale);
}

// Prepared clips already validate finite, nonzero quaternion samples. Short
// arc interpolation keeps the blended quaternion away from zero.
Mat4 SampledMatrix(const TransformPose& pose) {
    const auto [x,y,z,w] = pose.Rotation;
    const auto [a,b,c] = pose.Scale;
    const float normal = 2.f/(x*x+y*y+z*z+w*w);
    const float xx=x*x*normal, yy=y*y*normal, zz=z*z*normal;
    const float xy=x*y*normal, xz=x*z*normal, yz=y*z*normal;
    const float xw=x*w*normal, yw=y*w*normal, zw=z*w*normal;
    Mat4 matrix;
    matrix.Data[0]=(1-yy-zz)*a; matrix.Data[1]=(xy+zw)*a; matrix.Data[2]=(xz-yw)*a;
    matrix.Data[4]=(xy-zw)*b; matrix.Data[5]=(1-xx-zz)*b; matrix.Data[6]=(yz+xw)*b;
    matrix.Data[8]=(xz+yw)*c; matrix.Data[9]=(yz-xw)*c; matrix.Data[10]=(1-xx-yy)*c;
    matrix.Data[12]=pose.Position.X; matrix.Data[13]=pose.Position.Y; matrix.Data[14]=pose.Position.Z;
    return matrix;
}

Mat4 AffineProduct(const Mat4& a, const Mat4& b) {
    Mat4 output;
    for (std::size_t column=0;column<16;column+=4) {
        const auto x=b.Data[column], y=b.Data[column+1], z=b.Data[column+2];
        for (std::size_t row=0;row<3;++row)
            output.Data[column+row]=a.Data[row]*x+a.Data[row+4]*y+a.Data[row+8]*z;
    }
    output.Data[12]+=a.Data[12];
    output.Data[13]+=a.Data[13];
    output.Data[14]+=a.Data[14];
    return output;
}

Mat4 LocalMatrix(IGameObject& node) {
    return node.GetTransform()->GetLocalMatrix();
}

bool PlacePart(IGameObject& node, const Mat4& value) {
    Vector3 position, scale;
    Quaternion rotation;
    if (!RuntimeMath::TryDecomposeTRS(value,position,rotation,scale))
        return false;
    auto& transform = *node.GetTransform();
    transform.SetLocalPosition(position);
    transform.SetLocalScale(scale);
    return transform.SetLocalRotation(rotation);
}

bool BuildModelMatrices(const ModelData& model, std::vector<Mat4>& worlds, std::string& error) {
    worlds.resize(model.Nodes.size());
    for (std::size_t index = 0; index < model.Nodes.size(); ++index) {
        const auto& node = model.Nodes[index];
        if (node.Parent < -1 || node.Parent >= static_cast<std::int32_t>(index)) {
            error = "Cavalry requires a parent-ordered imported rig";
            return false;
        }
        const auto local = Matrix(node.Transform);
        worlds[index] = node.Parent < 0 ? local : worlds[std::size_t(node.Parent)] * local;
    }
    return true;
}

TransformPose Mix(const TransformPose& left, const TransformPose& right, float weight) {
    const auto& a = left.Rotation;
    auto b = right.Rotation;
    if (a.X*b.X + a.Y*b.Y + a.Z*b.Z + a.W*b.W < 0)
        b = {-b.X,-b.Y,-b.Z,-b.W};
    // Source samples are 30 Hz. Normalized interpolation follows the short
    // quaternion arc and also blends position/scale in the clip's local space.
    return {left.Position + (right.Position - left.Position) * weight,
        {std::lerp(a.X,b.X,weight),std::lerp(a.Y,b.Y,weight),std::lerp(a.Z,b.Z,weight),std::lerp(a.W,b.W,weight)},
        left.Scale + (right.Scale - left.Scale) * weight};
}

struct SamplingInterval {
    const AnimationFrame& Left;
    const AnimationFrame& Right;
    float Weight;
};
SamplingInterval Interval(const std::vector<AnimationFrame>& frames, float time) {
    auto right = std::upper_bound(frames.begin(),frames.end(),time,
        [](float value, const AnimationFrame& sample) { return value < sample.Time; });
    if (right == frames.begin()) return {*right,*right,0};
    const auto left = right - 1;
    if (right == frames.end()) return {*left,*left,0};
    return {*left,*right,std::clamp((time-left->Time)/(right->Time-left->Time),0.f,1.f)};
}

TransformPose SampleTrack(const SamplingInterval& interval, std::size_t track) {
    return interval.Weight == 0 ? interval.Left.Poses[track] :
        Mix(interval.Left.Poses[track],interval.Right.Poses[track],interval.Weight);
}

IGameObject* FindNamed(IGameObject& root, std::string_view name) {
    std::vector<IGameObject*> pending{&root};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        auto* node = pending[index];
        if (node->GetName() == name) return node;
        for (std::size_t child = 0; child < node->GetChildCount(); ++child)
            pending.push_back(node->GetChild(child));
    }
    return nullptr;
}
}

void CavalryStress::OnInitialized(IGameSproutRuntime* runtime) {
    if (RiderCount == 0) return;
    std::string error;
    if (!CaptureVisual(*runtime,error) || !SampleTerrain(*runtime,error)) {
        Fail(*runtime,error);
        return;
    }
    auto* group = _GameObject->GetScene()->CreateGameObject("Cavalry Riders",_GameObject);
    if (!group) {
        Fail(*runtime,"Cannot create the cavalry runtime group");
        return;
    }
    _GroupID = group->GetInstanceID();
    _Riders.reserve(RiderCount);
    GS_TRACE(runtime,"Cavalry skeleton queue: %u riders, %zu walkable cells",RiderCount,_StartPositions.size());
}

bool CavalryStress::CaptureVisual(IGameSproutRuntime& runtime, std::string& error) {
    if (!VisualTemplate || VisualTemplate == _GameObject || VisualTemplate->GetScene() != _GameObject->GetScene()) {
        error = "Cavalry needs an authored horse/rider template in this scene";
        return false;
    }
    auto* provider = runtime.GetResourceProvider();
    auto* horse = FindNamed(*VisualTemplate,"Warhorse Rig");
    auto* seat = FindNamed(*VisualTemplate,"Rider Seat");
    auto* knight = FindNamed(*VisualTemplate,"Mounted Knight");
    auto* knightAnimator = knight ? knight->GetComponent<IAnimatorComponent>() : nullptr;
    auto* horseAnimator = horse ? horse->GetComponent<IAnimatorComponent>() : nullptr;
    if (!provider || !horseAnimator || !seat || !seat->Parent() || !knightAnimator) {
        error = "Cavalry template requires the horse rig, saddle socket and seated rider";
        return false;
    }
    // HorseRider writes steering and slope rotation on the live template.
    // Keep the model's alignment on Warhorse Rig so each cavalry agent can
    // turn independently of the player's current direction.
    const auto& visual = *VisualTemplate->GetTransform();
    _HorseRig = RuntimeMath::ComposeTRS(visual.GetLocalPosition(),{},visual.GetLocalScale()) * LocalMatrix(*horse);
    _Seat = LocalMatrix(*seat) * LocalMatrix(*knight);

    ModelData horseModel, knightModel, helmetModel;
    if (!provider->LoadModel(HorseModel,horseModel,error) || !provider->LoadModel(KnightModel,knightModel,error) ||
        !provider->LoadModel(HelmetModel,helmetModel,error))
        return false;

    const std::array clipReferences{WalkClip,CanterClip,horseAnimator->GetDefaultClip()};
    std::array<AnimationClipData,GaitCount> importedClips;
    std::array<std::vector<std::size_t>,GaitCount> trackForNode;
    for (std::size_t index = 0; index < importedClips.size(); ++index) {
        auto& clip = importedClips[index];
        if (!provider->LoadAnimationClip(clipReferences[index],clip,error)) return false;
        if (clip.Frames.empty() || !std::isfinite(clip.Duration) || clip.Duration <= 0 ||
            std::any_of(clip.Frames.begin(),clip.Frames.end(),[&](const AnimationFrame& frame) {
                return frame.Poses.size()!=clip.Tracks.size();
            })) {
            error = "Cavalry gait must be a timed animation clip";
            return false;
        }
        auto& tracks = trackForNode[index];
        tracks.assign(horseModel.Nodes.size(),NoNode);
        std::unordered_map<std::string_view,std::size_t> trackIndices;
        for (std::size_t track = 0; track < clip.Tracks.size(); ++track)
            trackIndices.emplace(clip.Tracks[track],track);
        for (std::size_t node = 0; node < horseModel.Nodes.size(); ++node)
            if (auto found = trackIndices.find(horseModel.Nodes[node].Path); found != trackIndices.end())
                tracks[node] = found->second;
        if (std::all_of(tracks.begin(),tracks.end(),[](std::size_t track){return track==NoNode;})) {
            error = "Cavalry gait has no tracks for the horse skeleton";
            return false;
        }
    }

    AnimationClipData seatedClip;
    if (!provider->LoadAnimationClip(knightAnimator->GetDefaultClip(),seatedClip,error)) return false;
    if (seatedClip.Frames.empty() || seatedClip.Frames.front().Poses.size() != seatedClip.Tracks.size()) {
        error = "Cavalry template needs a valid mounted rider pose";
        return false;
    }
    for (std::size_t track = 0; track < seatedClip.Tracks.size(); ++track) {
        const auto node = std::find_if(knightModel.Nodes.begin(),knightModel.Nodes.end(),[&](const ModelNode& item) {
            return item.Path == seatedClip.Tracks[track];
        });
        if (node != knightModel.Nodes.end()) node->Transform = seatedClip.Frames.front().Poses[track];
    }

    std::vector<Mat4> horseRest, knightRest, helmetRest;
    if (!BuildModelMatrices(horseModel,horseRest,error) || !BuildModelMatrices(knightModel,knightRest,error) ||
        !BuildModelMatrices(helmetModel,helmetRest,error))
        return false;
    const auto socket = std::find_if(horseModel.Nodes.begin(),horseModel.Nodes.end(),[&](const ModelNode& node) {
        return node.Name == seat->Parent()->GetName();
    });
    if (socket == horseModel.Nodes.end()) {
        error = "Cavalry saddle socket is absent from the horse skin";
        return false;
    }
    _SeatBone = std::size_t(socket-horseModel.Nodes.begin());

    std::size_t triangleCount = 0;
    const auto prepare = [&](Part& part, const ModelData& model, const std::vector<Mat4>& poses,
            std::string_view meshName, std::string_view materialSource, bool animated) {
        const auto found = std::find_if(model.Nodes.begin(),model.Nodes.end(),[&](const ModelNode& node) {
            return node.Name == meshName && !node.Mesh.Guid.empty();
        });
        if (found == model.Nodes.end()) {
            error = "Cavalry equipment mesh is missing: " + std::string(meshName);
            return false;
        }
        part.Name = meshName;
        part.Mesh = found->Mesh;
        const auto modelPose = poses[std::size_t(found-model.Nodes.begin())] * Matrix(found->GeometryTransform);
        part.Local = animated ? _HorseRig * modelPose : modelPose;
        if (!RuntimeMath::TryInverseAffine(part.Local,part.InverseLocal)) {
            error = "Cavalry equipment transform is singular: " + part.Name;
            return false;
        }
        if (materialSource.empty()) {
            part.Materials = {ScoutMaterial};
        } else {
            const auto* authored = FindNamed(*VisualTemplate,materialSource);
            const auto* renderer = authored ? authored->GetComponent<IMeshRendererComponent>() : nullptr;
            if (!renderer || !renderer->GetMaterialCount()) {
                error = "Cavalry equipment material is missing: " + std::string(materialSource);
                return false;
            }
            for (std::size_t slot = 0; slot < renderer->GetMaterialCount(); ++slot)
                part.Materials.push_back(renderer->GetMaterial(slot));
        }
        MeshRenderData geometry;
        if (!provider->LoadMesh(part.Mesh,geometry,error)) return false;
        triangleCount += geometry.IndexCount/3;
        for (const auto& binding : geometry.Bones) {
            const auto bone = std::find_if(model.Nodes.begin(),model.Nodes.end(),[&](const ModelNode& node) {
                return node.Path == binding.NodePath;
            });
            if (bone == model.Nodes.end()) {
                error = "Cavalry equipment skeleton is incompatible: " + part.Name + " / " + binding.NodePath;
                return false;
            }
            const auto index = std::size_t(bone-model.Nodes.begin());
            part.Bones.push_back({index,binding.InverseBindMatrix});
            if (!animated) part.SeatedSkin.push_back(part.InverseLocal * poses[index] * binding.InverseBindMatrix);
        }
        return true;
    };

    if (ScoutMaterial.Guid.empty()) {
        error = "Cavalry needs the scout surcoat material";
        return false;
    }
    if (!prepare(_Parts[HorseBody],horseModel,horseRest,"Horse Body Low","Horse Body Geometry",true)
        || !prepare(_Parts[Saddle],horseModel,horseRest,"Saddle Low","Saddle Geometry",true)
        || !prepare(_Parts[Mane],horseModel,horseRest,"Horse Mane1 Low","Horse Mane1 Low Geometry",true)
        || !prepare(_Parts[Tail],horseModel,horseRest,"Horse Tail Low","Horse Tail Low Geometry",true)
        || !prepare(_Parts[KnightBody],knightModel,knightRest,"Knight_base_LOD4","Knight_base_LOD0 Geometry",false)
        || !prepare(_Parts[Chainmail],knightModel,knightRest,"Chainmail_LOD2","Chainmail_LOD0 Geometry",false)
        || !prepare(_Parts[Face],knightModel,knightRest,"KnightFace_LOD4","KnightFace_LOD0 Geometry",false)
        || !prepare(_Parts[Helmet],helmetModel,helmetRest,"Helmet_LOD2","Helmet_LOD0 Geometry",false)
        || !prepare(_Parts[Kit],knightModel,knightRest,"Shoulders_LOD2","Shoulders_LOD0 Geometry",false)
        || !prepare(_ScoutKit,knightModel,knightRest,"CoatOfArms_LOD4",{},false))
        return false;

    for (std::size_t part=HorseBody;part<=Tail;++part)
        if (_Parts[part].Bones.empty()) {
            error = "Cavalry horse mesh has no skin: " + _Parts[part].Name;
            return false;
        }
    // Evaluate the common horse skeleton once per agent, restricted to the
    // ancestors of joints referenced by visible horse parts or the saddle.
    const auto& nodes = horseModel.Nodes;
    std::vector<bool> required(nodes.size());
    required[_SeatBone]=true;
    for (std::size_t part=HorseBody;part<=Tail;++part)
        for (const auto& bone:_Parts[part].Bones)
            required[bone.Node]=true;
    for (std::size_t reverse=nodes.size();reverse>0;--reverse) {
        const auto index=reverse-1;
        const auto parent=nodes[index].Parent;
        if (required[index]&&parent>=0) required[std::size_t(parent)]=true;
    }
    std::vector<std::size_t> compactIndex(nodes.size(),NoNode);
    for (std::size_t index=0;index<nodes.size();++index) {
        if (!required[index]) continue;
        const auto& node=nodes[index];
        const auto parent=node.Parent<0 ? NoNode : compactIndex[std::size_t(node.Parent)];
        unsigned clips=0;
        for (std::size_t gait=0;gait<GaitCount;++gait)
            if (trackForNode[gait][index]!=NoNode) clips|=1u<<gait;
        compactIndex[index]=_HorseJoints.size();
        _HorseJoints.push_back({parent,clips,Matrix(node.Transform)});
    }
    _HorseWorld.resize(_HorseJoints.size());
    _SeatBone=compactIndex[_SeatBone];
    for (std::size_t part=HorseBody;part<=Tail;++part)
        for (auto& bone:_Parts[part].Bones)
            bone.Node=compactIndex[bone.Node];
    for (std::size_t gait=0;gait<GaitCount;++gait) {
        const auto& source=importedClips[gait];
        auto& clip=_Clips[gait];
        clip.Duration=source.Duration;
        clip.Frames.reserve(source.Frames.size());
        for (const auto& sourceFrame:source.Frames) {
            AnimationFrame frame;
            frame.Time=sourceFrame.Time;
            frame.Poses.reserve(_HorseJoints.size());
            for (std::size_t index=0;index<nodes.size();++index) {
                if (!required[index]) continue;
                const auto track=trackForNode[gait][index];
                frame.Poses.push_back(track==NoNode ? nodes[index].Transform : sourceFrame.Poses[track]);
            }
            clip.Frames.push_back(std::move(frame));
        }
    }
    GS_TRACE(&runtime,"Cavalry uses imported mesh LODs (%zu total triangles across both outfits) and source Walk/Canter clips",triangleCount);
    return true;
}

bool CavalryStress::SampleTerrain(IGameSproutRuntime& runtime, std::string& error) {
    if (!(std::isfinite(CenterX) && std::isfinite(CenterZ) &&
          std::isfinite(RadiusX) && std::isfinite(RadiusZ) && RadiusX >= 4 && RadiusZ >= 4)) {
        error = "Cavalry area needs finite extents larger than a walkable cell";
        return false;
    }
    _MinX = CenterX - RadiusX;
    _MinZ = CenterZ - RadiusZ;
    constexpr float cellMeters = 4;
    _GroundWidth = std::size_t(std::clamp(std::ceil(RadiusX * 2 / cellMeters), 2.f, 160.f)) + 1;
    _GroundDepth = std::size_t(std::clamp(std::ceil(RadiusZ * 2 / cellMeters), 2.f, 160.f)) + 1;
    _CellX = RadiusX * 2 / float(_GroundWidth - 1);
    _CellZ = RadiusZ * 2 / float(_GroundDepth - 1);
    _Ground.resize(_GroundWidth * _GroundDepth);
    auto* scene = _GameObject->GetScene();
    PhysicsQuerySettings query;
    if (VisualTemplate)
        if (auto* player = VisualTemplate->Parent())
            query.IgnoredObjectIDs.push_back(player->GetInstanceID());
    for (std::size_t z = 0; z < _GroundDepth; ++z) {
        for (std::size_t x = 0; x < _GroundWidth; ++x) {
            PhysicsRaycastHit hit;
            const Ray ray{{_MinX + float(x) * _CellX, 60, _MinZ + float(z) * _CellZ}, {0, -1, 0}};
            if (runtime.Raycast(scene, ray, 110, hit, query, error))
                _Ground[z * _GroundWidth + x] = {hit.Position.Y, hit.Normal.Y >= .75f};
            else if (!error.empty())
                return false;
        }
    }
    for (std::size_t z = 0; z + 1 < _GroundDepth; ++z) {
        for (std::size_t x = 0; x + 1 < _GroundWidth; ++x) {
            const Vector2 center{_MinX + (float(x) + .5f) * _CellX, _MinZ + (float(z) + .5f) * _CellZ};
            Vector3 position, normal;
            // Start on terrain with enough clearance for the randomized offset
            // and first stride. A grid corner alone can lie on a steep cell.
            constexpr float clearance = 1.5f;
            if (Ground(center.X, center.Y, position, normal) &&
                Ground(center.X - clearance, center.Y - clearance, position, normal) &&
                Ground(center.X + clearance, center.Y - clearance, position, normal) &&
                Ground(center.X - clearance, center.Y + clearance, position, normal) &&
                Ground(center.X + clearance, center.Y + clearance, position, normal)) {
                _StartPositions.push_back(center);
            }
        }
    }
    if (_StartPositions.empty()) {
        error = "Cavalry area contains no walkable ground";
        return false;
    }
    return true;
}

bool CavalryStress::Ground(float x, float z, Vector3& position, Vector3& normal) const {
    const float u = (x - _MinX) / _CellX, v = (z - _MinZ) / _CellZ;
    if (!(u >= 0 && v >= 0 && u < float(_GroundWidth - 1) && v < float(_GroundDepth - 1)))
        return false;
    const std::size_t ix = std::size_t(u), iz = std::size_t(v), index = iz * _GroundWidth + ix;
    const auto& a = _Ground[index];
    const auto& b = _Ground[index + 1];
    const auto& c = _Ground[index + _GroundWidth];
    const auto& d = _Ground[index + _GroundWidth + 1];
    if (!a.Walkable || !b.Walkable || !c.Walkable || !d.Walkable)
        return false;
    const float tx = u - float(ix), tz = v - float(iz);
    const float slopeX = std::lerp(b.Height - a.Height, d.Height - c.Height, tz) / _CellX;
    const float slopeZ = std::lerp(c.Height - a.Height, d.Height - b.Height, tx) / _CellZ;
    if (slopeX * slopeX + slopeZ * slopeZ > .75f)
        return false;
    position = {x, std::lerp(std::lerp(a.Height, b.Height, tx), std::lerp(c.Height, d.Height, tx), tz), z};
    return Vector3{-slopeX, 1, -slopeZ}.TryNormalize(normal);
}

bool CavalryStress::ConfigurePart(IGameObject& object, const Part& part, std::string& error) {
    auto* mesh = object.AddComponent<IMeshComponent>();
    auto* renderer = object.AddComponent<IMeshRendererComponent>();
    if (!mesh || !renderer || !renderer->SetMaterials(part.Materials) || !PlacePart(object,part.Local)) {
        error = "Cannot configure cavalry mesh: " + part.Name;
        return false;
    }
    mesh->SetMesh(part.Mesh);
    if (part.Bones.empty()) return true;
    auto* skin = object.AddComponent<ISkinnedMeshRendererComponent>();
    if (!skin || (!part.SeatedSkin.empty() && !skin->SetSkinPose(part.SeatedSkin))) {
        error = "Cannot configure cavalry skin: " + part.Name;
        return false;
    }
    return true;
}

bool CavalryStress::CreateRider(IGameObject& group, std::string& error) {
    Rider rider;
    const auto ordinal = std::uint32_t(_Riders.size() + 1);
    rider.RandomState = ordinal * 0x9e3779b9u;
    const auto index = std::size_t(RandomUnit(rider.RandomState) * float(_StartPositions.size()));
    const auto start = _StartPositions[std::min(index, _StartPositions.size() - 1)];
    rider.X = start.X;
    rider.Z = start.Y;
    rider.Heading = rider.DesiredHeading = RandomUnit(rider.RandomState) * Turn;
    rider.TurnAfter = RandomUnit(rider.RandomState) * 2.5f;
    rider.Cantering = RandomUnit(rider.RandomState) < .65f;
    rider.CanterWeight = rider.Cantering ? 1.f : 0.f;
    rider.SpeedAtGait = .9f + RandomUnit(rider.RandomState) * .22f;
    rider.Speed = std::lerp(WalkSpeed,CanterSpeed,rider.CanterWeight)*rider.SpeedAtGait;
    rider.GaitAfter = .9f + RandomUnit(rider.RandomState) * 4.f;
    rider.StridePhase = RandomUnit(rider.RandomState);
    rider.IdleTime = RandomUnit(rider.RandomState) * _Clips[Idle].Duration;
    rider.Scout = (ordinal % 3) == 0;
    rider.OutfitAfter = (ordinal <= 24 ? 2.f : 7.f) + RandomUnit(rider.RandomState) * 9.f;

    Vector3 position, normal;
    const float jitterX = start.X + (RandomUnit(rider.RandomState)-.5f) * _CellX * .55f;
    const float jitterZ = start.Y + (RandomUnit(rider.RandomState)-.5f) * _CellZ * .55f;
    if (Ground(jitterX,jitterZ,position,normal)) {
        rider.X = jitterX;
        rider.Z = jitterZ;
    } else if (!Ground(rider.X,rider.Z,position,normal)) {
        error = "Cavalry start cell has invalid terrain";
        return false;
    }
    rider.VisualGroundNormal = normal;

    auto& scene = *group.GetScene();
    auto* root = scene.CreateGameObject("Cavalry " + std::to_string(ordinal),&group);
    if (!root || !PlaceRider(*root->GetTransform(),position,normal,HeadingDirection(rider.Heading))) {
        error = "Cannot place a cavalry agent on sampled terrain";
        return false;
    }
    rider.ObjectID = root->GetInstanceID();
    auto* seat = scene.CreateGameObject("Rider Seat",root);
    if (!seat) {
        error = "Cannot create a cavalry saddle socket";
        return false;
    }
    rider.SeatID = seat->GetInstanceID();

    for (std::size_t slot = 0; slot < _Parts.size(); ++slot) {
        const auto& part = slot==Kit && rider.Scout ? _ScoutKit : _Parts[slot];
        const auto* name = slot==Kit ? "Rider Kit" : part.Name.c_str();
        auto* object = scene.CreateGameObject(name,slot<=Tail ? root : seat);
        if (!object || !ConfigurePart(*object,part,error)) {
            if (error.empty()) error = "Cannot create cavalry part: " + part.Name;
            return false;
        }
        object->SetLayer(CavalryRenderLayer);
        rider.Parts[slot] = object->GetInstanceID();
        const bool show = (slot!=Face||rider.Scout) && (slot!=Helmet||!rider.Scout);
        auto* renderer = object->GetComponent<IMeshRendererComponent>();
        renderer->SetVisible(show);
        // The horse and rider bodies supply the moving silhouette. The saddle
        // and small layered outfit pieces are optional depth casters.
        if (!DetailedShadows && slot != HorseBody && slot != KnightBody)
            renderer->SetShadowCastingMode(ShadowCastingMode::Off);
    }
    _Riders.push_back(rider);
    return true;
}

void CavalryStress::SampleHorse(const Rider& rider) {
    const auto interval=[&](Gait gait) {
        const auto& clip=_Clips[gait];
        return Interval(clip.Frames,gait==Idle ? rider.IdleTime : rider.StridePhase*clip.Duration);
    };
    const auto evaluate=[&](unsigned active,auto sample) {
        for (std::size_t index=0;index<_HorseJoints.size();++index) {
            const auto& joint=_HorseJoints[index];
            const auto local=(joint.Tracks&active) ? SampledMatrix(sample(index)) : joint.RestMatrix;
            _HorseWorld[index]=AffineProduct(joint.Parent==NoNode ? _HorseRig : _HorseWorld[joint.Parent],local);
        }
    };
    const auto idleWeight=rider.IdleWeight;
    const auto canterWeight=rider.CanterWeight;
    // Most riders sample one clip. Select it before walking the skeleton.
    if (idleWeight==1 || (idleWeight==0 && (canterWeight==0 || canterWeight==1))) {
        const auto gait=idleWeight==1 ? Idle : (canterWeight==1 ? Canter : Walk);
        const auto frame=interval(gait);
        evaluate(1u<<gait,[&](std::size_t index) {return SampleTrack(frame,index);});
        return;
    }

    // Both movement clips begin on right-hind hoof contact, so blending uses
    // the same stride phase. Untracked joints inherit their rest pose.
    const auto movingGait=canterWeight==1 ? Canter : Walk;
    const auto movingFrame=interval(movingGait);
    const bool blendGaits=canterWeight>0 && canterWeight<1;
    const auto canterFrame=blendGaits ? interval(Canter) : movingFrame;
    const auto idleFrame=idleWeight>0 ? interval(Idle) : movingFrame;
    const auto active=(1u<<movingGait)|(blendGaits ? 1u<<Canter : 0u)|(idleWeight>0 ? 1u<<Idle : 0u);
    evaluate(active,[&](std::size_t index) {
        auto moving=SampleTrack(movingFrame,index);
        if (blendGaits) moving=Mix(moving,SampleTrack(canterFrame,index),canterWeight);
        return idleWeight>0 ? Mix(moving,SampleTrack(idleFrame,index),idleWeight) : moving;
    });
}

bool CavalryStress::UpdateVisual(IGameSproutRuntime& runtime, Rider& rider, std::string& error) {
    SampleHorse(rider);
    auto* seat = runtime.QueryGameObject(rider.SeatID);
    if (!seat || !PlacePart(*seat,AffineProduct(_HorseWorld[_SeatBone],_Seat))) {
        error = "Cannot place the rider on the animated saddle socket";
        return false;
    }
    for (std::size_t slot = HorseBody; slot <= Tail; ++slot) {
        const auto& part = _Parts[slot];
        auto* object = runtime.QueryGameObject(rider.Parts[slot]);
        auto* skin = object ? object->GetComponent<ISkinnedMeshRendererComponent>() : nullptr;
        if (!skin) {
            error = "Cavalry live skin is missing: " + part.Name;
            return false;
        }
        _PaletteScratch.clear();
        for (const auto& bone : part.Bones)
            _PaletteScratch.push_back(AffineProduct(AffineProduct(part.InverseLocal,_HorseWorld[bone.Node]),bone.InverseBind));
        if (!skin->SetSkinPose(_PaletteScratch)) {
            error = "Cavalry rejected the pose of " + part.Name;
            return false;
        }
    }
    return true;
}

bool CavalryStress::ApplyOutfit(IGameSproutRuntime& runtime, const Rider& rider, std::string& error) {
    auto* kit = runtime.QueryGameObject(rider.Parts[Kit]);
    if (!kit) {
        error = "Cavalry outfit lost its equipment renderer";
        return false;
    }
    return ConfigurePart(*kit,rider.Scout ? _ScoutKit : _Parts[Kit],error);
}

bool CavalryStress::UpdateRiders(IGameSproutRuntime& runtime, float delta, std::string& error) {
    const float normalWeight = 1.f - std::exp(-14.f * delta);
    for (auto& rider : _Riders) {
        auto* root = runtime.QueryGameObject(rider.ObjectID);
        if (!root || !root->IsActiveSelf())
            continue;

        rider.GaitAfter -= delta;
        if (rider.GaitAfter <= 0) {
            rider.Cantering = !rider.Cantering;
            rider.GaitAfter = 3.f + RandomUnit(rider.RandomState)*5.f;
            ++_ClipChanges;
        }
        const auto target = rider.Cantering ? 1.f : 0.f;
        rider.CanterWeight += std::clamp(target-rider.CanterWeight,-delta*3.f,delta*3.f);
        rider.Speed = std::lerp(WalkSpeed,CanterSpeed,rider.CanterWeight)*rider.SpeedAtGait;
        rider.TurnAfter -= delta;
        if (rider.TurnAfter <= 0) {
            rider.DesiredHeading = rider.Heading+(RandomUnit(rider.RandomState)-.5f)*2.1f;
            rider.TurnAfter = .8f+RandomUnit(rider.RandomState)*2.6f;
        }
        const auto remaining = std::remainder(rider.DesiredHeading-rider.Heading,Turn);
        rider.Heading = std::remainder(rider.Heading+std::clamp(remaining,-2.f*delta,2.f*delta),Turn);
        const auto forward=HeadingDirection(rider.Heading);
        const auto x = rider.X+forward.X*rider.Speed*delta;
        const auto z = rider.Z+forward.Z*rider.Speed*delta;
        Vector3 position, normal;
        if (Ground(x,z,position,normal)) {
            rider.X = x;
            rider.Z = z;
        } else {
            const auto step = rider.Speed * delta;
            const auto walkableDirection = [&](float heading, float distance) {
                const float x = std::sin(heading), z = std::cos(heading);
                // A valid probe beyond a terrain seam does not make the first
                // step legal. Try the actual movement step before looking ahead.
                if (!Ground(rider.X+x*step,rider.Z+z*step,position,normal)) return false;
                return distance <= step ||
                    (Ground(rider.X+x*distance*.5f,rider.Z+z*distance*.5f,position,normal) &&
                     Ground(rider.X+x*distance,rider.Z+z*distance,position,normal));
            };
            const auto retreat = [&](float distance, int directions) {
                for (int attempt = 1; attempt <= directions; ++attempt) {
                    const auto offset = float((attempt + 1) / 2)*Turn/float(directions);
                    const auto heading = rider.Heading+(attempt % 2 ? offset : -offset);
                    if (walkableDirection(heading,distance)) {
                        rider.DesiredHeading = heading;
                        return true;
                    }
                }
                return false;
            };
            // A short step lets the horse escape a narrow walkable corner
            // where every half-metre probe crosses a rejected terrain cell.
            if (!walkableDirection(rider.DesiredHeading,.55f) &&
                !retreat(.55f,8) && !retreat(step,16))
                rider.DesiredHeading = rider.Heading + Turn/4.f;
            rider.TurnAfter = 2.5f;
            if (!Ground(rider.X,rider.Z,position,normal)) continue;
        }
        auto* transform = root->GetTransform();
        const auto distance = (position - transform->GetLocalPosition()).Length();
        // The height grid has continuous positions, but its slopes can jump
        // across cell boundaries. Smooth the mount and saddle's visual tilt.
        rider.VisualGroundNormal = rider.VisualGroundNormal + (normal - rider.VisualGroundNormal) * normalWeight;
        if (!PlaceRider(*transform,position,rider.VisualGroundNormal,forward)) {
            error = "Invalid terrain basis for moving cavalry";
            return false;
        }

        if (SwapEquipment) {
            rider.OutfitAfter -= delta;
            if (rider.OutfitAfter <= 0) {
                rider.Scout = !rider.Scout;
                if (!ApplyOutfit(runtime,rider,error)) return false;
                rider.OutfitAfter = 7.f+RandomUnit(rider.RandomState)*9.f;
                ++_OutfitChanges;
            }
        }
        if (AnimateCavalry) {
            const auto stride = std::lerp(WalkSpeed * _Clips[Walk].Duration,
                CanterSpeed * _Clips[Canter].Duration,rider.CanterWeight);
            rider.StridePhase += distance / stride;
            rider.StridePhase -= std::floor(rider.StridePhase);
            const auto targetIdle = distance <= 1e-5f ? 1.f : 0.f;
            rider.IdleWeight += std::clamp(targetIdle - rider.IdleWeight,-delta * 8.f,delta * 8.f);
            if (rider.IdleWeight > 0) {
                rider.IdleTime += delta;
                if (rider.IdleTime >= _Clips[Idle].Duration)
                    rider.IdleTime = std::fmod(rider.IdleTime,_Clips[Idle].Duration);
            }
            if (!UpdateVisual(runtime,rider,error)) return false;
        }
    }
    return true;
}

void CavalryStress::Update(IGameSproutRuntime* runtime, float deltaSeconds) {
    if (!_GroupID || !std::isfinite(deltaSeconds) || deltaSeconds <= 0) return;
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    const auto delta=std::min(deltaSeconds,.08f);
    if (!UpdateRiders(*runtime,delta,error)) {
        Fail(*runtime,error);
        return;
    }
    if (ProfileMovement && _Riders.size()==RiderCount) {
        _MovementMilliseconds += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        if (++_MovementFrames == 120) {
            GS_TRACE(runtime,"Cavalry script: %.3f ms/frame, %zu riders, %u gait changes, %u outfit changes",
                _MovementMilliseconds/_MovementFrames,_Riders.size(),_ClipChanges,_OutfitChanges);
            _MovementMilliseconds = 0;
            _MovementFrames = 0;
        }
    }
    if (_Riders.size() == RiderCount) return;

    auto* group = runtime->QueryGameObject(_GroupID);
    if (!group || !group->GetScene()) {
        Fail(*runtime,"Cavalry runtime group was removed");
        return;
    }
    const auto first = _Riders.size();
    const auto count = std::min<std::size_t>(RidersPerFrame,RiderCount-first);
    for (std::size_t i = 0; i < count; ++i) {
        if (!CreateRider(*group,error)) {
            Fail(*runtime,error);
            return;
        }
    }
}

void CavalryStress::LateUpdate(IGameSproutRuntime* runtime, float) {
    if (!_GroupID) return;
    // Newly assembled objects are visible to Runtime queries after the Update
    // structure barrier. Initialize their palettes even with clip playback paused.
    std::string error;
    while (_PoseReady < _Riders.size()) {
        if (!UpdateVisual(*runtime,_Riders[_PoseReady],error)) {
            Fail(*runtime,error);
            return;
        }
        ++_PoseReady;
    }
    if (!_ReportedReady && _PoseReady==RiderCount) {
        _ReportedReady = true;
        GS_TRACE(runtime,"Cavalry skeleton ready: %zu riders, %zu skinned horse parts",_Riders.size(),_Riders.size()*4);
    }
    if (AnimateCavalry && !_ReportedSwaps && _ReportedReady && _ClipChanges && _OutfitChanges) {
        _ReportedSwaps = true;
        GS_TRACE(runtime,"Cavalry actions verified: %u gait changes, %u outfit changes",_ClipChanges,_OutfitChanges);
    }
}

void CavalryStress::OnRelease(IGameSproutRuntime* runtime) {
    if (auto* group = runtime->QueryGameObject(_GroupID))
        if (auto* scene = group->GetScene())
            scene->DestroyGameObject(group);
    _GroupID = 0;
    _Riders.clear();
    _Ground.clear();
    _StartPositions.clear();
    _HorseJoints.clear();
    _HorseWorld.clear();
    _PaletteScratch.clear();
    _Parts = {};
    _ScoutKit = {};
    _Clips = {};
    _PoseReady = 0;
    _MovementFrames = 0;
    _MovementMilliseconds = 0;
    _ClipChanges = 0;
    _OutfitChanges = 0;
    _ReportedReady = false;
    _ReportedSwaps = false;
}

void CavalryStress::Fail(IGameSproutRuntime& runtime, const std::string& error) {
    GS_ERROR(&runtime,"Cavalry: %s",error.c_str());
    OnRelease(&runtime);
}
