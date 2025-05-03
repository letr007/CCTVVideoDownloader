import requests
import os
from typing import List
import concurrent.futures
from logger import logger
import time
import hashlib

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
        self._logger = logger # 日志记录器
        
        # 重试配置
        self.max_retries = 3  # 最大重试次数
        self.retry_delay = 2  # 重试延迟（秒）
        self.timeout = 30     # 请求超时时间（秒）
        
        self._logger.info(f"初始化下载任务 - 视频名称: {name}, 保存路径: {save_path}, 线程数: {thread_num}")
        self._logger.debug(f"下载链接列表: {urls}")

    def run(self) -> None:
        '''开始并行下载'''
        self._logger.info(f"开始下载任务 - {self.file_name}")
        self._logger.debug(f"使用线程池执行下载任务，最大线程数: {self.thread_num}")

        hash_name = hashlib.md5(self.file_name.encode()).hexdigest()
        tmp_path = os.path.join(self.save_path, hash_name)
        os.makedirs(tmp_path, exist_ok=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.thread_num) as executor:
            futures = {executor.submit(self.download_core, url, self.urls.index(url), tmp_path): url for url in self.urls}
            for future in concurrent.futures.as_completed(futures):
                if not self._is_running:
                    self._logger.warning("下载任务被用户终止")
                    break
                url = futures[future]
                try:
                    data = future.result()
                    self.download_info.emit(data)
                except Exception as exc:
                    self._logger.error(f"下载出错 {url}: {str(exc)}", exc_info=True)

    def download_core(self, url, num, tmp_path) -> List[str]:
        '''核心下载方法'''
        chunk_size = 1024
        download_size = 0
        retry_count = 0

        if not self._is_running:
            self._logger.debug(f"下载任务 {num} 被终止")
            return [num, 0, url, 0]

        # tmp_path = os.path.join(self.save_path, "ctvd_tmp")
        # os.makedirs(tmp_path, exist_ok=True)
        file_path = os.path.join(tmp_path, f"{num}.ts")
        
        while retry_count < self.max_retries:
            try:
                self._logger.debug(f"开始下载文件 {num}.ts - URL: {url} (尝试 {retry_count + 1}/{self.max_retries})")
                
                # 检查文件是否已存在且完整
                if os.path.exists(file_path):
                    file_size = os.path.getsize(file_path)
                    try:
                        response = requests.head(url, timeout=self.timeout)
                        content_size = int(response.headers['content-length'])
                        if file_size == content_size:
                            self._logger.info(f"文件 {num}.ts 已存在且完整，跳过下载")
                            return [num, 0, url, 100]
                    except:
                        pass
                
                response = requests.get(url, stream=True, timeout=self.timeout)
                response.raise_for_status()
                
                content_size = int(response.headers['content-length'])
                self._logger.debug(f"文件 {num}.ts 总大小: {content_size} bytes")
                
                with open(file_path, 'wb') as file:
                    for data in response.iter_content(chunk_size=chunk_size):
                        if not self._is_running:
                            self._logger.debug(f"文件 {num}.ts 下载被终止")
                            return [num, 0, url, 0]

                        file.write(data)
                        download_size += len(data)
                        progress_percent = (download_size / content_size) * 100
                        self.download_info.emit([num, 1, url, progress_percent])
                        
                        if progress_percent % 10 == 0:
                            self._logger.debug(f"文件 {num}.ts 下载进度: {progress_percent:.1f}%")
                
                # 验证文件完整性
                if os.path.getsize(file_path) == content_size:
                    self._logger.info(f"文件 {num}.ts 下载完成")
                    return [num, 0, url, 100]
                else:
                    raise Exception(f"文件大小不匹配: 期望 {content_size} 字节, 实际 {os.path.getsize(file_path)} 字节")
                
            except requests.RequestException as e:
                retry_count += 1
                if retry_count < self.max_retries:
                    self._logger.warning(f"下载请求失败 {num}.ts: {str(e)}，将在 {self.retry_delay} 秒后重试")
                    time.sleep(self.retry_delay)
                    continue
                else:
                    self._logger.error(f"下载请求失败 {num}.ts: {str(e)}，已达到最大重试次数")
                    return [num, 0, url, -1]
            except Exception as e:
                self._logger.error(f"下载过程出错 {num}.ts: {str(e)}", exc_info=True)
                return [num, 0, url, -1]
        
        return [num, 0, url, -1]

    def stop(self):
        '''终止线程方法'''
        self._logger.info("正在终止下载任务...")
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
