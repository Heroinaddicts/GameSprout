#!/bin/bash
set -euo pipefail

fail() { printf '%s\n' "$*" >&2; exit 1; }

case "${1:-}" in
    build)
        [[ $# -eq 6 ]] || fail "iOS exporter requires payload, GameLogic sources, output, build directory and CMake."
        payload=$2
        logic=$3
        output=$4
        work=$5
        cmake=$6
        sdk=${GAMESPROUT_IOS_SDK:-iphoneos}
        [[ "$sdk" == iphoneos || "$sdk" == iphonesimulator ]] || fail "iOS SDK must be iphoneos or iphonesimulator."
        team=${GAMESPROUT_IOS_TEAM_ID:-}
        sign=NO
        if [[ "$sdk" == iphonesimulator || -n "$team" ]]; then sign=YES; fi
        bundle=${GAMESPROUT_IOS_BUNDLE_IDENTIFIER:-com.gamesprout.player}
        [[ "$bundle" =~ ^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$ ]] || fail "Invalid iOS bundle identifier."
        [[ -f "$payload/Player.json" && -d "$payload/Artifact" && ! -e "$output" ]] || fail "Invalid iOS payload or staged output."
        [[ -z "$logic" || -f "$logic/CMakeLists.txt" ]] || fail "GameLogic source preparation is incomplete."
        [[ -f "${GAMESPROUT_SDL_SOURCE_DIR:-}/CMakeLists.txt" ]] || fail "Set GAMESPROUT_SDL_SOURCE_DIR to the SDL source with the GRHI GPU adapter extension."
        xcrun --sdk "$sdk" --show-sdk-path >/dev/null

        source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        build_dir="$work/$sdk"
        game="$build_dir/Game"
        mkdir -p "$game"
        /usr/bin/rsync -a --delete -- "$payload/" "$game/"
        configure_args=(-S "$source_dir" -B "$build_dir" -G Xcode
            -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_OSX_DEPLOYMENT_TARGET="${GAMESPROUT_IOS_DEPLOYMENT_TARGET:-16.0}" \
            -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED="$sign" \
            -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED="$sign" \
            -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$team" \
            -DGAMESPROUT_SDL_SOURCE_DIR="$GAMESPROUT_SDL_SOURCE_DIR" \
            -DGAMESPROUT_IOS_PAYLOAD="$game" -DGAMESPROUT_IOS_GAMELOGIC_SOURCES="$logic" \
            -DGAMESPROUT_IOS_BUNDLE_IDENTIFIER="$bundle" -DGAMESPROUT_IOS_SIGN="$sign")
        if [[ "$sdk" == iphonesimulator ]]; then
            configure_args+=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=-)
        fi
        "$cmake" "${configure_args[@]}"

        build_args=(-project "$build_dir/GameSproutIOSPlayer.xcodeproj" -target GameSproutPlayer
            -configuration Release -sdk "$sdk" -parallelizeTargets)
        if [[ "$sdk" == iphoneos && "$sign" == YES ]]; then build_args+=(-allowProvisioningUpdates); fi
        xcodebuild "${build_args[@]}" build
        product="$build_dir/Product/GameSproutPlayer.app"
        [[ -x "$product/GameSproutPlayer" && -f "$product/Game/Player.json" ]] || fail "iOS app is incomplete."
        if [[ -n "$logic" ]]; then
            [[ -f "$product/Frameworks/GameLogic.framework/GameLogic" ]] || fail "GameLogic.framework is missing from the iOS app."
        fi
        /usr/bin/ditto "$product" "$output"
        if [[ "$sdk" == iphoneos && "$sign" == NO ]]; then
            printf '%s\n' "Built an unsigned iOS app. Set GAMESPROUT_IOS_TEAM_ID to create a device-installable build."
        fi
        ;;
    run)
        [[ $# -eq 2 ]] || fail "iOS exporter requires the built .app path."
        output=$2
        [[ -f "$output/Info.plist" ]] || fail "Cannot open the built iOS app."
        bundle=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$output/Info.plist")
        platform=$(/usr/libexec/PlistBuddy -c 'Print CFBundleSupportedPlatforms:0' "$output/Info.plist")
        case "$platform" in
            iPhoneSimulator)
                fail "当前 SDL_GPU Metal 需要 iOS 真机。模拟器包可用于无窗口逻辑测试。"
                ;;
            iPhoneOS)
                device=${GAMESPROUT_IOS_DEVICE:-}
                [[ -n "$device" ]] || fail "Set GAMESPROUT_IOS_DEVICE to the connected device identifier."
                [[ -f "$output/embedded.mobileprovision" ]] || fail "Build with GAMESPROUT_IOS_TEAM_ID and a valid device provisioning profile."
                xcrun devicectl device install app --device "$device" "$output"
                xcrun devicectl device process launch --device "$device" --terminate-existing "$bundle"
                ;;
            *) fail "Invalid platform in iOS app Info.plist." ;;
        esac
        ;;
    *) fail "Usage: Export.sh build PAYLOAD GAMELOGIC OUTPUT WORK CMAKE | run OUTPUT" ;;
esac
