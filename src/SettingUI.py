# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'ctvd_setting.ui'
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
from PySide6.QtWidgets import (QAbstractButton, QApplication, QDialog, QDialogButtonBox,
    QLabel, QLineEdit, QPushButton, QRadioButton,
    QSizePolicy, QSpinBox, QWidget)

class Ui_Dialog(object):
    def setupUi(self, Dialog):
        if not Dialog.objectName():
            Dialog.setObjectName(u"Dialog")
        Dialog.resize(170, 170)
        self.buttonBox = QDialogButtonBox(Dialog)
        self.buttonBox.setObjectName(u"buttonBox")
        self.buttonBox.setGeometry(QRect(10, 140, 150, 20))
        self.buttonBox.setOrientation(Qt.Horizontal)
        self.buttonBox.setStandardButtons(QDialogButtonBox.Cancel|QDialogButtonBox.Ok)
        self.label = QLabel(Dialog)
        self.label.setObjectName(u"label")
        self.label.setGeometry(QRect(10, 10, 150, 20))
        self.lineEdit_file_save_path = QLineEdit(Dialog)
        self.lineEdit_file_save_path.setObjectName(u"lineEdit_file_save_path")
        self.lineEdit_file_save_path.setGeometry(QRect(10, 30, 130, 20))
        self.pushButton_open = QPushButton(Dialog)
        self.pushButton_open.setObjectName(u"pushButton_open")
        self.pushButton_open.setGeometry(QRect(140, 30, 20, 20))
        self.label_2 = QLabel(Dialog)
        self.label_2.setObjectName(u"label_2")
        self.label_2.setGeometry(QRect(10, 60, 100, 20))
        self.spinBox = QSpinBox(Dialog)
        self.spinBox.setObjectName(u"spinBox")
        self.spinBox.setGeometry(QRect(110, 60, 50, 20))
        self.spinBox.setValue(1)
        self.radioButton_ts = QRadioButton(Dialog)
        self.radioButton_ts.setObjectName(u"radioButton_ts")
        self.radioButton_ts.setGeometry(QRect(10, 110, 70, 20))
        self.radioButton_mp4 = QRadioButton(Dialog)
        self.radioButton_mp4.setObjectName(u"radioButton_mp4")
        self.radioButton_mp4.setGeometry(QRect(90, 110, 70, 20))
        self.label_3 = QLabel(Dialog)
        self.label_3.setObjectName(u"label_3")
        self.label_3.setGeometry(QRect(10, 90, 150, 20))

        self.retranslateUi(Dialog)
        self.buttonBox.accepted.connect(Dialog.accept)
        self.buttonBox.rejected.connect(Dialog.reject)

        QMetaObject.connectSlotsByName(Dialog)
    # setupUi

    def retranslateUi(self, Dialog):
        Dialog.setWindowTitle(QCoreApplication.translate("Dialog", u"\u8bbe\u7f6e", None))
        self.label.setText(QCoreApplication.translate("Dialog", u"\u6587\u4ef6\u4fdd\u5b58\u4f4d\u7f6e:", None))
        self.pushButton_open.setText(QCoreApplication.translate("Dialog", u"...", None))
        self.label_2.setText(QCoreApplication.translate("Dialog", u"\u4e0b\u8f7d\u4f7f\u7528\u7ebf\u7a0b\u6570:", None))
        self.radioButton_ts.setText(QCoreApplication.translate("Dialog", u"\u4e0d\u8f6c\u7801", None))
        self.radioButton_mp4.setText(QCoreApplication.translate("Dialog", u"\u8f6c\u7801MP4", None))
        self.label_3.setText(QCoreApplication.translate("Dialog", u"\u89c6\u9891\u4e0b\u8f7d\u5b8c\u6210\u540e\u662f\u5426\u8f6c\u7801?", None))
    # retranslateUi

