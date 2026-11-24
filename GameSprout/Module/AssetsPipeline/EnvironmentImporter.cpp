#include "AssetImportJob.h"
#include "../../PreparedResources/EnvironmentAssetFormat.h"

bool ImportEnvironment(AssetImportJob& job) {
    const auto& bytes=job.SourceBytes;
    const std::span source(reinterpret_cast<const std::uint8_t*>(bytes.constData()),std::size_t(bytes.size()));
    GameSproutRuntime::EnvironmentRenderData data;
    if(!PreparedResources::EnvironmentAssetFormat::Describe(source,data))
        return job.Fail("Invalid .environment header, cube layout or SH coefficients");
    Json::Value descriptor;
    descriptor["Format"]="GSENV001";
    descriptor["SkySize"]=data.Sky.Width;
    descriptor["SpecularSize"]=data.Specular.Width;
    descriptor["SpecularMipLevels"]=data.Specular.MipLevels;
    const auto name=QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString();
    return job.Emit(eAssetType::Environment,"main",name,descriptor,bytes);
}
