<h1 align="center">央视频下载器</h1>
<p align="center" class="shields">
    <a href="https://github.com/letr007/CCTVVideoDownloader/issues" style="text-decoration:none">
        <img src="https://img.shields.io/github/issues/letr007/CCTVVideoDownloader?style=for-the-badge" alt="GitHub issues"/>
    </a>
    <a href="https://github.com/letr007/CCTVVideoDownloader" style="text-decoration:none" >
        <img src="https://img.shields.io/github/stars/letr007/CCTVVideoDownloader?style=for-the-badge" alt="GitHub stars"/>
    </a>
    <a href="https://github.com/letr007/CCTVVideoDownloader" style="text-decoration:none" >
        <img src="https://img.shields.io/github/forks/letr007/CCTVVideoDownloader?style=for-the-badge" alt="GitHub forks"/>
    </a>
    <a href="https://linux.do" style="text-decoration:none" >
        <img src="https://img.shields.io/badge/LINUX_DO-社区友链-FFD700?style=for-the-badge" alt="LinuxDo"/>
    </a>
</p>

欢迎使用央视频下载器。项目同时提供桌面 GUI `CCTVVideoDownloader` 和无头命令行工具 `cctv-video`，可解析央视网页、列出节目视频并执行多线程下载。

## :white_check_mark:功能特点

- 解析央视节目、专辑和单集链接
- 按月份获取视频列表，可选择性包含节目看点
- 支持单个视频和批量下载
- 支持多线程分片下载、H5E 解密和 TS 合并
- 支持输出 MP4 或保留 TS
- 提供适合脚本处理的 JSON Lines 输出
- GUI 与下载核心解耦，CLI 运行不需要图形显示环境

## :zap:桌面应用

运行 `CCTVVideoDownloader`，导入节目链接后选择栏目和视频即可下载。保存路径、线程数、清晰度、输出格式和列表月份范围可在“设置”中调整。

## 命令行工具

查看完整帮助：

```bash
cctv-video --help
cctv-video --version
```

### 列出视频

```bash
# 使用配置文件中的月份范围
cctv-video list "https://tv.cctv.com/..."

# 指定月份范围
cctv-video list "https://tv.cctv.com/..." --from 202607 --to 202601

# 包含节目看点
cctv-video list "https://tv.cctv.com/..." --include-highlights

# 每行输出一个 JSON 对象
cctv-video list "https://tv.cctv.com/..." --json
```

`--from` 和 `--to` 使用 `yyyyMM` 格式，仅适用于 `list`。未指定时读取与 GUI 共用的配置。

普通输出中的序号从 `1` 开始，可直接用于 `download --select`。

### 从节目 URL 下载

```bash
# 下载列表中的最新一项
cctv-video download "https://tv.cctv.com/..."

# 下载全部视频
cctv-video download "https://tv.cctv.com/..." --select all

# 下载第 1、3、5 项
cctv-video download "https://tv.cctv.com/..." --select 1,3,5

# 指定保存目录、线程数和清晰度，并保留 TS
cctv-video download "https://tv.cctv.com/..." \
  --select latest \
  --output ./videos \
  --threads 4 \
  --quality 0 \
  --no-mp4
```

`--select` 支持：

- `latest`：最新一项，默认值
- `all`：全部项目
- 逗号分隔的序号：例如 `1,3,5`

URL 下载使用配置文件中的月份范围解析视频列表。清晰度值与 GUI 一致：`0` 最高、`1` 超清、`2` 高清、`3` 标清、`4` 流畅。

### 通过 GUID 直接下载

已知 32 位十六进制视频 GUID 时，可跳过网页和列表解析：

```bash
cctv-video download \
  --guid 0123456789abcdef0123456789abcdef \
  --title "视频标题" \
  --output ./videos \
  --threads 4 \
  --mp4
```

`--guid` 必须和 `--title` 一起使用，并且不能同时提供 URL 或列表参数。

### 通用下载参数

| 参数 | 说明 |
| --- | --- |
| `--output <directory>` | 保存目录；默认读取 GUI 共用配置 |
| `--quality <quality>` | 清晰度；默认读取 GUI 共用配置 |
| `--threads <count>` | 下载线程数，必须大于 `0`；默认读取 GUI 共用配置 |
| `--mp4` | 将结果封装为 MP4 |
| `--no-mp4` | 保留 TS 输出 |
| `--json` | 输出 JSON Lines 事件 |
| `--debug` | 将 Qt 内部日志按级别写入标准错误 |

### JSON Lines

`--json` 模式将运行期事件逐行写到标准输出，每行都是独立 JSON 对象，适合由 `jq`、日志采集器或其他程序增量处理。Qt 内部日志默认静默；使用 `--debug` 时会以 `DEBUG`、`INFO`、`WARNING` 或 `CRITICAL` 前缀写入标准错误，不会污染标准输出。

列表事件：

- `video`：视频条目，包含 `index`、`guid`、`title`、`time`、`channel`、`length`、`highlight` 和 `listType`
- `list_complete`：列表完成及条目数量
- `resolution_failed`：URL 解析失败

下载事件：

- `job`：任务状态或进度变化
- `job_finished`：任务结束；失败时包含 `error` 和 `category`
- `download_complete`：批量任务汇总
- `download_start_failed`：任务无法启动

示例：

```bash
cctv-video list "https://tv.cctv.com/..." --json | jq -c 'select(.event == "video")'

cctv-video download \
  --guid 0123456789abcdef0123456789abcdef \
  --title "视频标题" \
  --json
```

参数解析失败发生在 JSON 输出初始化之前，因此会以普通文本写入标准错误。

### 退出码与取消

| 退出码 | 含义 |
| ---: | --- |
| `0` | 成功 |
| `2` | 命令或参数错误 |
| `3` | URL 或视频列表解析失败 |
| `4` | 下载失败 |
| `130` | 收到终止信号或任务被取消 |

前台运行时可按 `Ctrl+C` 取消。CLI 处理 `SIGINT`，并在平台可用时处理 `SIGTERM`；正在执行的批量任务会请求取消后退出。

### 无头运行

`cctv-video` 使用 `QCoreApplication`，不需要 `DISPLAY`、Wayland 或 `QT_QPA_PLATFORM=offscreen`，可直接运行在 SSH 会话、容器或无桌面环境中：

```bash
cctv-video list "https://tv.cctv.com/..." --json
cctv-video download --guid 0123456789abcdef0123456789abcdef --title "视频标题"
```

## :hammer_and_wrench:构建方式

当前项目使用 CMake 构建。构建系统会生成以下主要目标：

- `CCTVVideoDownloaderCore`：仅依赖 Qt Core、Qt Network 和 FFmpeg 的静态核心库
- `CCTVVideoDownloader`：桌面 GUI
- `cctv-video`：无头 CLI

### 环境要求

- CMake 3.21+
- Qt 6.8+（Core / Gui / Widgets / Network；测试还需要 Test）
- C++17 编译器（Windows：MSVC 2022；macOS/Linux：系统工具链）
- Ninja，或其他受 CMake 支持的生成器
- 最小静态 FFmpeg 库，用于进程内 TS→MP4 remux

### 构建最小 FFmpeg

```bash
# macOS / Linux
./scripts/build-ffmpeg-min.sh
# 产出: third_party/ffmpeg-min/{include,lib}

# Windows PowerShell，需要 VS C++ 与 MSYS2/bash
powershell -ExecutionPolicy Bypass -File scripts/build-ffmpeg-min-windows.ps1
```

### 配置和构建

```bash
# macOS / Linux
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="/path/to/qt" \
  -DBUILD_TESTING=ON
cmake --build build --parallel

# Windows / Visual Studio 2022
cmake -S . -B build \
  -G "Visual Studio 17 2022" \
  -A x64 \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.x/msvc2022_64" \
  -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
```

单配置生成器通常将程序输出到 `build/`；Visual Studio 等多配置生成器通常输出到 `build/Release/`。

只构建 CLI：

```bash
cmake --build build --target cctv-video --parallel
```

当前顶层配置仍会查找 Qt Gui 和 Widgets，以同时定义桌面应用目标；运行已经构建好的 `cctv-video` 不需要图形会话。

### 测试

```bash
ctest --test-dir build --output-on-failure

# 多配置生成器
ctest --test-dir build -C Release --output-on-failure
```

测试包括：

- `core_regression_tests`：核心回归测试
- `core_headless_tests`：仅链接 core 与 Qt Test 的无头边界测试
- `cli_tests`：参数、序号选择、JSON 序列化和真实 CLI 进程测试

### 安装

```bash
cmake --install build --prefix ./install

# 多配置生成器
cmake --install build --config Release --prefix ./install
```

CLI 安装到 `install/bin/cctv-video`；Windows 对应文件名为 `cctv-video.exe`。macOS/Linux 安装流程还会安装桌面应用并运行 Qt 部署步骤。

## :beers:帮助与反馈

如有任何疑问或建议，请提交[issues](https://github.com/letr007/CCTVVideoDownloader/issues)。

## :rotating_light: 免责声明  

1. **使用限制**
   - 本工具仅供**技术研究**和**学习交流**使用
   - 严禁用于任何侵犯版权的行为
   - 禁止用于商业用途

2. **版权说明**
   - 央视网（CCTV）所有视频内容版权归中央广播电视总台所有
   - 未经授权，禁止以任何形式下载、传播或商用
   - 使用者应遵守《中华人民共和国著作权法》及相关法规

3. **免责条款**
   - 开发者不对工具的滥用行为负责
   - 使用者需自行承担因使用本工具而产生的所有法律责任
   - 如不同意以上条款，请立即停止使用本工具

##

<img alt="Star History Chart" src="https://api.star-history.com/svg?repos=letr007/CCTVVideoDownloader&type=Date" />
