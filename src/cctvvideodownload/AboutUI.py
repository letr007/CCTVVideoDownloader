# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'about.ui'
##
## Created by: Qt User Interface Compiler version 6.6.0
##
## WARNING! All changes made in this file will be lost when recompiling UI file!
################################################################################

from PySide6.QtCore import (QCoreApplication, QDate, QDateTime, QLocale,
    QMetaObject, QObject, QPoint, QRect,
    QSize, QTime, QUrl, Qt)
from PySide6.QtGui import (QBrush, QColor, QConicalGradient, QCursor,
    QFont, QFontDatabase, QGradient, QIcon,
    QImage, QKeySequence, QLinearGradient, QPainter,
    QPalette, QPixmap, QRadialGradient, QTransform)
from PySide6.QtWidgets import (QApplication, QDialog, QLabel, QSizePolicy,
    QWidget)

class Ui_Dialog(object):
    def setupUi(self, Dialog):
        if not Dialog.objectName():
            Dialog.setObjectName(u"Dialog")
        Dialog.resize(200, 170)
        self.label_img = QLabel(Dialog)
        self.label_img.setObjectName(u"label_img")
        self.label_img.setGeometry(QRect(90, 10, 100, 100))
        self.label_4 = QLabel(Dialog)
        self.label_4.setObjectName(u"label_4")
        self.label_4.setGeometry(QRect(10, 110, 70, 20))
        self.label_4.setAlignment(Qt.AlignCenter)
        self.label_2 = QLabel(Dialog)
        self.label_2.setObjectName(u"label_2")
        self.label_2.setGeometry(QRect(10, 10, 70, 15))
        font = QFont()
        font.setPointSize(10)
        self.label_2.setFont(font)
        self.label_2.setLayoutDirection(Qt.LeftToRight)
        self.label_2.setTextFormat(Qt.RichText)
        self.label_2.setAlignment(Qt.AlignLeading|Qt.AlignLeft|Qt.AlignVCenter)
        self.label_3 = QLabel(Dialog)
        self.label_3.setObjectName(u"label_3")
        self.label_3.setGeometry(QRect(10, 35, 70, 70))
        font1 = QFont()
        font1.setPointSize(9)
        self.label_3.setFont(font1)
        self.label_3.setTextFormat(Qt.RichText)
        self.label_3.setAlignment(Qt.AlignLeading|Qt.AlignLeft|Qt.AlignTop)
        self.label_3.setWordWrap(True)
        self.label_link = QLabel(Dialog)
        self.label_link.setObjectName(u"label_link")
        self.label_link.setGeometry(QRect(10, 130, 180, 30))
        self.label_link.setTextFormat(Qt.RichText)
        self.label_link.setAlignment(Qt.AlignCenter)
        self.label_link.setWordWrap(True)

        self.retranslateUi(Dialog)

        QMetaObject.connectSlotsByName(Dialog)
    # setupUi

    def retranslateUi(self, Dialog):
        Dialog.setWindowTitle(QCoreApplication.translate("Dialog", u"\u5173\u4e8e", None))
        self.label_img.setText(QCoreApplication.translate("Dialog", u"TextLabel", None))
        self.label_4.setText(QCoreApplication.translate("Dialog", u"<html><head/><body><p><span style=\" font-size:12pt; font-weight:700;\">\u9879\u76ee\u5730\u5740</span></p></body></html>", None))
        self.label_2.setText(QCoreApplication.translate("Dialog", u"<html><head/><body><p><span style=\" font-weight:700;\">\u4f5c\u8005:</span>letr</p></body></html>", None))
        self.label_3.setText(QCoreApplication.translate("Dialog", u"<html><head/><body><p>\u903b\u8f91\u6df7\u4e71</p><p>\u4ee3\u7801\u7a00\u70c2</p><p>\u5927\u4f6c\u8f7b\u55b7</p></body></html>", None))
        self.label_link.setText(QCoreApplication.translate("Dialog", u"<html><head/><body><p><a href=\"https://github.com/letr007/CCTVVideoDownload\"><span style=\" font-weight:700; text-decoration: underline; color:#555555;\">https://github.com/letr007/CCTVVideoDownload</span></a></p></body></html>", None))
    # retranslateUi

