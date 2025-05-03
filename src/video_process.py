import subprocess
import time
import hashlib

from logger import logger
from PyQt5.QtCore import QThread, pyqtSignal, QObject

from decrypt.decrypt import decrypt_files

class VideoProcess(QObject):

    # 创建信号
    concat_finished = pyqtSignal(bool)
    decrypt_finished = pyqtSignal(bool)

    def __init__(self):
        super().__init__()
        self._logger = logger
        self._logger.debug("初始化视频处理模块")

        self.video_concat = VideoConcat()
        self.video_decrypt = VideoDecrypt()

    def transfer(self, save_path:str, name:str) -> None:
        """传递参数方法"""
        self.file_save_path = save_path
        self.ts_path = os.path.join(save_path, hashlib.md5(name.encode()).hexdigest())
        self.name = name
        self._logger.debug(f"设置视频处理参数 - 保存路径: {save_path}, 名称: {name}")

    def concat(self) -> None:
        """拼接方法"""
        self._logger.info(f"开始视频拼接 - {self.name}")
        self.video_concat.transfer(self.file_save_path, self.ts_path, self.name)
        self.video_concat.start()
        def callback(flag):
            """回调"""
            if flag:
                self._logger.info(f"视频拼接完成 - {self.name}")
            else:
                self._logger.error(f"视频拼接失败 - {self.name}")
            self.concat_finished.emit(flag)
        self.video_concat.finished.connect(callback)

    def decrypt(self) -> None:
        """解密方法"""
        self._logger.info(f"开始视频解密 - {self.name}")
        self.video_decrypt.transfer(self.ts_path)
        self.video_decrypt.start()
        def callback(flag):
            """回调"""
            if flag:
                self._logger.info(f"视频解密完成 - {self.name}")
            else:
                self._logger.error(f"视频解密失败 - {self.name}")
            self.decrypt_finished.emit(flag)
        self.video_decrypt.finished.connect(callback)



class VideoConcat(QThread):
    """视频拼接线程"""

    # 创建信号
    finished = pyqtSignal(bool)
    def __init__(self) -> None:
        super().__init__()
        self._logger = logger
        self._logger.debug("初始化视频拼接线程")

    def transfer(self, save_path:str, ts_path:str, name:str) -> None:
        self.save_path = save_path
        self.ts_path = ts_path
        self.name = name
        self._logger.debug(f"设置拼接参数 - 保存路径: {save_path}, 名称: {name}")

    def run(self) -> None:
        import re, os, shutil
        path = os.path.join(self.ts_path, "ts_decrypt")
        try:
            # 获取文件列表
            file_list = os.listdir(path)
            ts_files = [i for i in file_list if re.match(r"\d+\.ts", i)]
            ts_files = sorted(ts_files, key=lambda x: int(x.split('.')[0]))
            self._logger.info(f"找到 {len(ts_files)} 个TS文件需要拼接")
            self._logger.debug(f"TS文件列表: {ts_files}")

            # 生成合并列表文件
            list_file = os.path.join(path, "video_list.txt")
            with open(list_file, "w+", encoding='utf-8') as f:
                # 格式: file 'ts_file_name'
                f.writelines(f"file '{ts_file}'\n" for ts_file in ts_files)
            self._logger.debug(f"生成合并列表文件: {list_file}")

            # 处理文件名中的非法字符
            illegal_chars_pattern = r'[\\/:*?"<>|]'
            filename = re.sub(illegal_chars_pattern, '', self.name)
            output_path = os.path.join(self.save_path, f'{filename}.mp4')
            self._logger.debug(f"输出文件路径: {output_path}")

            # 获取ffmpeg路径
            ffmpeg_path = resource_path(r"ffmpeg\ffmpeg.exe")
            self._logger.debug(f"FFmpeg路径: {ffmpeg_path}")

            # 构建ffmpeg命令
            command = [
                ffmpeg_path,
                '-f', 'concat',
                '-safe', '0',
                '-i', list_file,
                '-c', 'copy',
                '-y',
                output_path
            ]
            self._logger.debug(f"执行FFmpeg命令: {' '.join(command)}")

            # 执行ffmpeg
            process = subprocess.Popen(
                command,
                creationflags=subprocess.CREATE_NO_WINDOW,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            
            # 等待ffmpeg完成
            stdout, stderr = process.communicate()
            
            if process.returncode == 0:
                self._logger.info(f"视频拼接成功 - {output_path}")
                self.finished.emit(True)
            else:
                self._logger.error(f"视频拼接失败 - 返回码: {process.returncode}")
                self._logger.error(f"FFmpeg错误输出: {stderr.decode('utf-8')}")
                self.finished.emit(False)
            shutil.rmtree(self.ts_path)
        
        except Exception as e:
            self._logger.error(f"视频拼接过程出错: {str(e)}", exc_info=True)
            self.finished.emit(False)

class VideoDecrypt(QThread):
    """视频解密线程"""

    # 创建信号
    finished = pyqtSignal(bool)

    def __init__(self) -> None:
        super().__init__()
        self._logger = logger
        self._logger.debug("初始化视频解密线程")
        
        # 重试配置
        self.max_retries = 3  # 最大重试次数
        self.retry_delay = 2  # 重试延迟（秒）

    def transfer(self, save_path:str) -> None:
        self.save_path = save_path
        self._logger.debug(f"设置解密参数 - 保存路径: {save_path}")

    def run(self) -> None:
        import re, os, shutil
        try:
            # 获取文件列表
            file_list = os.listdir(self.save_path)
            ts_files = [i for i in file_list if re.match(r"\d+\.ts", i)]
            ts_files = sorted(ts_files, key=lambda x: int(x.split('.')[0]))
            
            # 确保解密目录存在
            os.makedirs(decrypt_path, exist_ok=True)
            
            # 遍历所有ts文件进行解密
            for ts_file in ts_files:
                input_file = os.path.join(path, ts_file)
                output_file = os.path.join(decrypt_path, ts_file)
                
                self._logger.info(f"开始解密: {input_file} -> {output_file}")
                try:
                    # 调用解密函数
                    decrypt_files(input_file, decrypt_path)
                    self._logger.info(f"解密成功: {ts_file}")
                except Exception as e:
                    self._logger.error(f"解密失败 {ts_file}: {str(e)}")
                    raise
            
            self._logger.info("所有文件解密完成")
            self.finished.emit(True)
                
        except Exception as e:
            self._logger.error(f"解密过程发生错误: {str(e)}", exc_info=True)
            self.finished.emit(False)

# 处理资源打包后的路径问题

import sys
import os

def resource_path(relative_path):
    """获取资源文件的绝对路径"""
    # 如果程序是打包后的（比如使用 PyInstaller 打包）
    if getattr(sys, 'frozen', False):
        # 获取打包后的可执行文件路径
        base_path = os.path.dirname(sys.executable)
        # 由于auto-py-to-exe的改动，添加以下目录
        base_path = os.path.join(base_path, "_internal")
    else:
        # 获取未打包时的脚本文件路径
        base_path = os.path.dirname(os.path.abspath(__file__))
    
    # 返回资源文件的绝对路径
    return os.path.join(base_path, relative_path)
