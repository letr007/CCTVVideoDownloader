import requests


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
        # import json
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
        # print(VideoInfo)
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
    
    def GetM3U8Urls(self, VideoInfo:list) -> None:
        '''
        '''
        # import json
        # print(json.dumps(VideoInfo, indent=4))
        hls_url = str(VideoInfo["hls_url"])
        # 获取main.m3u8
        main_m3u8 = requests.get(hls_url)
        # print(main_m3u8.status_code)
        main_m3u8_txt = main_m3u8.text
        # print(main_m3u8_txt)
        # 切分
        main_m3u8_list = main_m3u8_txt.split("\n")
        # print(main_m3u8_list)
        HD_m3u8_url = main_m3u8_list[-2]
        hls_head = hls_url.split("/")[2] # eg:hls.cntv.myhwcdn.cn
        HD_m3u8_url = "https://" + hls_head + HD_m3u8_url
        # print(HD_m3u8_url)
        # 获取1200.m3u8，即


