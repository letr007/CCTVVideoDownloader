<h1 align="center">央视频下载器</h1>

<p align="center">
  用于浏览和下载央视网视频的桌面应用，支持 Windows、macOS 和 Linux。
</p>

<p align="center">
  <a href="https://github.com/letr007/CCTVVideoDownloader/releases/latest"><img src="https://img.shields.io/github/v/release/letr007/CCTVVideoDownloader?style=flat-square" alt="Release"></a>
  <a href="https://github.com/letr007/CCTVVideoDownloader/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/letr007/CCTVVideoDownloader/ci.yml?branch=main&style=flat-square" alt="Build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue?style=flat-square" alt="GPLv3 License"></a>
  <a href="https://github.com/letr007/CCTVVideoDownloader"><img src="https://img.shields.io/github/stars/letr007/CCTVVideoDownloader?style=flat-square" alt="GitHub stars"></a>
  <a href="https://github.com/letr007/CCTVVideoDownloader"><img src="https://img.shields.io/github/forks/letr007/CCTVVideoDownloader?style=flat-square" alt="GitHub forks"></a>
  <a href="https://linux.do"><img src="https://img.shields.io/badge/LINUX_DO-社区友链-FFD700?style=flat-square" alt="LinuxDo"></a>
</p>

## 功能

- 导入央视网栏目、专辑和单集页面
- 按月份浏览节目，支持完整节目、看点和片段
- 单集或批量下载，多线程获取视频分片
- 支持 H5E 视频解密、TS 合并和 MP4 封装
- 下载进度、失败原因和批量结果集中展示
- 提供无图形界面的命令行工具 `cctv-video`

## 桌面应用

启动程序后，在顶部输入央视网链接并导入节目。选择栏目和视频，点击“下载”即可。

保存目录、下载线程、清晰度、输出格式和列表月份范围可在设置中调整。

## 命令行工具

源码构建会同时生成 `cctv-video`。它适合 SSH、容器和脚本任务，不需要图形环境。

列出节目，再按相同 URL 和序号下载：

```bash
URL='https://tv.cctv.com/lm/xwlb/index.shtml'

cctv-video list "$URL"
cctv-video download "$URL" --select 3
```

批量选择和下载参数：

```bash
# 最新一期、全部、第 1/3/5 项
cctv-video download "$URL" --select latest
cctv-video download "$URL" --select all
cctv-video download "$URL" --select 1,3,5

# 指定目录、线程、清晰度和输出格式
cctv-video download "$URL" --select 3 \
  --output ./videos \
  --threads 8 \
  --quality 0 \
  --mp4
```

已知视频 GUID 时可直接下载：

```bash
cctv-video download \
  --guid 0123456789abcdef0123456789abcdef \
  --title '视频标题'
```

常用选项：

- `--from yyyyMM --to yyyyMM`：设置 `list` 的月份范围
- `--include-highlights`：在列表中包含节目看点
- `--json`：输出 JSON Lines，便于脚本处理
- `--debug`：将内部诊断写入标准错误
- `--no-mp4`：保留 TS 文件
- `Ctrl+C`：取消当前任务

完整参数见：

```bash
cctv-video --help
```

## 下载

前往 [Releases](https://github.com/letr007/CCTVVideoDownloader/releases/latest) 下载适合当前系统的版本：

| 系统 | 文件 |
| --- | --- |
| Windows x64 | `CCTVVideoDownloader.*.win.x64.zip` |
| macOS Apple Silicon | `CCTVVideoDownloader.*.macos.arm64.dmg` |
| Linux x86_64 | `CCTVVideoDownloader.*.linux.x86_64.AppImage` |


## 从源码构建

需要 CMake 3.21+、Qt 6.8+、C++17 编译器和 Ninja。测试还需要 Qt Test。

先构建项目使用的最小静态 FFmpeg：

```bash
# macOS / Linux
./scripts/build-ffmpeg-min.sh

# Windows PowerShell（需要 Visual Studio 2022 和 MSYS2）
powershell -ExecutionPolicy Bypass -File scripts/build-ffmpeg-min-windows.ps1
```

配置、构建并运行测试：

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="/path/to/Qt" \
  -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

主要目标：

- `CCTVVideoDownloader`：桌面应用
- `cctv-video`：命令行工具
- `CCTVVideoDownloaderCore`：下载核心静态库

只构建命令行工具：

```bash
cmake --build build --target cctv-video --parallel
```

Windows 使用 Visual Studio 生成器时，在构建和测试命令中分别添加 `--config Release` 和 `-C Release`。

## 问题反馈

解析失败、下载异常或兼容性问题请提交 [Issue](https://github.com/letr007/CCTVVideoDownloader/issues)。报告中请附上节目链接、系统版本、程序版本和必要的 `--debug` 输出，避免上传版权内容或个人信息。

## 免责声明

本项目仅供技术研究和学习交流。央视视频内容版权归相关权利人所有，请遵守网站条款和所在地法律法规，不得将本工具用于侵权传播或商业用途。使用本项目产生的风险由使用者自行承担。

## 许可证

代码按 [GPL-3.0](LICENSE) 发布。
