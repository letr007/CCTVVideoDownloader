import requests
from typing import Dict,List

class CCTVVideoDownloadAPI:
    def __init__(self):
        pass

    def get_video_list(self, id:str) -> Dict[str, List[str]]:
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
    
    
            
        


