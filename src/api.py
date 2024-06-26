import requests
from typing import Dict,List
from bs4 import BeautifulSoup

class CCTVVideoDownloadAPI:
    def __init__(self):
        self._COLUMN_INFO = None

    def get_video_list(self, id:str) -> Dict[str, List[str]]:
        api_url = f"https://api.cntv.cn/NewVideo/getVideoListByColumn?id={id}&n=20&sort=desc&p=1&mode=0&serviceId=tvcctv"
        response = requests.get(api_url, timeout=10)
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
        self._COLUMN_INFO = dict_information
        return self._COLUMN_INFO
    
    def get_column_info(self, index: int) -> Dict[str, str]:
        if self._COLUMN_INFO != None:
            video_info = self._COLUMN_INFO[index]
            time = video_info[1]
            title = video_info[2]
            brief = self.brief_formating(video_info[4])
            # 获取图片
            try:
                response = requests.get(video_info[3])
                if response.status_code == 200:
                    image = response.content
                else:
                    image = None
            except Exception:
                image = None
            column_info = {
                "time": time,
                "title": title,
                "brief": brief,
                "image": image
                
            }
            return column_info
        
    def brief_formating(self, s:str) -> str:
        '''格式化介绍信息'''
        # 首先替换所有空格和\r为换行符
        replaced = s.replace(' ', '\n')
        replaced = replaced.replace('\r', '\n')
        
        # 消除连续的换行符
        import re
        result = re.sub(r'\n+', '\n', replaced)

        # string = ""
        # for i in range(0, len(result), 13):
        #     string += result[i:i+13] + '\n'

        return result
    

    
    #  已弃用
    def _get_http_video_info(self, guid:str) -> Dict:
        api_url = f"https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do?pid={guid}"
        response = requests.get(api_url, timeout=10)
        # json格式解析
        resp_format = response.json()
        return resp_format
    
    def get_m3u8_urls_450(self, guid:str) -> List:
        api_url = f"https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do?pid={guid}"
        response = requests.get(api_url, timeout=10)
        resp_format = response.json()
        hls_enc2_url = resp_format["hls_url"]
        # 获取main.m3u8
        main_m3u8 = requests.get(hls_enc2_url, timeout=5)
        main_m3u8_txt = main_m3u8.text
        # 切分
        main_m3u8_list = main_m3u8_txt.split("\n")
        HD_m3u8_url = main_m3u8_list[-2]
        hls_head = hls_enc2_url.split("/")[2] # eg:dhls2.cntv.qcloudcdn.com
        HD_m3u8_url = "https://" + hls_head + HD_m3u8_url
        # 获取2000.m3u8，即高清m3u8文件，内含ts
        video_m3u8 = requests.get(HD_m3u8_url)
        # 提取ts列表
        video_m3u8_list = video_m3u8.text.split("\n")
        video_list = []
        import re
        for i in video_m3u8_list:
            if re.match(r"\d+.ts", i):
                video_list.append(i)
        # 转化为urls列表
        dl_url_head = HD_m3u8_url[:-8]
        urls = []
        for i in video_list:
            tmp = dl_url_head + i
            urls.append(tmp)
        # print(urls)
        return urls
    
    def get_play_column_info(self, url:str) -> List:
        '''从视频播放页链接获取栏目标题和ID'''
        try:
            response = requests.get(url, timeout=5)
        except Exception:
            return None
        # 检测网页的编码，并重新编码为 utf-8
        response.encoding = response.apparent_encoding
        # 使用BeautifulSoup解析
        soup = BeautifulSoup(response.text, "html.parser")
        # 查找script
        script_tags = soup.find_all("script")
        import re
        # 取第一个script标签
        script = str(script_tags[0])
        # 匹配标题和ID
        match_title = re.search(r'var commentTitle\s*=\s*["\'](.*?)["\'];', script)
        match_id = re.search(r'var column_id\s*=\s*["\'](.*?)["\'];', script)

        if match_title and match_id:
            # 对标题处理
            match_title = match_title.group(1).split(" ")[0]

            column_value = [match_title, match_id.group(1)]
            return column_value
        else:
            return None

        
        
    
            
if __name__ == "__main__":
    api = CCTVVideoDownloadAPI()
    import json
    list1 = api.get_video_list("TOPC1451464665008914")
    print(list1)
    # list2 = api._get_http_video_info("8665a11a622e5601e64663a77355af15")
    # print(json.dumps(list2, indent=4))
    list3 = api.get_m3u8_urls_450("a5324e8cdda44d72bd569d1dba2e4988")
    # print(list3)
    # tmp = api.get_column_info(0)
    # print(tmp)
    # print(api.get_play_column_info("https://tv.cctv.com/2024/06/21/VIDEs2DfNN70XHJ1OySUipyV240621.shtml?spm=C31267.PXDaChrrDGdt.EbD5Beq0unIQ.3"))



