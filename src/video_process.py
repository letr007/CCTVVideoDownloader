import subprocess

from logger import logger
from PyQt5.QtCore import QThread, pyqtSignal, QObject

from decrypt.decrypt import decrypt_files

class VideoProcess(QObject):

    # 创建信号
    concat_finished = pyqtSignal(bool)
    decrypt_finished = pyqtSignal(bool)

    def __init__(self):
        super().__init__()

        self.video_concat = VideoConcat()
        self.video_decrypt = VideoDecrypt()

    def transfer(self, save_path:str, name:str) -> None:
        """传递参数方法"""
        self.file_save_path = save_path
        self.name = name


    def concat(self) -> None:
        """拼接方法"""
        self.video_concat.transfer(self.file_save_path, self.name)
        self.video_concat.start()
        def callback(flag):
            """回调"""
            self.concat_finished.emit(flag)
        self.video_concat.finished.connect(callback)

    def decrypt(self) -> None:
        """解密方法"""
        self.video_decrypt.transfer(self.file_save_path)
        self.video_decrypt.start()
        def callback(flag):
            """回调"""
            self.decrypt_finished.emit(flag)
        self.video_decrypt.finished.connect(callback)



class VideoConcat(QThread):
    """视频拼接线程"""

    # 创建信号
    finished = pyqtSignal(bool)
    def __init__(self) -> None:
        super().__init__()
        self._logger = logger

    def transfer(self, save_path:str, name:str) -> None:
        self.save_path = save_path
        self.name = name

    def run(self) -> None:
        import re, os, shutil
        path = os.path.join(self.save_path, "ctvd_tmp")
        decrypt_path = os.path.join(self.save_path, "ctvd_tmp/ts_decrypt")
        try:
            # 获取文件列表
            file_list = os.listdir(decrypt_path)
            ts_files = [i for i in file_list if re.match(r"\d+\.ts", i)]
            ts_files = sorted(ts_files, key=lambda x: int(x.split('.')[0]))
            self._logger.info(f"拼接临时文件:{ts_files}")

            # 生成合并列表文件
            with open(f"{decrypt_path}/video_list.txt", "w+") as f:
                for ts_file in ts_files:
                    tmp = "file '" + ts_file + "'\n"
                    f.write(tmp)

            # 手动指定ffmpeg的路径
            # os.environ['FFMPEG_BINARY'] = resource_path(r"./ffmpeg")
            # os.environ['FFPROBE_BINARY'] = resource_path(r"./ffmpeg")
            ffmpeg_path = resource_path(r"ffmpeg\ffmpeg.exe")
            # 合并.ts文件为单个视频
            # ffmpeg.input(f"{path}/video_list.txt", format="concat", safe=0).output(f"{self.save_path}\\{self.name}.mp4",c = 'copy', y = '-y').run()
            # os.system("cd C:/ffmpeg | ffmpeg -f concat -safe 0 -i {path}/video_list.txt -c copy {self.save_path}/{self.name}.mp4")

            illegal_chars_pattern = r'[\\/:*?"<>|]'  # 匹配非法字符
            filename = re.sub(illegal_chars_pattern, '', self.name)
            output_path = fr'{self.save_path}\{filename}.mp4'
            self._logger.info(f"输出路径:{output_path}")

            command = [f'{ffmpeg_path}', '-f', 'concat', '-safe', '0', '-i', fr'{decrypt_path}\video_list.txt', '-c', 'copy', '-y', output_path]
            # 使用 subprocess 调用 ffmpeg，并设置 creationflags 参数
            process = subprocess.Popen(command, creationflags=subprocess.CREATE_NO_WINDOW)
            # process = subprocess.Popen(command)

            # 等待 ffmpeg 进程结束
            process.wait()
            # 移除临时目录
            shutil.rmtree(path)

            self.finished.emit(True)
        
        except Exception as e:
            self.finished.emit(False)
            print(e)

class VideoDecrypt(QThread):
    """视频解密线程"""

    # 创建信号
    finished = pyqtSignal(bool)

    def __init__(self) -> None:
        super().__init__()
        self._logger = logger

    def transfer(self, save_path:str) -> None:
        self.save_path = save_path

    def run(self) -> None:
        import re, os, shutil
        path = os.path.join(self.save_path, "ctvd_tmp")
        decrypt_path = os.path.join(path, "ts_decrypt")
        try:
            # 获取文件列表
            file_list = os.listdir(path)
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
            self._logger.error(f"解密过程发生错误: {str(e)}")
            self.finished.emit(False)
            print(e)

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
