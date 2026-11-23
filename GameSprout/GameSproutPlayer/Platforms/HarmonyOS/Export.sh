#!/bin/bash
set -euo pipefail

fail() { printf '%s\n' "$*" >&2; exit 1; }
platform_dir=$(cd -- "$(dirname -- "$0")" && pwd -P)
repo_dir=$(cd -- "$platform_dir/../../../.." && pwd -P)
application_id=${GAMESPROUT_APPLICATION_ID:-org.gamesprout.player}
output_kind=${GAMESPROUT_HARMONY_OUTPUT:-hap}

setup_tools() {
    toolchain=${GAMESPROUT_HARMONY_TOOLS:-}
    if [[ -z "$toolchain" ]] && ! command -v hvigorw >/dev/null; then
        for candidate in "/Applications/DevEco-Studio.app/Contents" "/Applications/DevEco Studio.app/Contents"; do
            if [[ -x "$candidate/tools/hvigor/bin/hvigorw" ]]; then
                toolchain=$candidate
                break
            fi
        done
    fi
    if [[ -n "$toolchain" ]]; then
        toolchain=$(cd -- "$toolchain" && pwd -P) || fail "无法打开鸿蒙工具目录"
        export DEVECO_SDK_HOME="$toolchain/sdk"
        export PATH="$toolchain/bin:$toolchain/tool/node/bin:$toolchain/hvigor/bin:$toolchain/ohpm/bin:$toolchain/tools/hvigor/bin:$toolchain/tools/ohpm/bin:$toolchain/tools/node/bin:$PATH"
    fi
    if [[ -n "${DEVECO_SDK_HOME:-}" ]]; then
        export PATH="$DEVECO_SDK_HOME/default/openharmony/toolchains:$PATH"
    fi
}

[[ ${#application_id} -ge 7 && ${#application_id} -le 128 &&
    "$application_id" =~ ^[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9_]*[a-zA-Z0-9])?){2,}$ ]] ||
    fail "无效的鸿蒙应用标识 $application_id"

case "${1:-}" in
run)
    [[ $# -eq 2 && "$output_kind" == hap && -f "$2" ]] || fail "仅能运行已签名的 HAP"
    [[ -n "${GAMESPROUT_HARMONY_SIGNING_CONFIG:-}" ]] ||
        fail "鸿蒙设备需要已签名的 HAP，请用 DevEco Studio 签名配置构建"
    setup_tools
    command -v hdc >/dev/null || fail "缺少鸿蒙 SDK 的 hdc"
    devices=$(hdc list targets -v) || fail "无法查询鸿蒙设备"
    connected=$(printf '%s\n' "$devices" | tr -d '\r' | awk '$3 == "Connected" { print $1 }')
    device=${GAMESPROUT_HARMONY_DEVICE:-}
    if [[ -z "$device" ]]; then
        [[ -n "$connected" ]] || fail "连接并授权一台鸿蒙设备"
        [[ "$connected" != *$'\n'* ]] || fail "已连接多台鸿蒙设备，请选择运行设备"
        device=$connected
    fi
    printf '%s\n' "$connected" | grep -Fxq -- "$device" || fail "选中的鸿蒙设备尚未连接或授权"
    installation=$(hdc -t "$device" install -r "$2" 2>&1) || fail "$installation"
    printf '%s\n' "$installation"
    [[ "$installation" == *"install bundle successfully"* || "$installation" == *"result:successfully"* ]] ||
        fail "HDC 未确认 HAP 安装成功"
    launch=$(hdc -t "$device" shell aa start -a EntryAbility -b "$application_id" 2>&1) || fail "$launch"
    printf '%s\n' "$launch"
    [[ "$launch" == *"start ability successfully"* ]] || fail "HDC 未确认鸿蒙应用启动成功"
    exit
    ;;
build)
    [[ $# -eq 6 ]] || fail "鸿蒙导出参数不完整"
    ;;
*)
    fail "鸿蒙导出仅支持 build 和 run"
    ;;
esac

payload=$2
logic=$3
output=$4
work=$5
sdl_source=${GAMESPROUT_SDL_SOURCE_DIR:-"$repo_dir/../SDL"}
sdl_source=$(cd -- "$sdl_source" && pwd -P) || fail "无法打开 SDL 源码目录"
template="$sdl_source/openharmony-project"
[[ -f "$template/entry/src/main/ets/entryability/EntryAbility.ets" ]] ||
    fail "缺少与本地 SDL 库同版本的鸿蒙 Stage 项目: $template"

signing=${GAMESPROUT_HARMONY_SIGNING_CONFIG:-}
case "$output_kind" in
deveco)
    [[ ! -e "$output" && ! -L "$output" ]] || fail "DevEco 工程输出已存在: $output"
    project=$output
    signing=""
    ;;
hap)
    project="$work/HarmonyStage"
    setup_tools
    hvigor=$(command -v hvigorw || true)
    [[ -n "$hvigor" ]] || fail "缺少鸿蒙 hvigorw，请安装 DevEco Studio 或配置 GAMESPROUT_HARMONY_TOOLS"
    if [[ -z "${JAVA_HOME:-}" ]]; then
        if [[ -n "$toolchain" && -x "$toolchain/jbr/Contents/Home/bin/java" ]]; then
            export JAVA_HOME="$toolchain/jbr/Contents/Home"
        else
            java_home=$(/usr/libexec/java_home -v '17+' 2>/dev/null || true)
            if [[ -n "$java_home" ]]; then export JAVA_HOME="$java_home"; fi
        fi
    fi
    if [[ -n "${JAVA_HOME:-}" ]]; then export PATH="$JAVA_HOME/bin:$PATH"; fi
    java -version >/dev/null 2>&1 || fail "缺少 HAP 打包使用的 JDK，请安装 JDK 17 或设置 JAVA_HOME"
    ;;
*) fail "未知的鸿蒙输出类型 $output_kind" ;;
esac

# Keep Hvigor's native build tree for incremental exports. The upstream Stage
# template and generated profiles are small and can be refreshed independently.
python3 "$platform_dir/ConfigureProject.py" "$project" "$platform_dir" "$sdl_source" "$logic" "$application_id" "$signing"
python3 "$platform_dir/../PackMobilePayload.py" "$payload" \
    "$project/entry/src/main/resources/rawfile/Game.gspack"
if [[ "$output_kind" == deveco ]]; then
    printf 'GameSprout DevEco Project v1\n' > "$project/.gamesprout-deveco"
    exit 0
fi

products="$project/entry/build/default/outputs/default"
if [[ -n "$signing" ]]; then
    hap="$products/entry-default-signed.hap"
else
    hap="$products/entry-default-unsigned.hap"
fi
(cd -- "$project" && "$hvigor" assembleHap --mode module -p product=default --no-daemon)
[[ -s "$hap" ]] || fail "Hvigor 未生成预期的 HAP: $hap"
python3 - "$hap" "$logic" "$application_id" <<'PY'
import json
import os
from pathlib import Path
import sys
from zipfile import ZipFile

required = {"libSDL3.so", "libmain.so", "Game.gspack", "modules.abc", "module.json"}
if sys.argv[2]:
    required.add("libGameLogic.so")
with ZipFile(sys.argv[1]) as package:
    missing = required - {Path(name).name for name in package.namelist()}
    if missing:
        sys.exit("HAP 缺少文件 " + ", ".join(sorted(missing)))
    app = json.loads(package.read("module.json"))["app"]
    expected = (sys.argv[3], os.environ.get("GAMESPROUT_HARMONY_VERSION_NAME") or "1.0.0",
                int(os.environ.get("GAMESPROUT_HARMONY_VERSION_CODE") or "1000000"))
    if (app.get("bundleName"), app.get("versionName"), app.get("versionCode")) != expected:
        sys.exit("HAP 的应用标识或版本与构建配置不一致")
PY
cp "$hap" "$output"
