# 02 · 发行协议与信任链

返回 [文档入口](README.md)。本文定义 GameSprout Game Launch Platform 的发行对象、签名权限、兼容检查和发布事务。文中的协议、服务、状态和接口是设计方案。部署、缓存和安装由 [分发与安装](03-delivery-and-installation.md) 细化，灰度指标和故障响应见 [运营与质量](04-operations-and-quality.md)。

## 公司发行和创作者发行

| 记录 | 责任方 | 内容 | 何时更新 |
| --- | --- | --- | --- |
| 引擎发行 `EngineRelease` | GameSprout 引擎团队 | 公司发布的引擎版本及其目标构建集合 | 公司建立新的引擎发行时登记 |
| 引擎构建 `EngineBuild` | GameSprout 引擎团队 | 某个目标的 Player、Runtime、GRHI、SDL 与实际依赖、运行要求、SDK 指纹、平台签名和验证证据 | 公司生成新目标构建时写入新记录 |
| 游戏发行 `GameRelease` | 创作者提交，平台构建并审查 | 游戏 ID、作者修订、构建配置快照、场景入口、平台产物、游戏逻辑和资源摘要、所需引擎构建 | 游戏代码、资源、打包配置或所依赖引擎发生变化时写入新记录 |
| 渠道状态 `Channel` | 拥有该渠道发布权限的操作人 | 按平台、渠道及适用设备范围选择发行，保存版本序号、滚动比例、暂停或撤销记录 | 通过审核、调整灰度、停止发布或回滚时更新指针 |

引擎和游戏发行都以不可变产物为依据。公司发布新版引擎后，已发行游戏继续解析到原来固定的引擎。创作者选定新版引擎后生成新的游戏候选版本，平台用这个候选验证原生脚本、资源格式、启动和关键玩法，再由有权人员调整渠道。平台维护引擎构建到游戏发行的反向索引，能列出受引擎缺陷影响的游戏、渠道和可用修复构建。

同一账户可以在不同组织担任不同角色。创作者对所属游戏拥有提交、查看和授权范围内的发布权。引擎团队负责官方 SDK、工具链、执行壳和引擎支持周期。发行管理人员操作受权渠道。生产签名服务验证获准产物并执行签名，权力范围由密钥和信任策略限定。

## 仓库里的起点

| 已有能力 | 发行平台需要补齐的工作 |
| --- | --- |
| Hub 保存项目历史，通过 `--ProjectRoot` 启动 Editor。 [HubProjectManager.cpp](../GameSprout/GameSproutHub/HubProjectManager.cpp#L199) | 游戏目录、发行客户端、安装状态和启动凭据。 |
| Build Settings 在本机根据选中和激活的配置构建，收集启用场景、资源 GUID 闭包及 GameLogic 源，给 Shader 记录工具链指纹。 [BuildSetting.cpp](../GameSprout/Module/BuildSetting/BuildSetting.cpp#L212) · [BuildInputs.cpp](../GameSprout/Module/BuildSetting/BuildInputs.cpp#L54) · [ShaderImporter.cpp](../GameSprout/Module/AssetsPipeline/ShaderImporter.cpp#L40) | 可重试的构建委托，独立于 Editor 当前选择。冻结源码、依赖和导入设置，形成一个明确的输入修订。 |
| `Player.json` 记录启用场景、是否有原生代码和资源条目。导出时检查复制的资产摘要。Player 校验资源格式、加载原生模块并打开第一个启用场景。 [PlayerBuildJob.cpp](../GameSprout/Module/BuildSetting/PlayerBuildJob.cpp#L197) · [PlayerPackage.cpp](../GameSprout/GameSproutPlayer/PlayerPackage.cpp#L36) · [PlayerMain.cpp](../GameSprout/GameSproutPlayer/PlayerMain.cpp#L83) | 签名发行清单、版本锁定、分渠道装载策略及网络安装的完整验证。 |
| macOS Player 按包内约定查资源和动态库。Runtime 从自身所在目录加载 GRHI。 [PlayerMain.cpp](../GameSprout/GameSproutPlayer/PlayerMain.cpp#L258) · [RuntimeViewport.cpp](../GameSprout/GameSproutRuntime/Renderer/RuntimeViewport.cpp#L15) | 桌面 Player 显式区分引擎包和游戏安装位置。每个引擎构建保持其动态库、渲染依赖和启动壳成套部署。 |
| 当前 Editor 构建平台定义包括 macOS、Android、HarmonyOS、iOS、Linux 和两个 Server 目标，Windows 还没有进入该构建目标表。 [IBuildSetting.h](../GameSprout/Module/Interface/IBuildSetting.h#L8) | Windows 导出和验证、各渠道的生产签名和提交、目标设备测试。每个变体独立记录支持范围和证据。 |

## 标识、平台变体和版本规则

游戏 ID 和组织 ID 由服务端登记，长期稳定。展示名、公司名和市场语言可以更新。原生包身份使用各渠道的 Bundle ID、Package Name 或 Publisher Identity，由游戏与渠道绑定。绑定迁移涉及平台账号、签名和已安装应用的升级路径，独立审批和记录。

每个引擎发行可包含多个引擎构建，每个构建都针对一个明确的目标，如 macOS arm64。游戏发行在目标上绑定精确的 `engineBuildId` 和引擎清单摘要，绑定由签名覆盖。桌面私有发行可先把引擎和游戏打包进按平台完整签名的 Player。共享缓存接入后，桌面客户端可以复用本机已经验证的同一引擎构建。移动商店变体可以把引擎和 GameLogic 都链接或打包进提交产物，发行记录仍保留实际对应的引擎构建。

兼容检查采用明确的目标信息，包括操作系统、最低系统版本、CPU/ABI、图形后端、原生脚本接口版本、SDK/编译器/C++ 标准库或 CRT 指纹、资源格式和分发渠道。设备报告的能力决定是否提供该变体。以 GameSprout 的图形策略为约束，Apple 目标使用 Metal，Windows 使用 D3D12，Android、Linux 和 HarmonyOS 使用 Vulkan。运行时选择逻辑根据签名的目标约束匹配真实设备，生成可解释的失败原因。 [GRHI 驱动选择](../GameSprout/GRHI/GRHI/DeviceSDL.cpp)

产品展示版本供玩家和开发者阅读。每个渠道另存单调增长的发布代数，操作系统和商店使用各自要求的版本号。原生 ABI 的整数版本用于快速拒绝不兼容入口，仍要校验完整构建身份。现有 SDK 注释要求 SDK、编译器和 CRT 匹配，当前 Player 会检查 `ScriptModuleABIVersion`，该入口检查发生在系统装载动态库之后。 [RuntimeScriptApi.h](../GameSprout/GameSproutRuntime/Api/RuntimeScriptApi.h#L182) · [PlayerMain.cpp](../GameSprout/GameSproutPlayer/PlayerMain.cpp#L100)

引擎维护状态与游戏渠道独立记录。停止接收使用旧引擎的新发行前，应能列出受影响的创作者及已验证的升级目标。下载保留期覆盖仍在服务的游戏与可用回退版本。发现必须撤销的旧引擎时，平台用反向索引定位关联的游戏并推进重建、重测及重新提交。

## 发行记录与接口契约

| 概念 | 建议保留的字段 |
| --- | --- |
| 构建输入 `BuildSnapshot` | 项目修订摘要、资源导入输入和依赖锁定、构建配置、平台目标、指定引擎和 SDK、构建任务发起人。 |
| 引擎构建 `EngineBuild` | 引擎发行 ID、构建 ID、目标、API/ABI 与资源协议版本、组件版本、最终引擎产物摘要、工具链和构建来源证明。 |
| 游戏变体 `GameVariant` | 游戏发行 ID、目标、渠道安装方式、所需引擎构建 ID 和摘要、可下载产物或商店产品引用、产物的 SHA-256 和字节数、支持的存档及联机协议范围。 |
| 发行证据 `ReleaseEvidence` | 候选和最终产物摘要、测试运行 ID、硬件及系统范围、实际执行结果、审批人、例外原因及适用范围。 |
| 渠道修订 `ChannelRevision` | 渠道 ID、代数、所选游戏变体、按目标设置的发布范围、有效期、停止或回退原因、操作者。 |

发行记录的传输编码、字段大小上限、时间与摘要格式、未知字段及扩展策略需要明确版本。签名覆盖约定编码后的完整内容，并区分引擎、游戏和渠道用途。公钥、信任范围及轮换信息从已受信任的根元数据解析。内部存储格式和 API 均可渐进更新，签名输入必须稳定。商店变体分别记录提交物摘要和商店返回的产品、构建及版本标识。商店拆包或重签后的设备安装物以系统签名和商店状态为依据。

下面是独立桌面变体的示意记录。摘要和 ID 是占位符。

```json
{
  "schemaVersion": 1,
  "gameId": "game-example",
  "gameReleaseId": "release-example-2",
  "displayVersion": "0.2.0",
  "variant": {
    "platform": "macos",
    "architecture": "arm64",
    "graphicsBackend": "metal",
    "installMode": "launcher",
    "engineBuildId": "engine-example-macos-arm64",
    "engineManifestSha256": "<sha256>",
    "gameManifestSha256": "<sha256>"
  }
}
```

接口名称用于描述职责，最终路由由服务实现确定。

| 操作 | 契约 |
| --- | --- |
| `SubmitBuild` | 校验组织、游戏与目标权限，接受已冻结的输入和幂等键，返回任务 ID。同一权限范围内使用同一键和同一输入会返回同一次任务，输入变化时显式报冲突。 |
| `GetBuildJob` | 返回可供 Editor 或 CI 查询的状态快照、各平台阶段、失败原因和获准查看的日志。断线后从快照恢复。 |
| `ApproveRelease` | 关联最终产物摘要和已完成验证结果，保存批准范围及依据。产物或审核范围变化后重新审批。 |
| `PromoteChannel` | 根据预期渠道代数提交目标发行和灰度范围。并发代数不一致时返回冲突。候选发布时再次核对产物可用性、目标状态和权限。 |
| `ResolveLaunchPlan` | 按账户权利、渠道、地区、设备和撤销状态返回匹配的安装方式与签名清单。渠道禁用或变体未验证时给出可识别的结果。 |
| `Pause`、`Rollback`、`Revoke` | 产生有审计信息的新渠道修订或撤销记录。已转交商店的操作保留外部提交 ID 和可核对的状态。 |

## 构建、审查和密钥

可重试任务的基本过程如下。

1. 冻结项目快照和平台配置，固定输入依赖、编译器、SDK、引擎构建与环境，并保存输入摘要。
2. 每个目标在隔离的临时构建环境运行，限制任务能读取的项目、凭据、网络和构建缓存。创作者可提交的原生代码和构建扩展均按不可信输入运行。平台签名凭据留在独立的签名服务中。
3. 收集 GameLogic、资产、安装包和符号，检查产物摘要。构建证明关联源码修订、已解析依赖、构建器身份、构建参数及产物摘要。代码、工具链或签名时间戳可能影响最终字节，按验证结果区分可追溯、可重新构建和逐字节可复现。 [SLSA 构建来源](https://slsa.dev/spec/v1.2/build-provenance)
4. 针对适用目标执行装包、兼容验证、原生窗口启动、首场景和必要玩法检查。候选构建的检查结果关联被测摘要。平台签署或商店重签会产生新的交付身份，按渠道继续验签并验证最终可安装的版本。
5. 签名服务核对已批准的精确摘要及发布类型，再执行平台签名或发行清单签名。签名后的最终文件放到受控发布存储。平台签名、公证和商店审核结果分别记载。

信任根由平台独立保管，根签名权限可采用离线密钥和多方授权。在线发行密钥按职责及范围分别管理引擎、游戏目标、渠道和下载授权。让游戏密钥只签获授权的项目。生产签名使用可审计的密钥服务或硬件保管，构建机只能提交签名作业的依据。设备内置或通过可信链更新根公钥。

轮换先生成新的可信密钥集，按根的版本规则签发并传给客户端，然后切换签发。泄露时停用受影响密钥，从可用信任根更新授权并撤销受影响的引擎、游戏或渠道签名。保留可查的签发记录用于列举影响范围，使用未受影响的密钥重新签发已核对的产物。根密钥治理、备份及恢复过程要能演练。平台代码签名证书失效时，另按操作系统和渠道流程处理原应用的升级身份。离线或长期未更新的客户端有传播延迟，按事先约定的有效期和离线策略处理。

更新元数据可以采用 TUF 的根、目标、快照与时间戳职责。平台批准对旧游戏版本的回退时，仍签发更高代数的渠道记录并指向已验证的旧产物。客户端据此识别正规回退与过期元数据。 [TUF 规范](https://theupdateframework.github.io/specification/latest/)

## 产物验证与启动前信任边界

大文件在私有构建区上传，验证后的产物按摘要写入不可变存储。不同发布可引用同一份实际相同的内容。候选清单签名并确认所需对象可用后，发行才可进入渠道。写入相同路径的新内容或读取未知临时文件会破坏版本绑定，平台通过不可变写入策略和发布时的检查防止这种情况。

Launcher 从可信根校验渠道和发行元数据，检查更新代数、有效期、撤销状态和目标匹配。下载后先验证签名声明的长度和实际摘要，再按允许的文件结构解包到暂存区。验证平台签名及内嵌文件清单完成后，原子地启用新安装。Launcher 启动已验证的 Player，Player 在加载任何 GameLogic 前确认游戏与引擎绑定，包括动态库依赖路径。仓库现有 `GSPACK1` 接收应用包内的资源文件，把头部摘要用于本地缓存目录。公网发行要增加完整字节验证和版本留存。 [MobilePayload.h](../GameSprout/GameSproutPlayer/Platforms/MobilePayload.h#L87)

原生游戏逻辑由独立 Player 进程运行，Launcher 保存登录状态和安装控制权，游戏只取得本次会话所需的最小授权。独立进程能隔离崩溃，需要有操作系统权限策略才能约束本机文件和网络访问。更新程序、下载权限和平台签名也分别校验，各自承担发布者身份、文件完整性或访问资格的验证。

## 发布、失败和回退

```text
构建任务    已提交 → 已排队 → 构建中 → 检查中 → 完成
变体状态    暂存 → 已验证 → 已批准 → 可发布
渠道状态    内部测试 → 灰度 → 公开
应急操作    暂停 → 恢复 / 回退 / 撤销
```

构建支持取消和失败终止，已存在的正式版本可继续运行。失败的目标保留诊断、支持继续重试。在允许先发部分平台的渠道中，发布计划逐一声明纳入的变体，失败变体不进入目录。承诺同批上线的渠道等待所有必需目标验证通过。商店审核和分阶段上线是外部过程，控制台展示真实进度。

灰度按版本和设备范围稳定分组。比较实际下载安装、启动和运行表现，再扩大覆盖。发现 ABI、资源或渲染异常时可暂停受影响的变体或设备范围。回退需确认旧版本能读取当前存档并连接兼容服务器，必要时先恢复兼容数据或协议。已安装实例由客户端按该渠道的策略处理，安装失败会保留之前可用的完整版本。

常规暂停停止新增下载或调整灰度，仍允许正常运行的已装版本。安全撤销阻断对应产物的新安装并按规则限制后续启动，撤销记录由可信元数据发布。正在运行和离线的版本如何处理需要可预期的策略，紧急撤销的生效范围要在控制台明确。外部商店能提供的回退和分阶段控制以各渠道实际能力为准。Apple App Store 的已发布版本需要提交新版来修复，暂停灰度只适用于受控阶段。 [App Store 分阶段发布](https://developer.apple.com/help/app-store-connect/update-your-app/release-a-version-update-in-phases) · [App Store 创建新版本](https://developer.apple.com/help/app-store-connect/update-your-app/create-a-new-version)

## 渠道、签名与缺口

| 渠道 | 原生代码和资源的发布方式 | 当前需要补齐 |
| --- | --- | --- |
| macOS 官网或桌面 Launcher | 按引擎构建安装完整的签名、公证 Player 包，运行依赖随包。游戏资源放在独立的已验证安装区。另行交付原生 GameLogic 时，确认签名身份和库校验边界。 | 现有导出使用本地 ad-hoc 签名。补 Developer ID、Hardened Runtime、公证和首次用户安装验证。Hardened Runtime 默认允许加载 Apple 或同 Team ID 签名的库。由同一签名身份承接审核后的 GameLogic 或采用独立签名游戏包，均需按各自流程验证。 [现有导出签名](../GameSprout/Module/BuildSetting/PlayerBuildJob.cpp#L153) · [macOS 公证](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution/) · [Apple Library Validation](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation) |
| Windows 桌面分发或 Microsoft Store | 桌面 Launcher 验证按架构发行的 Player 和原生代码。选择 MSIX 或已有安装器路线时，对应采用它的签名和更新机制。 | 补 Windows 构建与导出器，验证 D3D12、所需运行库和安装升级。按 MSIX、MSI 或 EXE 的渠道要求核对负责签名的一方。 [Windows 分发路径](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/choose-distribution-path) |
| Linux 桌面 | 使用 Launcher 验证可安装软件包、发行版运行要求和 Vulkan 支持。 | 已有通过 Lima 生成 Linux 产物的路径。生产证据需覆盖目标系统和图形环境，本机导出结果按实际可运行范围标记。 [Linux 导出说明](../GameSprout/GameSproutPlayer/Platforms/Linux/README.md) |
| Android Google Play | 原生代码通过 Google Play 提交。游戏按渠道支持的方式交付资源，可评估 Play Asset Delivery。 | 现有导出生成 APK 并支持签名配置，默认调试签名。补应用标识和版本配置、生产签名、AAB 与商店提交、真实安装与升级验证。 [Android 导出](../GameSprout/GameSproutPlayer/Platforms/Android/Export.sh) · [Google Play 政策](https://support.google.com/googleplay/android-developer/answer/18265346) · [Play Asset Delivery](https://developer.android.com/guide/playcore/asset-delivery) |
| iOS App Store | 原生 GameLogic 随经系统签名和商店审核的应用提交。Launcher 目录或商业方案按适用审核条款提供入口。 | 已有将 GameLogic Framework 随应用构建的路径。补真实签名、App Store Connect 流程和真机覆盖。原生逻辑通过云端下载执行需遵守平台审核限制。 [iOS Player 打包](../GameSprout/GameSproutPlayer/Platforms/iOS/PlayerIOS.cmake) · [Apple 审核指南](https://developer.apple.com/app-store/review/guidelines/) |
| HarmonyOS | 按目标设备和发行渠道交付经批准的签名应用。HAP 和 DevEco 交付物分别标记用途。 | 已有 HAP 和 DevEco 生成及本地签名前置检查。生产流程仍需对应签名、审核和真实设备升级证据。 [HarmonyOS 导出](../GameSprout/GameSproutPlayer/Platforms/HarmonyOS/Export.sh) |

桌面官网、自有客户端和官方商店应采用各自的签名、缓存与安装方式。移动端资源可使用适用渠道允许的 CDN 或商店交付服务。若进入其他地区的替代分发渠道，应建立单独的渠道变体，记录当期资格和行为限制。

## 发布前验证

| 场景 | 可验收结果 |
| --- | --- |
| 换用同一展示版本的其他引擎构建 | 客户端在启动前指出精确身份不匹配，等待已批准的组合。 |
| CDN 对象被替换、下载截断或签名过期 | 客户端拒绝激活、保留当前可用版本并给出可诊断信息。 |
| 游戏构建只通过部分目标 | 公开目录只呈现审核通过、已安装验证且纳入发布计划的变体。 |
| 签名机暂时不可用、任务被重复提交或发布指针并发改写 | 不产生半发布版本。重试恢复任务状态，渠道变更通过代数校验。 |
| 已签名版本启动后发现真实输入或渲染问题 | 能关联产物与测试证据，暂停相关渠道或变体，验证可用回退后再扩大范围。 |
| 撤销在线签名密钥或特定引擎 | 受影响范围可枚举，新安装按策略失败，收到元数据的客户端可执行撤销处置，已批准的替代版本可恢复发行。 |
| 设备离线以及商店无法降级 | 分别按离线有效期或商店补发策略保留可用状态，并记录客户端的真实结果。 |

首个验证可以从现有 macOS 干净渠道与 FantasyKingdom 开始，产物和审核证据关联这次实际发行。跨系统兼容、商店上架、故障演练和性能数据逐项扩大测试范围，状态始终以实际证据为准。
