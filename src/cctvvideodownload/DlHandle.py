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
    
class ThreadHandle(QThread):

    # dv = Signal(int)
    def __init__(self, urls:list) -> None:
        super().__init__(parent=None)
        self.urls = urls

    def run(self) -> None:
        import os
        Run = True
        while Run:
            # n = 0
            # print(n)
            path = "C:\\"
            if not os.path.exists("%s/xwzk_tmp"%path):
                os.makedirs("%s/xwzk_tmp"%path)
            # if 
            if len(self.urls) != 0:
                t1 = ThreadHand_1(self.urls[0],"")
                t1.start()
                del self.urls[0]
            if len(self.urls) != 0:
                t2 = ThreadHand_2(self.urls[0],"")
                t2.start()
                del self.urls[0]
            if len(self.urls) != 0:
                t3 = ThreadHand_3(self.urls[0],"")
                t3.start()
                del self.urls[0]
            else:
                Run = False




        
        # Run = True
        # while Run:
        #     n = 0
        #     # print(n)
        #     path = "C:\\"
        #     if not os.path.exists("%s/xwzk_tmp"%path):
        #         os.makedirs("%s/xwzk_tmp"%path)
        #     for i in self.urls[:2]:
        #         n = int(n) + 1
        #         if n <= 9:
        #             n = "0" + str(n)
        #             tup = (i,n)
        #             print(tup)
        #             t = threading.Thread(target=self.download, args=tup)
                    
        #         else:
        #             tup = (i,n)
        #             t = threading.Thread(target=self.download, args=tup)
        #         t.start()

        #     Run = False

    
    
    def download(self, url:str, num:str) -> int:
        import os,requests
        response = requests.get(url, stream=True)
        chunk_size = 1024
        size = 0
        
        content_size = int(response.headers['content-length'])
        path = "C:\\"
        if response.status_code == 200:
            p = path + "xwzk_tmp\\" + num + ".mp4"
            size = content_size/chunk_size/1024
            print(size,content_size)
            with open(p, "wb") as f:
                for data in response.iter_content(chunk_size=chunk_size):
                    f.write(data)
                    size += len(data)
                    print(size)
                    value = size*50 / content_size
                    print(int(value))
                    self.dv.emit(int(value))
                    return 0

class ThreadHand_1(QThread):
    dv_1 = Signal(int)
    def __init__(self, url, num) -> None:
        super().__init__()
        self.url = url
        self.num = num

    def run(self) -> None:
        import os,requests
        Run = True
        while Run:
            response = requests.get(self.url, stream=True)
            chunk_size = 1024*1024
            size = 0
            
            content_size = int(response.headers['content-length'])
            path = "C:\\"
            if response.status_code == 200:
                p = path + "xwzk_tmp/" + self.num + ".mp4"
                size = content_size/chunk_size/1024
                print(size,content_size)
                with open(p, "wb") as f:
                    for data in response.iter_content(chunk_size=chunk_size):
                        f.write(data)
                        size += len(data)
                        # print(size)
                        value = size*100 / content_size
                        # print(int(value))
                        self.dv_1.emit(int(value))
                Run = False
        return super().run()
    
class ThreadHand_2(QThread):
    dv_2 = Signal(int)
    def __init__(self, url, num) -> None:
        super().__init__()
        self.url = url
        self.num = num

    def run(self) -> None:
        Run = True
        while Run:
            import os,requests
            response = requests.get(self.url, stream=True)
            chunk_size = 1024*1024
            size = 0
            
            content_size = int(response.headers['content-length'])
            path = "C:\\"
            if response.status_code == 200:
                p = path + "xwzk_tmp/" + self.num + ".mp4"
                size = content_size/chunk_size/1024
                print(size,content_size)
                with open(p, "wb") as f:
                    for data in response.iter_content(chunk_size=chunk_size):
                        f.write(data)
                        size += len(data)
                        # print(size)
                        value = size*100 / content_size
                        # print(int(value))
                        self.dv_2.emit(int(value))
                Run = False
                    
        return super().run()

class ThreadHand_3(QThread):
    dv_3 = Signal(int)
    def __init__(self, url, num) -> None:
        super().__init__()
        self.url = url
        self.num = num

    def run(self) -> None:
        Run = True
        while Run:
            import os,requests
            response = requests.get(self.url, stream=True)
            chunk_size = 1024*1024
            size = 0
            
            content_size = int(response.headers['content-length'])
            path = "C:\\"
            if response.status_code == 200:
                p = path + "xwzk_tmp/" + self.num + ".mp4"
                size = content_size/chunk_size/1024
                print(size,content_size)
                with open(p, "wb") as f:
                    for data in response.iter_content(chunk_size=chunk_size):
                        f.write(data)
                        size += len(data)
                        # print(size)
                        value = size*100 / content_size
                        # print(int(value))
                        self.dv_3.emit(int(value))
                Run = False
        return super().run()

    
# if __name__ == '__main__':
#     vd = VideoDownload()
#     vd.GetVideoList("TOPC1451558687534149")