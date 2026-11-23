#include "../MobilePayload.h"

#include <SDL3/SDL_system.h>
#include <dlfcn.h>

namespace GameSproutPlayer {
bool MobilePaths(std::filesystem::path& binary, std::filesystem::path& directory, std::string& error) {
    binary.clear();
    Dl_info image{};
    if (dladdr(reinterpret_cast<const void*>(&MobilePaths), &image) && image.dli_fname) {
        const auto path = std::filesystem::path(image.dli_fname);
        std::error_code ignored;
        if (path.is_absolute() && std::filesystem::is_regular_file(path, ignored))
            binary = path.parent_path();
    }
    return MobilePayload::Install(SDL_GetAndroidInternalStoragePath(), directory, error);
}
}
