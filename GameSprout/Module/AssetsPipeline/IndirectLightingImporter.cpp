#include "AssetImportJob.h"
#include "../../PreparedResources/IndirectLightingAssetFormat.h"

bool ImportIndirectLighting(AssetImportJob& job) {
    const auto& bytes=job.SourceBytes;
    const std::span source(reinterpret_cast<const std::uint8_t*>(bytes.constData()),std::size_t(bytes.size()));
    GameSproutRuntime::IndirectLightingRenderData data;
    if(!PreparedResources::IndirectLightingAssetFormat::Decode(source,data))
        return job.Fail("Invalid .irradiance dimensions, file size or spherical harmonics coefficients");
    Json::Value descriptor;
    descriptor["Format"]="GSIRR001";
    descriptor["Width"]=data.Dimensions[0];
    descriptor["Height"]=data.Dimensions[1];
    descriptor["Depth"]=data.Dimensions[2];
    const auto name=QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString();
    return job.Emit(eAssetType::Irradiance,"main",name,descriptor,bytes);
}
