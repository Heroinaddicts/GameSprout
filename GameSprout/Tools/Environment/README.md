# 烘制天空环境

用 Python 安装 NumPy 和 OpenEXR。输入 Radiance `.hdr` 时还需让 FFmpeg 的 `ffmpeg` 和 `ffprobe` 可从 PATH 找到。输入格式为 2:1 经纬全景，RGB 值按线性辐射处理。

```sh
python3 -m pip install numpy OpenEXR
python3 GameSprout/Tools/Environment/BakeEnvironment.py Day.exr "/path/to/project/Assets/Lighting/Day.environment" --sky-size 512 --reflection-size 128 --samples 256
```

`--sky-size` 是可见天空每面的分辨率，`--reflection-size` 是预滤波反射最高 mip 每面的分辨率。两者单独调整。烘制结果保存在同一资源中，包含线性 RGBA16F 六面天空、完整粗糙度 mip 链和漫反射 SH9。HDRI 与场景里的方向光同时提供太阳照明时，可用 `--lighting-clamp` 限制烘制参与照明的辐射峰值，保留可见天空的亮部。

把 `.environment` 放入项目 Assets。在 Editor 的 **窗口 → 环境照明与天空** 选择资源，将环境光来源设为「天空环境」，并启用天空绘制。可分别调整天空方向、天空亮度、环境漫反射强度和反射强度。启用 HDR 后可使用固定 EV 曝光和色调映射。Scene、Game 和 Player 使用相同场景设置。Built-in Lit 材质可在高级选项中独立开关环境反射和直接光镜面高光。

Runtime 通过 `IScene::GetEnvironmentLighting()` 和 `SetEnvironmentLighting()` 访问配置。使用资源的 GUID 和 LocalID 赋值给 `SkyEnvironment`。运行时载入预处理产物，按资源版本缓存 GPU 纹理。在同一设备与资源提供者上的多个视口复用资源。调整旋转和亮度无需重新烘制或上传。每个 HDR 视口按当前尺寸分配线性场景色，最后合成到视口的显示目标。
