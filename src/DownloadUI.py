# -*- coding: utf-8 -*-

# Form implementation generated from reading ui file 'dialog.ui'
#
# Created by: PyQt5 UI code generator 5.15.9
#
# WARNING: Any manual changes made to this file will be lost when pyuic5 is
# run again.  Do not edit this file unless you know what you are doing.


from PyQt5 import QtCore, QtGui, QtWidgets


class Ui_Dialog(object):
    def setupUi(self, Dialog):
        Dialog.setObjectName("Dialog")
        Dialog.resize(350, 250)
        Dialog.setMinimumSize(QtCore.QSize(350, 250))
        Dialog.setMaximumSize(QtCore.QSize(350, 250))
        self.label_7 = QtWidgets.QLabel(Dialog)
        self.label_7.setGeometry(QtCore.QRect(10, 10, 51, 21))
        self.label_7.setObjectName("label_7")
        self.progressBar_all = QtWidgets.QProgressBar(Dialog)
        self.progressBar_all.setGeometry(QtCore.QRect(60, 10, 280, 20))
        self.progressBar_all.setProperty("value", 24)
        self.progressBar_all.setObjectName("progressBar_all")
        self.tableWidget = QtWidgets.QTableWidget(Dialog)
        self.tableWidget.setGeometry(QtCore.QRect(10, 40, 330, 200))
        self.tableWidget.setObjectName("tableWidget")
        self.tableWidget.setColumnCount(4)
        self.tableWidget.setRowCount(0)
        item = QtWidgets.QTableWidgetItem()
        self.tableWidget.setHorizontalHeaderItem(0, item)
        item = QtWidgets.QTableWidgetItem()
        self.tableWidget.setHorizontalHeaderItem(1, item)
        item = QtWidgets.QTableWidgetItem()
        self.tableWidget.setHorizontalHeaderItem(2, item)
        item = QtWidgets.QTableWidgetItem()
        self.tableWidget.setHorizontalHeaderItem(3, item)

        self.retranslateUi(Dialog)
        QtCore.QMetaObject.connectSlotsByName(Dialog)

    def retranslateUi(self, Dialog):
        _translate = QtCore.QCoreApplication.translate
        Dialog.setWindowTitle(_translate("Dialog", "下载..."))
        self.label_7.setText(_translate("Dialog", "总进度:"))
        item = self.tableWidget.horizontalHeaderItem(0)
        item.setText(_translate("Dialog", "任务"))
        item = self.tableWidget.horizontalHeaderItem(1)
        item.setText(_translate("Dialog", "状态"))
        item = self.tableWidget.horizontalHeaderItem(2)
        item.setText(_translate("Dialog", "链接"))
        item = self.tableWidget.horizontalHeaderItem(3)
        item.setText(_translate("Dialog", "进度"))

if __name__ == "__main__":
    # DEBUG
    import sys
    from qt_material import apply_stylesheet
    app = QtWidgets.QApplication(sys.argv)
    apply_stylesheet(app, theme='dark_blue.xml')
    Dialog = QtWidgets.QDialog()
    ui = Ui_Dialog()
    ui.setupUi(Dialog)
    Dialog.show()
    sys.exit(app.exec_())
