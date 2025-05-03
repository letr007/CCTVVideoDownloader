#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Nuitka 自动构建脚本 for 央视频下载器
用法: 
  python build.py       # 默认编译
  python build.py --debug  # 调试模式(不启用LTO)
"""

import os
import sys
import shutil
import subprocess
import argparse
from pathlib import Path

# 项目配置
PROJECT_NAME = "CCTVVideoDownloader"
MAIN_SCRIPT = "./src/main.py"  # 根据你的实际路径调整
ICON_FILE = "./src/resources/cctvvideodownload.ico"  # 根据你的实际路径调整
DATA_DIRS = {
    "./src/decrypt": "decrypt",
    "./src/ffmpeg": "ffmpeg"
}
OUTPUT_DIR = "build"  # 根据你的需求调整

def build_nuitka(enable_lto=True, show_console=False):
    """执行Nuitka编译命令"""
    # 基础命令
    cmd = [
        sys.executable,
        "-m", "nuitka",
        "--standalone",
        f"--windows-icon-from-ico={ICON_FILE}",
        "--enable-plugin=pyqt5",
        f"--output-dir={OUTPUT_DIR}",
    ]
    
    # 条件参数
    if enable_lto:
        cmd.append("--lto=yes")
    if not show_console:
        cmd.append("--windows-console-mode=disable")
    
    # 添加数据目录（注意=前后不能有空格）
    for src, dest in DATA_DIRS.items():
        cmd.extend([f"--include-data-dir={src}={dest}"])
    
    # 添加主脚本
    cmd.append(MAIN_SCRIPT)
    
    # 打印确认信息
    print("🛠 正在编译项目，请稍候...")
    print("🔧 使用的命令:", " ".join(cmd))
    
    # 使用subprocess运行命令
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"编译失败，错误码: {e.returncode}")

def clean_output():
    """清理输出目录"""
    out_path = Path(OUTPUT_DIR)
    if out_path.exists():
        print("🧹 正在清理输出目录...")
        shutil.rmtree(out_path)

def upx_main():
    """使用UPX进行压缩"""
    dist_dir = os.path.join(OUTPUT_DIR, f"{Path(MAIN_SCRIPT).stem}.dist")
    for root, dirs, files in os.walk(dist_dir):
        for file in files:
            file_path = os.path.join(root, file)
            if file.endswith(f"{Path(MAIN_SCRIPT).stem}.exe"):
                print(f"📦 正在压缩文件: {file_path}")
                subprocess.run(["upx", "--best", file_path])


def main():
    parser = argparse.ArgumentParser(description=f'{PROJECT_NAME} 构建脚本')
    parser.add_argument('--debug', action='store_true', help='调试模式 (禁用LTO)')
    parser.add_argument('--console', action='store_true', help='显示控制台窗口')
    args = parser.parse_args()
    
    try:
        clean_output()
        build_nuitka(
            enable_lto=not args.debug,
            show_console=args.console
        )
        
        # 复制LICENSE等额外文件
        extra_files = ["LICENSE", "README.md"]
        dist_dir = os.path.join(OUTPUT_DIR, f"{Path(MAIN_SCRIPT).stem}.dist")
        for file in extra_files:
            if os.path.exists(file):
                shutil.copy2(file, dist_dir)
        # 复制ffmpeg目录
        shutil.copytree("./src/ffmpeg", os.path.join(dist_dir, "ffmpeg"))
        
        print(f"✅ 编译完成！输出目录: {dist_dir}")
        upx_main()
    except Exception as e:
        print(f"❌ 构建过程中出错: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()