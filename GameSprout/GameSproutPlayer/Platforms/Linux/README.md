# Linux Player 构建

macOS Editor 通过 Lima 的 Linux 虚拟机构建目标 ELF。产物架构随 Linux 构建机，可用 ARM64 或 x86_64。默认实例名是 `gamesprout-linux`。使用 Homebrew 安装 Lima 后，启动并配置 Ubuntu 构建机。

```sh
limactl start --name=gamesprout-linux --vm-type=vz --mount-none --cpus=4 --memory=6 --disk=20 --containerd=none template:ubuntu-24.04
limactl shell --workdir=/ gamesprout-linux -- sudo apt-get update
limactl shell --workdir=/ gamesprout-linux -- sudo apt-get install -y build-essential cmake ninja-build clang pkg-config rsync libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev libvulkan1 mesa-vulkan-drivers xvfb xauth
```

在 Editor 中切换到 **Linux** 或 **Linux Server** 后构建 `.tar.gz`。解压后运行 `./GameSproutPlayer`。Linux Server 使用同一套 Runtime 和 GameLogic，以无窗口模式启动。Linux 图形版需要 Vulkan 和桌面会话。在无桌面的构建机中，Build And Run 通过 Xvfb 启动，画面留在虚拟显示器里。Exporter 确认首帧后即返回。它输出 Linux VM 中的进程 PID 和日志路径，可在 VM 中向 PID 发出 TERM 关闭预览。可在启动 Editor 时设置 `GAMESPROUT_LINUX_PREVIEW_FRAMES`，让构建机上的预览在指定帧数后退出。

可以使用 `GAMESPROUT_LIMA_EXECUTABLE` 指定 Lima 可执行文件，使用 `GAMESPROUT_LIMA_INSTANCE` 指定另一台已启动的构建机。构建使用 Editor 配置的 GRHI SDL 源码树。Linux VM 内的构建缓存保存在 `~/.cache/gamesprout-linux-player`，工程资源只传输到当次任务的临时目录。
