# Valley Road 骑马观光

## 主城堡

在 GameSprout Editor 打开 `Assets/Scenes/MedievalCastle.scene`，进入 Play 后点击 Game 画面，按 W 从城外沿石桥骑过主门进入内院。移动、转向和镜头操作与 Valley Road 相同。

主城堡使用 The Medieval Castle 的日间建筑布局及原地形高度。GameSprout 为石墙、塔楼、桥、门和台阶重新绑定材质与碰撞，场景采用原包的 `MC_HDRP_Day.exr`，预烘制 HDR 天空、漫反射及反射 mip。在城门与内院分别增加带实时阴影的暖色聚光灯，塔楼保留原场景中一对点光源的位置。

HDR 全景与内院预览分别在 `Preview/MedievalCastleHDR.png` 和 `Preview/MedievalCastleCourtyardHDR.png`。同一机位的城门局部灯开关对比在 `Preview/MedievalCastleGateLight.png` 和 `Preview/MedievalCastleGateUnlit.png`。

在 Editor 的「窗口 → 环境照明与天空」可调整天空资源、旋转、亮度、环境光、反射、HDR 曝光、色调映射和聚光灯阴影预算。门洞与庭院的灯具在 Hierarchy 的场景根节点下，可通过 Inspector 调颜色、范围、锥角和阴影。若重建主城堡，运行 `python3 Tools/ApplyCastleLighting.py . "Assets/MedievalCastle/Lighting/Medieval Day.environment"` 恢复当前照明方案。

### 主城堡反射与间接光

主城堡场景使用两处盒式反射探针（城门石廊、内庭西侧），并使用由 36 个位置的六面场景捕获生成的两块 SH9 间接光体积。水面使用 Y=-2.75 米的实时平面倒影。捕获限定静态城墙、桥、院地与地形，排除马匹、骑士和水面。

同机位预览位于 `Preview/SpatialLighting`。每组 `-on.png` 和 `-off.png` 只切换该组对照的功能。烘制位置和场景参数保存在 `Tools/CastleSpatialLighting.json`。使用 `Tools/ApplyCastleSpatialLighting.py --compiled <已烘制目录>` 可把对应的 `.environment` 和 `.irradiance` 资源及引用写回场景。

### 1000 骑实时蒙皮压测

`MedievalCastle.scene` 的 `Cavalry Stress` 在 Play 时创建 1000 骑。每骑运行时求值步态与骨骼矩阵，移动、阴影和换装走 GameSprout 的骨骼网格实例化路径。测试要求 Editor 与 GameLogic 使用同一套 ABI 12 SDK。

`CavalryStress` 从 Warhorse Rig 读取模型校准角，每匹马由各自的移动方向驱动根节点。玩家运行中的 Mount Visual 转向不会传给骑兵。地形边界的视觉倾角平滑过渡，步态相位随实际位移推进。用 Game 窗口的 W、Shift + W、S 和右键镜头测试走、跑、倒退、转镜，松开移动键后验证回到 Idle。

2026-11-25 在 macOS Metal 验证全量 1000 骑、480 帧，1000 骑均移动并更新实时蒙皮。更新引擎 Debug 库并重启后，原项目已保存场景在 2292 × 952 的 Editor Game 窗口读到 30.7、35.0 和 39.3 FPS。旧窗口在 2292 × 954 读到 8.5 FPS，当时包含临时测试改动。以上为本机读数，持续 60 FPS 尚未达到。

## Valley Road

用 GameSprout Editor 打开本项目，加载 `Assets/Scenes/ValleyRoad.scene`，点击上方「运行」，然后点一下 Game 画面接收输入。

- W 前进，A / D 转向，S 刹车并可缓慢倒退。
- Shift + W 加速，松开方向键后减速停下。
- 按住右键环绕观察，滚轮缩放，Esc 释放 Game 输入。

路线从谷地土路进入村庄，可继续走到远处的石拱门和塔楼。路面有车辙、碎石和不规则路肩，草丛、林带和石墙沿途分布。场景使用傍晚侧光、软阴影、距离雾和程序生成的云层。地形和建筑碰撞使用 GameSprout 的项目资源及 Runtime 接口。

马匹、鞍具和步态来自 Horse Animset Pro，骑士装备来自 Knighthood。项目使用从 `美素资产` 导入的工作副本。马的混合蒙皮权重均为零，导入副本采用等效线性蒙皮。三条马匹步态已处理为原地动画，位移由 `HorseRider` 脚本和物理控制。骑士使用贴合马鞍的坐姿。

场景已写入 Assets，`Preview/ValleyRoad.png` 和 `Preview/ValleyRoadRide.png` 分别是起点和骑行途中画面。要重新组装，可在项目根目录运行 `python3 Tools/BuildTour.py Tools/TourAssets.json --replace`。该命令会重建场景、程序生成的环境资源和骑士材质，会覆盖这些文件上的手工修改。
