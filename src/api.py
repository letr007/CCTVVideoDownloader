"""
API模块，提供API插件管理和兼容性支持
"""
import os
import importlib
import pkgutil
from typing import Dict, List, Optional, Any

from logger import logger
from plugins import APIManager, APIPlugin
from plugins.cctv_column_api import CCTVColumnAPIPlugin
from plugins.cctv_album_api import CCTVAlbumAPIPlugin

class CCTVVideoDownloaderAPI:
    """
    为了向后兼容而保留的类
    """
    def __init__(self):
        self._api = API()
    
    def __getattr__(self, name):
        return getattr(self._api, name)

class API:
    """
    API管理类，用于加载和管理API插件
    """
    
    def __init__(self):
        self._logger = logger
        self._api_manager = APIManager()
        
        # 注册内置插件
        self._register_builtin_plugins()
        
        # 加载外部插件
        self._load_plugins()
        
        # 获取当前活动的插件
        self._active_plugin = self._api_manager.get_active_plugin()
        if self._active_plugin:
            self._logger.info(f"使用API插件: {self._active_plugin.name} v{self._active_plugin.version}")
        else:
            self._logger.error("没有可用的API插件")
    
    def _register_builtin_plugins(self):
        """注册内置插件"""
        # 注册CCTV栏目API插件
        column_api = CCTVColumnAPIPlugin()
        self._api_manager.register_plugin(column_api)
        self._logger.info(f"已注册内置插件: {column_api.name}")
        
        # 注册CCTV专辑API插件
        album_api = CCTVAlbumAPIPlugin()
        self._api_manager.register_plugin(album_api)
        self._logger.info(f"已注册内置插件: {album_api.name}")
    
    def _load_plugins(self):
        """加载外部插件"""
        # 插件目录
        plugin_dir = os.path.join(os.path.dirname(__file__), "plugins")
        
        # 确保插件目录存在
        if not os.path.exists(plugin_dir):
            self._logger.warning(f"插件目录不存在: {plugin_dir}")
            return
        
        # 遍历插件目录
        for _, name, is_pkg in pkgutil.iter_modules([plugin_dir]):
            # 跳过内置插件和非包
            if name == "cctv_api" or not is_pkg:
                continue
            
            try:
                # 导入插件模块
                module = importlib.import_module(f"plugins.{name}")
                
                # 查找插件类
                for attr_name in dir(module):
                    attr = getattr(module, attr_name)
                    # 检查是否是APIPlugin的子类且不是APIPlugin本身
                    if isinstance(attr, type) and issubclass(attr, APIPlugin) and attr != APIPlugin:
                        # 实例化插件并注册
                        plugin = attr()
                        self._api_manager.register_plugin(plugin)
                        self._logger.info(f"已加载插件: {plugin.name} v{plugin.version}")
            except Exception as e:
                self._logger.error(f"加载插件 {name} 失败: {str(e)}")
    
    def get_video_list(self, ids:tuple, start_index: int = 0, end_index: int = 99) -> Dict[int, List[str]]:
        """
        获取视频列表的指定区间数据
        :param ids: 栏目ID元组 (column_id, item_id)
        :param start_index: 起始索引(包含)
        :param end_index: 结束索引(包含)
        :return: 字典，键为从0开始的索引，值为视频信息列表 [guid, time, title, image, brief]
        """
        # 首先尝试使用当前活动的插件
        if self._active_plugin:
            result = self._active_plugin.get_video_list(ids, start_index, end_index)
            if result:  # 如果获取成功，直接返回结果
                return result
            
            # 如果当前插件获取失败，尝试其他插件
            self._logger.warning(f"使用插件 {self._active_plugin.name} 获取视频列表失败，尝试其他插件")
            
            # 获取所有可用的插件
            plugins = self._api_manager.get_all_plugins()
            for name, plugin in plugins.items():
                if plugin != self._active_plugin:  # 跳过当前已经尝试过的插件
                    self._logger.info(f"尝试使用插件 {name} 获取视频列表")
                    result = plugin.get_video_list(ids, start_index, end_index)
                    if result:  # 如果获取成功，切换到该插件并返回结果
                        self._active_plugin = plugin
                        self._logger.info(f"成功切换到插件 {name}")
                        return result
            
            # 所有插件都尝试失败
            self._logger.error("所有API插件都无法获取视频列表")
            return {}
        else:
            self._logger.error("没有可用的API插件")
            return {}
    
    def get_column_info(self, index: int) -> Dict[str, str]:
        """
        获取栏目信息
        :param index: 视频索引
        :return: 栏目信息字典，包含time, title, brief, image等键
        """
        if self._active_plugin:
            return self._active_plugin.get_column_info(index)
        else:
            self._logger.error("没有可用的API插件")
            return {}
    
    def get_encrypt_m3u8_urls(self, guid: str, quality: str = "0") -> List[str]:
        """
        获取加密m3u8的urls
        :param guid: 视频ID
        :param quality: 视频质量，"0"（最高清晰度）、"1"（超清）、"2"（高清）、"3"（标清）、"4"（流畅）
        :return: 加密m3u8的urls列表
        """
        if self._active_plugin:
            return self._active_plugin.get_encrypt_m3u8_urls(guid, quality)
        else:
            self._logger.error("没有可用的API插件")
            return []
    
    def get_play_column_info(self, url: str) -> Optional[List[str]]:
        """
        从视频播放页链接获取栏目标题和ID
        :param url: 视频播放页链接
        :return: 栏目信息列表 [标题, 栏目ID, 视频ID]，如果获取失败则返回None
        """
        if self._active_plugin:
            return self._active_plugin.get_play_column_info(url)
        else:
            self._logger.error("没有可用的API插件")
            return None
    
    def brief_formating(self, s: str) -> str:
        """
        格式化介绍信息
        :param s: 介绍信息
        :return: 格式化后的介绍信息
        """
        if self._active_plugin:
            return self._active_plugin.brief_formating(s)
        else:
            self._logger.error("没有可用的API插件")
            return s
    
    def set_api_plugin(self, name: str) -> bool:
        """
        设置当前使用的API插件
        :param name: 插件名称
        :return: 是否设置成功
        """
        result = self._api_manager.set_active_plugin(name)
        if result:
            self._active_plugin = self._api_manager.get_active_plugin()
            self._logger.info(f"切换到API插件: {self._active_plugin.name} v{self._active_plugin.version}")
        else:
            self._logger.error(f"API插件 {name} 不存在")
        return result
    
    def get_available_plugins(self) -> Dict[str, Dict[str, str]]:
        """
        获取所有可用的API插件信息
        :return: 插件信息字典，键为插件名称，值为包含name, description, version, author的字典
        """
        plugins = self._api_manager.get_all_plugins()
        result = {}
        for name, plugin in plugins.items():
            result[name] = {
                "name": plugin.name,
                "description": plugin.description,
                "version": plugin.version,
                "author": plugin.author
            }
        return result
    
    def get_current_plugin_name(self) -> Optional[str]:
        """
        获取当前使用的API插件名称
        :return: 插件名称，如果没有则返回None
        """
        if self._active_plugin:
            return self._active_plugin.name
        return None


if __name__ == "__main__":
    api = API()
    print(f"当前使用的API插件: {api.get_current_plugin_name()}")
    print(f"可用的API插件: {api.get_available_plugins()}")
    
    # 测试API功能
    list3 = api.get_encrypt_m3u8_urls("a5324e8cdda44d72bd569d1dba2e4988", "2")
    print(list3)
