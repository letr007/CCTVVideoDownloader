from abc import ABC, abstractmethod
from typing import Dict, List, Optional, Any

class APIPlugin(ABC):
    """
    API插件的抽象基类
    所有API插件必须继承此类并实现所有抽象方法
    """
    
    @property
    @abstractmethod
    def name(self) -> str:
        """插件名称"""
        pass
    
    @property
    @abstractmethod
    def description(self) -> str:
        """插件描述"""
        pass
    
    @property
    @abstractmethod
    def version(self) -> str:
        """插件版本"""
        pass
    
    @property
    @abstractmethod
    def author(self) -> str:
        """插件作者"""
        pass
    
    @abstractmethod
    def get_video_list(self, ids: tuple, start_index: int = 0, end_index: int = 99) -> Dict[int, List[str]]:
        """获取视频列表

        :param ids: 视频ID列表
        :param start_index: 开始索引
        :param end_index: 结束索引
        :return: 字典，键为从0开始的索引，值为视频信息列表 [guid, time, title, image, brief]
        """
        pass
    
    @abstractmethod
    def get_column_info(self, index: int) -> Dict[str, str]:
        """获取栏目信息

        :param index: 视频索引
        :return: 栏目信息字典，包含time, title, brief, image等键
        """
        pass
    
    @abstractmethod
    def get_encrypt_m3u8_urls(self, guid: str, quality: str = "0") -> List[str]:
        """获取加密m3u8的urls

        :param guid: 视频ID
        :param quality: 视频质量，"0"（最高清晰度）、"1"（超清）、"2"（高清）、"3"（标清）、"4"（流畅）
        :return: 加密m3u8的urls列表
        """
        pass
    
    @abstractmethod
    def get_play_column_info(self, url: str) -> Optional[List[str]]:
        """从视频播放页链接获取栏目标题和ID

        :param url: 视频播放页链接
        :return: 栏目信息列表 [标题, 栏目ID, 视频ID]，如果获取失败则返回None
        """
        pass
    
    @abstractmethod
    def brief_formating(self, s: str) -> str:
        """格式化介绍信息

        :param s: 介绍信息
        :return: 格式化后的介绍信息
        """
        pass


class APIManager:
    """
    API插件管理器
    用于加载、管理和使用API插件
    """
    
    def __init__(self):
        self._plugins = {}
        self._active_plugin = None
    
    def register_plugin(self, plugin: APIPlugin) -> None:
        """注册插件

        :param plugin: API插件实例
        """
        self._plugins[plugin.name] = plugin
        # 如果是第一个注册的插件，则设为活动插件
        if self._active_plugin is None:
            self._active_plugin = plugin.name
    
    def get_plugin(self, name: str) -> Optional[APIPlugin]:
        """获取指定名称的插件

        :param name: 插件名称
        :return: 插件实例，如果不存在则返回None
        """
        return self._plugins.get(name)
    
    def get_active_plugin(self) -> Optional[APIPlugin]:
        """获取当前活动的插件

        :return: 当前活动的插件实例，如果没有则返回None
        """
        if self._active_plugin is None:
            return None
        return self._plugins.get(self._active_plugin)
    
    def set_active_plugin(self, name: str) -> bool:
        """设置活动插件

        :param name: 插件名称
        :return: 是否设置成功
        """
        if name in self._plugins:
            self._active_plugin = name
            return True
        return False
    
    def get_all_plugins(self) -> Dict[str, APIPlugin]:
        """获取所有已注册的插件

        :return: 插件字典，键为插件名称，值为插件实例
        """
        return self._plugins
