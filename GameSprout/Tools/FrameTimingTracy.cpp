#include <tracy/Tracy.hpp>
#include <mutex>
#include <string>
#include <unordered_set>

namespace GameSproutFrameTiming {
TRACY_API const char* InternTracyLabel(const char* name) noexcept {
    struct Labels {
        std::mutex Mutex;
        std::unordered_set<std::string> Names;
    };
    try {
        // Tracy can query these pointers during shutdown. Keep each distinct
        // label for the lifetime of the process, even across plugin reloads.
        static auto* const labels = new Labels;
        const std::lock_guard lock(labels->Mutex);
        return labels->Names.emplace(name).first->c_str();
    } catch (...) {
        return nullptr;
    }
}
}
