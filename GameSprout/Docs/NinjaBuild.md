# Ninja 并行构建

日常仍然只维护 `CMakeLists.txt`，在 Qt Creator 中点击原来的构建按钮。CMake 根据工程配置生成构建规则，Ninja 负责并行执行这些规则；不需要手工阅读或修改生成的 `build.ninja`。

Foundation、GRHI、GameSproutRuntime、GameSproutEditor、GameSproutHub、Module、View 和 Tools/ShaderTools 各自提供 `CMakePresets.json`，统一使用以下预设：

| 配置预设 / 构建预设 | 构建类型 | 中间文件目录 |
| --- | --- | --- |
| `ninja-debug` | Debug | 当前工程的 `build/Ninja_Debug` |
| `ninja-release` | Release | 当前工程的 `build/Ninja_Release` |

预设使用 CMake 3.21 支持的 schema v3；ShaderTools 自身仍要求 CMake 3.24。预设中不保存机器路径、环境变量或固定并发数。

## 并发规则

Ninja 默认并行执行。工程的 `Foundation/ParallelBuild.cmake` 自动查询本机逻辑核心数，将编译池上限和 Qt autogen 并发数设置为该值。构建预设不填写 `jobs`，普通构建无需附加 `--parallel 10`。

只有构建依赖已满足的任务才能同时执行；链接和其他依赖仍由构建图调度。该配置不承诺按核心数等比例缩短耗时。

## Qt Creator

在项目的构建配置中选择对应 Ninja 预设，并使用包含编译器和 Qt 路径的现有 Kit。Ninja 可以使用 Qt 安装目录中已有的工具，不要求另外安装或设置环境变量。

已经打开的旧配置不会仅因新增预设自动切换。在当前项目执行 `Build > Reload CMake Presets`，再导入并选择 `Ninja Debug` 或 `Ninja Release` 对应的配置。优先使用预设生成的独立 Kit，保留原 Kit 以及原有 Run/Deploy 参数，避免修改其他项目共用的全局 Kit。

如果预设导入阶段提示找不到 Ninja，可先按下方命令行示例显式指定本机 Ninja 和 Qt 路径完成配置，再通过 `Import Existing Build` 导入生成的 `build/Ninja_Debug` 目录。这样仍无需设置环境变量。修改已有 `.user` 配置文件时，应先正常关闭 Qt Creator 并备份文件，避免运行中的旧配置覆盖修改。

构建目录分别使用 `build/Ninja_Debug` 和 `build/Ninja_Release`。旧的 Unix Makefiles 缓存目录保留，不与 Ninja 共用。切换后检查有效配置中的 `CMAKE_GENERATOR` 为 `Ninja`、`CMAKE_MAKE_PROGRAM` 指向 Ninja。

此后继续使用 Qt Creator 的普通构建按钮即可；无需在每个工程手工填写核心数。

## 命令行

在需要构建的工程目录内运行：

```sh
cmake --preset ninja-debug
cmake --build --preset ninja-debug
```

Release 使用对应预设：

```sh
cmake --preset ninja-release
cmake --build --preset ninja-release
```

如果终端找不到 Ninja，或 Qt 未安装到 CMake 默认搜索路径，配置时显式传入本机路径即可：

```sh
cmake --preset ninja-debug \
    -DCMAKE_MAKE_PROGRAM:FILEPATH=/path/to/ninja \
    -DCMAKE_PREFIX_PATH:PATH=/path/to/Qt/platform
cmake --build --preset ninja-debug
```

显式路径只进入本地构建缓存，不写进公共预设。源文件自动发现、按目录显示工程树以及既有最终产物的 Bin 输出规则保持原有约定。新的目录隔离的是中间文件，不会把最终动态库改成另一套输出路径。

## 工程范围

GameSproutEditor 已经统一构建其 Module 和 View 插件，日常构建 Editor 时无需再单独重复构建 Module、View。它们的预设保留给独立插件开发使用。

Foundation、GRHI、Runtime 和 ShaderTools 继续独立构建，通过公共 SDK 与已有二进制接入 Editor；没有修改的上游不必每次重建。Ninja 不会自动补齐尚未构建的 IMPORTED SDK。单个 Module/View 的独立工程也可以使用 `-G Ninja` 配置，其现有公共并发设置会自动生效。

## Debug 运行性能

AppleClang、Clang 和 GNU 工具链下，GRHI、GameSproutRuntime 和本机 GameLogic 的 Debug 构建默认用 `-O2` 优化，保留 Debug 配置的调试符号和断言。GameLogic 的代码反射和原生编译使用同一组配置参数，参数变动会使脚本缓存自动更新。Kit 的显式 Debug 优化等级可以覆盖 GameLogic 的默认值。优化后，调试时部分变量可能被内联或优化掉。

现有 Ninja 预设分别保留 Debug 和 Release 的中间文件，两者默认写入共用的 `Bin`。更新上游库时，先构建 GRHI 和 Runtime，再构建 GameSproutEditor 将对应 SDK 和脚本编译器放入应用包。重新启动 Editor 后，进程会加载应用包中更新的动态库。做性能对比时同时记录窗口尺寸、场景和实际加载的 Runtime、GRHI、GameLogic 构建配置。

## 本机验证记录

2026-11-01，在本机 10 核、32 GB 内存环境下进行 Debug 验证：

- Editor 隔离目录从空目录构建，编译 223 个对象文件，约 50.3 秒；Ninja 日志和进程采样均确认峰值 10 个并行编译任务。
- Editor 隔离目录无改动再次构建约 0.8 秒，实际项目的 `build/Ninja_Debug` 无改动构建也通过。
- Runtime 构建成功，11 项 CTest 全部通过。
- Foundation、GRHI、Runtime、Editor、Hub、Module、View 的实际 Ninja Debug 配置均通过，编译池深度自动检测为 10。
- Qt Creator 普通构建按钮已在 Runtime 工程验证，执行 `cmake --build ... --target all`，由 Ninja 执行并成功结束，没有附加 `--parallel` 参数。

这些是当前代码和本机环境下的记录，不代表所有机器的固定耗时，也未据此推算其他生成器的加速倍数。

### 2026-11-25 · MountAndBlade 骑兵压测

- macOS Metal，旧 Editor 的 Game 窗口读到 8.5 FPS。主线程采样显示，GRHI 上传缓冲的逐字节初始化和销毁约占 23%。GRHI 改为直接把上传数据复制到未初始化的暂存区，Runtime 将每个网格固定的骨骼包围盒数据移到加载阶段。
- 重建 Debug 依赖并重启 Editor 后，用原项目已保存的 `MedievalCastle.scene` 运行 1000 骑。2292 × 952 的 Game 窗口三次读数为 30.7、35.0 和 39.3 FPS。旧窗口为 2292 × 954，含临时测试改动。新版数值是现场读数，持续 60 FPS 尚未达到。
- 独立 MountAndBlade 项目的 `Assets/Scripts/CavalryStress.cpp` 和 `CavalryStress.h` 修复了骑兵横滑。生成时保留 `Mount Visual` 的本地位置和缩放，模型校准角取自子节点 `Warhorse Rig`，移除从玩家继承的额外 −90° 朝向。每匹马在出生时初始化视觉坡面法线，逐帧用 `1 - exp(-14 * deltaSeconds)` 平滑，只影响视觉倾角。全量验证 480 帧共 469,174 个移动样本，1000 骑持续更新实际骨骼姿态。马身和骑手的可见朝向相对移动方向的最大偏差分别为 15.1° 和 6.6°。
- 本机 Debug 验证通过 Runtime 50 项 CTest、GRHI 4 项 CTest 和 `ScriptCompiler.CommandLine`。真实 GPU 验证在 macOS Metal 上执行。

#### Metal 呈现等待复测

Apple M4 内屏 60 Hz，使用原项目的 `MedievalCastle.scene` 和 Editor Game 视口。通过 Runtime 输入接口重复骑行和转镜，每轮统计第 8 至 26 秒。上屏间隔取自 `CAMetalDrawable.presentedTime`，GPU 执行时间取自同一批提交的 Metal 完成回调。

先前慢帧中，SDL 在 GPU fence 就绪后进入 `nextDrawable`，主线程平均再等 18.12 ms。Core Animation 仍可能占用 drawable，GPU 完成只表示命令执行结束。改为后台预取并在就绪时唤醒 Editor。只预取、仍由固定定时器驱动的对照组，上屏 P95 为 33.333 ms，1019 个间隔中 59 个超过 16.7 ms。加入就绪唤醒后，正式版主线程取图 P95 为 0.008–0.009 ms，最长 0.036 ms。

| 正式版骑行与转镜 | 实际上屏 P95 | 超过 16.7 ms | 最长上屏间隔 |
| --- | ---: | ---: | ---: |
| 第 1 轮 | 16.667 ms | 3 / 1075 | 50.000 ms |
| 第 2 轮 | 16.667 ms | 34 / 1041 | 66.666 ms |
| 第 3 轮 | 16.667 ms | 8 / 1071 | 33.334 ms |

偏离原路线并进入密集骑兵区域的额外一轮，GPU P95 为 21.97 ms，上屏 P95 为 33.333 ms。原生异步视口测试覆盖 Scene 与 Game 并存、改尺寸、销毁及重建。SceneView 的无界面测试通过，`SceneView.RenderScheduling` 的滚轮停止后阴影更新断言失败。

#### 密集骑兵 GPU 复测

沿用上述设备与统计方式，Game 视口约 2292 × 952 像素。使用先前密集路线记录的相机位置重建转镜，通过原 GameLogic 输入推进骑行。对照版和最终版主画面三角形 P95 均约 700 万，场景文件内容保持一致。21.97 ms 来自先前现场样本，本轮可重复对照的 GPU P95 实测为 28.30 ms。

对照版的 Metal 计数器记录了 201 个提交。主画面颜色通道 P95 为 13.15 ms，动态方向光阴影为 12.43 ms。动态阴影每帧提交约 1050 万个三角形，顶点阶段中位耗时 8.22 ms，同一蒙皮网格在多个级联执行。平面反射在执行帧上的 P95 为 1.47 ms。各通道时间可能重叠。加入索引与阴影裁剪优化后、引入紧凑顶点流前复采，颜色和动态阴影通道 P95 分别为 11.60 ms 和 8.13 ms。

| 密集路线 | GPU P95 | 实际上屏 P95 | 超过 16.7 ms | 最长上屏间隔 |
| --- | ---: | ---: | ---: | ---: |
| 优化前对照 | 28.300 ms | 33.333 ms | 315 / 757 | 50.000 ms |
| 最终版第 1 轮 | 16.781 ms | 16.667 ms | 9 / 1070 | 33.333 ms |
| 最终版第 2 轮 | 16.864 ms | 16.667 ms | 4 / 1075 | 33.333 ms |
| 最终版第 3 轮 | 18.844 ms | 33.333 ms | 122 / 957 | 33.334 ms |

两轮达到上屏 P95 目标。反射可见时动态阴影仍需扩大覆盖，密集路线的 60 FPS 稳定性有待改善。相同 GameLogic 的 600 帧固定步检查确认 1000 骑均移动并更新实时骨骼，416 骑实际换装，1000 骑均保留投影，共 2000 个活动蒙皮投影部件。最终版构建、SDK 部署和原项目 Game 视口出图已验证，macOS Metal 上 Runtime CTest 50/50 与 GRHI CTest 4/4 通过。

复测时让基线和候选使用同一项目快照、Game 分辨率及骑行输入，记录实际加载的 SDL、GRHI、Runtime 和 GameLogic 配置。按 GPU 通道采样定位瓶颈，再关闭通道计数器复测上屏。每轮保留上屏 P95、最长间隔和超预算帧数。引擎实现与兼容边界见 [Editor 模块边界](EditorModuleBoundaries.md#实时蒙皮实例化)。
