import subprocess
from PyQt5 import QtCore,QtWidgets
from PyQt5.QtGui import QIcon,QPixmap,QMovie,QDesktopServices

from qt_material import apply_stylesheet

from MainUI import Ui_MainWindow as MainUI
from logger import logger
from api import CCTVVideoDownloaderAPI as API
from download_engine import DownloadEngine as Engine
from video_process import VideoProcess as Process
from ImportUI import Ui_Dialog as ImportUI
from AboutUI import Ui_Dialog as AboutUI
from SettingUI import Ui_Dialog as SettingUI
from DownloadUI import Ui_Dialog as DownloadUI
from ConcatUI import Ui_Dialog as ConcatUI


class CCTVVideoDownloader():
    def __init__(self):
        self._mainUI = None
        self._SETTINGS = {}
        self._PROGRAMME = {}

        self._SELECT_ID = None # 选中的栏目ID
        self._SELECT_INDEX = None # 选中的节目索引

        self.api = API()
        self.worker = Engine()
        self.process = Process()

    def setup_ui(self) -> None:
        '''初始化'''
        # 初始化日志
        self._logger = logger
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
                video_information = self.api.get_video_list(self._SELECT_ID, int(self._SETTINGS["video_display_num"]))
                self.VIDEO_INFO = video_information
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
        # 节目信息
        self._WILL_DOWNLOAD = {
            "name": self.VIDEO_INFO[self._SELECT_INDEX][2],
            "guid": self.VIDEO_INFO[self._SELECT_INDEX][0],
        }

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
        # 锁定线程数上限与下限
        self.dialog_setting.spinBox_thread.setMaximum(5)
        self.dialog_setting.spinBox_thread.setMinimum(1)
        # 填充默认值
        self.dialog_setting.lineEdit_file_save_path.setText(self._SETTINGS["file_save_path"])
        self.dialog_setting.spinBox_thread.setValue(int(self._SETTINGS["threading_num"]))
        self.dialog_setting.spinBox_program.setValue(int(self._SETTINGS["video_display_num"]))
        self.dialog_setting.comboBox_quality.setCurrentIndex(self._SETTINGS["quality"])
        # 绑定按钮
        def open_file_save_path():
            file_save_path = self.dialog_setting.lineEdit_file_save_path.text()
            file_save_path = QtWidgets.QFileDialog.getExistingDirectory(self._dialog_setting_base, "选择保存路径", file_save_path)
            if file_save_path:
                self.dialog_setting.lineEdit_file_save_path.setText(file_save_path)

        def save_settings():
            file_save_path = self.dialog_setting.lineEdit_file_save_path.text()
            thread_num = self.dialog_setting.spinBox_thread.value()
            video_display_num = self.dialog_setting.spinBox_program.value()
            self._SETTINGS["file_save_path"] = file_save_path
            self._SETTINGS["threading_num"] = str(thread_num)
            self._SETTINGS["video_display_num"] = str(video_display_num)
            self._SETTINGS["quality"] = self.dialog_setting.comboBox_quality.currentIndex()
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
        self._logger.info(f"使用线程数:{self._SETTINGS['threading_num']}")
        # 锁定下载按钮
        self.main_ui.pushButton.setEnabled(False)
        # 获取下载视频参数
        # urls = self.api.get_m3u8_urls_450(self._WILL_DOWNLOAD["guid"])
        try:
            urls = self.api.get_encrypt_m3u8_urls(self._WILL_DOWNLOAD["guid"], str(self._SETTINGS["quality"]))
        except ValueError as e:
            self._raise_error(e)
        file_save_path = self._SETTINGS["file_save_path"]
        name = self._WILL_DOWNLOAD["name"]
        self._dialog_download_base = QtWidgets.QDialog()
        self._dialog_concat_base = QtWidgets.QDialog()
        self._dialog_decrypt_base = QtWidgets.QDialog()
        self.dialog_download = DownloadUI()
        self.dialog_concat = ConcatUI()
        # 暂时复用一下拼接的UI
        self.dialog_decrypt = ConcatUI()
        self.dialog_download.setupUi(self._dialog_download_base)
        self._dialog_download_base.closeEvent = lambda event: self.worker.quit()
        self._dialog_download_base.closeEvent = lambda event: self.main_ui.pushButton.setEnabled(True)
        # 设置模态
        self._dialog_download_base.setModal(True)
        self._dialog_concat_base.setModal(True)
        self._dialog_decrypt_base.setModal(True)
        # 去除边框
        self._dialog_concat_base.setWindowFlags(QtCore.Qt.FramelessWindowHint)
        self._dialog_decrypt_base.setWindowFlags(QtCore.Qt.FramelessWindowHint)
        # 初始化表格
        self.dialog_download.tableWidget.setRowCount(len(urls) - 1)
        self.dialog_download.progressBar_all.setValue(0)
        self.dialog_download.tableWidget.setColumnWidth(0, 55)
        self.dialog_download.tableWidget.setColumnWidth(1, 65)
        self.dialog_download.tableWidget.setColumnWidth(2, 85)
        self.dialog_download.tableWidget.setColumnWidth(3, 70)

        def center_dialog_on_main_window(dialog, main_window):
            # 获取主窗口的几何信息
            main_window_rect = main_window.frameGeometry()
            # 获取屏幕中心点
            center_point = main_window_rect.center()
            # 设置对话框的位置为中心点
            dialog.move(center_point.x() - dialog.width() // 2, 
                        center_point.y() - dialog.height() // 2)

        # 在显示对话框之前调用函数设定位置
        center_dialog_on_main_window(self._dialog_download_base, self._mainUI)
        self._dialog_download_base.show()

        self._progress_dict = {i: 0 for i in range(len(urls))}

        # 开始下载
        self.worker.transfer(name, urls, file_save_path, int(self._SETTINGS["threading_num"]))
        self.worker.start()

        def video_concat():
            self._logger.info("开始视频拼接")
            self.process.transfer(self._SETTINGS["file_save_path"], self._WILL_DOWNLOAD["name"])
            self.dialog_concat.setupUi(self._dialog_concat_base)
            # 在显示对话框之前调用函数设定位置
            center_dialog_on_main_window(self._dialog_concat_base, self._mainUI)
            self._dialog_concat_base.show()
            self.process.concat()
            self.process.concat_finished.connect(concat_finished)

        def video_decrypt():
            self._logger.info("开始视频解密")
            self.process.transfer(self._SETTINGS["file_save_path"], self._WILL_DOWNLOAD["name"])
            self.dialog_decrypt.setupUi(self._dialog_decrypt_base)
            self.dialog_decrypt.label.setText("视频解密中...")
            center_dialog_on_main_window(self._dialog_decrypt_base, self._mainUI)
            self._dialog_decrypt_base.show()
            self.process.decrypt()
            self.process.decrypt_finished.connect(decrypt_finished)
        def concat_finished(flag: bool):
            if flag:
                self._logger.info("视频拼接完成")
                # 解锁下载按钮
                self.main_ui.pushButton.setEnabled(True)
                self._dialog_concat_base.close()

        def decrypt_finished(flag: bool):
            if flag:
                self._logger.info("视频解密完成")
                self._dialog_decrypt_base.close()
                video_concat()

        def display_info(info: list):
            """更新表格中的下载信息并计算总进度"""
            row = int(info[0]) - 1  # 行索引
            
            # 状态文本映射
            status_text = {
                1: "下载中",
                0: "完成", 
                -1: "等待"
            }
            
            # 创建表格项
            items = [
                QtWidgets.QTableWidgetItem(str(info[0])),  # 序号
                QtWidgets.QTableWidgetItem(status_text.get(info[1], "未知")),  # 状态
                QtWidgets.QTableWidgetItem(info[2]),  # URL
                QtWidgets.QTableWidgetItem(f"{int(info[3])}%")  # 进度
            ]
            
            # 批量设置表格项
            for col, item in enumerate(items):
                self.dialog_download.tableWidget.setItem(row, col, item)
            
            # 更新进度字典
            self._progress_dict[info[0]] = int(info[3])
            
            # 计算并更新总进度
            total_progress = sum(self._progress_dict.values()) / len(self._progress_dict) * 2
            self.dialog_download.progressBar_all.setValue(int(total_progress))
            
            # 刷新表格视图
            self.dialog_download.tableWidget.viewport().update()
            
            # 检查是否全部完成
            if total_progress >= 100:
                self._logger.info("下载完成")
                self._dialog_download_base.close()
                video_decrypt()



        # 生成信息列表
        info_list = []
        for num, url in enumerate(urls, start=1):  # 从 1 开始计数
            info = [str(num), -1, url, "0"]
            info_list.append(info)
            display_info(info)

        
        self.worker.download_info.connect(display_info)


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
        self.dialog_about.label_link.linkActivated.connect(lambda: QDesktopServices.openUrl(QtCore.QUrl("https://github.com/letr007/CCTVVideoDownloader")))

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
        path = os.path.join(path, "CCTVVideoDownloader.log")
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
    # 美化主题
    apply_stylesheet(app, theme='dark_blue.xml')
    # 实例化主类
    CTVD = CCTVVideoDownloader()
    # 初始化UI
    CTVD.setup_ui()
    # 进入主循环
    sys.exit(app.exec_())


if __name__ == "__main__":
    main() 