#!/usr/bin/env python3
"""Set up the SDL HarmonyOS Stage project for the packaged game."""

import json
from pathlib import Path
import sys


def cmake_quote(value: str) -> str:
    equals = ""
    while f"]{equals}]" in value:
        equals += "="
    return f"[{equals}[{value}]{equals}]"


def configure(project: Path, platform: Path, sdl: str, logic: str, app_id: str, signing_path: str) -> None:
    app_info_path = project / "AppScope/app.json5"
    app_info = json.loads(app_info_path.read_text())
    app_info["app"]["bundleName"] = app_id
    app_info["app"]["vendor"] = "GameSprout"
    app_info_path.write_text(json.dumps(app_info, indent=2) + "\n")

    # SDL's source project uses JSON5. Generate the project-level build
    # profile with the same SDK floor, one arm64 module and optional signing.
    product = {
        "name": "default",
        "targetSdkVersion": "5.0.4(16)",
        "compatibleSdkVersion": "5.0.4(16)",
        "runtimeOS": "HarmonyOS",
        "buildOption": {
            "nativeCompiler": "BiSheng",
            "strictMode": {"caseSensitiveCheck": True, "useNormalizedOHMUrl": True},
        },
    }
    app = {"products": [product], "buildModeSet": [{"name": "debug"}, {"name": "release"}]}
    if signing_path:
        signing = json.loads(Path(signing_path).read_text())
        if not isinstance(signing, dict) or not signing.get("name") or not signing.get("material"):
            raise ValueError("HarmonyOS signing file must contain a DevEco signingConfigs object")
        app["signingConfigs"] = [signing]
        product["signingConfig"] = signing["name"]
    profile = {"app": app, "modules": [{
        "name": "entry", "srcPath": "./entry",
        "targets": [{"name": "default", "applyToProducts": ["default"]}],
    }]}
    (project / "build-profile.json5").write_text(json.dumps(profile, indent=2) + "\n")
    # The JavaScript bridge is SDL's. Its local native module is our only
    # dependency, so build exports need no test package downloads.
    (project / "oh-package.json5").write_text('{"modelVersion":"6.1.1","dependencies":{}}\n')
    (project / "entry/build-profile.json5").write_text(json.dumps({
        "apiType": "stageMode",
        "buildOption": {
            "resOptions": {"copyCodeResource": {"enable": False}},
            "externalNativeOptions": {"path": "./src/main/cpp/CMakeLists.txt", "abiFilters": ["arm64-v8a"]},
        },
        "targets": [{"name": "default"}],
    }, indent=2) + "\n")

    native = project / "entry/src/main/cpp/CMakeLists.txt"
    native.write_text(
        "cmake_minimum_required(VERSION 3.21)\n"
        "project(GameSproutHarmonyOS LANGUAGES C CXX)\n"
        f"set(GAMESPROUT_SDL_SOURCE {cmake_quote(sdl)})\n"
        f"set(GAMESPROUT_GAMELOGIC_SOURCE {cmake_quote(logic)})\n"
        "if(NOT \"$ENV{GAMESPROUT_JOLT_SOURCE_DIR}\" STREQUAL \"\")\n"
        "    set(FETCHCONTENT_SOURCE_DIR_GAMESPROUT_JOLT \"$ENV{GAMESPROUT_JOLT_SOURCE_DIR}\")\n"
        "endif()\n"
        f"include({cmake_quote(str(platform.parent / 'MobilePlayer.cmake'))})\n"
    )
    for resource, key in (
        ("AppScope/resources/base/element/string.json", "app_name"),
        ("entry/src/main/resources/base/element/string.json", "EntryAbility_label"),
    ):
        resources = project / resource
        strings = json.loads(resources.read_text())
        for entry in strings["string"]:
            if entry["name"] == key:
                entry["value"] = "GameSprout Player"
        resources.write_text(json.dumps(strings, indent=2) + "\n")


if __name__ == "__main__":
    configure(Path(sys.argv[1]), Path(sys.argv[2]), *sys.argv[3:])
