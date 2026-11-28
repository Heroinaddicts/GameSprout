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
