import sys
from PyQt5.QtWidgets import QApplication, QDialog, QVBoxLayout, QLabel
from PyQt5.QtCore import Qt

class FramelessDialog(QDialog):
    def __init__(self):
        super().__init__()
        
        # 设置无边框
        self.setWindowFlags(Qt.FramelessWindowHint)
        
        # 设置对话框内容
        layout = QVBoxLayout()
        label = QLabel("这是一个无边框的对话框")
        layout.addWidget(label)
        
        self.setLayout(layout)
        self.setFixedSize(300, 200)  # 可选：设置固定大小

if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    # 保持对话框的引用
    dialog = FramelessDialog()
    dialog.show()
    
    sys.exit(app.exec_())
