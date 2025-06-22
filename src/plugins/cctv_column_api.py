import re
import requests
from typing import Dict, List, Optional
from bs4 import BeautifulSoup

from logger import logger
from plugins import APIPlugin

class CCTVColumnAPIPlugin(APIPlugin):
    """
    CCTV栏目API插件实现
    使用栏目ID获取视频列表
    """
    
    @property
    def name(self) -> str:
        return "CCTV_Column"
    
    @property
    def description(self) -> str:
        return "央视视频栏目API插件，使用栏目ID获取视频信息和下载链接"
    
    @property
    def version(self) -> str:
        return "1.0.0"
    
    @property
    def author(self) -> str:
        return "CCTVVideoDownloader Author"
    
    def __init__(self):
        self._COLUMN_INFO = None
        self._logger = logger
    
    def get_video_list(self, ids:tuple, start_index: int = 0, end_index: int = 99) -> Dict[int, List[str]]:
        """
        获取视频列表的指定区间数据
        :param ids: 栏目ID元组 (column_id, item_id)
        :param start_index: 起始索引(包含)
        :param end_index: 结束索引(包含)
        :return: 字典，键为从0开始的索引，值为视频信息列表 [guid, time, title, image, brief]
        """
        # 参数校验
        if start_index > end_index:
            start_index, end_index = end_index, start_index  # 自动交换
        
        # 计算需要获取的页数范围
        page_size = 100  # API限制每页最多100条
        start_page = start_index // page_size + 1
        end_page = end_index // page_size + 1
        
        # 定义列表
        list_information = []
        
        # 遍历指定页数范围
        for page in range(start_page, end_page + 1):
            # 计算当前页的起始和结束索引
            page_start_index = (page - 1) * page_size
            
            # 计算当前页需要获取的数量(始终取整页)
            current_page_size = page_size
            
            # 构建API URL
            api_url = f"https://api.cntv.cn/NewVideo/getVideoListByColumn?id={ids[0]}&n={current_page_size}&sort=desc&p={page}&mode=0&serviceId=tvcctv"
            
            try:
                response = requests.get(api_url, timeout=10)
                response.raise_for_status()  # 检查响应状态
                
                # json格式解析
                resp_format = response.json()
                list_details = resp_format["data"]["list"]
                
                # 处理当前页的数据，只保留在目标区间内的
                for i, item in enumerate(list_details):
                    current_index = page_start_index + i
                    if start_index <= current_index <= end_index:
                        guid, time, title, image, brief = item["guid"], item["time"], item["title"], item["image"], item["brief"]    
                        list_tmp = [guid, time, title, image, brief]
                        list_information.append(list_tmp)
                        
            except requests.exceptions.RequestException as e:
                self._logger.error(f"获取第 {page} 页数据失败: {str(e)}")
                return {}  # 返回空字典表示获取失败
            except (KeyError, ValueError) as e:
                self._logger.error(f"解析第 {page} 页数据失败: {str(e)}")
                return {}  # 返回空字典表示获取失败
        
        # 列表转字典，键从0开始
        dict_information = {i: item for i, item in enumerate(list_information)}
        self._COLUMN_INFO = dict_information
        return self._COLUMN_INFO
    
    def get_column_info(self, index: int) -> Dict[str, str]:
        """
        获取栏目信息
        :param index: 视频索引
        :return: 栏目信息字典，包含time, title, brief, image等键
        """
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
        return {}
    
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
        result = re.sub(r'\n+', '\n', replaced)

        return result
    
    def get_encrypt_m3u8_urls(self, guid:str, quality:str="0") -> List[str]:
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

    def get_play_column_info(self, url:str) -> Optional[List[str]]:
        '''
        从视频播放页链接获取栏目标题和ID
        :param url: 视频播放页链接
        :return: 栏目信息列表 [标题, 栏目ID, 视频ID]，如果获取失败则返回None
        '''
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
        # 取第一个script标签
        script = str(script_tags[0])
        # 匹配标题和ID
        match_title = re.search(r'var commentTitle\s*=\s*["\'](.*?)["\'];', script)
        match_item_id = re.search(r'var itemid1\s*=\s*["\'](.*?)["\'];', script)
        match_column_id = re.search(r'var column_id\s*=\s*["\'](.*?)["\'];', script)

        if match_title and match_column_id and match_item_id:
            # 对标题处理
            match_title = match_title.group(1).split(" ")[0]

            column_value = [match_title, match_column_id.group(1), match_item_id.group(1)]
            self._logger.info(f"获取栏目标题: {match_title}, 栏目ID: {match_column_id.group(1)}, 视频ID: {match_item_id.group(1)}")
            return column_value
        else:
            return None
