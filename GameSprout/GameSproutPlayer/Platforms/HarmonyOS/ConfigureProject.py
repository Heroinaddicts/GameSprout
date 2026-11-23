#!/usr/bin/env python3
"""Set up the SDL HarmonyOS Stage project for the packaged game."""

import json
import os
from pathlib import Path
import re
import shutil
import sys


HARMONY_VERSIONS = {
    16: "5.0.4(16)", 17: "5.0.5(17)", 18: "5.1.0(18)", 19: "5.1.1(19)",
    20: "6.0.0(20)", 21: "6.0.1(21)", 22: "6.0.2(22)", 23: "6.1.0(23)",
    24: "6.1.1(24)", 26: "26.0.0",
}

SOURCE_EXCLUDES = shutil.ignore_patterns(
    ".git", ".DS_Store", ".idea", ".qtcreator", ".hvigor", ".cxx", "local.properties",
    "build", "Build", "cmake-build-*", "CMakeFiles", "Tests", "oh_modules", "node_modules",
    "__pycache__", "*.p12", "*.cer", "*.p7b", "build-profile.json5", "sign-config.json5",
)


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def copy_sources(source: Path, destination: Path) -> None:
    shutil.copytree(source, destination, ignore=SOURCE_EXCLUDES, dirs_exist_ok=True)


def stage_portable_sources(project: Path, platform: Path, sdl: Path, logic: str) -> None:
    native = project / "Native"
    engine = platform.parents[2]
    for name in ("GameSproutPlayer", "GameSproutRuntime", "GRHI", "GPhysics", "ObjectDocument", "PreparedResources"):
        copy_sources(engine / name, native / "GameSprout" / name)
    foundation = native / "GameSprout/Foundation"
    copy_sources(engine / "Foundation/Utils/JsonCpp", foundation / "Utils/JsonCpp")
    shutil.copy2(engine / "Foundation/ParallelBuild.cmake", foundation)
    # PreparedResources uses these schema headers without linking an Editor module.
    module = native / "GameSprout/Module"
    for name in ("Shared/AnimationDefinition.h", "Shared/AvatarMaskDefinition.h",
                 "Shared/AnimationImportSettingsJson.h", "Shared/PhysicsMaterialDefinition.h",
                 "Interface/AnimationImportSettings.h"):
        header = module / name
        header.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(engine / "Module" / name, header)

    vendor_sdl = native / "SDL"
    for name in ("cmake", "include", "src", "build-scripts", "wayland-protocols"):
        copy_sources(sdl / name, vendor_sdl / name)
    for name in ("CMakeLists.txt", "LICENSE.txt"):
        shutil.copy2(sdl / name, vendor_sdl)
    if logic:
        # ScriptCompiler provides a clean snapshot of the selected source files.
        shutil.copytree(Path(logic), native / "GameLogic")

    jolt_path = os.environ.get("GAMESPROUT_JOLT_SOURCE_DIR")
    if jolt_path:
        jolt = Path(jolt_path)
        copy_sources(jolt / "Jolt", native / "Jolt/Jolt")
        copy_sources(jolt / "Build", native / "Jolt/Build")
        shutil.copy2(jolt / "LICENSE", native / "Jolt")


def load_signing(path: Path) -> dict:
    signing = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(signing, dict) or not isinstance(signing.get("name"), str):
        raise ValueError("鸿蒙签名文件应包含一项 DevEco signingConfigs 对象")
    material = signing.get("material")
    if not isinstance(material, dict) or not signing["name"]:
        raise ValueError("鸿蒙签名配置缺少 name 或 material")
    for field in ("storePassword", "keyAlias", "keyPassword", "signAlg"):
        if not isinstance(material.get(field), str) or not material[field]:
            raise ValueError(f"鸿蒙签名配置缺少 {field}")
    for field in ("certpath", "profile", "storeFile"):
        value = material.get(field)
        if not isinstance(value, str) or not value:
            raise ValueError(f"鸿蒙签名配置缺少 {field}")
        file = (path.parent / Path(value).expanduser()).resolve()
        if not file.is_file():
            raise ValueError(f"鸿蒙签名材料 {field} 不存在")
        material[field] = str(file)
    return signing


def cmake_quote(value: str) -> str:
    equals = ""
    while f"]{equals}]" in value:
        equals += "="
    return f"[{equals}[{value}]{equals}]"


def configure(project: Path, platform: Path, sdl: str, logic: str, app_id: str, signing_path: str) -> None:
    portable = os.environ.get("GAMESPROUT_HARMONY_OUTPUT") == "deveco"
    version_name = os.environ.get("GAMESPROUT_HARMONY_VERSION_NAME") or "1.0.0"
    version_code = int(os.environ.get("GAMESPROUT_HARMONY_VERSION_CODE") or "1000000")
    app_name = os.environ.get("GAMESPROUT_HARMONY_APP_NAME") or "GameSprout Player"
    compatible = int(os.environ.get("GAMESPROUT_HARMONY_COMPATIBLE_API") or "16")
    target = int(os.environ.get("GAMESPROUT_HARMONY_TARGET_API") or "16")
    if not re.fullmatch(r"[A-Za-z0-9_.{}]{1,127}", version_name) or not 0 <= version_code <= 2147483647:
        raise ValueError("无效的鸿蒙应用版本号")
    if compatible not in HARMONY_VERSIONS or target not in HARMONY_VERSIONS or compatible > target:
        raise ValueError("鸿蒙最低 API 和目标 API 必须为 16–24 或 26，且最低 API 不高于目标 API")

    signing = load_signing(Path(signing_path)) if signing_path and not portable else None
    # The cached Stage tree retains its native objects between direct builds.
    copy_sources(Path(sdl) / "openharmony-project", project)
    if portable:
        stage_portable_sources(project, platform, Path(sdl), logic)

    app_info_path = project / "AppScope/app.json5"
    app_info = json.loads(app_info_path.read_text(encoding="utf-8"))
    app_info["app"]["bundleName"] = app_id
    app_info["app"]["vendor"] = "GameSprout"
    app_info["app"]["versionCode"] = version_code
    app_info["app"]["versionName"] = version_name
    write_json(app_info_path, app_info)

    product = {
        "name": "default",
        "targetSdkVersion": HARMONY_VERSIONS[target],
        "compatibleSdkVersion": HARMONY_VERSIONS[compatible],
        "runtimeOS": "HarmonyOS",
        "buildOption": {
            "nativeCompiler": "BiSheng",
            "strictMode": {"caseSensitiveCheck": True, "useNormalizedOHMUrl": True},
        },
    }
    app = {"products": [product], "buildModeSet": [{"name": "debug"}, {"name": "release"}]}
    if signing:
        app["signingConfigs"] = [signing]
        product["signingConfig"] = signing["name"]
    profile = {"app": app, "modules": [{
        "name": "entry", "srcPath": "./entry",
        "targets": [{"name": "default", "applyToProducts": ["default"]}],
    }]}
    profile_path = project / "build-profile.json5"
    if signing:
        profile_path.touch(mode=0o600, exist_ok=True)
        profile_path.chmod(0o600)
    write_json(profile_path, profile)
    # The JavaScript bridge is SDL's. Its local native module is our only
    # dependency, so build exports need no test package downloads.
    write_json(project / "oh-package.json5", {"modelVersion": "6.1.1", "dependencies": {}})
    (project / "oh-package-lock.json5").unlink(missing_ok=True)
    write_json(project / "entry/build-profile.json5", {
        "apiType": "stageMode",
        "buildOption": {
            "resOptions": {"copyCodeResource": {"enable": False}},
            "externalNativeOptions": {"path": "./src/main/cpp/CMakeLists.txt", "abiFilters": ["arm64-v8a"]},
        },
        "targets": [{"name": "default"}],
    })

    include_file = platform.parent / "MobilePlayer.cmake"
    if portable:
        source_paths = (
            'get_filename_component(native_root "${CMAKE_CURRENT_LIST_DIR}/../../../../Native" ABSOLUTE)\n'
            'set(GAMESPROUT_SDL_SOURCE "${native_root}/SDL")\n'
            + ('set(GAMESPROUT_GAMELOGIC_SOURCE "${native_root}/GameLogic")\n' if logic else '')
            + 'if(EXISTS "${native_root}/Jolt/Build/CMakeLists.txt")\n'
            '    set(FETCHCONTENT_SOURCE_DIR_GAMESPROUT_JOLT "${native_root}/Jolt")\n'
            'endif()\n'
            'include("${native_root}/GameSprout/GameSproutPlayer/Platforms/MobilePlayer.cmake")\n'
        )
    else:
        source_paths = (
            f"set(GAMESPROUT_SDL_SOURCE {cmake_quote(sdl)})\n"
            f"set(GAMESPROUT_GAMELOGIC_SOURCE {cmake_quote(logic)})\n"
            'if(NOT "$ENV{GAMESPROUT_JOLT_SOURCE_DIR}" STREQUAL "")\n'
            '    set(FETCHCONTENT_SOURCE_DIR_GAMESPROUT_JOLT "$ENV{GAMESPROUT_JOLT_SOURCE_DIR}")\n'
            'endif()\n'
            f"include({cmake_quote(str(include_file))})\n"
        )
    native = project / "entry/src/main/cpp/CMakeLists.txt"
    native.write_text(
        "cmake_minimum_required(VERSION 3.21)\n"
        "project(GameSproutHarmonyOS LANGUAGES C CXX)\n"
        + source_paths, encoding="utf-8",
    )
    for resource, key in (
        ("AppScope/resources/base/element/string.json", "app_name"),
        ("entry/src/main/resources/base/element/string.json", "EntryAbility_label"),
    ):
        resources = project / resource
        strings = json.loads(resources.read_text(encoding="utf-8"))
        for entry in strings["string"]:
            if entry["name"] == key:
                entry["value"] = app_name
        write_json(resources, strings)


if __name__ == "__main__":
    try:
        configure(Path(sys.argv[1]), Path(sys.argv[2]), *sys.argv[3:])
    except (OSError, ValueError) as error:
        sys.exit(f"无法生成鸿蒙构建工程：{error}")
