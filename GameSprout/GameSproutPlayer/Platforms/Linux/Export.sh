#!/bin/bash
set -euo pipefail

fail() { printf '%s\n' "$*" >&2; exit 1; }

operation=${1:-}
case "$operation" in
    build)
        [[ $# == 7 ]] || fail "Usage: Export.sh build <payload> <script sources> <output> <work> <cmake> <platform>"
        payload=$2
        scripts=$3
        output=$4
        work=$5
        platform=$7
        ;;
    run)
        [[ $# == 3 ]] || fail "Usage: Export.sh run <archive> <platform>"
        output=$2
        platform=$3
        ;;
    *) fail "Expected a Linux build or run command." ;;
esac
[[ "$platform" == Linux || "$platform" == LinuxServer ]] || fail "Unknown Linux platform: $platform"

platform_directory="$(cd "$(dirname "$0")" && pwd -P)"
lima=${GAMESPROUT_LIMA_EXECUTABLE:-$(command -v limactl || true)}
if [[ -z "$lima" ]]; then
    for candidate in /opt/homebrew/bin/limactl /usr/local/bin/limactl; do
        if [[ -x "$candidate" ]]; then lima=$candidate; break; fi
    done
fi
[[ -n "$lima" ]] || fail "Install Lima to export Linux Player. See Platforms/Linux/README.md."
instance=${GAMESPROUT_LIMA_INSTANCE:-gamesprout-linux}
guest() { "$lima" shell --workdir=/ "$instance" -- "$@"; }
host_tar() { COPYFILE_DISABLE=1 tar --no-xattrs --no-acls --no-fflags "$@"; }

guest_dir="$(guest mktemp -d /tmp/gamesprout-build.XXXXXXXX)" ||
    fail "Start the Linux builder with limactl start $instance."
[[ "$guest_dir" =~ ^/tmp/gamesprout-build\.[A-Za-z0-9]+$ ]] || fail "Unexpected Linux builder work directory."
keep_guest_dir=0
cleanup() {
    if [[ "$keep_guest_dir" == 0 ]]; then guest rm -rf "$guest_dir" >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "$operation" == run ]]; then
    [[ -s "$output" ]] || fail "Linux Player archive was not found: $output"
    guest tar -xzf - -C "$guest_dir" < "$output"
    frames=${GAMESPROUT_LINUX_PREVIEW_FRAMES:-0}
    [[ "$frames" =~ ^[0-9]+$ ]] || fail "GAMESPROUT_LINUX_PREVIEW_FRAMES must be a frame count."
    if [[ "$frames" == 0 ]]; then
        guest tee "$guest_dir/RunGuest.sh" < "$platform_directory/RunGuest.sh" >/dev/null
        guest bash "$guest_dir/RunGuest.sh" "$guest_dir" "$platform" detach
        keep_guest_dir=1
    else
        guest bash -s -- "$guest_dir" "$platform" "$frames" < "$platform_directory/RunGuest.sh"
    fi
    exit
fi

[[ -s "$payload/Player.json" && -s "$payload/ProjectSettings.json" ]] || fail "Player payload is missing its manifest or project settings."
[[ -z "$scripts" || -f "$scripts/CMakeLists.txt" ]] || fail "GameLogic source snapshot is incomplete."

root=${GAMESPROUT_SOURCE_ROOT:-$(cd "$platform_directory/../../../.." && pwd -P)}
if [[ -d "$root/GameSprout" ]]; then root="$root/GameSprout"; fi
[[ -f "$root/GameSproutPlayer/CMakeLists.txt" ]] || fail "GameSprout build sources are unavailable."
sdl=${GAMESPROUT_SDL_SOURCE_DIR:-"$root/../../SDL"}
[[ -f "$sdl/CMakeLists.txt" ]] && grep -q SDL_GRHI_GPU_ADAPTERS_VERSION "$sdl/include/SDL3/SDL_gpu.h" ||
    fail "Set GAMESPROUT_SDL_SOURCE_DIR to the patched SDL checkout used by GRHI."

# The guest owns all Linux build paths and the shared host tree is never mounted.
mkdir -p "$work"
source_archive="$work/LinuxSources.tar"
host_tar --exclude=build --exclude=Build --exclude='cmake-build-*' --exclude=CMakeFiles \
    --exclude=.DS_Store --exclude=.git --exclude=Tests -cf "$source_archive" \
    -C "$root" GameSproutPlayer GameSproutRuntime GRHI GPhysics ObjectDocument PreparedResources \
    Foundation/ParallelBuild.cmake Foundation/BuildParallel.cmake.in Foundation/Utils/JsonCpp \
    Module/Shared/AnimationDefinition.h Module/Shared/AvatarMaskDefinition.h \
    Module/Shared/AnimationImportSettingsJson.h Module/Shared/PhysicsMaterialDefinition.h \
    Module/Interface/AnimationImportSettings.h
sdl_archive="$work/LinuxSDL.tar"
host_tar --exclude=build --exclude=Build --exclude='cmake-build-*' --exclude=CMakeFiles \
    --exclude=.git --exclude=.DS_Store -cf "$sdl_archive" -C "$sdl" .
digest="$({ shasum -a 256 < "$source_archive"; shasum -a 256 < "$sdl_archive"; } | shasum -a 256 | cut -d ' ' -f 1)"
guest tee "$guest_dir/Source.tar" < "$source_archive" > /dev/null
guest tee "$guest_dir/SDL.tar" < "$sdl_archive" > /dev/null
guest mkdir -p "$guest_dir/Game" "$guest_dir/Scripts"
host_tar -cf - -C "$payload" . | guest tar -xf - -C "$guest_dir/Game"
if [[ -n "$scripts" ]]; then
    host_tar -cf - -C "$scripts" . | guest tar -xf - -C "$guest_dir/Scripts"
fi
guest bash -s -- "$guest_dir" "$digest" < "$platform_directory/BuildGuest.sh"
guest cat "$guest_dir/Player.tar.gz" > "$output"
[[ -s "$output" ]] || fail "Linux builder did not produce a Player archive."
