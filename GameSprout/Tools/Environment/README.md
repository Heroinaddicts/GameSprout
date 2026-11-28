# 烘制天空环境

用 Python 安装 NumPy 和 OpenEXR。输入 Radiance `.hdr` 时还需让 FFmpeg 的 `ffmpeg` 和 `ffprobe` 可从 PATH 找到。输入格式为 2:1 经纬全景，RGB 值按线性辐射处理。

```sh
python3 -m pip install numpy OpenEXR
python3 GameSprout/Tools/Environment/BakeEnvironment.py Day.exr "/path/to/project/Assets/Lighting/Day.environment" --sky-size 512 --reflection-size 128 --samples 256
```

`--sky-size` 是可见天空每面的分辨率，`--reflection-size` 是预滤波反射最高 mip 每面的分辨率。两者单独调整。烘制结果保存在同一资源中，包含线性 RGBA16F 六面天空、完整粗糙度 mip 链和漫反射 SH9。HDRI 与场景里的方向光同时提供太阳照明时，可用 `--lighting-clamp` 限制烘制参与照明的辐射峰值，保留可见天空的亮部。

把 `.environment` 放入项目 Assets。在 Editor 的 **窗口 → 环境照明与天空** 选择资源，将环境光来源设为「天空环境」，并启用天空绘制。可分别调整天空方向、天空亮度、环境漫反射强度和反射强度。启用 HDR 后可使用固定 EV 曝光和色调映射。Scene、Game 和 Player 使用相同场景设置。Built-in Lit 材质可在高级选项中独立开关环境反射和直接光镜面高光。

Runtime 通过 `IScene::GetEnvironmentLighting()` 和 `SetEnvironmentLighting()` 访问配置。使用资源的 GUID 和 LocalID 赋值给 `SkyEnvironment`。运行时载入预处理产物，按资源版本缓存 GPU 纹理。在同一设备与资源提供者上的多个视口复用资源。调整旋转和亮度无需重新烘制或上传。每个 HDR 视口按当前尺寸分配线性场景色，最后合成到视口的显示目标。

## 场景天气

在 **窗口 → 场景天气** 启用天气。可设置起始小时、完整昼夜的秒数、星空亮度与密度、双层云和闪电。昼夜秒数为 0 时固定时间。应用设置后，场景保存天气参数。通过 `.weather` 导入和导出可以在场景之间复用预设。时间预览、天气过渡预览和手动闪电使用临时状态。

Script 和 Module 包含 `RuntimeWeatherApi.h`，从 Runtime 查询天气接口。

```cpp
if (auto* weather = GameSproutRuntime::QueryWeatherRuntime(*runtime)) {
    GameSproutRuntime::WeatherSettings settings;
    if (weather->GetWeatherSettings(*scene, settings)) {
        settings.Enabled = true;
        weather->SetWeatherSettings(*scene, settings);
        weather->SetTimeOfDay(*scene, 22.0f);
    }
}
```

`TransitionWeather` 在指定秒数内混合预设，`GetWeatherState` 返回当前天气快照。`TriggerLightning` 使用世界坐标和半径，`SubscribeLightning` 每次触发收到一条事件。订阅在 Script 或 Module 的 `Release` 中注销。Scene、Game 和水面倒影共用同一场景的天气状态。

## 环境组合

在 **窗口 → 环境组合** 编辑效果实例、配置层、字段覆盖和曲线。每个效果实例有独立 ID，同一种效果可以添加多次。字段的目标填实例 ID，留空表示场景全局参数。参数目录列出当前 Runtime 注册的字段和默认值。游戏模块在 Play 时注册自定义效果，启动后可以在目录里查看，也可在编辑状态按已知 ID 配置。点「应用」将整组改动作为一条场景撤销记录，下次保存时写入 `.scene` 文件。

- 每个层只修改声明的字段。优先级从低到高应用，权重在 0 到 1 之间。可用世界坐标的中心、半尺寸和渐变距离让层只在指定区域内参与混合。雾从每个视口的相机位置取样。如果场景大气关闭，用 `Fog.Strength` 启用局部雾，再用 `Fog.Start`、`Fog.End` 和 `Fog.Color` 配置距离和颜色。
- 每条曲线绑定一个字段，按场景时钟独立求值。关键帧以秒为单位，按时间递增排列，循环曲线以最后一个关键帧时间为周期。曲线也可设置优先级、权重。
- 旧「场景天气」配置和 `IWeatherRuntime` 接口可以继续使用。旧昼夜、星空和双层云提供基础值，组合只覆盖指定参数。使用 `GameSprout.DayNight` 和 `GameSprout.Starfield` 配置昼夜亮度与星空，使用 `GameSprout.CloudLayer` 添加独立云层。每层云可分别配置 `Coverage`、`Density`、`Scale`、`WindDirectionDegrees` 和 `WindSpeed`。每个视口在同一次天空绘制中最多处理八个可见云层，包含旧天气中启用的云层。

Script 或 Module 包含 `RuntimeEnvironmentApi.h`，通过 `QueryEnvironmentRuntime(*runtime)` 获取接口。`RegisterEnvironmentParameter` 注册可供配置层、曲线和项目逻辑共用的全局字段。类型支持浮点、布尔、三维向量、颜色和资源引用，插值可选线性、最短角度和阶跃。`ResolveEnvironment` 在指定世界位置求值，`FindEnvironmentValue<T>` 读取解析结果。注册后的参数由项目逻辑或效果消费。

`RegisterEnvironmentEffect` 注册自定义天空叠加效果，描述包含类型 ID、材质引用、字段定义和是否绘入平面倒影。Runtime 通过全屏顶点着色器提供 `TEXCOORD0` 世界方向，同名参数写入项目材质；Fragment Shader 可使用 `GameSproutFrame` 读取时间等帧数据。参考主城堡使用的 `MountAndBlade/Assets/Scripts/CastleEnvironment.cpp` 和 `CastleAurora.shader`。参数或效果的所有者应在 `Release` 中注销注册项。

一次性效果事件通过 `EmitEnvironmentEvent` 发出。以字符串标识类型，可附带类型化字段，也可以注册事件描述供调用时校验。内置 `GameSprout.Lightning` 使用世界位置、半径、持续时间和亮度等字段。旧 `TriggerLightning` 同样会发出这个事件，已有闪电回调继续工作。通过 `SubscribeEnvironmentEvents` 订阅，在 `Release` 里注销。场景时钟和闪电持续时间由 Runtime 更新，Scene、Game 和反射只读取结果。

在 MountAndBlade 打开 `Medieval Castle` 并 Play 会加载项目的极光效果。看向西方，极光在运行约 8 秒后开始出现。第三层云和护城河局部雾来自同一个场景的组合配置。

## 场景局部反射和间接光

在主场景的编辑模式打开 **窗口 → 局部光照与反射**。窗口保存的配置属于场景，通过 `IScene::GetEnvironmentLighting()` / `SetEnvironmentLighting()` 也可访问。

- **反射探针**的捕获位置独立于世界坐标影响盒。可以设置全尺寸、边缘过渡、强度、优先级和接收层。选择「烘焙所选探针」可设置捕获距离、每面分辨率、反射分辨率与过滤采样数。材质启用环境反射后，Runtime 按物体选择探针并按像素混合到天空反射。
- **烘焙间接光**的体积可调世界坐标范围、强度、优先级和边缘过渡。选择「烘焙所选体积」可设置 X/Y/Z 的采样数和捕获质量。每处采样点从实际线性场景辐射计算漫反射 SH9。Built-in Lit 在材质表面插值照明，移动物体也能接收体积照明。体积适合门洞、室内和庭院的局部间接光。
- **平面反射**配置世界 Y 高度、反射强度、分辨率比例和上限、最大距离及反射图层。Built-in Lit 材质需勾选「接收平面反射」，并用于与反射平面等高的表面。当前每个场景使用一个水平平面，反射材质进入视野时才绘制镜像相机。Scene 和 Game 统计包含反射尺寸和绘制数。

启用「仅采集静态」时，反射采集使用节点的 `ReflectionProbeStatic`，间接光采集使用 `ContributeGI`。根据需要标记建筑、地面等参与烘焙的网格。采集不用之前的局部探针、体积或平面倒影，因此重复烘焙不会将上次结果再累积。采集分帧进行，每面同步读取 HDR 数据，CPU 过滤由后台任务完成。场景或资产改动会中止当前采集。成功后新资源写到 `Assets/Lighting/Baked` 并自动绑定，撤销会还原场景引用。

自定义离线工具可用 Runtime 的 `RenderView::CaptureLinearColor` 与离屏 `ReadLinearColorHalf()` 输出颜色。参考 `LightingCapture.h`。`.gscube` 文件由 `GSCUBE01` 标识、两项小端 u32（每面边长、保留 0）、六组紧密线性 RGBA16F 组成，面依次为 +X、−X、+Y、−Y、+Z、−Z。该格式可交给 `BakeEnvironment.py`，也可按 X 优先、Y 次之、Z 最后的顺序给 `BakeIrradiance.py` 提供一组编号捕获。

```sh
python3 GameSprout/Tools/Environment/BakeEnvironment.py Gate.gscube Gate.environment --sky-size 128 --reflection-size 64
python3 GameSprout/Tools/Environment/BakeIrradiance.py GateCaptures Gate.irradiance --grid 3 2 3
```
