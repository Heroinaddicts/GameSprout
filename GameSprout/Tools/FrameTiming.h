#pragma once

#define GAMESPROUT_TIMING_JOIN_IMPL(a, b) a##b
#define GAMESPROUT_TIMING_JOIN(a, b) GAMESPROUT_TIMING_JOIN_IMPL(a, b)

// Set GAMESPROUT_FRAME_TIMING to a writable path prefix before starting a
// Windows process. Each stream writes absolute steady-clock times in nanoseconds
// to <prefix>-<process id>-<stream>.csv, including time spent waiting.
#if defined(_WIN32)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <process.h>
#include <string>

namespace GameSproutFrameTiming {

inline long long Now() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

class Stream {
public:
    explicit Stream(const wchar_t* name) noexcept {
        wchar_t* prefix = nullptr;
        std::size_t length = 0;
        if (_wdupenv_s(&prefix, &length, L"GAMESPROUT_FRAME_TIMING") == 0 && length > 1) {
            try {
                const auto path = std::wstring(prefix) + L"-" + std::to_wstring(_getpid()) + L"-" + name + L".csv";
                if (_wfopen_s(&_File, path.c_str(), L"wb") == 0) {
                    std::setvbuf(_File, _Buffer, _IOFBF, sizeof(_Buffer));
                    std::fputs("event,start_ns,end_ns,value\n", _File);
                }
            } catch (...) {}
        }
        std::free(prefix);
    }

    ~Stream() { if (_File) std::fclose(_File); }
    explicit operator bool() const noexcept { return _File != nullptr; }

    void Write(const char* name, long long start, long long end, unsigned long long value) noexcept {
        std::fprintf(_File, "%s,%lld,%lld,%llu\n", name, start, end, value);
    }

private:
    std::FILE* _File = nullptr;
    char _Buffer[64 * 1024];
};

inline Stream& Runtime() noexcept { static Stream stream(L"Runtime"); return stream; }
inline Stream& GRHI() noexcept { static Stream stream(L"GRHI"); return stream; }
inline Stream& Player() noexcept { static Stream stream(L"Player"); return stream; }
inline Stream& GameView() noexcept { static Stream stream(L"GameView"); return stream; }
inline Stream& SceneView() noexcept { static Stream stream(L"SceneView"); return stream; }

class Scope {
public:
    Scope(Stream& stream, const char* name) noexcept
        : _Stream(stream), _Name(name), _Start(stream ? Now() : 0) {}

    ~Scope() { if (_Stream) _Stream.Write(_Name, _Start, Now(), 0); }

private:
    Stream& _Stream;
    const char* _Name;
    long long _Start;
};

inline void Count(Stream& stream, const char* name, unsigned long long value) noexcept {
    if (!stream) return;
    const auto time = Now();
    stream.Write(name, time, time, value);
}

}

#define GAMESPROUT_CSV_FRAME_SCOPE(stream, name) \
    ::GameSproutFrameTiming::Scope GAMESPROUT_TIMING_JOIN(gameSproutTimingScope, __COUNTER__)( \
        ::GameSproutFrameTiming::stream(), name)
#define GAMESPROUT_CSV_FRAME_COUNT(stream, name, value) \
    ::GameSproutFrameTiming::Count(::GameSproutFrameTiming::stream(), name, value)
#else
#define GAMESPROUT_CSV_FRAME_SCOPE(stream, name) ((void)0)
#define GAMESPROUT_CSV_FRAME_COUNT(stream, name, value) ((void)0)
#endif

#if defined(GAMESPROUT_TRACY_ENABLED)
#include <cstdint>
#include <tracy/Tracy.hpp>

namespace GameSproutFrameTiming {
TRACY_API const char* InternTracyLabel(const char* name) noexcept;
}

// A plug-in can unload while the collector still has pending events. The
// transient zone copies its source metadata, and plot/frame labels are
// interned by the one Tracy client shared by the process.
#define GAMESPROUT_FRAME_SCOPE(stream, name) \
    GAMESPROUT_CSV_FRAME_SCOPE(stream, name); \
    ZoneTransientN(GAMESPROUT_TIMING_JOIN(gameSproutTracyZone, __COUNTER__), name, true)
#define GAMESPROUT_FRAME_COUNT(stream, name, value) \
    do { \
        const auto gamesproutFrameCount = static_cast<std::uint64_t>(value); \
        GAMESPROUT_CSV_FRAME_COUNT(stream, name, gamesproutFrameCount); \
        if (TracyIsConnected) { \
            static const auto* const label = ::GameSproutFrameTiming::InternTracyLabel(name); \
            if (label) TracyPlot(label, static_cast<std::int64_t>(gamesproutFrameCount)); \
        } \
    } while (false)
#define GAMESPROUT_FRAME_MARK() FrameMark
#define GAMESPROUT_FRAME_MARK_NAMED(name) \
    do { \
        if (TracyIsConnected) { \
            static const auto* const label = ::GameSproutFrameTiming::InternTracyLabel(name); \
            if (label) FrameMarkNamed(label); \
        } \
    } while (false)
#else
#define GAMESPROUT_FRAME_SCOPE(stream, name) GAMESPROUT_CSV_FRAME_SCOPE(stream, name)
#define GAMESPROUT_FRAME_COUNT(stream, name, value) GAMESPROUT_CSV_FRAME_COUNT(stream, name, value)
#define GAMESPROUT_FRAME_MARK() ((void)0)
#define GAMESPROUT_FRAME_MARK_NAMED(name) ((void)0)
#endif
