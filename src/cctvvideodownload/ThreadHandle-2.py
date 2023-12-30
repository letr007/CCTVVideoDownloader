from typing import Optional
import PySide6.QtCore
import requests,os
from PySide6 import QtCore
from PySide6.QtCore import QObject
from PySide6.QtCore import Signal,QRunnable,QThreadPool,QThread

# 线程核心代码
class DownloadWorker(QRunnable):

    # log_signal = Signal(int)
    communication = None
    def __init__(self) -> None:
        super(DownloadWorker, self).__init__()
        self.url = None
        self.thread_logo = None
        path = "C:\\"
        if not os.path.exists("%s/ctvd_tmp"%path):
            os.makedirs("%s/ctvd_tmp"%path)

    # 用来初始化变量的函数
    def transfer(self,url, thread_logo, communication) -> None:
        self.thread_logo = thread_logo
        self.communication = communication
        self.url = url
    
    # 主函数
    def run(self) -> None:
        import os,requests
        Run = True
        while Run:
            response = requests.get(self.url, stream=False)
            chunk_size = 1024*1024
            size = 0
            
            content_size = int(response.headers['content-length'])
            path = "C:\\"
            if response.status_code == 200:
                p = path + "ctvd_tmp/" + self.thread_logo + ".mp4"
                size = content_size/chunk_size/1024
                (size,content_size)
                with open(p, "wb") as f:
                    # f.write(response.content)
                    for data in response.iter_content(chunk_size=chunk_size):
                        f.write(data)
                        size += len(data)
                        # print(size)
                        value = size*100 / content_size
                        print(int(value))
                        
                        
                        
                Run = False

class Tasks(QObject):
    communication = None
    def __init__(self, communication, urls) -> None:
        super(Tasks, self).__init__()
        self.communication = communication
        self.urls = urls
        self.pool = QThreadPool()
        self.pool.globalInstance()
        
    def start(self):
        self.pool.setMaxThreadCount(3)
        num = 0
        for i in self.urls:
            num += 1
            worker = DownloadWorker()
            worker.transfer(i, str(num), self.communication)
            worker.setAutoDelete(True)
            self.pool.start(worker)
        self.pool.waitForDone()


class ThreadHandle(QThread):
    log_signal = Signal(int)
    def __init__(self, urls, communication) -> None:
        super(ThreadHandle, self).__init__()
        self.task = Tasks(
            communication = communication,
            urls= urls
        )
        

    def run(self) -> None:
        self.task.start()

