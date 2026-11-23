#!/bin/bash
set -euo pipefail

fail() { printf '%s\n' "$*" >&2; exit 1; }
platform_dir=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo_dir=$(cd -- "$platform_dir/../../../.." && pwd -P)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-"$HOME/Library/Android/sdk"}}
application_id=${GAMESPROUT_APPLICATION_ID:-org.gamesprout.player}
min_sdk=24
sdk_level=35
build_tools="${GAMESPROUT_ANDROID_BUILD_TOOLS:-$sdk/build-tools/35.0.0}"

[[ "$application_id" =~ ^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$ ]] ||
    fail "无效的 Android 应用标识 $application_id"

case "${1:-}" in
run)
    [[ $# -eq 2 && -f "$2" ]] || fail "缺少要运行的 APK"
    adb_tool="$sdk/platform-tools/adb"
    if [[ ! -x "$adb_tool" ]]; then
        adb_tool=$(command -v adb || true)
    fi
    [[ -n "$adb_tool" ]] || fail "缺少 Android SDK Platform Tools 的 adb"
    "$adb_tool" get-state | grep -qx device || fail "连接并授权一台 Android 设备，或启动 Android 模拟器"
    "$adb_tool" install -r "$2"
    launch=$("$adb_tool" shell am start -W -n "$application_id/org.libsdl.app.SDLActivity")
    printf '%s\n' "$launch"
    printf '%s\n' "$launch" | tr -d '\r' | grep -qx 'Status: ok' ||
        fail "Android 未能启动 GameSprout Player"
    exit
    ;;
build)
    [[ $# -eq 6 ]] || fail "Android 导出参数不完整"
    ;;
*)
    fail "Android 导出仅支持 build 和 run"
    ;;
esac

payload=$2
logic=$3
output=$4
work=$5
cmake=$6
sdl_source=${GAMESPROUT_SDL_SOURCE_DIR:-"$repo_dir/../SDL"}
ndk=${ANDROID_NDK_HOME:-"$sdk/ndk/28.2.13676358"}
java_home=${JAVA_HOME:-}
if [[ -z "$java_home" && -x /usr/libexec/java_home ]]; then
    java_home=$(/usr/libexec/java_home -v 17 2>/dev/null || true)
fi
[[ -x "$java_home/bin/javac" && -x "$java_home/bin/keytool" ]] ||
    fail "请配置 JDK 17 的 JAVA_HOME，Android 打包需要 javac 和 keytool"
export JAVA_HOME="$java_home"
export PATH="$JAVA_HOME/bin:$PATH"

[[ -x "$cmake" ]] || fail "缺少可执行的 CMake: $cmake"
[[ -f "$ndk/build/cmake/android.toolchain.cmake" ]] ||
    fail "缺少 Android NDK r28c，请安装它或配置 ANDROID_NDK_HOME"
android_jar="$sdk/platforms/android-$sdk_level/android.jar"
[[ -f "$android_jar" ]] || fail "缺少 Android SDK Platform $sdk_level: $android_jar"
for tool in aapt2 d8 zipalign apksigner; do
    [[ -x "$build_tools/$tool" ]] || fail "缺少 Android SDK Build Tools 35.0.0: $build_tools/$tool"
done
java_source="$sdl_source/android-project/app/src/main/java"
[[ -f "$java_source/org/libsdl/app/SDLActivity.java" ]] ||
    fail "缺少与 SDL 本地库同版本的 SDLActivity: $java_source"

cmake_options=(
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI=arm64-v8a
    "-DANDROID_PLATFORM=android-$min_sdk"
    -DANDROID_STL=c++_shared
    "-DGAMESPROUT_SDL_SOURCE=$sdl_source"
    "-DGAMESPROUT_GAMELOGIC_SOURCE=$logic"
    "-DGAMESPROUT_MOBILE_NATIVE_OUTPUT=$work/Native/Libraries"
)
if [[ -n "${GAMESPROUT_NINJA:-}" ]]; then
    cmake_options+=("-DCMAKE_MAKE_PROGRAM=$GAMESPROUT_NINJA")
elif ! command -v ninja >/dev/null && [[ -x "$HOME/Qt/Tools/Ninja/ninja" ]]; then
    cmake_options+=("-DCMAKE_MAKE_PROGRAM=$HOME/Qt/Tools/Ninja/ninja")
fi
mkdir -p "$work"
staging=$(mktemp -d "$work/apk.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT
mkdir -p "$staging/assets" "$staging/classes" "$staging/lib/arm64-v8a"

"$cmake" -S "$platform_dir" -B "$work/Native" -G Ninja "${cmake_options[@]}"
targets=(GameSproutPlayer)
if [[ -n "$logic" ]]; then targets+=(GameLogic); fi
"$cmake" --build "$work/Native" --target "${targets[@]}"

native="$work/Native/Libraries"
cp "$native/libSDL3.so" "$native/libmain.so" "$staging/lib/arm64-v8a/"
if [[ -n "$logic" ]]; then cp "$native/libGameLogic.so" "$staging/lib/arm64-v8a/"; fi
clang_runtime=$(find "$ndk/toolchains/llvm/prebuilt" -path '*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so' -print -quit)
[[ -n "$clang_runtime" ]] || fail "Android NDK 没有 arm64-v8a libc++_shared.so"
cp "$clang_runtime" "$staging/lib/arm64-v8a/"

python3 "$platform_dir/../PackMobilePayload.py" "$payload" "$staging/assets/Game.gspack"
"$JAVA_HOME/bin/javac" --release 8 -classpath "$android_jar" -d "$staging/classes" "$java_source"/org/libsdl/app/*.java
"$JAVA_HOME/bin/jar" cf "$staging/classes.jar" -C "$staging/classes" .
"$build_tools/d8" --lib "$android_jar" --min-api "$min_sdk" --output "$staging" "$staging/classes.jar"
"$build_tools/aapt2" link -I "$android_jar" --manifest "$platform_dir/AndroidManifest.xml" \
    --rename-manifest-package "$application_id" --min-sdk-version "$min_sdk" --target-sdk-version "$sdk_level" \
    --version-code 1 --version-name 1.0 --no-compress -A "$staging/assets" -o "$staging/unsigned.apk"
( cd -- "$staging" && zip -q -0 -r unsigned.apk classes.dex lib )
"$build_tools/zipalign" -P 16 -f 4 "$staging/unsigned.apk" "$staging/aligned.apk"

keystore=${GAMESPROUT_ANDROID_KEYSTORE:-"$HOME/.android/debug.keystore"}
alias=${GAMESPROUT_ANDROID_KEY_ALIAS:-androiddebugkey}
if [[ -z "${GAMESPROUT_ANDROID_KEYSTORE:-}" ]]; then
    mkdir -p "$(dirname -- "$keystore")"
    if [[ ! -f "$keystore" ]]; then
        "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$keystore" -storepass android \
            -keypass android -alias "$alias" -keyalg RSA -keysize 2048 -validity 10000 \
            -dname 'CN=Android Debug,O=Android,C=US'
    fi
    "$build_tools/apksigner" sign --v4-signing-enabled false --ks "$keystore" --ks-key-alias "$alias" \
        --ks-pass pass:android --key-pass pass:android --out "$output" "$staging/aligned.apk"
else
    [[ -f "$keystore" && -n "${GAMESPROUT_ANDROID_KEYSTORE_PASSWORD:-}" && -n "${GAMESPROUT_ANDROID_KEY_PASSWORD:-}" ]] ||
        fail "自定义 Android 签名需要有效的 keystore 和两个签名密码环境变量"
    "$build_tools/apksigner" sign --v4-signing-enabled false --ks "$keystore" --ks-key-alias "$alias" \
        --ks-pass env:GAMESPROUT_ANDROID_KEYSTORE_PASSWORD --key-pass env:GAMESPROUT_ANDROID_KEY_PASSWORD \
        --out "$output" "$staging/aligned.apk"
fi
"$build_tools/apksigner" verify "$output"
