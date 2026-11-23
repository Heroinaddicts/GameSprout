#include "PreparedResourceDecode.h"
#include "../Module/Shared/PhysicsMaterialDefinition.h"

bool PreparedResources::ResourceProvider::LoadPhysicsMaterial(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::PhysicsMaterialData& output,std::string& error) {
    output={};error.clear();
    // A deleted source/subasset retains its reference in Scene/Prefab data.
    // Missing identity uses the default surface; a present but corrupt artifact
    // must still fail the normal descriptor/byte validation below.
    if(IsMissing(reference))return true;
    Json::Value descriptor;std::vector<std::uint8_t> bytes;
    if(!Read(reference,eAssetType::PhysicsMaterial,descriptor,bytes,error))return false;
    if(descriptor["Encoding"]!="RawBytes"||descriptor["Format"]!="PhysicsMaterial1"||descriptor["Layout"]!="F32F32U32U32LE") {
        error="Invalid prepared PhysicsMaterial descriptor";return false;
    }
    return PhysicsMaterialDefinition::Decode(bytes.data(),bytes.size(),output,error);
}
