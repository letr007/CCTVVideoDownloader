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
        pass

    def main(self) -> None:
        # 创建显示窗体
        self.dialog = QtWidgets.QDialog()
        self.dialog_ui = Ui_Dialog()
        self.dialog_ui.setupUi(self.dialog)
        self.dialog.show()
        # 重置信息
        self.dialog_ui.progressBar_all.setValue(0)
        self.dialog_ui.tableWidget.setColumnWidth(0, 30)
        self.dialog_ui.tableWidget.setColumnWidth(1, 55)
        self.dialog_ui.tableWidget.setColumnWidth(2, 200)
        self.dialog_ui.tableWidget.setColumnWidth(3, 43)
        # 获得视频链接
        vd = VideoDownload()
        Vinfo = vd.GetHttpVideoInfo(self.VDinfo)
        urls = vd.GetDownloadUrls(Vinfo)

    def transfer_VideoInfo(self, info:any) -> None:
        '''传入视频信息'''
        self.VDinfo = info

class Dialog():
    def __init__(self) -> None:
        self.main()

    def main(self) -> None:
        # 创建显示窗体
        self.dialog = QtWidgets.QDialog()
        self.dialog_ui = Ui_Dialog()
        self.dialog_ui.setupUi(self.dialog)
        self.dialog.show()
        # 重置信息
        self.dialog_ui.progressBar_all.setValue(0)
        self.dialog_ui.tableWidget.setColumnWidth(0, 30)
        self.dialog_ui.tableWidget.setColumnWidth(1, 55)
        self.dialog_ui.tableWidget.setColumnWidth(2, 200)
        self.dialog_ui.tableWidget.setColumnWidth(3, 43)
        # 获得视频链接
        vd = VideoDownload()
        Vinfo = vd.GetHttpVideoInfo(self.dict1[self.dl_index][1])
        urls = vd.GetDownloadUrls(Vinfo)