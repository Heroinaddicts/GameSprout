#include "UnityAnimationImport.h"
#include "AnimationClipPreparation.h"
#include "AnimationResourceValidation.h"
#include "UnityAnimationSource.h"
#include "../Shared/AnimationImportSettingsJson.h"
#include "../Shared/AssetValueBinary.h"
#include "../Shared/UnityAnimationImportSettings.h"
#include <QDataStream>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace {
using GameSproutRuntime::Quaternion;
using GameSproutRuntime::SkeletonNode;
using GameSproutRuntime::TransformPose;
using UnityAnimationSource::Track;

bool ToFloat(double input, float& output) {
    if (!std::isfinite(input) || std::fabs(input)>std::numeric_limits<float>::max()) return false;
    output=static_cast<float>(input);
    return true;
}

template<std::size_t Size>
bool ReadNumbers(const Json::Value& value, float (&numbers)[Size]) {
    if (!value.isArray() || value.size()!=Size) return false;
    for (Json::ArrayIndex index=0;index<Size;++index)
        if (!value[index].isNumeric() || !ToFloat(value[index].asDouble(),numbers[index])) return false;
    return true;
}

bool ReadPose(const Json::Value& value, TransformPose& output) {
    float position[3],rotation[4],scale[3];
    if (!value.isObject() || !ReadNumbers(value["Translation"],position) || !ReadNumbers(value["Rotation"],rotation) ||
        !ReadNumbers(value["Scale"],scale)) return false;
    output.Position={position[0],position[1],position[2]};
    output.Rotation={rotation[0],rotation[1],rotation[2],rotation[3]};
    output.Scale={scale[0],scale[1],scale[2]};
    return output.Rotation.TryNormalize(output.Rotation);
}

bool IsAcyclic(const std::vector<SkeletonNode>& nodes) {
    std::vector<std::uint8_t> visited(nodes.size());
    for (std::size_t index=0;index<nodes.size();++index) {
        auto node=static_cast<std::int32_t>(index);
        while (node>=0 && visited[node]==0) {
            visited[node]=1;
            node=nodes[node].Parent;
        }
        if (node>=0 && visited[node]==1) return false;
        node=static_cast<std::int32_t>(index);
        while (node>=0 && visited[node]==1) {
            visited[node]=2;
            node=nodes[node].Parent;
        }
    }
    return true;
}

Quaternion Product(Quaternion a, Quaternion b) {
    return {a.W*b.X+a.X*b.W+a.Y*b.Z-a.Z*b.Y,
            a.W*b.Y-a.X*b.Z+a.Y*b.W+a.Z*b.X,
            a.W*b.Z+a.X*b.Y-a.Y*b.X+a.Z*b.W,
            a.W*b.W-a.X*b.X-a.Y*b.Y-a.Z*b.Z};
}

void RotatePosition(Quaternion q, std::array<double,4>& p) {
    const double x=q.X,y=q.Y,z=q.Z,w=q.W;
    const double cx=y*p[2]-z*p[1],cy=z*p[0]-x*p[2],cz=x*p[1]-y*p[0];
    p[0]+=2*(w*cx+y*cz-z*cy);
    p[1]+=2*(w*cy+z*cx-x*cz);
    p[2]+=2*(w*cz+x*cy-y*cx);
}

struct BoundTrack {
    const Track* Source;
    std::size_t Node;
    const Quaternion* Correction;
};

bool SampleTrack(const BoundTrack& track,const TransformPose& rest,double time,TransformPose& result) {
    result=rest;
    if (!track.Source) return true;
    if (!track.Source->Translation.empty()) {
        auto point=UnityAnimationSource::Sample(track.Source->Translation,time);
        // A fallback bind channel is already in target space. Correct only the source curves.
        if (track.Correction) RotatePosition(*track.Correction,point);
        if (!ToFloat(point[0],result.Position.X) || !ToFloat(point[1],result.Position.Y) || !ToFloat(point[2],result.Position.Z))
            return false;
    }
    if (!track.Source->Rotation.empty()) {
        const auto q=UnityAnimationSource::Sample(track.Source->Rotation,time,true);
        Quaternion rotation;
        if (!ToFloat(q[0],rotation.X) || !ToFloat(q[1],rotation.Y) || !ToFloat(q[2],rotation.Z) ||
            !ToFloat(q[3],rotation.W) || !rotation.TryNormalize(rotation)) return false;
        if (track.Correction) rotation=Product(*track.Correction,rotation);
        if (!rotation.TryNormalize(result.Rotation)) return false;
    }
    if (!track.Source->Scale.empty()) {
        const auto scale=UnityAnimationSource::Sample(track.Source->Scale,time);
        if (!ToFloat(scale[0],result.Scale.X) || !ToFloat(scale[1],result.Scale.Y) || !ToFloat(scale[2],result.Scale.Z)) return false;
    }
    return true;
}
}

bool ImportUnityAnimation(AssetImportJob& job) {
    if (job.Cancelled()) return false;
    if (!job.Settings.isMember("UnityClip")) return job.Fail("Select a target Skeleton in Unity animation import settings");

    UnityAnimationImportSettings::Binding binding;
    std::string error;
    if (!UnityAnimationImportSettings::Read(job.Settings["UnityClip"],binding,error)) return job.Fail(error);
    if (job.Settings.isMember("SourceAvatar")) return job.Fail("UnityClip binds a Skeleton directly and cannot use SourceAvatar");

    UnityAnimationSource::Clip clip;
    if (!UnityAnimationSource::Parse({job.SourceBytes.constData(),static_cast<std::size_t>(job.SourceBytes.size())},clip,error))
        return job.Fail(error);
    if (!clip.IgnoredCurves.empty() && !binding.TransformOnly)
        return job.Fail("Unity AnimationClip has unsupported curves ("+clip.IgnoredCurves.front()+
            "); enable UnityClip.TransformOnly to import only skeletal transforms");

    AnimationClipImportOptions options;
    if (job.Settings.isMember("ClipOptions") && !AnimationImportSettingsJson::Read(job.Settings["ClipOptions"],options,false))
        return job.Fail("Invalid Unity animation ClipOptions");

    Json::Value skeletonDescriptor,skeletonBody;
    QByteArray skeletonBytes;
    if (!ReadAnimationPrepared(job,binding.Skeleton,eAssetType::Skeleton,skeletonDescriptor,skeletonBytes)) return false;
    if (skeletonDescriptor["Encoding"]!="BinaryValueLE1" || !AssetValueBinary::Decode(skeletonBytes,skeletonBody) || !skeletonBody.isObject() ||
        !skeletonBody["Nodes"].isArray() || skeletonBody["Nodes"].empty() || skeletonBody["Nodes"].size()>100000)
        return job.Fail("Invalid prepared UnityClip target Skeleton");

    const auto& sourceNodes=skeletonBody["Nodes"];
    std::vector<SkeletonNode> nodes;
    nodes.reserve(sourceNodes.size());
    std::map<std::string,std::size_t,std::less<>> canonical;
    std::size_t root=0,rootCount=0;
    for (const auto& value:sourceNodes) {
        SkeletonNode node;
        if (!value.isObject() || !value["Path"].isString() || !value["Name"].isString() || !value["Parent"].isInt() ||
            (value.isMember("InheritMode") && value["InheritMode"]!=0) ||
            !ReadPose(value["Transform"],node.Transform)) return job.Fail("Invalid UnityClip Skeleton node or bind pose");
        node.Path=value["Path"].asString();
        node.Name=value["Name"].asString();
        node.Parent=value["Parent"].asInt();
        if (!AnimationImportSettingsJson::Text(node.Path,65536,false) || !AnimationImportSettingsJson::Text(node.Name,65536) ||
            node.Parent< -1 || node.Parent>=static_cast<std::int32_t>(sourceNodes.size()) ||
            node.Parent==static_cast<std::int32_t>(nodes.size()) || !canonical.emplace(node.Path,nodes.size()).second)
            return job.Fail("Invalid or duplicate UnityClip Skeleton path");
        if (node.Parent==-1) {root=nodes.size();++rootCount;}
        nodes.push_back(std::move(node));
    }
    if (!IsAcyclic(nodes)) return job.Fail("UnityClip target Skeleton contains a cyclic parent chain");
    if (!binding.RootPath.empty()) {
        const auto found=canonical.find(binding.RootPath);
        if (found==canonical.end()) return job.Fail("UnityClip.RootPath is absent from its target Skeleton: "+binding.RootPath);
        root=found->second;
    } else if (rootCount!=1) return job.Fail("UnityClip requires RootPath when the target Skeleton has no unique root");

    const auto missing=nodes.size();
    std::map<std::pair<std::int32_t,std::string_view>,std::size_t> children;
    for (std::size_t index=0;index<nodes.size();++index) {
        const auto& node=nodes[index];
        auto [found,inserted]=children.emplace(std::make_pair(node.Parent,std::string_view(node.Name)),index);
        // Unity string paths cannot distinguish same-name siblings.
        if (!inserted) found->second=missing;
    }
    const auto resolve=[&](std::string_view path) {
        auto node=root;
        while (!path.empty()) {
            const auto slash=path.find('/');
            const auto segment=path.substr(0,slash);
            const auto child=children.find({static_cast<std::int32_t>(node),segment});
            if (segment.empty() || child==children.end() || child->second==missing) return missing;
            node=child->second;
            if (slash==std::string_view::npos) return node;
            path.remove_prefix(slash+1);
            if (path.empty()) return missing;
        }
        return node;
    };

    std::vector<BoundTrack> tracks;
    std::vector<std::int32_t> trackIndices(nodes.size(),-1);
    std::size_t correctionsUsed=0;
    for (const auto& track:clip.Tracks) {
        const auto node=resolve(track.Path);
        if (node==missing || trackIndices[node]>=0)
            return job.Fail("Unity animation transform path is missing or ambiguous in the target Skeleton: "+track.Path);
        const auto correction=binding.RotationCorrections.find(track.Path);
        const bool hasCorrection=correction!=binding.RotationCorrections.end();
        correctionsUsed+=hasCorrection;
        trackIndices[node]=static_cast<std::int32_t>(tracks.size());
        tracks.push_back({&track,node,hasCorrection?&correction->second:nullptr});
    }
    if (correctionsUsed!=binding.RotationCorrections.size())
        return job.Fail("UnityClip.RotationCorrections contains an unanimated Unity path");
    if (tracks.empty()) return job.Fail("Unity AnimationClip has no bound transform curves");

    if (options.ExtractRootMotion) {
        const auto selected=canonical.find(options.RootMotionNode);
        if (selected==canonical.end() || trackIndices[selected->second]<0)
            return job.Fail("UnityClip RootMotionNode must identify a bound native Skeleton path");
        for (auto ancestor=nodes[selected->second].Parent;ancestor>=0;ancestor=nodes[ancestor].Parent) {
            if (trackIndices[ancestor]<0) {
                trackIndices[ancestor]=static_cast<std::int32_t>(tracks.size());
                tracks.push_back({nullptr,static_cast<std::size_t>(ancestor),nullptr});
            }
        }
    }

    const double duration=clip.Stop-clip.Start;
    const double samples=duration*clip.SampleRate;
    if (!std::isfinite(clip.Start) || !std::isfinite(duration) || duration<0 || duration>1000000 ||
        !std::isfinite(clip.SampleRate) || clip.SampleRate<=0 || clip.SampleRate>1000 || samples>1000000)
        return job.Fail("Invalid Unity animation time range or sample rate");
    // Unity serializes timing as float. Snap near-integer sample counts so a
    // time encoded as 12.000001 at 60 Hz does not add an almost duplicate sample.
    const double nearest=std::round(samples);
    const double tolerance=2*std::numeric_limits<float>::epsilon()*std::max(1.0,samples);
    const auto intervals=duration==0?0u:static_cast<std::uint32_t>(std::max(1.0,
        std::fabs(samples-nearest)<=tolerance?nearest:std::ceil(samples)));
    const auto frameCount=intervals+1;
    const auto bytes=(4ull+40ull*tracks.size())*frameCount;
    if (bytes>512ull*1024*1024) return job.Fail("Unity animation samples exceed 512 MiB limit");

    QByteArray payload;
    payload.reserve(static_cast<qsizetype>(bytes));
    QDataStream stream(&payload,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    float previous=-1;
    for (std::uint32_t frame=0;frame<frameCount;++frame) {
        if (job.Cancelled()) return false;
        const double time=frame==intervals?duration:duration*frame/intervals;
        const auto localTime=static_cast<float>(time);
        if (localTime<=previous) return job.Fail("Unity animation sample times exceed float precision");
        previous=localTime;
        stream<<localTime;
        for (const auto& track:tracks) {
            const auto& node=nodes[track.Node];
            TransformPose pose;
            if (!SampleTrack(track,node.Transform,frame==intervals?clip.Stop:clip.Start+time,pose))
                return job.Fail("Invalid Unity animation transform sample: "+node.Path);
            stream<<pose.Position.X<<pose.Position.Y<<pose.Position.Z;
            stream<<pose.Rotation.X<<pose.Rotation.Y<<pose.Rotation.Z<<pose.Rotation.W;
            stream<<pose.Scale.X<<pose.Scale.Y<<pose.Scale.Z;
        }
    }
    if (stream.status()!=QDataStream::Ok) return job.Fail("Cannot write Unity animation samples");

    Json::Value descriptor;
    descriptor["Duration"]=duration;
    descriptor["SampleRate"]=clip.SampleRate;
    descriptor["FrameCount"]=frameCount;
    descriptor["Tracks"]=Json::Value(Json::arrayValue);
    descriptor["BlendShapeTracks"]=Json::Value(Json::arrayValue);
    descriptor["Layout"]="float32:Time;per-track:Translation3,RotationQuaternion4,Scale3;per-blend-track:Weight1";
    descriptor["SourceFormat"]="UnityYAML";
    descriptor["SourceLoop"]=clip.Loop;
    std::vector<std::int32_t> parents;
    parents.reserve(tracks.size());
    for (const auto& track:tracks) {
        const auto& node=nodes[track.Node];
        descriptor["Tracks"].append(node.Path);
        parents.push_back(node.Parent<0?-1:trackIndices[node.Parent]);
    }
    if (!PrepareAnimationClip(job,descriptor,payload,options,parents)) return false;
    if (!clip.IgnoredCurves.empty())
        job.Result.Diagnostics+="Imported only skeletal transforms from Unity AnimationClip; ignored curves include "+clip.IgnoredCurves.front()+"\n";
    const auto name=clip.Name.empty()?QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString():clip.Name;
    return job.Emit(eAssetType::AnimationClip,"main",name,std::move(descriptor),std::move(payload));
}
