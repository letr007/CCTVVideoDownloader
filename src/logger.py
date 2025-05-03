import logging
import os
from logging.handlers import RotatingFileHandler
import sys
from datetime import datetime

class CustomLogger(logging.Logger):
    """
    自定义日志类，继承自logging.Logger。
    该类初始化时会自动配置日志输出到指定的文件，并设置日志级别。
    """

    def __init__(self, name, log_file, level=logging.DEBUG):
        """
        初始化自定义日志器。

        :param name: 日志器的名称。
        :param log_file: 日志文件的路径。
        :param level: 日志级别，默认为DEBUG。
        """
        super().__init__(name, level)
        
        # 确保日志目录存在
        log_dir = os.path.dirname(log_file)
        if log_dir and not os.path.exists(log_dir):
            os.makedirs(log_dir)
        
        # 创建文件处理器，使用轮转功能
        # 每个日志文件最大10MB，最多保留5个备份
        file_handler = RotatingFileHandler(
            log_file,
            maxBytes=10*1024*1024,  # 10MB
            backupCount=5,
            encoding='utf-8'
        )
        
        # 设置日志格式
        formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(funcName)s - %(message)s'
        )
        file_handler.setFormatter(formatter)
        
        # 创建控制台处理器
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setFormatter(formatter)
        
        # 将处理器添加到日志器
        self.addHandler(file_handler)
        self.addHandler(console_handler)
        
        # 记录启动信息
        self.info(f"日志系统初始化完成 - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        self.info(f"Python版本: {sys.version}")
        self.info(f"工作目录: {os.getcwd()}")

# 创建全局日志器实例
logger = CustomLogger("CCTVVideoDownloader", "CCTVVideoDownloader.log")

# 添加日志级别说明
"""
日志级别说明：
DEBUG    - 调试信息，用于开发调试
INFO     - 一般信息，记录程序运行状态
WARNING  - 警告信息，不影响程序运行但需要注意
ERROR    - 错误信息，程序运行出错
CRITICAL - 严重错误，可能导致程序崩溃
"""

#DEBUG 调试信息
#INFO 信息
#WARNING 警告
#ERROR 错误
#CRITICAL 严重错误