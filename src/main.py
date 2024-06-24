import subprocess
from PyQt5 import QtCore,QtWidgets
from PyQt5.QtGui import QIcon,QPixmap,QMovie,QDesktopServices

from MainUI import Ui_MainWindow as MainUI
from logger import CustomLogger
from api import CCTVVideoDownloadAPI as API
from download_engine import DownloadEngine as engine
from ImportUI import Ui_Dialog as ImportUI
from AboutUI import Ui_Dialog as AboutUI
from SettingUI import Ui_Dialog as SettingUI
from DownloadUI import Ui_Dialog as DownloadUI
from download_engine import DownloadEngine

class CCTVVideoDownload():
    def __init__(self):
        self._mainUI = None
        self._SETTINGS = {}
        self._PROGRAMME = {}

        self._SELECT_ID = None # 选中的栏目ID
        self._SELECT_INDEX = None # 选中的节目索引

        self.api = API()

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
        # 设置表格只读
        self.main_ui.tableWidget_Config.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self.main_ui.tableWidget_List.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
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
        self._logger.info("刷新节目列表...")
        # 检查更新节目单
        self._checkout_config()
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
        self._logger.info("刷新视频列表...")
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

                self._logger.info("视频列表刷新完成")
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
                self._raise_warning("图片获取失败")
                self._logger.warning("图片获取失败")
        else:
            pass

        # 恢复下载按钮
        self.main_ui.pushButton.setEnabled(True)
             
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

    def _open_save_location(self) -> None:
        '''打开文件保存位置'''
        path = self._SETTINGS["file_save_path"]
        command = ["explorer", path]
        # 创建STARTUPINFO对象以隐藏命令行窗口
        startupinfo = subprocess.STARTUPINFO()
        # startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        subprocess.run(command, startupinfo=startupinfo)

    def _dialog_setting(self) -> None:
        '''设置对话框'''
        self._logger.info("打开设置")
        self._dialog_setting_base = QtWidgets.QDialog()
        self.dialog_setting = SettingUI()
        self.dialog_setting.setupUi(self._dialog_setting_base)
        # 锁定
        self.dialog_setting.radioButton_ts.setChecked(True)
        self.dialog_setting.radioButton_mp4.setEnabled(False)
        self.dialog_setting.radioButton_ts.setEnabled(False)
        self.dialog_setting.spinBox.setEnabled(False)
        # 填充默认路径
        self.dialog_setting.lineEdit_file_save_path.setText(self._SETTINGS["file_save_path"])
        # 绑定按钮
        def open_file_save_path():
            file_save_path = self.dialog_setting.lineEdit_file_save_path.text()
            file_save_path = QtWidgets.QFileDialog.getExistingDirectory(self._dialog_setting_base, "选择保存路径", file_save_path)
            if file_save_path:
                self.dialog_setting.lineEdit_file_save_path.setText(file_save_path)

        def save_settings():
            file_save_path = self.dialog_setting.lineEdit_file_save_path.text()
            self._SETTINGS["file_save_path"] = file_save_path
            self._logger.info(f"保存设置:{self._SETTINGS}")
            # 更新配置
            import json
            with open("config.json", "r", encoding="utf-8") as f:
                config = json.loads(f.read())
            config["settings"] = self._SETTINGS
            with open("config.json", "w", encoding="utf-8") as f:
                f.write(json.dumps(config, indent=4))
            self._logger.info("配置已更新")

        self.dialog_setting.pushButton_open.clicked.connect(open_file_save_path)
        self.dialog_setting.buttonBox.accepted.connect(save_settings)



        self._dialog_setting_base.show()

    def _dialog_download(self) -> None:
        '''下载对话框'''
        self._logger.info("开始下载")
        # 获取下载视频参数
        self._DOWNLOAD_INFO = 
        self._dialog_download_base = QtWidgets.QDialog()
        self.dialog_download = DownloadUI()
        self.dialog_download.setupUi(self._dialog_download_base)
        # 设置模态
        self._dialog_download_base.setModal(True)
        self._dialog_download_base.show()
        # 开始下载
        worker = DownloadEngine()
        worker.transfer()
        def display_info():
            pass


        

    def _dialog_about(self) -> None:
        '''关于对话框'''
        # 输出日志
        self._logger.info("打开关于")
        self._dialog_about_base = QtWidgets.QDialog()
        self.dialog_about = AboutUI()
        self.dialog_about.setupUi(self._dialog_about_base)
        # 显示动图
        self._movie = QMovie(":/resources/afraid.gif")
        self.dialog_about.label_img.setMovie(self._movie)
        self._movie.setScaledSize(QtCore.QSize(100,100))
        # 设置模态
        self._dialog_about_base.setModal(True)

        self._dialog_about_base.show()
        self._movie.start()
        # 链接
        self.dialog_about.label_link.setOpenExternalLinks(True)
        self.dialog_about.label_link.linkActivated.connect(lambda: QDesktopServices.openUrl(QtCore.QUrl("https://github.com/letr007/CCTVVideoDownload")))

    def _dialog_import(self) -> None:
        '''节目导入对话框'''
        self._logger.info("打开节目导入")
        self._dialog_import_base = QtWidgets.QDialog()
        self.dialog_import = ImportUI()
        self.dialog_import.setupUi(self._dialog_import_base)
        # 设置模态
        self._dialog_import_base.setModal(True)
        self._dialog_import_base.show()
        url = None
        def url():
            # 获取值
            url = self.dialog_import.lineEdit.text()
            self._dialog_import_base.close()
            # 请求获取节目信息
            column_info = self.api.get_play_column_info(url)
            if column_info != None:
                import json
                with open("config.json", "r", encoding="utf-8") as f:
                # 读取配置文件
                    config = json.loads(f.read())
                # 获取当前最大的 key 值，并自增
                max_key = max(map(int, config["programme"].keys())) + 1 if config["programme"] else 1
                # 检查 id 是否已经存在
                for prog in config["programme"].values():
                    if prog["id"] == column_info[1]:
                        self._logger.warning(f"节目ID [{column_info[1]}] 已存在")
                        return
                    
                config["programme"][str(max_key)] = {"name": column_info[0], "id": column_info[1]}
                with open("config.json", "w+", encoding="utf-8") as f:
                    # 写入配置文件
                    f.write(json.dumps(config, indent=4))

                self._flash_programme_list()
                
                self._logger.info(f"导入节目:{column_info[0]}")

        self.dialog_import.buttonBox.accepted.connect(url)



        
    def _function_connect(self) -> None:
        '''连接信号与槽'''
        # 绑定退出
        self.main_ui.actionexit.triggered.connect(self._mainUI.close)
        # 绑定刷新按钮
        self.main_ui.flash_list.clicked.connect(self._flash_video_list)
        self.main_ui.flash_program.clicked.connect(self._flash_programme_list)
        # 绑定栏目表格点击事件
        self.main_ui.tableWidget_Config.cellClicked.connect(self._is_program_selected)
        # 绑定节目表格点击事件
        self.main_ui.tableWidget_List.cellClicked.connect(self._is_video_selected)
        # 绑定导入
        self.main_ui.actionimport.triggered.connect(self._dialog_import)
        # 绑定关于
        self.main_ui.actionabout.triggered.connect(self._dialog_about)
        # 绑定打开文件保存位置
        self.main_ui.actionfile.triggered.connect(self._open_save_location)
        # 绑定设置
        self.main_ui.actionsetting.triggered.connect(self._dialog_setting)
        # 绑定下载
        self.main_ui.pushButton.clicked.connect(self._dialog_download)


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
    QtWidgets.QApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling)
    QtWidgets.QApplication.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps)
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