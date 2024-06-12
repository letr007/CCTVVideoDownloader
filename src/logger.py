import logging

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
        
        # 创建一个FileHandler，用于写入日志文件，并指定编码为UTF-8
        file_handler = logging.FileHandler(log_file, encoding='utf-8')
        
        # 设置日志格式
        formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
        file_handler.setFormatter(formatter)
        
        # 将文件处理器添加到日志器
        self.addHandler(file_handler)

#DEBUG 调试信息
#INFO 信息
#WARNING 警告
#ERROR 错误
#CRITICAL 严重错误