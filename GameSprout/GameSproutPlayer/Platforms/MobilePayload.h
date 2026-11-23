#pragma once

#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace GameSproutPlayer {
namespace MobilePayload {
inline bool Read(SDL_IOStream* stream, void* data, std::size_t size) {
    auto* cursor = static_cast<char*>(data);
    while (size != 0) {
        const auto bytes = SDL_ReadIO(stream, cursor, size);
        if (bytes == 0) return false;
        cursor += bytes;
        size -= bytes;
    }
    return true;
}

inline bool ReadInteger(SDL_IOStream* stream, std::uint64_t& value) {
    std::array<unsigned char, 8> bytes{};
    if (!Read(stream, bytes.data(), bytes.size())) return false;
    value = 0;
    for (std::size_t index = 0; index != bytes.size(); ++index)
        value |= std::uint64_t{bytes[index]} << (index * 8);
    return true;
}

inline bool Hex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

inline bool ValidName(std::string_view name) {
    if (name == "Player.json" || name == "ProjectSettings.json") return true;
    constexpr std::string_view directory = "Artifact/";
    constexpr std::string_view suffix = ".gsasset";
    if (!name.starts_with(directory) || !name.ends_with(suffix)) return false;
    name.remove_prefix(directory.size());
    name.remove_suffix(suffix.size());
    if (name.size() < 34 || name[32] != '_') return false;
    const auto guid = name.substr(0, 32);
    const auto localID = name.substr(33);
    return std::all_of(guid.begin(), guid.end(), Hex) &&
        std::all_of(localID.begin(), localID.end(), [](char value) { return value >= '0' && value <= '9'; });
}

inline bool WriteFiles(SDL_IOStream* stream, const std::filesystem::path& directory) {
    std::uint64_t fileCount;
    if (!ReadInteger(stream, fileCount) || fileCount < 2) return false;
    std::vector<char> block(1024 * 1024);
    for (std::uint64_t index = 0; index < fileCount; ++index) {
        std::uint64_t nameSize, bytesLeft;
        if (!ReadInteger(stream, nameSize) || nameSize == 0 || nameSize > 80 ||
            !ReadInteger(stream, bytesLeft)) return false;
        std::string name(static_cast<std::size_t>(nameSize), '\0');
        if (!Read(stream, name.data(), name.size()) || !ValidName(name)) return false;
        const auto file = directory / name;
        if (std::filesystem::exists(file)) return false;
        std::ofstream output(file, std::ios::binary);
        if (!output) return false;
        while (bytesLeft != 0) {
            const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(bytesLeft, block.size()));
            if (!Read(stream, block.data(), size) || !output.write(block.data(), size)) return false;
            bytesLeft -= size;
        }
        output.close();
        if (!output) return false;
    }
    char extra;
    return SDL_ReadIO(stream, &extra, 1) == 0 && SDL_GetIOStatus(stream) == SDL_IO_STATUS_EOF &&
        std::filesystem::is_regular_file(directory / "Player.json") &&
        std::filesystem::is_regular_file(directory / "ProjectSettings.json");
}

inline bool Install(const char* storage, std::filesystem::path& directory, std::string& error) {
    error.clear();
    if (!storage || !*storage) {
        error = "GameSprout Player cannot access its application storage";
        return false;
    }
    SDL_IOStream* const stream = SDL_IOFromFile("assets://Game.gspack", "rb");
    if (!stream) {
        error = std::string("Cannot open the packaged game: ") + SDL_GetError();
        return false;
    }
    std::filesystem::path staging;
    try {
        std::array<char, 8> magic{};
        std::string digest(64, '\0');
        if (!Read(stream, magic.data(), magic.size()) ||
            std::string_view(magic.data(), magic.size()) != "GSPACK1\n" ||
            !Read(stream, digest.data(), digest.size()) || !std::all_of(digest.begin(), digest.end(), Hex)) {
            error = "Invalid mobile game package header";
        } else {
            const auto root = std::filesystem::path(storage) / "GameSproutPayload";
            directory = root / digest;
            if (!std::filesystem::is_regular_file(directory / "Player.json") ||
                !std::filesystem::is_regular_file(directory / "ProjectSettings.json")) {
                staging = root / ("staging-" + std::to_string(getpid()));
                std::filesystem::remove_all(staging);
                std::filesystem::create_directories(staging / "Artifact");
                if (!WriteFiles(stream, staging)) {
                    error = "Cannot extract the packaged game to application storage";
                } else {
                    std::filesystem::remove_all(directory);
                    std::filesystem::rename(staging, directory);
                }
            }
            if (error.empty()) {
                std::error_code cleanup;
                for (auto child = std::filesystem::directory_iterator(root, cleanup);
                     !cleanup && child != std::filesystem::directory_iterator(); child.increment(cleanup)) {
                    const auto name = child->path().filename().string();
                    if (child->path() != directory && name.size() == 64 && std::all_of(name.begin(), name.end(), Hex)) {
                        std::error_code ignored;
                        std::filesystem::remove_all(child->path(), ignored);
                    }
                }
            }
        }
    } catch (const std::exception& exception) {
        error = std::string("Cannot install the packaged game: ") + exception.what();
    }
    if (!staging.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    }
    SDL_CloseIO(stream);
    return error.empty();
}
}
}
