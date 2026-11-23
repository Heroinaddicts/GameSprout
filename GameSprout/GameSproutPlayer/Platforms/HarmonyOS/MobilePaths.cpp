#include "../MobilePayload.h"

#include <SDL3/SDL_system.h>

namespace GameSproutPlayer {
bool MobilePaths(std::filesystem::path& binary, std::filesystem::path& directory, std::string& error) {
    binary.clear(); // The HAP module's linker namespace resolves packaged native libraries by soname.
    return MobilePayload::Install(SDL_GetOpenHarmonyInternalStoragePath(), directory, error);
}
}
