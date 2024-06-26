import requests
import os
from typing import List
import concurrent.futures

from PyQt5.QtCore import QThread, pyqtSignal, QObject

class DownloadWorker(QThread):
    download_info = pyqtSignal(list)

    def __init__(self, urls: List[str], save_path: str, name: str, thread_num: int):
        super().__init__()
        self.urls = urls
        self.save_path = save_path
        self.file_name = name
        self.thread_num = thread_num
        self._is_running = True

    def run(self) -> None:
        '''开始并行下载'''
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.thread_num) as executor:
            futures = {executor.submit(self.download_core, url, self.urls.index(url)): url for url in self.urls}
            for future in concurrent.futures.as_completed(futures):
                if not self._is_running:
                    break
                url = futures[future]
                try:
                    data = future.result()
                    self.download_info.emit(data)
                except Exception as exc:
                    print(f'{url} generated an exception: {exc}')

    def download_core(self, url, num):
        '''核心下载函数，返回下载信息'''
        if not self._is_running:
            return [num, 0, url, 0]  # 立即返回，表示下载已终止

        tmp_path = os.path.join(self.save_path, "ctvd_tmp")
        os.makedirs(tmp_path, exist_ok=True)
        response = requests.get(url, stream=True)
        chunk_size = 1024
        download_size = 0
        try:
            content_size = int(response.headers['content-length'])
            if response.status_code == 200:
                file_path = os.path.join(tmp_path, f"{num}.ts")
                with open(file_path, 'wb') as file:
                    for data in response.iter_content(chunk_size=chunk_size):
                        if not self._is_running:
                            return [num, 0, url, 0]  # 立即返回，表示下载已终止

                        file.write(data)
                        download_size += len(data)
                        progress_percent = (download_size / content_size) * 100
                        self.download_info.emit([num, 1, url, progress_percent])
        except Exception as e:
            print(e)
        return [num, 0, url, 100]

    def stop(self):
        '''设置控制变量以终止线程'''
        self._is_running = False

class DownloadEngine(QObject):
    download_info = pyqtSignal(list)

    def __init__(self) -> None:
        super().__init__()
        self.worker = None

    def transfer(self, name: str, urls: List[str], save_path: str, thread_num: int) -> None:
        self.worker = DownloadWorker(urls, save_path, name, thread_num)
        self.worker.download_info.connect(self._callback)

    def start(self) -> None:
        if self.worker:
            self.worker.start()

    def _callback(self, info: list) -> None:
        self.download_info.emit(info)

    def quit(self) -> None:
        if self.worker:
            self.worker.stop()
            self.worker.wait()  # 等待线程安全退出
            self.worker = None
