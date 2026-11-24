#pragma once
#include "AssetFactoryCommon.hpp"

class EnvironmentMeta : public AssetMetaWithSettings<NativeAssetImportSettings<INativeAssetImportSettings>> {
public:
    EnvironmentMeta(const Guid& guid,const std::string& path)
        : AssetMetaWithSettings(eAssetImporterType::Environment,guid,path) {}
};

inline IAssetMeta* OnEnvironmentAssetCreate(const Foundation::FileSystemWatcherEvent& event,
    const std::string&,const std::string&,void*) {
    return CreateAssetMeta<EnvironmentMeta>(event);
}
