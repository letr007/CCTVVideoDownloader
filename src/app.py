"""
央视频下载器
"""
import sys,os

try:
    from importlib import metadata as importlib_metadata
except ImportError:
    # Backwards compatibility - importlib.metadata was added in Python 3.8
    import importlib_metadata

from PySide6 import QtWidgets
from PySide6.QtCore import Signal,QUrl,QSize,Qt
from PySide6.QtGui import QDesktopServices,QMovie,QIcon

from cctvvideodownload.MainUI import Ui_MainWindow
from cctvvideodownload.DialogUI import Ui_Dialog as DownloadDialog
from cctvvideodownload.SettingUI import Ui_Dialog as SettingDialog
from cctvvideodownload.AboutUI import Ui_Dialog as AboutDialog
from cctvvideodownload.DlHandle import VideoDownload
from cctvvideodownload.ThreadHandle import ThreadHandle,ConcatThread

import cctvvideodownload.compiled_resources




class CCTVVideoDownload(QtWidgets.QMainWindow, Ui_MainWindow, DownloadDialog, SettingDialog):
    # 定义信号
    log_signal = Signal(int)
    def __init__(self, parent=None):
        super(CCTVVideoDownload, self).__init__(parent)
        self.SETTINGS = {}
        self._DEFAULT_SETTINGS = r"""
{
    "settings":{
        "file_save_path":"C:\\Video",
        "threading_num":"1",
        "whether_transcode":"False"
    },
    "programme": {
                
    }
}
"""
        self.init_ui()

    def init_ui(self):
        self.setupUi(self)
        self.setWindowTitle('央视频下载器')
        self.setWindowIcon(QIcon(":/resources/ico.ico"))
        # 菜单栏动作
        self.actionexit.triggered.connect(self.close)
        self.actionin.triggered.connect(self.config_in)
        self.actionexit.triggered.connect(self.exit)
        self.actionfile.triggered.connect(self.open_file_path)
        self.actionsetting.triggered.connect(self.setting)
        self.actionabout.triggered.connect(self.about)
        # 槽绑定
        self.pushButton_Download.clicked.connect(self.download_start)
        self.pushButton_FlashConfig.clicked.connect(self.config_reload)
        self.tableWidget_Config.cellClicked.connect(self.choose_config)
        self.pushButton_FlashList.clicked.connect(self.flash_list)
        self.tableWidget_List.cellClicked.connect(self.list_choose)

        self.pushButton_Download.setEnabled(False)
        self.pushButton_FlashList.setEnabled(False)

        # 检查配置文件
        self.check_config()

        self.show()

    # def main(self) -> None:
    #     # 菜单栏动作
    #     self.actionexit.triggered.connect(self.close)
    #     self.actionin.triggered.connect(self.config_in)
    #     self.actionexit.triggered.connect(self.exit)
    #     self.actionfile.triggered.connect(self.open_file_path)
    #     # 槽绑定
    #     self.pushButton_Download.clicked.connect(self.download_start)
    #     self.pushButton_FlashConfig.clicked.connect(self.config_reload)
    #     self.tableWidget_Config.cellClicked.connect(self.choose_config)
    #     self.pushButton_FlashList.clicked.connect(self.flash_list)
    #     self.tableWidget_List.cellClicked.connect(self.list_choose)

    #     self.pushButton_Download.setEnabled(False)
    #     self.pushButton_FlashList.setEnabled(False)

    #     # self.output("-"*38)
    #     # self.output("初次使用请先 配置>导入配置")
    #     # self.output("使用方法:重载配置>选中一个配置>刷新列表>选中一个视频>下载视频")
    #     # self.output("下载完成后 程序>打开文件位置")
    #     # self.output("-"*38)
        
    def check_config(self) -> None:
        '''检查配置文件并添加到属性settings'''
        import json
        if not os.path.exists("./config.json"):
            self.output("INFO", "配置检查", "未找到配置文件")
            self.output("INFO", "配置检查", "重建配置文件...")
            try:
                with open("config.json", "w+") as f:
                    f.write(self._DEFAULT_SETTINGS)
                self.output("OKEY", "配置检查", "配置文件重建完成")
            except Exception as e:
                self.output("ERROR", "重建配置文件时出现错误\n错误详情:%s"% e)
        try:
            with open("./config.json", "r", encoding='utf-8') as f:
                config = json.load(f) # 将文件解析为字典
                self.SETTINGS = config["settings"]
        except Exception as e:
            self.output("ERROR", "加载配置文件时出现错误\n错误详情:%s"% e)

        
        
    def setting(self) -> None:
        '''设置项'''
        # 实例化设置窗口
        self.dialog_base = QtWidgets.QDialog()
        self.dialog_setting = SettingDialog()
        self.dialog_setting.setupUi(self.dialog_base)
        self.dialog_base.setModal(True)
        self.dialog_base.show()
        # 加载保存的设置
        self.dialog_setting.lineEdit_file_save_path.setText(str(self.SETTINGS["file_save_path"]))
        self.dialog_setting.spinBox.setValue(int(self.SETTINGS["threading_num"]))
        if self.SETTINGS["whether_transcode"] == "True":
            self.dialog_setting.radioButton_mp4.setChecked(True)
        elif self.SETTINGS["whether_transcode"] == "False":
            self.dialog_setting.radioButton_ts.setChecked(True)
        # 槽绑定
        self.dialog_setting.pushButton_open.clicked.connect(self.open_file_save_path)
        self.dialog_setting.buttonBox.accepted.connect(self.save_settings)
        # 锁
        self.dialog_setting.spinBox.setEnabled(False)
        self.dialog_setting.radioButton_mp4.setEnabled(False)
            
    def open_file_save_path(self) -> None:
        file_save_path = QtWidgets.QFileDialog.getExistingDirectory(self, "选择文件夹", "C:\\")
        self.dialog_setting.lineEdit_file_save_path.setText(str(file_save_path))

    def save_settings(self) -> None:
        '''保存设置项'''
        import json
        self.SETTINGS["file_save_path"] = self.dialog_setting.lineEdit_file_save_path.text()
        self.SETTINGS["threading_num"] = str(self.dialog_setting.spinBox.value())
        if self.dialog_setting.radioButton_mp4.isChecked():
            self.SETTINGS["whether_transcode"] = "True"
        else:
            self.SETTINGS["whether_transcode"] = "False"
        # 写入JSON
        try:
            with open("config.json", "r", encoding='utf-8') as f:
                config = json.load(f)
                config["settings"] = self.SETTINGS
            with open("config.json", "w", encoding='utf-8') as file:
                    json.dump(config, file, ensure_ascii=False, indent=4)
        except Exception as e:
            self.output("ERROR", "写入配置文件时出现错误\n错误详情:%s"%e)

    def about(self) -> None:
        self.about_base = QtWidgets.QDialog()
        self.dialog_about = AboutDialog()
        self.dialog_about.setupUi(self.about_base)
        self.movie = QMovie(":/resources/afraid.gif")
        self.dialog_about.label_img.setMovie(self.movie)
        self.movie.setScaledSize(QSize(100,100))
        self.about_base.show()
        self.movie.start()
        self.dialog_about.label_link.setOpenExternalLinks(True)
        self.dialog_about.label_link.linkActivated.connect(self.open_link)

    def open_link(self) -> None:
        QDesktopServices.openUrl(QUrl("https://github.com/letr007/CCTVVideoDownload"))

    def flash_list(self) -> None:
        '''刷新视频列表'''
        # 获取视频列表信息
        vd = VideoDownload()
        self.output("INFO","视频信息","获取列表...")
        try:
            self.dict1 = vd.GetVideoList(self.choise_id)
            self.tableWidget_List.setRowCount(len(self.dict1))
            self.tableWidget_List.setColumnWidth(0, 200)
            num = 0
            for i in self.dict1:
                item1 = QtWidgets.QTableWidgetItem(self.dict1[i][0])
                self.tableWidget_List.setItem(num, 0, item1)
                num += 1
            self.tableWidget_List.viewport().update()
            self.output("OKEY","视频信息","获取成功")
            self.pushButton_Download.setEnabled(True)
        except Exception as e:
            self.output("ERROR", "在获取视频信息时出现错误\n错误详情:%s"%e)

            

    def list_choose(self, r:int, c:int) -> None:
        '''接受信号，表格'''
        choose_item = self.tableWidget_List.item(r,0).text()
        self.output("INFO","视频信息","已选中 %s" %choose_item)
        self.dl_index = r + 1
        self.choose_name = choose_item




    def choose_config(self, r:int, c:int) -> None:
        '''接受信号，返回表格选择值'''
        choose_item = self.tableWidget_Config.item(r,0).text()
        # self.config_choise = r + 1
        self.choise_id = self.tableWidget_Config.item(r,1).text()
        self.output("INFO", "节目信息", "已选中 %s" %choose_item)
        
        
        

    def config_in(self) -> None:
        '''导入配置方法'''
        import json
        # 文件选择
        filepath, _ = QtWidgets.QFileDialog.getOpenFileName(self, "选择节目单文件", os.path.abspath(r"C:\\"), "CTVD节目单(*.cdi)")
        if filepath != "":
            with open(filepath, "r", encoding="utf-8") as f:
                config = json.load(f)  # 解析文件内容为字典
                # print(config)
                # 将其添加到字典
                with open("config.json", "r", encoding='utf-8') as file_1:
                    config_json = json.load(file_1)
                    config_json['programme'] = config
                with open("config.json", "w", encoding='utf-8') as file_2:
                    json.dump(config_json, file_2, ensure_ascii=False, indent=4)  # 禁用 ASCII 编码以保持原格式

            self.output("OKEY", "节目信息", "%s > config.json" % filepath)
            # 重载配置
            # self.config = config
            self.config_reload()
        else:
            self.output("INFO", "节目信息", "导入已取消")
        
        # self.output(str(config))
        
        
    def config_reload(self) -> None:
        '''config重载方法'''
        import json
        try:
            with open(os.path.abspath("config.json"), "r", encoding="utf-8") as f:
                content = json.loads(f.read())
            config = content["programme"]
            # 表格操作
            # print(config)
            self.tableWidget_Config.setRowCount(len(config))
            num = 0
            for i in config:
                dict_tmp = config[i]
                name = dict_tmp['name']
                id = dict_tmp['id']
                # print(name,id)
                # 加入表格
                item1 = QtWidgets.QTableWidgetItem(name)
                item2 = QtWidgets.QTableWidgetItem(id)
                self.tableWidget_Config.setItem(num, 0, item1)
                self.tableWidget_Config.setItem(num, 1, item2)
                num += 1
            self.tableWidget_Config.viewport().update()
            self.output("OKEY","节目信息", "节目信息重载完成")
            self.pushButton_FlashList.setEnabled(True)

        except Exception as e:
            self.output("ERROR","在重载节目信息时出现错误\n错误详情:%s"% e)

    def concat(self, finish:bool) -> None:
        '''合并视频方法'''
        if finish:
            self.output("OKEY","视频下载","下载完成!")
            self.output("INFO","视频合并","开始合并...")
            self.work = ConcatThread()
            self.work.transfer(self.choose_name, self.SETTINGS["file_save_path"])
            self.work.start()
            self.work.concat_finish.connect(self.concat_finish)

    def concat_finish(self, name:str) -> None:
        '''合并完成'''
        self.output("OKEY","视频合并","视频 %s 合并完成!" % name)
        self.pushButton_Download.setEnabled(True)

    def download_start(self) -> None:
        '''开始下载'''
        self.output("INFO","视频下载","开始下载...")
        self.pushButton_Download.setEnabled(False)
        try:
            self.thread = ThreadHandle()
            self.thread.transfer(self.dict1[self.dl_index][1], 10, self.SETTINGS["file_save_path"])
            self.thread.main()
            self.thread.download_finish.connect(self.concat)
        except Exception as e:
            self.output("ERROR","在下载视频时出现错误\n错误详情:%s"% e)
        


    def output(self, type:str, *msg) -> None:
        if type == "OKEY":
            string = "[<font color='#0000FF'>"+msg[0]+"</font>]<font color='#00FF00'>"+msg[1]+"</font>"
        elif type == "ERROR":
            string = "[<font color='#FF0000'>ERROR</font>]<font color='#FF4500'>"+msg[0]+"</font>"
        elif type == "INFO":
            string = "[<font color='#0000FF'>"+msg[0]+"</font>]<font color='#4169E1'>"+msg[1]+"</font>"
        self.textBrowser.append(string)

    def open_file_path(self) -> None:
        path = "C:\\Video"
        os.system("start explorer %s" % path)

    def exit(self) -> None:
        '''关闭方法'''
        self.close()


def main():
    # Linux desktop environments use app's .desktop file to integrate the app
    # to their application menus. The .desktop file of this app will include
    # StartupWMClass key, set to app's formal name, which helps associate
    # app's windows to its menu item.
    #
    # For association to work any windows of the app must have WMCLASS
    # property set to match the value set in app's desktop file. For PySide2
    # this is set with setApplicationName().

    # Find the name of the module that was used to start the app
    app_module = sys.modules['__main__'].__package__
    # Retrieve the app's metadata
    metadata = importlib_metadata.metadata(app_module)

    QtWidgets.QApplication.setApplicationName(metadata['Formal-Name'])

    app = QtWidgets.QApplication(sys.argv)
    main_window = CCTVVideoDownload()
    sys.exit(app.exec())