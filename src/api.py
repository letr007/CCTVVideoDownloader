import requests
from typing import Dict,List
from bs4 import BeautifulSoup
from logger import logger

class CCTVVideoDownloaderAPI:
    def __init__(self):
        self._COLUMN_INFO = None
        self._logger = logger

    def get_video_list(self, id:str, num:int=100) -> Dict[str, List[str]]:
        """
        获取视频列表
        :param id: 栏目ID
        :param num: 需要获取的视频数量，当大于100时会自动分页获取
        :return: 字典，键为索引，值为视频信息列表 [guid, time, title, image, brief]
        """
        # 计算需要获取的页数
        page_size = 100  # API限制每页最多100条
        total_pages = (num + page_size - 1) // page_size  # 向上取整
        
        # 定义列表
        list_information = []
        list_index = []
        current_index = 0
        
        # 遍历所有页
        for page in range(1, total_pages + 1):
            # 计算当前页需要获取的数量
            current_page_size = min(page_size, num - (page - 1) * page_size)
            
            # 构建API URL
            api_url = f"https://api.cntv.cn/NewVideo/getVideoListByColumn?id={id}&n={current_page_size}&sort=desc&p={page}&mode=0&serviceId=tvcctv"
            
            try:
                response = requests.get(api_url, timeout=10)
                response.raise_for_status()  # 检查响应状态
                
                # json格式解析
                resp_format = response.json()
                list_detials = resp_format["data"]["list"]
                
                # 处理当前页的数据
                for item in list_detials:
                    guid, time, title, image, brief = item["guid"], item["time"], item["title"], item["image"], item["brief"]    
                    list_tmp = [guid, time, title, image, brief]
                    list_information.append(list_tmp)
                    list_index.append(current_index)
                    current_index += 1
                    
            except requests.exceptions.RequestException as e:
                self._logger.error(f"获取第 {page} 页数据失败: {str(e)}")
                continue
            except (KeyError, ValueError) as e:
                self._logger.error(f"解析第 {page} 页数据失败: {str(e)}")
                continue
        
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
        """
        格式化介绍信息
        :param s: 介绍信息
        :return: 格式化后的介绍信息
        """
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
    
    # 已弃用
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
    
    def get_encrypt_m3u8_urls(self, guid:str, quality:str="0") -> List:
        """
        获取加密m3u8的urls
        :param guid: 视频ID
        :param quality: 视频质量，"0"（最高清晰度）、"1"（超清）、"2"（高清）、"3"（标清）、"4"（流畅）
        :return: 加密m3u8的urls列表
        """
        api_url = f"https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do?pid={guid}"
        response = requests.get(api_url, timeout=10)
        resp_format = response.json()
        hls_h5e_url = resp_format["manifest"]["hls_h5e_url"]
        
        # 获取m3u8
        main_m3u8 = requests.get(hls_h5e_url)
        if main_m3u8.status_code != 200:
            raise ValueError(f"获取视频ts列表失败，状态码为{main_m3u8.status_code}")
            
        main_m3u8_txt = main_m3u8.text
        # 切分
        main_m3u8_list = main_m3u8_txt.split("\n")
        
        # 解析m3u8文件，获取不同清晰度的URL
        quality_map = {
            "4": {"bandwidth": 460800, "resolution": "480x270"},
            "3": {"bandwidth": 870400, "resolution": "640x360"},
            "2": {"bandwidth": 1228800, "resolution": "1280x720"},
            "1": {"bandwidth": 2048000, "resolution": "1280x720"}
        }
        
        # 存储所有清晰度的URL
        quality_urls = {}
        current_quality = None
        
        for line in main_m3u8_list:
            line = line.strip()
            if not line:
                continue
                
            # 检查是否是清晰度信息行
            if line.startswith("#EXT-X-STREAM-INF"):
                # 提取带宽信息
                bandwidth = int(line.split("BANDWIDTH=")[1].split(",")[0])
                # 查找对应的清晰度
                for q, info in quality_map.items():
                    if info["bandwidth"] == bandwidth:
                        current_quality = q
                        break
            # 如果是URL行且已找到对应的清晰度
            elif current_quality and not line.startswith("#"):
                quality_urls[current_quality] = line
                current_quality = None
        
        # 选择对应的清晰度URL
        if quality == "0":
            # 选择最高清晰度
            selected_quality = max(quality_urls.keys(), key=lambda x: int(x))
        else:
            if quality not in quality_urls:
                raise ValueError(f"不支持的清晰度: {quality}，支持的清晰度有: {', '.join(quality_urls.keys())}")
            selected_quality = quality
            
        # 构建完整的m3u8 URL
        h5e_head = hls_h5e_url.split("/")[2]
        m3u8_url:str = "https://" + h5e_head + quality_urls[selected_quality]
        
        self._logger.info(f"选择清晰度: {selected_quality}, URL: {m3u8_url}")
        
        # 获取对应清晰度的m3u8文件
        video_m3u8 = requests.get(m3u8_url)
        if video_m3u8.status_code != 200:
            raise ValueError(f"获取视频ts列表失败，状态码为{video_m3u8.status_code}")
            
        # 提取ts列表
        video_m3u8_list = video_m3u8.text.splitlines()
        video_list = [i for i in video_m3u8_list if i.endswith('.ts')]
                
        # 转化为urls列表
        dl_url_head = "/".join(m3u8_url.split("/")[:-1])+"/"  # 移除最后的.m3u8
        urls = [dl_url_head + i for i in video_list]
            
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
    api = CCTVVideoDownloaderAPI()
    import json
    # list1 = api.get_video_list("TOPC1451464665008914",20)
    # print(list1)
    # list2 = api._get_http_video_info("8665a11a622e5601e64663a77355af15")
    # print(json.dumps(list2, indent=4))
    # list3 = api.get_m3u8_urls_450("a5324e8cdda44d72bd569d1dba2e4988")
    list3 = api.get_encrypt_m3u8_urls("a5324e8cdda44d72bd569d1dba2e4988", "2")
    print(list3)
    # tmp = api.get_column_info(0)
    # print(tmp)
    # print(api.get_play_column_info("https://tv.cctv.com/2024/06/21/VIDEs2DfNN70XHJ1OySUipyV240621.shtml?spm=C31267.PXDaChrrDGdt.EbD5Beq0unIQ.3"))
# string = "/asp/h5e/hls/2000/0303000a/3/default/a5324e8cdda44d72bd569d1dba2e4988/2000.m3u8"
# a = "/".join(string.split("/")[:-1])
# print(a)


