from typing import Optional
import PySide6.QtCore
import requests,os
from PySide6 import QtCore,QtWidgets
from PySide6.QtCore import QObject
from PySide6.QtCore import Signal,QRunnable,QThreadPool,QThread

from cctvvideodownload.DialogUI import Ui_Dialog
from cctvvideodownload.DlHandle import VideoDownload

class ThreadHandle():
    def __init__(self) -> None:
        # Dialog()
        # self.main()
        pass

    def main(self) -> None:
        # 创建显示窗体
        self.dialog_ui = QtWidgets.QDialog()
        self.dialog = Ui_Dialog()
        self.dialog.setupUi(self.dialog_ui)
        # self.dialog_ui.setWindowModality()
        self.dialog_ui.show()
        # 重置信息
        self.dialog.progressBar_all.setValue(0)
        self.dialog.tableWidget.setColumnWidth(0, 30)
        self.dialog.tableWidget.setColumnWidth(1, 55)
        self.dialog.tableWidget.setColumnWidth(2, 160)
        self.dialog.tableWidget.setColumnWidth(3, 43)
        # 获得视频链接
        vd = VideoDownload()
        Vinfo = vd.GetHttpVideoInfo(self.VDinfo)
        self.urls = vd.GetDownloadUrls(Vinfo)
        
        self.dialog.tableWidget.setRowCount(len(self.urls))

        # list1 = [["1","等待","https://www.cctv.com/aa/aaa/aa/bb","0"],
        #          ["2","完成","https://www.cctv.com/aa/aaa/aa/bb","100"],
        #          ["3","下载中","https://www.cctv.com/aa/aaa/aa/bb","36"]]
       
        # 生成信息列表
        num = 1
        self.info_list = []
        for i in self.urls:
            info_list = [
                str(num),
                "等待",
                i,
                "0"
            ]
            num += 1
            self.info_list.append(info_list)
        self.display(self.info_list)
        # 抽取thread_logo
        self.thread_logo_list = []
        for i in self.info_list:
            tmp = i[0]
            self.thread_logo_list.append(tmp)
        


    def display(self, info:list) -> None:
        '''将信息显示到表格中'''
        # i:
        # ["1/2/3...","{等待/下载中/完成}","{url}","{value}"]
        for i in info:
            item1 = QtWidgets.QTableWidgetItem(i[0])
            item2 = QtWidgets.QTableWidgetItem(i[1])
            item3 = QtWidgets.QTableWidgetItem(i[2])
            item4 = QtWidgets.QTableWidgetItem(i[3] + "%")
            self.dialog.tableWidget.setItem(int(i[0])-1, 0, item1)
            self.dialog.tableWidget.setItem(int(i[0])-1, 1, item2)
            self.dialog.tableWidget.setItem(int(i[0])-1, 2, item3)
            self.dialog.tableWidget.setItem(int(i[0])-1, 3, item4)
            self.dialog.tableWidget.viewport().update()


    def transfer_VideoInfo(self, info:any) -> None:
        '''传入视频信息'''
        self.VDinfo = info

class DownloadVideo(QThread, QObject):
    # 定义信号
    info = Signal(list)

    def __init__(self) -> None:
        super(DownloadVideo, self).__init__()
        # 检查路径
        path = "C:\\"
        if not os.path.exists("%s/ctvd_tmp"%path):
            os.makedirs("%s/ctvd_tmp"%path)


    def transfer(self, list:list) -> None:
        '''传入下载参数'''
        self.thread_logo = int(list[0])
        self.state = list[1]
        self.url = list[2]
        self.value = int(list[3])

    def run(self):
        Run = True
        # 主要下载逻辑
        while Run:
            response = requests.get(self.url, stream=False)
            chunk_size = 1024*1024
            size = 0
            self.state = "下载中"
            
            content_size = int(response.headers['content-length'])
            path = "C:\\"
            if response.status_code == 200:
                p = path + "ctvd_tmp/" + self.thread_logo + ".mp4"
                size = content_size/chunk_size/1024
                (size,content_size)
                with open(p, "wb") as f:
                    # f.write(response.content)
                    for data in response.iter_content(chunk_size=chunk_size):
                        f.write(data)
                        size += len(data)
                        # print(size)
                        value = size*100 / content_size
                        #print(int(value))
                        self.value = value
                        list2 = [
                            str(self.thread_logo),
                            self.state,
                            self.url,
                            str(self.value)
                                ]
                        self.info.emit(list2)

                self.state = "完成"        
                list2 = [
                            str(self.thread_logo),
                            self.state,
                            self.url,
                            str(self.value)
                                ]
                self.info.emit(list2)
                Run = False