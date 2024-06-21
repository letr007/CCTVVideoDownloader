from PyQt5 import QtCore,QtWidgets
from PyQt5.QtGui import QIcon,QPixmap

from MainUI import Ui_MainWindow as MainUI
from logger import CustomLogger
from api import CCTVVideoDownloadAPI as API
from download_engine import DownloadEngine as engine

class CCTVVideoDownload():
    def __init__(self):
        self._mainUI = None
        self._SETTINGS = {}
        self._PROGRAMME = {}

        self._SELECT_ID = None # 选中的栏目ID
        self._SELECT_INDEX = None # 选中的节目索引

    def setup_ui(self) -> None:
        '''初始化'''
        # 初始化日志
        self._logger = CustomLogger("CCTVVideoDownload", "CCTVVideoDownload.log")
        self._logger.info("程序初始化...")
        # 加载主UI
        self._mainUI = QtWidgets.QMainWindow()
        # 实例化主UI
        self.main_ui = MainUI()
        # 输出日志
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
        self._logger.info("栏目列表刷新完成")

    def _flash_video_list(self) -> None:
        '''刷新视频列表'''
        self.api = API()
        if self._SELECT_ID != None:
            if self._PROGRAMME != {}:
                # 获取节目信息
                video_information = self.api.get_video_list(self._SELECT_ID)
                self.main_ui.tableWidget_List.setRowCount(len(video_information))
                self.main_ui.tableWidget_List.setColumnWidth(0, 300)
                for i in range(len(video_information)):
                    item1 = QtWidgets.QTableWidgetItem(video_information[i][2])
                    self.main_ui.tableWidget_List.setItem(i, 0, item1)
                self.main_ui.tableWidget_List.viewport().update()
            else:
                self._logger.error("节目单为空!")
                self._raise_warning("节目单为空!")
        else:
            self._logger.error("未选中栏目而试图刷新列表")
            self._raise_warning("您还未选择节目!")

    def _display_video_info(self) -> None:
        if self._SELECT_INDEX != None:
            # 获取信息
            video_info = self.api.get_column_info(self._SELECT_INDEX)
            # 将信息显示到label
            self.main_ui.label_title.setText(video_info['title'])
            self.main_ui.label_introduce.setText(video_info['brief'])
            time_new = video_info["time"].replace(" ", "\n")
            self.main_ui.label_time.setText(time_new)
            if video_info["image"] != None:
                pixmap = QPixmap()
                pixmap.loadFromData(video_info["image"])
                self.main_ui.label_img.setPixmap(pixmap)
            else:
                self.main_ui.label_img.setText("图片加载失败")
                self._logger("图片加载失败")
        else:
            pass
            


    
    def _is_program_selected(self, r:int, c:int) -> None:
        # 获取ID
        selected_item_id = self.main_ui.tableWidget_Config.item(r, 1).text()
        # 获取名称
        selected_item_name = self.main_ui.tableWidget_Config.item(r, 0).text()
        # 输出日志
        self._logger.info(f"选中栏目:{selected_item_name}")
        # 设置ID
        self._SELECT_ID = selected_item_id

        self._flash_video_list()

    def _is_video_selected(self, r:int, c:int) -> None:
        # 获取INDEX
        self._SELECT_INDEX = self.main_ui.tableWidget_List.currentRow()
        # 输出日志
        self._logger.info(f"选中节目索引:{self._SELECT_INDEX}")

        self._display_video_info()
        
    def _function_connect(self) -> None:
        '''连接信号与槽'''
        # 绑定退出
        self.main_ui.actionexit.triggered.connect(self._mainUI.close)
        # 刷新节目列表
        self.main_ui.flash_program.clicked.connect(self._flash_programme_list)
        # 绑定栏目表格点击事件
        self.main_ui.tableWidget_Config.cellClicked.connect(self._is_program_selected)
        # 绑定节目表格点击事件
        self.main_ui.tableWidget_List.cellClicked.connect(self._is_video_selected)


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
        QtWidgets.QMessageBox.critical(self._mainUI, "错误", f"错误详情:\n{error}\n请检查日志文件\n{path}")
        sys.exit(1)

    def _raise_warning(self, warning: str) -> None:
        '''警告抛出,抛出警告'''
        QtWidgets.QMessageBox.warning(self._mainUI, "警告", warning)

        
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