#pragma once
#include "AssetFactoryCommon.hpp"

class IrradianceMeta : public AssetMetaWithSettings<NativeAssetImportSettings<INativeAssetImportSettings>> {
public:
    IrradianceMeta(const Guid& guid,const std::string& path)
        : AssetMetaWithSettings(eAssetImporterType::Irradiance,guid,path) {}
};

inline IAssetMeta* OnIrradianceAssetCreate(const Foundation::FileSystemWatcherEvent& event,
    const std::string&,const std::string&,void*) {
    return CreateAssetMeta<IrradianceMeta>(event);
}
