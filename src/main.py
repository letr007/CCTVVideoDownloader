from PyQt5 import QtCore,QtWidgets
from PyQt5.QtGui import QIcon
from MainUI import Ui_MainWindow as MainUI
from logger import CustomLogger
from api import CCTVVideoDownloadAPI

class CCTVVideoDownload():
    def __init__(self):
        self._mainUI = None
        self._SETTINGS = {}
        self._PROGRAMME = {}

        self._SELECT_ID = 0

    def setup_ui(self) -> None:
        '''初始化'''
        # 初始化日志
        self._logger = CustomLogger("CCTVVideoDownload", "CCTVVideoDownload.log")
        self._logger.info("程序初始化...")
        # 加载主UI
        self._mainUI = QtWidgets.QMainWindow()
        # 实例化主UI
        self.main_ui = MainUI()

        self._logger.info("加载主UI...")
        # 加载UI
        self.main_ui.setupUi(self._mainUI)
        # 锁定下载按钮
        self.main_ui.pushButton.setEnabled(False)
        # 设置标题
        self._mainUI.setWindowTitle("央视频下载器")
        # 设置图标
        self._mainUI.setWindowIcon(QIcon(":/resources/cctvvideodownload.ico"))

        # 检查配置文件
        self._checkout_config()

        # 初始化
        self._flash_programme_list()
        self._flash_video_list()

        # 连接信号与槽
        self._function_connect()

        # 显示UI
        self._mainUI.show()
        self._logger.info("程序初始化完成")

    def _flash_programme_list(self) -> None:
        '''刷新节目列表'''
        config = self._PROGRAMME
        self.main_ui.tableWidget_Config.setRowCount(len(config))
        # 遍历
        num = 0
        for i in config:
            dict_tmp = config[i]
            name = dict_tmp['name']
            id = dict_tmp['id']
            # 加入表格
            item1 = QtWidgets.QTableWidgetItem(name)
            item2 = QtWidgets.QTableWidgetItem(id)
            self.main_ui.tableWidget_Config.setItem(num, 0, item1)
            self.main_ui.tableWidget_Config.setItem(num, 1, item2)
            num += 1
        # 更新
        self.main_ui.tableWidget_Config.viewport().update()
        self._logger.info("节目列表刷新完成")

    def _flash_video_list(self) -> None:
        '''刷新视频列表'''
        api = CCTVVideoDownloadAPI()
        video_information = api.get_video_list(self._PROGRAMME[self._SELECT_ID])
        self.main_ui.tableWidget_List.setRowCount(len(video_information))
        self.main_ui.tableWidget_List.setColumnWidth(0, 200)

    
    def _is_program_selected(self, r:int, c:int) -> None:
        selected_item = self.main_ui.tableWidget_Config.item(r, c).text()

        
    def _function_connect(self) -> None:
        '''连接信号与槽'''
        pass

    def _checkout_config(self) -> None:
        '''检查配置文件'''
        import json,os
        if not os.path.exists("./config.json"):
            self._logger.warning("配置文件不存在")
            from settings import DEFAULT_CONFIG
            # 创建配置文件
            try:
                with open("config.json", "w+", encoding="utf-8") as f:
                    f.write(json.dumps(DEFAULT_CONFIG))
                    self._logger.info("创建配置文件成功")
            except Exception as e:
                self._logger.error("创建配置文件失败")
                self._logger.debug(f"错误详情:{e}")
                self._raise_error(e)
                return
        try:
            with open("config.json", "r", encoding="utf-8") as f:
                # 读取配置文件
                config = json.loads(f.read())
                self._SETTINGS = config["settings"]
                self._PROGRAMME = config["programme"]
                self._logger.info("读取配置文件成功")
        except Exception as e:
            self._logger.error("读取配置文件失败")
            self._logger.debug(f"错误详情:{e}")
            self._raise_error(e)
            return

    def _raise_error(self, error: Exception) -> None:
        '''错误抛出,仅抛出引发程序异常退出的错误'''
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