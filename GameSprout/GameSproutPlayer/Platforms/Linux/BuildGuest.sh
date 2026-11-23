#!/bin/bash
set -euo pipefail

work=$1
fingerprint=$2
fail() { printf '%s\n' "$*" >&2; exit 1; }
for tool in cmake ninja clang clang++ flock rsync readelf; do
    command -v "$tool" >/dev/null || fail "Linux builder needs $tool. Install the dependencies in Platforms/Linux/README.md."
done
case "$(uname -m)" in
    aarch64) architecture="AArch64" ;;
    x86_64) architecture="Advanced Micro Devices X86-64" ;;
    *) fail "Linux Player supports ARM64 and x86_64 builders." ;;
esac
cache="$HOME/.cache/gamesprout-linux-player"
mkdir -p "$cache"
exec 9>"$cache/build.lock"
flock 9
source="$cache/Source"
sdl="$cache/SDL"
sdk="$cache/GRHI"
runtime="$cache/Runtime"
output="$work/Player"
jobs="$(nproc)"
mkdir -p "$source" "$sdl" "$sdk" "$runtime" "$output"

# Keep fixed source/build directories so Ninja can reuse objects between exports.
if [[ ! -f "$cache/fingerprint" || "$fingerprint" != "$(cat "$cache/fingerprint")" ]]; then
    mkdir -p "$work/Source" "$work/SDL"
    tar -xf "$work/Source.tar" -C "$work/Source"
    tar -xf "$work/SDL.tar" -C "$work/SDL"
    rsync -a --checksum --delete "$work/Source/" "$source/"
    rsync -a --checksum --delete "$work/SDL/" "$sdl/"
fi

configure() {
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ "$@"
}

configure -S "$sdl" -B "$cache/SDLBuild" -DCMAKE_C_COMPILER=clang \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
    -DSDL_EXAMPLES=OFF -DSDL_INSTALL=ON -DSDL_X11=ON -DSDL_VULKAN=ON \
    -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF
cmake --build "$cache/SDLBuild" --parallel "$jobs"

sdl_config="$cache/SDLBuild"
configure -S "$source/GRHI" -B "$cache/GRHIBuild" "-DSDL3_DIR=$sdl_config" \
    "-DGAMESPROUT_GRHI_OUTPUT_DIRECTORY=$sdk"
cmake --build "$cache/GRHIBuild" --parallel "$jobs"
configure -S "$source/GameSproutRuntime" -B "$cache/RuntimeBuild" \
    "-DGAMESPROUT_GRHI_BINARY_DIRECTORY=$sdk" "-DGAMESPROUT_RUNTIME_OUTPUT_DIRECTORY=$runtime"
cmake --build "$cache/RuntimeBuild" --parallel "$jobs"
configure -S "$source/GameSproutPlayer" -B "$cache/PlayerBuild" "-DSDL3_DIR=$sdl_config" \
    "-DGAMESPROUT_GRHI_BINARY_DIRECTORY=$sdk" "-DGAMESPROUT_RUNTIME_BINARY_DIRECTORY=$runtime"
cmake --build "$cache/PlayerBuild" --parallel "$jobs"
printf '%s\n' "$fingerprint" > "$cache/fingerprint"

cp "$cache/PlayerBuild/GameSproutPlayer" "$cache/PlayerBuild/libGameSproutRuntime.so" \
    "$cache/PlayerBuild/libGRHI.so" "$cache/PlayerBuild/libSDL3.so.0" "$output/"
if [[ -f "$work/Scripts/CMakeLists.txt" ]]; then
    mkdir -p "$cache/Scripts"
    rsync -a --checksum --delete "$work/Scripts/" "$cache/Scripts/"
    configure -S "$cache/Scripts" -B "$cache/ScriptBuild" \
        "-DGAMESPROUT_RUNTIME_SDK_DIR=$source/GameSproutRuntime/Api"
    cmake --build "$cache/ScriptBuild" --target GameLogic --parallel "$jobs"
    cp "$cache/ScriptBuild/libGameLogic.so" "$output/"
fi
cp -R "$work/Game" "$output/"
for executable in "$output/GameSproutPlayer" "$output/"*.so "$output/"*.so.0; do
    readelf -h "$executable" | grep -Fq "$architecture" ||
        fail "Linux build output has the wrong architecture: $executable"
done
chmod +x "$output/GameSproutPlayer"
tar -czf "$work/Player.tar.gz" -C "$output" .
