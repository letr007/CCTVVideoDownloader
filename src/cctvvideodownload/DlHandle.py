from typing import Optional
import PySide6.QtCore
import requests
from PySide6 import QtCore
from PySide6.QtCore import Signal,QThread


class VideoDownload():
    def __init__(self) -> None:
        pass

    def GetVideoList(self, id:str) -> dict:
        '''
        调用getVideoListByColumn?API获取节目列表信息
        并对返回数据处理
        :return:
        '''
        api = "https://api.cntv.cn/NewVideo/getVideoListByColumn?id=%s&n=20&sort=desc&p=1&mode=0&serviceId=tvcctv"%id
        resp = requests.get(api)
        # print(resp.status_code)
        # print(json.dumps(resp.json(), indent=4))
        # 对返回的数据进行处理
        recv = resp.json()
        v_list = recv["data"]["list"]
        l_info = []
        l_index = []
        num = 0
        for i in v_list:
            title = i["title"]
            guid = i["guid"]
            l_1 = [title, guid]
            l_info.append(l_1)
            num += 1
            l_index.append(num)
        dict1 = dict(zip(l_index, l_info))
        # print(dict1)
        return dict1
    
    def GetHttpVideoInfo(self, pid) -> list:
        '''
        调取getHttpVideoInfo?doAPI获取视频源链接
        :param pid:
        :return VideoInfo:
        '''
        ghv_url = "https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do?" + "pid=" + pid
        ghv_resp = requests.get(ghv_url)
        VideoInfo = ghv_resp.json()
        return VideoInfo

    def GetDownloadUrls(self, VideoInfo:list) -> list:
        '''
        获取视频源下载链接
        返回一个包含链接的列表
        :param VideoInfo:
        :return urls:
        '''
        chapters = VideoInfo["video"]["chapters4"]
        # print(json.dumps(chapters, indent=4))
        urls = []
        for i in chapters:
            url = i["url"]
            urls.append(url)
        # print(urls)
        return urls
    