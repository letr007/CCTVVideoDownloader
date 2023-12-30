# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'ctvd.ui'
##
## Created by: Qt User Interface Compiler version 6.6.0
##
## WARNING! All changes made in this file will be lost when recompiling UI file!
################################################################################

from PySide6.QtCore import (QCoreApplication, QDate, QDateTime, QLocale,
    QMetaObject, QObject, QPoint, QRect,
    QSize, QTime, QUrl, Qt)
from PySide6.QtGui import (QAction, QBrush, QColor, QConicalGradient,
    QCursor, QFont, QFontDatabase, QGradient,
    QIcon, QImage, QKeySequence, QLinearGradient,
    QPainter, QPalette, QPixmap, QRadialGradient,
    QTransform)
from PySide6.QtWidgets import (QApplication, QFrame, QHeaderView, QLabel,
    QMainWindow, QMenu, QMenuBar, QPushButton,
    QSizePolicy, QTableWidget, QTableWidgetItem, QTextBrowser,
    QWidget)

class Ui_MainWindow(object):
    def setupUi(self, MainWindow):
        if not MainWindow.objectName():
            MainWindow.setObjectName(u"MainWindow")
        MainWindow.resize(500, 500)
        MainWindow.setMinimumSize(QSize(500, 500))
        MainWindow.setMaximumSize(QSize(500, 500))
        self.actionfile = QAction(MainWindow)
        self.actionfile.setObjectName(u"actionfile")
        self.actionexit = QAction(MainWindow)
        self.actionexit.setObjectName(u"actionexit")
        self.actionin = QAction(MainWindow)
        self.actionin.setObjectName(u"actionin")
        self.actionout = QAction(MainWindow)
        self.actionout.setObjectName(u"actionout")
        self.centralwidget = QWidget(MainWindow)
        self.centralwidget.setObjectName(u"centralwidget")
        self.textBrowser = QTextBrowser(self.centralwidget)
        self.textBrowser.setObjectName(u"textBrowser")
        self.textBrowser.setGeometry(QRect(259, 259, 221, 201))
        self.line_2 = QFrame(self.centralwidget)
        self.line_2.setObjectName(u"line_2")
        self.line_2.setGeometry(QRect(240, 0, 3, 500))
        self.line_2.setFrameShape(QFrame.VLine)
        self.line_2.setFrameShadow(QFrame.Sunken)
        self.line_3 = QFrame(self.centralwidget)
        self.line_3.setObjectName(u"line_3")
        self.line_3.setGeometry(QRect(250, 240, 240, 3))
        self.line_3.setFrameShape(QFrame.HLine)
        self.line_3.setFrameShadow(QFrame.Sunken)
        self.pushButton_FlashConfig = QPushButton(self.centralwidget)
        self.pushButton_FlashConfig.setObjectName(u"pushButton_FlashConfig")
        self.pushButton_FlashConfig.setGeometry(QRect(270, 20, 200, 60))
        icon = QIcon()
        icon.addFile(u":/flash.png", QSize(), QIcon.Normal, QIcon.Off)
        self.pushButton_FlashConfig.setIcon(icon)
        self.pushButton_FlashList = QPushButton(self.centralwidget)
        self.pushButton_FlashList.setObjectName(u"pushButton_FlashList")
        self.pushButton_FlashList.setGeometry(QRect(270, 90, 200, 60))
        self.pushButton_FlashList.setIcon(icon)
        self.pushButton_Download = QPushButton(self.centralwidget)
        self.pushButton_Download.setObjectName(u"pushButton_Download")
        self.pushButton_Download.setGeometry(QRect(270, 160, 200, 60))
        icon1 = QIcon()
        icon1.addFile(u":/download.png", QSize(), QIcon.Normal, QIcon.Off)
        self.pushButton_Download.setIcon(icon1)
        self.line = QFrame(self.centralwidget)
        self.line.setObjectName(u"line")
        self.line.setGeometry(QRect(11, 239, 219, 3))
        self.line.setFrameShape(QFrame.HLine)
        self.line.setFrameShadow(QFrame.Sunken)
        self.label_2 = QLabel(self.centralwidget)
        self.label_2.setObjectName(u"label_2")
        self.label_2.setGeometry(QRect(11, 248, 48, 16))
        self.label = QLabel(self.centralwidget)
        self.label.setObjectName(u"label")
        self.label.setGeometry(QRect(11, 11, 48, 16))
        self.tableWidget_Config = QTableWidget(self.centralwidget)
        if (self.tableWidget_Config.columnCount() < 2):
            self.tableWidget_Config.setColumnCount(2)
        __qtablewidgetitem = QTableWidgetItem()
        self.tableWidget_Config.setHorizontalHeaderItem(0, __qtablewidgetitem)
        __qtablewidgetitem1 = QTableWidgetItem()
        self.tableWidget_Config.setHorizontalHeaderItem(1, __qtablewidgetitem1)
        self.tableWidget_Config.setObjectName(u"tableWidget_Config")
        self.tableWidget_Config.setGeometry(QRect(11, 32, 219, 201))
        self.tableWidget_List = QTableWidget(self.centralwidget)
        if (self.tableWidget_List.columnCount() < 1):
            self.tableWidget_List.setColumnCount(1)
        __qtablewidgetitem2 = QTableWidgetItem()
        self.tableWidget_List.setHorizontalHeaderItem(0, __qtablewidgetitem2)
        self.tableWidget_List.setObjectName(u"tableWidget_List")
        self.tableWidget_List.setGeometry(QRect(11, 269, 219, 201))
        MainWindow.setCentralWidget(self.centralwidget)
        self.menubar = QMenuBar(MainWindow)
        self.menubar.setObjectName(u"menubar")
        self.menubar.setGeometry(QRect(0, 0, 500, 21))
        self.menu = QMenu(self.menubar)
        self.menu.setObjectName(u"menu")
        self.menu_2 = QMenu(self.menubar)
        self.menu_2.setObjectName(u"menu_2")
        MainWindow.setMenuBar(self.menubar)

        self.menubar.addAction(self.menu.menuAction())
        self.menubar.addAction(self.menu_2.menuAction())
        self.menu.addAction(self.actionfile)
        self.menu.addSeparator()
        self.menu.addAction(self.actionexit)
        self.menu_2.addAction(self.actionin)
        self.menu_2.addAction(self.actionout)

        self.retranslateUi(MainWindow)

        QMetaObject.connectSlotsByName(MainWindow)
    # setupUi

    def retranslateUi(self, MainWindow):
        MainWindow.setWindowTitle(QCoreApplication.translate("MainWindow", u"MainWindow", None))
        self.actionfile.setText(QCoreApplication.translate("MainWindow", u"\u6253\u5f00\u6587\u4ef6\u4f4d\u7f6e", None))
        self.actionexit.setText(QCoreApplication.translate("MainWindow", u"\u9000\u51fa", None))
        self.actionin.setText(QCoreApplication.translate("MainWindow", u"\u5bfc\u5165\u914d\u7f6e", None))
        self.actionout.setText(QCoreApplication.translate("MainWindow", u"\u5bfc\u51fa\u914d\u7f6e", None))
        self.pushButton_FlashConfig.setText(QCoreApplication.translate("MainWindow", u"\u91cd\u8f7d\u914d\u7f6e", None))
        self.pushButton_FlashList.setText(QCoreApplication.translate("MainWindow", u"\u5237\u65b0\u5217\u8868", None))
        self.pushButton_Download.setText(QCoreApplication.translate("MainWindow", u"\u4e0b\u8f7d\u89c6\u9891", None))
        self.label_2.setText(QCoreApplication.translate("MainWindow", u"\u89c6\u9891\u5217\u8868", None))
        self.label.setText(QCoreApplication.translate("MainWindow", u"\u914d\u7f6e\u4fe1\u606f", None))
        ___qtablewidgetitem = self.tableWidget_Config.horizontalHeaderItem(0)
        ___qtablewidgetitem.setText(QCoreApplication.translate("MainWindow", u"\u540d\u79f0", None));
        ___qtablewidgetitem1 = self.tableWidget_Config.horizontalHeaderItem(1)
        ___qtablewidgetitem1.setText(QCoreApplication.translate("MainWindow", u"ID", None));
        ___qtablewidgetitem2 = self.tableWidget_List.horizontalHeaderItem(0)
        ___qtablewidgetitem2.setText(QCoreApplication.translate("MainWindow", u"\u540d\u79f0", None));
        self.menu.setTitle(QCoreApplication.translate("MainWindow", u"\u7a0b\u5e8f", None))
        self.menu_2.setTitle(QCoreApplication.translate("MainWindow", u"\u914d\u7f6e", None))
    # retranslateUi

