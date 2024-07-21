import sys
from PyQt5.QtWidgets import QApplication, QPushButton, QMainWindow
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("Fluent按钮")
        self.setGeometry(100, 100, 300, 200)

        button = QPushButton("Fluent按钮", self)
        button.setGeometry(50, 50, 200, 50)

        # 应用 Fluent 风格的 QSS
        button.setStyleSheet("""
            QPushButton {
                background-color: #0078D7;
                color: white;
                border: none;
                border-radius: 5px;
                padding: 10px;
                font-size: 16px;
            }
            QPushButton:hover {
                background-color: #005A9E;
            }
            QPushButton:pressed {
                background-color: #004578;
            }
        """)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    font = QFont("Microsoft Yahei", 10)
    app.setFont(font)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())
