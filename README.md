<h1 align="center">央视频下载器</h1>
<p align="center" class="shields">
    <a href="https://github.com/letr007/CCTVVideoDownloader/issues" style="text-decoration:none">
        <img src="https://img.shields.io/github/issues/letr007/CCTVVideoDownloader.svg" alt="GitHub issues"/>
    </a>
    <a href="https://github.com/letr007/CCTVVideoDownloader" style="text-decoration:none" >
        <img src="https://img.shields.io/github/stars/letr007/CCTVVideoDownloader.svg" alt="GitHub stars"/>
    </a>
    <a href="https://github.com/letr007/CCTVVideoDownloader" style="text-decoration:none" >
        <img src="https://img.shields.io/github/forks/letr007/CCTVVideoDownloader.svg" alt="GitHub forks"/>
    </a>
</p>

欢迎使用央视频下载器！该程序允许您从[央视网](https://tv.cctv.com)获取视频内容，并支持多线程处理。以下是该程序的一些主要功能和使用说明。

## :white_check_mark:功能特点

- 获取节目列表信息
- 支持多线程处理
- 支持从链接中解析视频ID

## :zap:使用方法

运行程序，从节目列表中选择一个栏目，点击后会自动刷新视频列表，选择一个视频后， 
将在右侧得到此节目的详细信息，然后您可以进行相关操作。

## :pencil:配置设置

您可以通过`设置`菜单来配置程序的一些参数，包括保存路径、线程数等设置。

## :hammer_and_wrench:构建方式

当前项目以 CMake 作为主要构建方式。

### 环境要求

- CMake 3.21+
- Visual Studio 2022
- Qt 6.8.0（`msvc2022_64`）

### 配置

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="qt的msvc路径"
```

### 构建

```powershell
cmake --build build --config Release
```

构建完成后，可执行文件默认位于：

```text
build/Release/CCTVVideoDownloader.exe
```

运行所需的 `decrypt` 目录会在构建后自动复制到输出目录。

## :beers:帮助与反馈

如有任何疑问或建议，请提交[issues](https://github.com/letr007/CCTVVideoDownload/issues)。

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

