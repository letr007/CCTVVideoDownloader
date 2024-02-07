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
from PySide6.QtCore import Signal

from cctvvideodownload.MainUI import Ui_MainWindow
from cctvvideodownload.DialogUI import Ui_Dialog
from cctvvideodownload.DlHandle import VideoDownload
from cctvvideodownload.ThreadHandle import ThreadHandle,ConcatThread




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
        self.actionexit.triggered.connect(self.exit)
        self.actionfile.triggered.connect(self.open_file_path)
        # 槽绑定
        self.pushButton_Download.clicked.connect(self.download_start)
        self.pushButton_FlashConfig.clicked.connect(self.config_reload)
        self.tableWidget_Config.cellClicked.connect(self.choose_config)
        self.pushButton_FlashList.clicked.connect(self.flash_list)
        self.tableWidget_List.cellClicked.connect(self.list_choose)

        self.pushButton_Download.setEnabled(False)
        self.pushButton_FlashList.setEnabled(False)

        # self.output("-"*38)
        # self.output("初次使用请先 配置>导入配置")
        # self.output("使用方法:重载配置>选中一个配置>刷新列表>选中一个视频>下载视频")
        # self.output("下载完成后 程序>打开文件位置")
        # self.output("-"*38)f
        


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
        self.pushButton_Download.setEnabled(True)

    def list_choose(self, r:int, c:int) -> None:
        '''接受信号，表格'''
        choose_item = self.tableWidget_List.item(r,0).text()
        self.output("[视频信息]已选中 %s" %choose_item)
        self.dl_index = r + 1
        self.choose_name = choose_item




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
        filepath, _ = QtWidgets.QFileDialog.getOpenFileName(self, "选择配置文件",os.path.abspath(r"C:\\"), "CTVD配置文件(*.cdi)")
        if filepath != "":
            self.output("[导入配置]%s > config.ini" % filepath)
            # 将*.cdi配置写入config.ini，便于重载
            with open(filepath, "r", encoding="utf-8") as f:
                config = f.read()
            with open("config.ini", "w+") as f:
                f.write(config)
            # 重载配置
            # self.config = config
            self.config_reload()
        else:
            self.output("[导入配置]已取消")
        
        # self.output(str(config))
        
        
    def config_reload(self) -> None:
        '''配置重载方法'''
        import json
        try:
            with open(os.path.abspath("config.ini"), "r", encoding="utf-8") as f:
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
            self.pushButton_FlashList.setEnabled(True)

        except Exception as e:
            self.output(str(e))
            self.output("[ERROR]配置重载失败。可能是缺失配置文件，请导入配置")

    def concat(self, finish:bool) -> None:
        '''合并视频方法'''
        if finish:
            self.output("[视频下载]下载完成!")
            self.output("[视频合并]开始合并...")
            self.work = ConcatThread(self.choose_name)
            self.work.start()
            self.work.concat_finish.connect(self.concat_finish)

    def concat_finish(self, name:str) -> None:
        '''合并完成'''
        self.output("[视频合并]视频 %s 合并完成!" % name)
        self.pushButton_Download.setEnabled(True)

    def download_start(self) -> None:
        '''开始下载'''
        self.output("[视频下载]开始下载...")
        self.pushButton_Download.setEnabled(False)
        self.thread = ThreadHandle()
        self.thread.transfer(self.dict1[self.dl_index][1], 10)
        self.thread.main()
        self.thread.download_finish.connect(self.concat)
        


    def output(self, msg:str) -> None:
        self.textBrowser.append(msg)

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