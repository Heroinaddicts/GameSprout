#include "PreparedResourceProvider.h"
#include "IndirectLightingAssetFormat.h"

bool PreparedResources::ResourceProvider::LoadIndirectLighting(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::IndirectLightingRenderData& data,std::string& error) {
    data={};
    Json::Value descriptor;
    std::vector<std::uint8_t> bytes;
    if(!Read(reference,eAssetType::Irradiance,descriptor,bytes,error))return false;
    if(descriptor["Encoding"]!="RawBytes"||descriptor["Format"]!="GSIRR001"||
       !IndirectLightingAssetFormat::Decode(bytes,data)) {
        error="Invalid prepared irradiance grid";
        return false;
    }
    return true;
}
