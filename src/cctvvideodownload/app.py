"""
央视频下载器
"""
import sys

try:
    from importlib import metadata as importlib_metadata
except ImportError:
    # Backwards compatibility - importlib.metadata was added in Python 3.8
    import importlib_metadata

from PySide6 import QtWidgets
from PySide6.QtCore import Signal

from cctvvideodownload.MainUI import Ui_MainWindow
from cctvvideodownload.DialogUI import Ui_Dialog
from cctvvideodownload.DlHandle import VideoDownload
from cctvvideodownload.ThreadHandle import ThreadHandle




class CCTVVideoDownload(QtWidgets.QMainWindow, Ui_MainWindow, Ui_Dialog):
    # 定义信号
    log_signal = Signal(int)
    def __init__(self, parent=None):
        super(CCTVVideoDownload, self).__init__(parent)
        self.init_ui()

    def init_ui(self):
        self.setupUi(self)
        self.setWindowTitle('央视频下载器')
        self.config_choise = None
        self.main()
        self.show()

    def main(self) -> None:
        # 菜单栏动作
        self.actionexit.triggered.connect(self.close)
        self.actionin.triggered.connect(self.config_in)
        # 槽绑定
        self.pushButton_Download.clicked.connect(self.download_start)
        self.pushButton_FlashConfig.clicked.connect(self.config_reload)
        self.tableWidget_Config.cellClicked.connect(self.choose_config)
        self.pushButton_FlashList.clicked.connect(self.flash_list)
        self.tableWidget_List.cellClicked.connect(self.list_choose)
        self.log_signal.connect(self.log_signal_event)

    def log_signal_event(self, p_int) -> None:
        # self.dialog_ui.label_num1
        self.dialog_ui.progressBar_all.setValue(p_int)

    

    def flash_list(self) -> None:
        '''刷新视频列表'''
        # 获取视频列表信息
        vd = VideoDownload()
        self.output("[视频信息]获取列表...")
        self.dict1 = vd.GetVideoList(self.choise_id)
        self.tableWidget_List.setRowCount(len(self.dict1))
        self.tableWidget_List.setColumnWidth(0, 200)
        num = 0
        for i in self.dict1:
            item1 = QtWidgets.QTableWidgetItem(self.dict1[i][0])
            self.tableWidget_List.setItem(num, 0, item1)
            num += 1
        self.tableWidget_List.viewport().update()
        self.output("[视频信息]获取成功")

    def list_choose(self, r:int, c:int) -> None:
        '''接受信号，表格'''
        choose_item = self.tableWidget_List.item(r,0).text()
        self.output("[视频信息]已选中 %s" %choose_item)
        self.dl_index = r + 1




    def choose_config(self, r:int, c:int) -> None:
        '''接受信号，返回表格选择值'''
        choose_item = self.tableWidget_Config.item(r,0).text()
        # self.config_choise = r + 1
        self.choise_id = self.tableWidget_Config.item(r,1).text()
        self.output("[配置信息]已选中 %s" %choose_item)
        
        

    def config_in(self) -> None:
        '''导入配置方法'''
        # import json
        # 文件选择
        filepath, _ = QtWidgets.QFileDialog.getOpenFileName(self, "选择配置文件", r"C://", "CTVD配置文件(*.cdi)")
        if filepath != "":
            self.output("[导入配置]%s > config.ini" % filepath)
            # 将*.cdi配置写入config.ini，便于重载
            with open(filepath, "r", encoding="utf-8") as f:
                config = f.read()
            with open("config.ini", "w+") as f:
                f.write(config)
            # 重载配置
            self.config = config
            self.config_reload(config)
        else:
            self.output("[导入配置]已取消")
        
        # self.output(str(config))
        
        
    def config_reload(self) -> None:
        '''配置重载方法'''
        import json
        try:
            with open("config.ini", "r", encoding="utf-8") as f:
                content = f.read()
            config = json.loads(content)
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
            self.output("[重载配置]重载完成")

        except Exception as e:
            self.output(str(e))
            self.output("[ERROR]配置重载失败。可能是缺失配置文件，请导入配置")



    def download_start(self) -> None:
        self.thread = ThreadHandle()
        self.thread.transfer_VideoInfo(self.dict1[self.dl_index][1])
        self.thread.main()
        


    def output(self, msg:str) -> None:
        self.textBrowser.append(msg)

    # def exit(self) -> None:
    #     '''关闭方法'''
    #     self.close()


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