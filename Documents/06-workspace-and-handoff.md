# 06 · 换机与交接

返回 [文档入口](README.md)。本文记录切换开发机时如何恢复工作、怎样复验，以及下一次交接写在哪里。日期以团队沟通时区为准。当前记录日为 2026-11-23。

## 当前接力点

| 项目 | 记录 |
| --- | --- |
| Git 基线 | `main` 的引擎代码 `d8053d0`。近期发行/输入改动对应 `46449e5`、`5635b6e`、`18f6d70`、`145f243`、`d8053d0`。切换分支后以工作区实际 HEAD 为准 |
| 最近完成 | 平台首轮设计文档。代码已有 Editor 本地导出、独立 Player、资源和 GameLogic 加载。玩家 Launcher、在线发行服务、正式签名发行和 CDN 安装方案处于设计阶段 |
| 实测主机 | macOS arm64，Qt 6、Clang、Ninja 和带 GRHI 扩展的 SDL3。其他主机以 [平台矩阵](01-platform-architecture.md) 的验证状态为准 |
| 主测试项目 | `GameSproutFantasyKingdom`，外部项目目录。保留其原始项目源和稳定 GUID。当前代表性启动场景为 `Demo_01` |
| 首个可做事项 | 安装依赖并核验现有 macOS 包。接着为两次本地导出定义与验证发行清单，完成 [阶段 0](05-roadmap-and-acceptance.md#阶段-0--本地端到端样例) |
| 已知阻塞或外部依赖 | 定制 SDL3 的源码/补丁或已构建 SDK 当前在仓库外。原 FantasyKingdom、授权素材和创作者代码也在仓库外。正式 macOS 签名、公证、干净机器及后续目标构建环境需要另行准备 |
| 持久依据 | 本目录、Git 历史及仓库内 CTest。临时包、调试日志、Qt Creator Kit 和操作者本机路径是在原验证机器上产生的本地数据 |

## 换机前后的顺序

1. 在原机检查 Git 工作区和分支，确认待继续的代码、测试和本目录文档进入计划同步的分支。尚未提交的本地文件要先保留，换机前按实际授权同步。官方仓库地址和分支以团队当前配置为准。
2. 在新机克隆或更新同一 Git 仓库，确认与接力点对应的分支、提交和已有修改。示例命令在仓库根运行。

```sh
git status --short
git branch --show-current
git log -5 --oneline
git rev-parse --show-toplevel
```

3. 分别恢复工具链与项目源。Qt、Ninja、Xcode/Clang、定制 SDL SDK 和各平台导出 SDK 由开发机配置。任选目录，使用绝对路径填写本机变量。
4. 通过项目授权渠道恢复 FantasyKingdom。保留 `Assets/` 与各 `.meta`、项目身份与项目设置、游戏脚本和正式源场景。项目和素材授权需随恢复路径传递。素材和项目配置的完整性比历史缓存更重要。项目的 `Library/` 可由导入重建。
5. 原测试项目不可用时，先以 [仓库第三人称示例](../GameSprout/Examples/ThirdPerson/README.md) 验证构建和 Player。该示例可新建到独立的空目标。大型场景和摇杆的验收再用原 FantasyKingdom。需要重导场景时参考 [UnitySceneBridge](../GameSprout/Tools/UnitySceneBridge/README.md)，其输出与用户之前手写的 GameLogic 需要分别恢复。
6. 构建基础组件，跑小规模基线测试，再开始路线图的当前切片。整理下一步和阻塞，并记录新的环境、命令及实际运行结果。

平台发布凭据、SSH 密钥、证书私钥、用户数据和游戏商用素材由各自的受控机制迁移。Git 文档记录凭据项名称、管理方和申请入口。恢复原测试场景前先确认项目正在哪台主机编辑和当前未保存内容。

## 开发机最低环境

| 依赖 | 恢复和核验 |
| --- | --- |
| Git、CMake、Ninja、Python 3 | `git --version`、`cmake --version`、`ninja --version`、`python3 --version`。主要工程要求 CMake 3.21 以上，部分工具要求更高版本 |
| C++20 编译器 | macOS 用兼容的 AppleClang/Clang，核对 `clang++ --version`、`xcode-select -p`、`xcodebuild -version`。原生脚本生成需要仓库支持的 Clang 驱动 |
| Qt 6 开发包 | Qt Widgets、Core、Gui，测试还需要 Qt Test。将对应 Kit 的 CMake 包根传给 `CMAKE_PREFIX_PATH`。使用与当前 SDK 相同的机器架构 |
| SDL3 定制 SDK | 需要 GRHI 的本地显卡扩展 v2，例如 `SDL_GetGPUAdapters` 与 `SDL_GetGPUAdapterExtensionVersion`。从受控位置恢复源码或 SDK。普通上游 SDL 文件无法满足该扩展。记录 SDK 的版本/提交和架构，`SDL3_DIR` 指向可供 CMake 查找的包目录 |
| 依赖下载 | Foundation 和 Jolt 等依赖首次配置可能下载。按仓库根说明核对镜像和工具链，保持可追溯版本 |
| Shader 工具 | 使用游戏自定义 Shader 时准备 DXC、SDL_shadercross 和 SPIRV-Cross。ShaderTools 配置完成后构建 Editor 以部署工具 |
| 非 macOS 目标 SDK | Android 的 SDK/NDK/JDK，HarmonyOS 的 DevEco 工具链/签名，iOS 的 Xcode SDK，Linux 的 Lima 或受支持环境均按对应 Export 脚本核验。Windows 发行器仍是待建项 |

2026-11-23 核对过原机的外部 SDL 源码，HEAD 为 `c17963b8f`，工作区还包含未提交改动。仅凭该提交无法从本仓库恢复实际 SDL SDK。换机前保存可访问的定制 SDK 或完整补丁和构建参数，恢复后通过 GRHI 的扩展版本检查和实际设备测试确认。外部源文件和凭据由各自的仓库或受控备份保存。

参考 [仓库构建与启动](../README.md#构建与启动)、[Ninja 构建说明](../GameSprout/Docs/NinjaBuild.md)、[GRHI SDK](../GameSprout/GRHI/README.md)、[ShaderTools](../GameSprout/Tools/ShaderTools/README.txt)、[Player 导出脚本](../GameSprout/GameSproutPlayer/Platforms) 和 [Linux 构建机设置](../GameSprout/GameSproutPlayer/Platforms/Linux/README.md)。

## 构建、启动与检查

以下命令面向 macOS 新开发机。变量先换成本机实际路径。四个项目按顺序构建。`Bin/` 包含生成的 SDK 和 Editor 产物，保持同一架构和构建配置。

```sh
export GAMESPROUT_QT_PREFIX='/path/to/Qt/macos'
export GAMESPROUT_SDL_PACKAGE='/path/to/custom/SDL/build'
export GAMESPROUT_PROJECT_ROOT='/path/to/GameSproutFantasyKingdom'

cmake -S GameSprout/Foundation -B GameSprout/Foundation/build/Ninja_Debug \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build GameSprout/Foundation/build/Ninja_Debug --parallel

cmake -S GameSprout/GRHI -B GameSprout/GRHI/build/Ninja_Debug \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSDL3_DIR="$GAMESPROUT_SDL_PACKAGE"
cmake --build GameSprout/GRHI/build/Ninja_Debug --parallel

cmake -S GameSprout/GameSproutRuntime -B GameSprout/GameSproutRuntime/build/Ninja_Debug \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGAMESPROUT_RUNTIME_BUILD_TESTS=ON
cmake --build GameSprout/GameSproutRuntime/build/Ninja_Debug --target GameSproutRuntime --parallel

cmake -S GameSprout/GameSproutEditor -B GameSprout/GameSproutEditor/build/Ninja_Debug \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSDL3_DIR="$GAMESPROUT_SDL_PACKAGE" \
  -DCMAKE_PREFIX_PATH="$GAMESPROUT_QT_PREFIX"
cmake --build GameSprout/GameSproutEditor/build/Ninja_Debug --target GameSproutEditor --parallel
```

如果 Qt Creator 或独立 Kit 的 Ninja 未加入 PATH，配置时传入本机 `-DCMAKE_MAKE_PROGRAM=/path/to/ninja`。CMake 缓存包含构建机绝对路径，换机后重新配置。生成目录可以选择新位置，历史产物仅作参考。原项目编辑中的改动先正常保存或明确处理，避免第二个 Editor 实例争用同一项目。

```sh
open -n "$PWD/Bin/GameSproutEditor.app" --args "--ProjectRoot=$GAMESPROUT_PROJECT_ROOT"
```

在 Editor 检查资源导入和脚本编译日志。Play 后点 Game 画面获取焦点，确认输入。用 Build Settings 选择 macOS、激活配置并导出到空闲测试路径。先做独立包渲染冒烟，再通过系统正常启动路径测试实际键鼠。以下 `GAMESPROUT_TEST_APP` 指向这次导出的包。

```sh
export GAMESPROUT_TEST_APP='/path/to/TestGame.app'
"$GAMESPROUT_TEST_APP/Contents/MacOS/GameSproutPlayer" --frames=10
open -n "$GAMESPROUT_TEST_APP"
```

冒烟查看是否加载 GameLogic、进入预期的首个启用场景、呈现一帧并退出。手动记录按下与松开 W、右键转镜、切换焦点后恢复和独立窗口关闭。需要正式分发时增加 Developer ID 签名、公证、Gatekeeper 及另一台干净机器验证。

### 独立测试入口

先构建对应的测试程序。下面是重跑文档所用输入和渲染覆盖的命令。

```sh
cmake --build GameSprout/GameSproutRuntime/build/Ninja_Debug \
  --target RuntimeInputChecks RuntimeRendererChecks \
  RuntimeRenderSnapshotGPUChecks RuntimeSharedRenderResourcesGPUChecks --parallel
ctest --test-dir GameSprout/GameSproutRuntime/build/Ninja_Debug --output-on-failure \
  -R '^Runtime\.(GameplayInput|RendererGPU|RenderSnapshotsGPU|SharedRenderResourcesGPU)$'

GAMESPROUT_TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/gamesprout-checks.XXXXXX")"
cmake -S GameSprout/Tests/SceneDocument -B "$GAMESPROUT_TEST_ROOT/scene" \
  -G Ninja -DCMAKE_PREFIX_PATH="$GAMESPROUT_QT_PREFIX" -DSDL3_DIR="$GAMESPROUT_SDL_PACKAGE"
cmake --build "$GAMESPROUT_TEST_ROOT/scene" --target SceneDocumentChecks SceneManagerChecks --parallel
ctest --test-dir "$GAMESPROUT_TEST_ROOT/scene" --output-on-failure

cmake -S GameSprout/View/PlayModeUI/Tests -B "$GAMESPROUT_TEST_ROOT/viewport" \
  -G Ninja -DCMAKE_PREFIX_PATH="$GAMESPROUT_QT_PREFIX" \
  -DGAMESPROUT_TEST_RUNTIME_DIRECTORY="$PWD/Bin"
cmake --build "$GAMESPROUT_TEST_ROOT/viewport" --target GameViewportInputChecks --parallel
ctest --test-dir "$GAMESPROUT_TEST_ROOT/viewport" --output-on-failure -R '^PlayModeUI\.GameInput'
```

[RuntimeHost](../GameSprout/Module/RuntimeHost/Tests/CMakeLists.txt)、[Editor PlayMode](../GameSprout/Tests/PlayMode/CMakeLists.txt)、[AnimationEditor](../GameSprout/Tests/AnimationEditor/CMakeLists.txt) 也各有独立 CMake 与 CTest 入口。实测 GPU 用例需要支持相应后端的设备。若桌面交互、签名或真机设备条件缺失，记录待验与具体缺口。

## 历史验证快照

下列为 2026-11-23 已完成工作，详细命令、证据范围可见所列 Git 提交正文。本次文档整理没有复跑引擎或发行实测。

| 证据 | 结果与范围 |
| --- | --- |
| `18f6d70` macOS 独立 Player | 旧包装路径创建两个 SDL 窗口。修复后复用一个窗口。FantasyKingdom 启动 `Demo_01`，加载 2 Script 和 3 Module，呈现 2560×1440 首帧。本地 ad hoc 签名校验通过。原生键盘验证走路与松键，右键转镜经 SDL 回放，光标和文本接口成功 |
| `145f243` 设备与 UI 输入 | Runtime 游戏输入与 GPU 回归、RuntimeHost、PlayModeUI（含 2× DPI）、AnimationEditor 和独立 PlayMode 测试通过。FantasyKingdom Editor 的同帧拖出摇杆测到约 1.99 m/s，配置 2 m/s。双触点为注入测试 |
| `d8053d0` Canvas | Scene/Prefab 读写与场景编辑持久化测试通过，Canvas .ui 编译，通过原场景启用安全区后的重复 Game 视窗测试 |
| `5635b6e` 鸿蒙构建配置 | SDK API 24 从 Editor 构建未签名 HAP、导出 DevEco 工程并迁移目录后由 Hvigor 复建。没有实体机安装与签名运行证据 |
| 包体大小抽样 | 修复版 FantasyKingdom 的包中，游戏资源原始文件约 1784 MiB，Player 与引擎相关文件约 19.8 MiB。该样本用于讨论交付优先级，网络传输要以正式压缩包实际测量 |

通过 `git show -s --format=%B <commit>` 可读完整验证记录。真实触屏、触控笔及实体手柄范围以各提交为准，注入测试和虚拟设备测试分别标明。

## 每轮交接记录模板

将最近一轮简记更新在「当前接力点」。较长工作可追加以下日志并链接具体测试。提交或另存证据前清理凭据、个人路径和无权共享的资产。用 Git 提交、仓库内路径或团队可访问的证据编号链接记录。

```markdown
### YYYY-MM-DD · 本轮目标

- 分支、源码提交、发行或测试样例 ID：
- 环境（平台、CPU、GPU、工具/SDK 版本、项目来源）：
- 完成内容与改动入口：
- 实际验证（命令、结果、证据位置）：
- 未执行或失败的验证、原因与影响：
- 下一步（一个可直接执行的任务）：
- 外部依赖、需要的授权及恢复入口：
- 决策变更编号：
```
