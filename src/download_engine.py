import requests
import os
from typing import List
import concurrent.futures

from PyQt5.QtCore import QThread, pyqtSignal, QObject

class DownloadWorker(QThread):
    download_info = pyqtSignal(list)

    def __init__(self, urls: List[str], save_path: str, name: str, thread_num: int):
        super().__init__()
        # 设置参数
        self.urls = urls # 下载链接列表
        self.save_path = save_path # 保存路径
        self.file_name = name # 视频名称
        self.thread_num = thread_num # 线程数量
        self._is_running = True # 线程运行状态控制参数

    def run(self) -> None:
        '''开始并行下载'''
        # 使用线程池执行下载任务
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.thread_num) as executor:
            futures = {executor.submit(self.download_core, url, self.urls.index(url)): url for url in self.urls} # 创建任务字典
            for future in concurrent.futures.as_completed(futures): # 遍历任务字典
                if not self._is_running:
                    break
                url = futures[future]
                try: # 处理下载结果
                    data = future.result() # 获取下载结果
                    self.download_info.emit(data) # 发送下载进度信息
                except Exception as exc:
                    print(f'{url} 产生了一个错误: {exc}')

    def download_core(self, url, num) -> List[str, int, str, int]:
        '''核心下载方法'''
        # 设置下载参数
        chunk_size = 1024
        download_size = 0

        if not self._is_running:
            return [num, 0, url, 0]  # 立即返回，表示下载已终止

        tmp_path = os.path.join(self.save_path, "ctvd_tmp") # 临时文件路径
        os.makedirs(tmp_path, exist_ok=True) # 创建临时文件夹
        response = requests.get(url, stream=True) # 发送请求
        try:
            content_size = int(response.headers['content-length']) # 获取文件大小
            if response.status_code == 200:
                file_path = os.path.join(tmp_path, f"{num}.ts") # 拼接文件路径
                with open(file_path, 'wb') as file:
                    # 循环下载数据
                    for data in response.iter_content(chunk_size=chunk_size):
                        if not self._is_running:
                            return [num, 0, url, 0]  # 立即返回，表示下载已终止

                        file.write(data) # 写入文件
                        download_size += len(data) # 更新下载大小
                        progress_percent = (download_size / content_size) * 100 # 计算下载进度
                        self.download_info.emit([num, 1, url, progress_percent]) # 发送下载进度信息
        except Exception as e:
            print(e)
        return [num, 0, url, 100]

    def stop(self):
        '''终止线程方法'''
        self._is_running = False

class DownloadEngine(QObject):
    # 设置信号
    download_info = pyqtSignal(list) # [num, is_downloading, url, progress_percent]

    def __init__(self) -> None:
        super().__init__()
        self.worker = None

    def transfer(self, name: str, urls: List[str], save_path: str, thread_num: int) -> None:
        """
        name: 下载视频名称
        urls: 下载视频链接
        save_path: 保存路径
        thread_num: 下载线程数量
        """
        self.worker = DownloadWorker(urls, save_path, name, thread_num)
        self.worker.download_info.connect(self._callback)

    def start(self) -> None:
        if self.worker:
            self.worker.start()

    def _callback(self, info: list) -> None:
        """回调方法"""
        # 将信息传往主线程
        self.download_info.emit(info)

    def quit(self) -> None:
        if self.worker:
            self.worker.stop()
            self.worker.wait()  # 等待线程安全退出
            self.worker = None
