import requests
from typing import Dict,List

class CCTVVideoDownloadAPI:
    def __init__(self):
        pass

    def _get_video_list(self, id:str) -> Dict[str, List[str]]:
        api_url = f"https://api.cntv.cn/NewVideo/getVideoListByColumn?id={id}&n=20&sort=desc&p=1&mode=0&serviceId=tvcctv"
        response = requests.get(api_url)
        # json格式解析
        resp_format = response.json()
        list_detials = resp_format["data"]["list"]
        # 定义列表
        list_information = []
        list_index = []
        # 索引
        index = 0
        # 遍历
        for i in list_detials:
            guid, time, title, image, brief = i["guid"], i["time"], i["title"], i["image"], i["brief"]    
            list_tmp = [guid, time, title, image, brief]
            list_information.append(list_tmp)
            list_index.append(index)
            index += 1
        # 列表转字典
        dict_information = dict(zip(list_index, list_information))
        return dict_information
    
    def _get_http_video_info(self, guid:str) -> Dict:
        api_url = f"https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do?pid={guid}"
        response = requests.get(api_url)
        # json格式解析
        resp_format = response.json()
        return resp_format
    
    def get_m3u8_urls(self, videoinfo:List) -> List:
        hls_url = str(videoinfo["hls_url"])
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
        # 获取1200.m3u8，即高清m3u8文件，内含ts
        video_m3u8 = requests.get(HD_m3u8_url)
        # print(video_m3u8.status_code)
        # 提取ts列表
        video_m3u8_list = video_m3u8.text.split("\n")
        video_list = []
        for i in video_m3u8_list:
            if re.match(r"\d+.ts", i):
                video_list.append(i)
        # print(video_list)
        # 转化为urls列表
        dl_url_head = HD_m3u8_url[:-9]
        urls = []
        for i in video_list:
            tmp = dl_url_head + i
            urls.append(tmp)
        # print(urls)
        return urls
    
    def get_video_info(self, id:str, index:int) -> Dict:
        video_list = self._get_video_list(id)
        video_info = video_list[index]
        video_guid = video_info[0]
        video_info = self._get_http_video_info(video_guid)
    
            
if __name__ == "__main__":
    api = CCTVVideoDownloadAPI()
    list1 = api.get_video_list("TOPC1451559180488841")
    print(list1)
    list2 = api._get_http_video_info("230e579a10f14ab18ad0ce407964a9cb")
    print(list2)


