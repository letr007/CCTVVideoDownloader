import requests
import os
from typing import List,Dict

class DownloadEngine:

    def __init__(self) -> None:
        self.url = None
        self.save_path = None
        self.file_name = None

    def transfer(self, url:List, save_path, ) -> None:
        '''传递下载参数'''
        self.url = url
        self.save_path = save_path
        self.file_path = os.path.join(self.save_path, self.file_name)
