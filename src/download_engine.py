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
    '''下载核心类,继承至QThread,仅负责从链接下载'''

    # 创建信号
    info = pyqtSignal(list) # 
    # []

    def __init__(self, url, path, num):
        super(DownloadCore, self).__init__()
        # 初始化一些参数
        self.STATE = True
        self.url = url
        self.tmp_path = f"{path}/ctvd_tmp" # 下载临时文件路径
        self.num = num # 下载文件的标识

    def run(self) -> None:
        '''下载核心'''
        while self.STATE:
            # GET 
            response = requests.get(self.url, stream=True)
            chunk_size = 1024
            self.download_state = 1 # 1:下载中 0:下载完成
            download_size = 0 # 初始化已下载大小
            try:
                # 获取文件大小
                content_size = int(response.headers['content-length'])

                if response.status_code == 200:
                    file_path = f"{self.tmp_path}/{self.num}.ts"
                    with open(file_path, 'wb') as file:
                        for data in response.iter_content(chunk_size=chunk_size):
                            file.write(data) # 写入数据
                            download_size += len(data) # 累加已下载数据量
                            progress_percent = (download_size / content_size) * 100 # 计算百分比

                            print(f"Download Progress: {progress_percent:.2f}%", end="\r")  # 使用'\r'回到行首，实现动态更新

                            self.info.emit([self.num, self.download_state, self.url, progress_percent])

            except Exception as e:
                print(e)

            finally:
                self.STATE = False
                self.download_state = 0
                self.info.emit([self.num, self.download_state, self.url, progress_percent])





                


        
        

    
