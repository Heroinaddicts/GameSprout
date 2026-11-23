#pragma once

#include <cstdint>
#include <string>
#if __has_include(<source_location>)
#include <source_location>
#endif

namespace GameSproutRuntime {
#if defined(__cpp_lib_source_location)
    using RuntimeSourceLocation = std::source_location;
#else
    struct RuntimeSourceLocation {
        static constexpr RuntimeSourceLocation current(
            const char* file = __builtin_FILE(), std::uint_least32_t line = __builtin_LINE()) noexcept {
            return RuntimeSourceLocation(file, line);
        }

        constexpr const char* file_name() const noexcept { return _File; }
        constexpr std::uint_least32_t line() const noexcept { return _Line; }

    private:
        constexpr RuntimeSourceLocation(const char* file, std::uint_least32_t line) noexcept
            : _File(file), _Line(line) {}

        const char* _File;
        std::uint_least32_t _Line;
    };
#endif

    enum class RuntimeLogLevel : std::uint8_t {
        Debug,
        Trace,
        Warning,
        Error
    };

    struct RuntimeLogRecord {
        RuntimeLogLevel Level;
        std::string Message;
        std::string File;
        std::uint_least32_t Line;
    };

    class IRuntimeLogListener {
    public:
        virtual ~IRuntimeLogListener() = default;
        // Delivered on the Runtime owner thread. Copy the record to retain it.
        virtual void OnRuntimeLog(const RuntimeLogRecord& record) = 0;
    };
}
