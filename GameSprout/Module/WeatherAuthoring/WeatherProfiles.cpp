#include "WeatherAuthoring.h"
#include "SceneWeatherDocument.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <exception>
#include <sstream>

namespace {
constexpr qint64 MaxProfileBytes=64*1024;

bool ProfileFilename(const QString& path) {
    return QFileInfo(path).suffix().compare(QStringLiteral("weather"),Qt::CaseInsensitive)==0;
}
}

bool WeatherAuthoring::ImportProfile(std::uint64_t sceneID,const std::string& filename,std::string& error) {
    WeatherAuthoringSnapshot snapshot;
    if(!Resolve(sceneID,snapshot,error))return false;
    if(!snapshot.Editable){error="请停止运行后再导入天气预设。";return false;}
    const auto path=QString::fromStdString(filename);
    QFile file(path);
    if(!ProfileFilename(path)||!file.open(QIODevice::ReadOnly)) {
        error="无法打开 .weather 预设。";return false;
    }
    const auto bytes=file.read(MaxProfileBytes+1);
    if(file.error()!=QFileDevice::NoError||bytes.size()>MaxProfileBytes||!file.atEnd()) {
        error="无法读取 .weather 预设或文件超出 64 KiB。";return false;
    }
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"]=true;
    builder["failIfExtra"]=true;
    Json::Value profile;
    std::istringstream stream(bytes.toStdString());
    if(!Json::parseFromStream(builder,stream,&profile,&error)||!profile.isObject()||profile.size()!=2||
       !profile["Version"].isUInt()||profile["Version"].asUInt()!=1||!profile.isMember("Weather")) {
        if(error.empty())error="当前支持版本 1 的 .weather 预设。";
        return false;
    }
    try {return ApplySettings(sceneID,SceneWeatherDocument::Read(profile["Weather"],"WeatherProfile.Weather"),error);}
    catch(const std::exception& exception){error=exception.what();return false;}
}

bool WeatherAuthoring::ExportProfile(std::uint64_t sceneID,const std::string& filename,std::string& error) const {
    WeatherAuthoringSnapshot snapshot;
    if(!Resolve(sceneID,snapshot,error))return false;
    const auto path=QString::fromStdString(filename);
    if(!ProfileFilename(path)){error="预设文件扩展名应为 .weather。";return false;}
    try {
        Json::Value profile(Json::objectValue);
        profile["Version"]=Json::UInt(1);
        profile["Weather"]=SceneWeatherDocument::Serialize(snapshot.Settings);
        Json::StreamWriterBuilder writer;
        writer["indentation"]="  ";
        const auto bytes=QByteArray::fromStdString(Json::writeString(writer,profile))+ '\n';
        QSaveFile file(path);
        if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit())return true;
        error=file.errorString().toStdString();
    } catch(const std::exception& exception){error=exception.what();}
    return false;
}
