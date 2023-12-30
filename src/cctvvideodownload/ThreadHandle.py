from typing import Optional
import PySide6.QtCore
import requests,os
from PySide6 import QtCore,QtWidgets
from PySide6.QtCore import QObject
from PySide6.QtCore import Signal,QRunnable,QThreadPool,QThread

from cctvvideodownload.DialogUI import Ui_Dialog

class Dialog()