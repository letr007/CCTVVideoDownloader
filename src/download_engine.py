import requests
import os
from typing import List,Dict

from PyQt5.QtCore import QThread, pyqtSignal

class DownloadEngine:
    
    # 创建信号

    def __init__(self) -> None:
        self.url = None
        self.save_path = None
        self.file_name = None

    def transfer(self, name: str, url: List, save_path: str, thread_num: 1) -> None:
        '''
        传递下载参数
        name: str 视频名称
        url: List[str] 视频下载链接
        save_path: str 视频保存路径
        thread_num: int 线程数
        '''
        self.url = url
        self.save_path = save_path
        self.file_name = name
        self.file_path = os.path.join(self.save_path, self.file_name)
        self.thread_num = thread_num

    def start(self) -> None:
        '''开始下载'''
        pass

    def _callback(self) -> None:
        '''回调函数，接收下载线程返回的信号'''
        pass

class DownloadCore(QThread):
    # 创建信号
    info = pyqtSignal(list)
    # []

    def __init__(self):
        super(DownloadCore, self).__init__()
        # 初始化一些参数
        self.STATE = True

    def run(self) -> None:
        '''下载核心'''
        while self.STATE:
            pass
        
        

    
