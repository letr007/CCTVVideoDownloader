from PyQt5 import QtCore,QtWidgets
from PyQt5.QtGui import QIcon
from MainUI import Ui_MainWindow as MainUI
from logger import CustomLogger

class CCTVVideoDownload():
    def __init__(self):
        self.mainUI = None
        self._SETTINGS = {}

    def setup_ui(self) -> None:
        '''初始化'''
        # 初始化日志
        self._logger = CustomLogger("CCTVVideoDownload", "CCTVVideoDownload.log")
        self._logger.info("程序初始化...")
        # 加载主UI
        self.mainUI = QtWidgets.QMainWindow()
        # 实例化主UI
        _main_ui = MainUI()

        self._logger.info("加载主UI...")
        # 加载UI
        _main_ui.setupUi(self.mainUI)
        # 锁定下载按钮
        _main_ui.pushButton.setEnabled(False)
        # 设置标题
        self.mainUI.setWindowTitle("央视频下载器")
        # 设置图标
        self.mainUI.setWindowIcon(QIcon(":/resources/cctvvideodownload.ico"))

        # 检查配置文件
        self._checkout_config()

        # 显示UI
        self.mainUI.show()
        self._logger.info("程序初始化完成")
        
    def _function_connect(self) -> None:
        '''连接信号与槽'''
        

    def _checkout_config(self) -> None:
        '''检查配置文件'''
        import json,os
        if not os.path.exists("./config.json"):
            self._logger.warning("配置文件不存在")
            from settings import DEFAULT_CONFIG
            # 创建配置文件
            try:
                with open("config.json", "w+") as f:
                    f.write(json.dumps(DEFAULT_CONFIG))
                    self._logger.info("创建配置文件成功")
            except Exception as e:
                self._logger.error("创建配置文件失败")
                self._logger.debug(f"错误详情:{e}")
                self._raise_error(e)
                return
        try:
            with open("config.json", "r") as f:
                # 读取配置文件
                config = json.load(f)
                self._SETTINGS = config["settings"]
                self._logger.info("读取配置文件成功")
        except Exception as e:
            self._logger.error("读取配置文件失败")
            self._logger.debug(f"错误详情:{e}")
            self._raise_error(e)
            return

    def _raise_error(self, error: Exception) -> None:
        '''错误抛出'''
        self._logger.critical("程序异常退出")
        self._logger.critical(f"错误详情:{error}")
        # 给出错误提示窗口
        import sys,os
        path = os.getcwd()
        path = os.path.join(path, "CCTVVideoDownload.log")
        QtWidgets.QMessageBox.critical(self.mainUI, "错误", f"错误详情:\n{error}\n请检查日志文件\n{path}")
        sys.exit(1)
        
def main():
    import sys
    # 创建QApplication对象，它是整个应用程序的入口
    app = QtWidgets.QApplication(sys.argv)
    # 实例化主类
    CTVD = CCTVVideoDownload()
    # 初始化UI
    CTVD.setup_ui()
    # 进入主循环
    sys.exit(app.exec_())


if __name__ == "__main__":
    main() 