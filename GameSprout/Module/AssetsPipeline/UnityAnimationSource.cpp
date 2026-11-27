#include "UnityAnimationSource.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace UnityAnimationSource {
namespace {
using Text = std::string_view;
using Value = std::array<double,4>;

Text Trim(Text text) {
    const auto first=text.find_first_not_of(" \t\r");
    if(first==Text::npos)return {};
    const auto last=text.find_last_not_of(" \t\r");
    return text.substr(first,last-first+1);
}

std::pair<Text,Text> Field(Text text) {
    const auto separator=text.find(':');
    return separator==Text::npos?std::pair{Text{},Text{}}:
        std::pair{Trim(text.substr(0,separator)),Trim(text.substr(separator+1))};
}

bool ListItem(Text text) {
    return text=="-"||text.starts_with("- ");
}

class Lines {
public:
    explicit Lines(Text source):_Source(source) {
        if(_Source.starts_with("\xef\xbb\xbf"))_Source.remove_prefix(3);
        Next();
    }

    void Next() {
        while(!_Source.empty()) {
            const auto end=_Source.find('\n');
            const auto row=_Source.substr(0,end);
            _Source=end==Text::npos?Text{}:_Source.substr(end+1);
            ++Number;
            Body=Trim(row);
            if(Body.empty()||Body.starts_with('#'))continue;
            Indent=row.find_first_not_of(' ');
            return;
        }
        Body={};
    }

    bool AtClip() {
        while(Indent==0 && (Body.starts_with('%')||Body.starts_with("---")))Next();
        return Indent==0&&Body=="AnimationClip:";
    }

    bool InField(std::size_t indent) const {
        return !Body.empty()&&(Indent>indent || (Indent==indent&&ListItem(Body)));
    }

    Text Body;
    std::size_t Indent = 0, Number = 0;
private:
    Text _Source;
};

bool Number(Text text,double& value,bool allowStep=false) {
    if(allowStep) {
        constexpr auto infinity=std::numeric_limits<double>::infinity();
        if(text==".inf"||text=="+.inf"||text=="Infinity"||text=="+Infinity") {
            value=infinity;
            return true;
        }
        if(text=="-.inf"||text=="-Infinity") {
            value=-infinity;
            return true;
        }
    }
    bool parsed=false;
    value=QByteArray::fromRawData(text.data(),text.size()).toDouble(&parsed);
    return parsed&&std::isfinite(value);
}

bool Vector(Text text,std::size_t dimensions,Value& value,bool allowStep=false) {
    if(text.size()<2||text.front()!='{'||text.back()!='}')return false;
    text.remove_prefix(1);text.remove_suffix(1);
    constexpr Text names="xyzw";
    unsigned seen=0;
    while(!text.empty()) {
        const auto comma=text.find(',');
        const auto [name,number]=Field(text.substr(0,comma));
        if(name.size()!=1)return false;
        const auto axis=names.find(name.front());
        if(axis>=dimensions||(seen&(1u<<axis))||!Number(number,value[axis],allowStep))return false;
        seen|=1u<<axis;
        if(comma==Text::npos)break;
        text.remove_prefix(comma+1);
    }
    return seen==(1u<<dimensions)-1;
}

bool Scalar(std::string_view text,std::string& value) {
    if(text.starts_with('"')) {
        QByteArray array("[");
        array.append(text.data(),text.size());
        array.append(']');
        const auto json=QJsonDocument::fromJson(array);
        if(!json.isArray()||json.array().size()!=1||!json.array().at(0).isString())return false;
        const auto utf8=json.array().at(0).toString().toUtf8();
        value.assign(utf8.constData(),utf8.size());
    } else if(text.starts_with('\'')) {
        if(text.size()<2||!text.ends_with('\''))return false;
        text.remove_prefix(1);text.remove_suffix(1);
        value.clear();
        while(!text.empty()) {
            const auto quote=text.find('\'');
            value+=text.substr(0,quote);
            if(quote==Text::npos)break;
            text.remove_prefix(quote+1);
            if(!text.starts_with('\''))return false;
            value+='\'';
            text.remove_prefix(1);
        }
    } else value=text;
    return value.find('\0')==std::string::npos;
}

class Parser {
public:
    Parser(Text source,std::string& error):_Line(source),_Error(error) {}

    bool Read(Clip& clip) {
        if(!_Line.AtClip())return Fail("expected a Unity AnimationClip document");
        _Line.Next();
        if(_Line.Body.empty()||!_Line.Indent)return Fail("empty AnimationClip document");
        const auto indent=_Line.Indent;
        while(!_Line.Body.empty() && _Line.Indent==indent) {
            const auto [name,value]=Field(_Line.Body);
            _Line.Next();
            if(name=="m_Name") {
                if(!ReadScalar(value,indent,clip.Name))return false;
            } else if(name=="m_SampleRate") {
                if(!Number(value,clip.SampleRate)||clip.SampleRate<=0)return Fail("invalid m_SampleRate");
            } else if(name=="m_AnimationClipSettings") {
                if(!ReadSettings(indent,clip))return false;
            } else if(name=="m_PositionCurves") {
                if(!ReadTracks(indent,value,3,&Track::Translation,clip))return false;
            } else if(name=="m_RotationCurves") {
                if(!ReadTracks(indent,value,4,&Track::Rotation,clip))return false;
            } else if(name=="m_ScaleCurves") {
                if(!ReadTracks(indent,value,3,&Track::Scale,clip))return false;
            } else {
                const bool nonempty=value=="[]"||value=="null"?false:!value.empty()||_Line.InField(indent);
                if(nonempty&&(name=="m_CompressedRotationCurves"||name=="m_EulerCurves"))
                    return Fail("unsupported serialized rotation channel "+std::string(name));
                if(nonempty&&(name=="m_FloatCurves"||name=="m_PPtrCurves"||name=="m_Events"||name=="m_MuscleClip"))
                    clip.IgnoredCurves.emplace_back(name);
                Skip(indent);
            }
        }
        if(!_Line.Body.empty()&&_Line.Body!="...")return Fail("unexpected data after the AnimationClip");
        if(clip.Tracks.empty())return Fail("AnimationClip has no supported transform curves");
        clip.Start=_Start.value_or(_FirstKey);
        clip.Stop=_Stop.value_or(_LastKey);
        if(clip.Stop<clip.Start)return Fail("clip stop time precedes its start time");
        return true;
    }

private:
    bool Fail(const std::string& message) {
        _Error="Unity AnimationClip line "+std::to_string(_Line.Number)+": "+message;
        return false;
    }

    void Skip(std::size_t indent) {
        while(_Line.InField(indent))_Line.Next();
    }

    bool ReadScalar(Text value,std::size_t indent,std::string& output) {
        if(value==">"||value==">-"||value=="|"||value=="|-") {
            output.clear();
            while(!_Line.Body.empty()&&_Line.Indent>indent) {
                if(!output.empty())output+=value.starts_with('>')?' ':'\n';
                output+=_Line.Body;
                _Line.Next();
            }
            return true;
        }
        std::string joined(value);
        while(!_Line.Body.empty()&&_Line.Indent>indent) {
            if(!joined.empty())joined+=' ';
            joined+=_Line.Body;
            _Line.Next();
        }
        if(!Scalar(joined,output))return Fail("invalid quoted text");
        return true;
    }

    bool ReadSettings(std::size_t indent,Clip& clip) {
        while(_Line.InField(indent)) {
            const auto [name,value]=Field(_Line.Body);
            double number=0;
            if(name=="m_StartTime"||name=="m_StopTime") {
                if(!Number(value,number))return Fail("invalid "+std::string(name));
                (name=="m_StartTime"?_Start:_Stop)=number;
            } else if(name=="m_LoopTime") {
                if(value=="1"||value=="true")clip.Loop=true;
                else if(value!="0"&&value!="false")return Fail("invalid m_LoopTime");
            }
            _Line.Next();
        }
        return true;
    }

    bool ReadTracks(std::size_t indent,Text value,std::size_t dimensions,Curve Track::*channel,Clip& clip) {
        if(value=="[]")return true;
        if(!value.empty()||!_Line.InField(indent)||!ListItem(_Line.Body))return Fail("expected a transform curve list");
        const auto itemIndent=_Line.Indent;
        while(_Line.Indent==itemIndent&&ListItem(_Line.Body)) {
            const auto [firstName,firstValue]=Field(Trim(_Line.Body.substr(1)));
            _Line.Next();
            Curve keys;
            std::string path;
            bool hasPath=false;
            const auto entry=[&](Text name,Text fieldValue,std::size_t fieldIndent) {
                if(name=="curve")return ReadCurve(fieldIndent,fieldValue,dimensions,keys);
                if(name=="path") {
                    if(hasPath)return Fail("duplicate transform curve path");
                    hasPath=true;
                    return ReadScalar(fieldValue,fieldIndent,path);
                }
                Skip(fieldIndent);
                return true;
            };
            if(!firstName.empty()&&!entry(firstName,firstValue,itemIndent+2))return false;
            while(!_Line.Body.empty()&&_Line.Indent>itemIndent) {
                const auto fieldIndent=_Line.Indent;
                const auto [name,fieldValue]=Field(_Line.Body);
                _Line.Next();
                if(!entry(name,fieldValue,fieldIndent))return false;
            }
            if(!hasPath||keys.empty())return Fail("transform curve is missing a path or keyframes");
            auto [index,added]=_Tracks.try_emplace(path,clip.Tracks.size());
            if(added) {
                Track track;
                track.Path=std::move(path);
                clip.Tracks.push_back(std::move(track));
            }
            auto& destination=clip.Tracks[index->second].*channel;
            if(!destination.empty())return Fail("duplicate transform curve for path "+index->first);
            _FirstKey=std::min(_FirstKey,keys.front().Time);
            _LastKey=std::max(_LastKey,keys.back().Time);
            destination=std::move(keys);
        }
        return true;
    }

    bool ReadCurve(std::size_t indent,Text value,std::size_t dimensions,Curve& keys) {
        if(!value.empty()||!keys.empty())return Fail("invalid or duplicate transform curve");
        while(_Line.InField(indent)) {
            const auto fieldIndent=_Line.Indent;
            const auto [name,fieldValue]=Field(_Line.Body);
            _Line.Next();
            if(name=="m_Curve") {
                if(!ReadKeys(fieldIndent,fieldValue,dimensions,keys))return false;
            } else Skip(fieldIndent);
        }
        return true;
    }

    bool ReadKeys(std::size_t indent,Text value,std::size_t dimensions,Curve& keys) {
        if(!keys.empty()||!value.empty()||!_Line.InField(indent)||!ListItem(_Line.Body))
            return Fail("expected transform keyframes");
        const auto itemIndent=_Line.Indent;
        while(_Line.Indent==itemIndent&&ListItem(_Line.Body)) {
            Key key;
            bool hasTime=false,hasValue=false,hasIn=false,hasOut=false;
            const auto entry=[&](Text name,Text fieldValue) {
                if(name=="time") {
                    if(hasTime||!Number(fieldValue,key.Time))return Fail("invalid key time");
                    hasTime=true;
                } else if(name=="value") {
                    if(hasValue||!Vector(fieldValue,dimensions,key.Value))return Fail("invalid transform key value");
                    hasValue=true;
                } else if(name=="inSlope") {
                    if(hasIn||!Vector(fieldValue,dimensions,key.InSlope,true))return Fail("invalid incoming tangent");
                    hasIn=true;
                } else if(name=="outSlope") {
                    if(hasOut||!Vector(fieldValue,dimensions,key.OutSlope,true))return Fail("invalid outgoing tangent");
                    hasOut=true;
                } else if(name=="weightedMode"&&fieldValue!="0")return Fail("weighted transform tangents are unsupported");
                return true;
            };
            const auto [name,fieldValue]=Field(Trim(_Line.Body.substr(1)));
            if(!entry(name,fieldValue))return false;
            _Line.Next();
            while(!_Line.Body.empty()&&_Line.Indent>itemIndent) {
                const auto [nextName,nextValue]=Field(_Line.Body);
                if(!entry(nextName,nextValue))return false;
                _Line.Next();
            }
            if(!hasTime||!hasValue)return Fail("keyframe is missing its time or transform value");
            if(!keys.empty()&&key.Time<=keys.back().Time)return Fail("transform key times must be strictly increasing");
            keys.push_back(key);
        }
        return true;
    }

    Lines _Line;
    std::string& _Error;
    std::map<std::string,std::size_t,std::less<>> _Tracks;
    std::optional<double> _Start,_Stop;
    double _FirstKey=std::numeric_limits<double>::infinity();
    double _LastKey=-std::numeric_limits<double>::infinity();
};
}

bool LooksLikeUnityClip(Text source) {
    return Lines(source).AtClip();
}

bool Parse(Text source,Clip& clip,std::string& error) {
    Clip result;
    if(!Parser(source,error).Read(result))return false;
    clip=std::move(result);
    error.clear();
    return true;
}

std::array<double,4> Sample(const Curve& curve,double time,bool rotation) {
    if(curve.empty())return {};
    const auto right=std::lower_bound(curve.begin(),curve.end(),time,[](const Key& key,double t){return key.Time<t;});
    if(right==curve.end())return curve.back().Value;
    if(right==curve.begin()||right->Time==time)return right->Value;
    const auto& left=*(right-1);
    const double duration=right->Time-left.Time;
    const double t=(time-left.Time)/duration;
    const double t2=t*t,t3=t2*t;
    const double leftValueWeight=2*t3-3*t2+1,rightValueWeight=-2*t3+3*t2;
    const double leftSlopeWeight=(t3-2*t2+t)*duration,rightSlopeWeight=(t3-t2)*duration;
    double hemisphere=1;
    if(rotation) {
        double dot=0;
        for(std::size_t i=0;i<4;++i)dot+=left.Value[i]*right->Value[i];
        if(dot<0)hemisphere=-1;
    }
    Value result{};
    for(std::size_t i=0;i<4;++i) {
        if(std::isinf(left.OutSlope[i])||std::isinf(right->InSlope[i]))result[i]=left.Value[i];
        else result[i]=leftValueWeight*left.Value[i]+leftSlopeWeight*left.OutSlope[i]+
            hemisphere*(rightValueWeight*right->Value[i]+rightSlopeWeight*right->InSlope[i]);
    }
    return result;
}
}
