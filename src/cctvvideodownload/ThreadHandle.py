import os
import requests
import concurrent.futures
from PySide6 import QtWidgets
from PySide6.QtCore import QObject
from PySide6.QtCore import Signal, QThread

from cctvvideodownload.DialogUI import Ui_Dialog
from cctvvideodownload.DlHandle import VideoDownload


class ThreadHandle(QObject):
    # 创建信号
    download_finish = Signal(bool)

    def __init__(self) -> None:
        super(ThreadHandle, self).__init__()
        # 初始化一些属性
        self.work_finish_flags = []
        self.workers = []
        self.Threading_num = 3
        self.task_list = []
        self.task_list_names = []
        self.task_list_values = []



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
        self.dialog.tableWidget.setColumnWidth(2, 155)
        self.dialog.tableWidget.setColumnWidth(3, 48)
        # 获得视频链接
        vd = VideoDownload()
        Vinfo = vd.GetHttpVideoInfo(self.VDinfo)
        # self.urls = vd.GetDownloadUrls(Vinfo)
        self.urls = vd.GetM3U8Urls(Vinfo)

        
        self.dialog.tableWidget.setRowCount(len(self.urls))

        # list1 = [["1","等待","https://www.cctv.com/aa/aaa/aa/bb","0"],
        #          ["2","完成","https://www.cctv.com/aa/aaa/aa/bb","100"],
        #          ["3","下载中","https://www.cctv.com/aa/aaa/aa/bb","36"]]
       
        # 生成信息列表
        num = 1
        self.info_list = []
        for i in self.urls:
            info = [str(num), "等待", i, "0"]
            num += 1
            self.info_list.append(info)

        self.display(self.info_list)

        self.task_list = self.split_list(self.info_list, self.Threading_num)
        self.all_value = 0
        self.finish_value = len(self.info_list) * 100
        # 多线程
        # self.start_download(self.info_list, self.Threading_num)
        # 暂时不再使用多线程
        self.worker = DownloadVideo()
        self.worker.info.connect(self.callback)
        self.worker.finished.connect(self._new_work)
        self._new_work()

    def _new_work(self) -> None:
        '''派发新任务'''
        if len(self.info_list) != 0:
            self.worker.transfer(self.info_list[0])
            del self.info_list[0]
            self.worker.start()
        else:
            self.dialog_ui.close() # 关闭窗体
            self.download_finish.emit(True) # 发送信号
        
        
    def start_download(self, task_list, threading_num):
        self.task_list = task_list
        self.Threading_num = threading_num

        self.work_finish_flags = [False] * self.Threading_num

        with concurrent.futures.ThreadPoolExecutor(max_workers=self.Threading_num) as executor:
            for index in range(self.Threading_num):
                worker = DownloadVideo()
                worker.info.connect(self.callback)
                future = executor.submit(self._download_task, worker, index)
                future.add_done_callback(lambda:self._on_worker_finish(index))

                self.workers.append(worker)

    def _download_task(self, worker, index):
        for task in self.task_list[index::self.Threading_num]:
            worker.transfer(task)
            worker.start()
            del self.task_list[self.task_list.index(task)]

    def _on_worker_finish(self, index):
        self.work_finish_flags[index] = True
        if all(self.work_finish_flags):
            self._is_finished()

    def _is_finished(self):
        # Perform actions after all workers finish
        self.dialog_ui.close() # 关闭窗体
        self.download_finish.emit(True) # 发送信号

    #     # 生成状态列表
    #     for i in range(self.Threading_num):
    #         self.work_finish_flags.append(False)

    #     # 均分任务
    #     # self.work1_list, self.work2_list, self.work3_list = self.split_list(self.info_list, 3)
    #     self.task_list = self.split_list(self.info_list, self.Threading_num)
    #     # 通过循环创建属性名称和值
    #     self.task_list_names = []
    #     for i in range(self.Threading_num):
    #         attr_name = "task%s_list" %str(i + 1)
    #         self.task_list_names.append(attr_name)
    #         self.task_list_values.append(self.task_list[i])
    #     # 将其添加为属性
    #     for attr_name, attr_value in zip(self.task_list_names, self.task_list_values):
    #         setattr(self, attr_name, attr_value)
        
    #     # 多线程下载
    #     for index in range(self.Threading_num):
    #         worker = DownloadVideo()
    #         # 设置回调
    #         worker.info.connect(self.callback)
    #         # worker.finished.connect(getattr(self, f"new_worker{index+1}"))
    #         worker.finished.connect(lambda idx=index + 1: self.new_work(idx)) # 此处采用lambda表达式以在完成后执行new_work
    #         self.workers.append(worker)
    #         print(self.workers)
    #         setattr(self, f"worker{index + 1}", worker)
    #         # worker.start_download(getattr(self, f"task{index+1}_list"))
    #         # 循环结束后再启动所有的 DownloadVideo 对象
    #     for index in range(self.Threading_num):
    #         self.new_work(index + 1)

    #     # 调用一次方法，开始线程
    #     for worker in self.workers:
    #         worker.start()
    #     # 初始化总进度及状态
    #     self.all_value = 0
    #     self.finish_value = len(self.info_list) * 100
    #     # self.work1_finish = False
    #     # self.work2_finish = False
    #     # self.work3_finish = False
        
    # def new_work(self, index:int) -> None:
    #     '''创建新任务'''
    #     task_list = getattr(self, f"task{index}_list")
    #     if len(task_list) != 0:
    #         index2 = int(task_list[0][0])
    #         print(task_list)
    #         print(self.workers)
    #         print(len(self.workers))
    #         self.workers[index].transfer(task_list[0])
    #         del task_list[0]
    #         self.workers[index].start()
    #     else:
    #         self.work_finish_flags[index] = True
    #         self.is_finish()
        
    # def is_finish(self) -> None:
    #     '''下载完成后执行方法'''
    #     if all(self.work_finish_flags):
    #         self.dialog_ui.close() # 关闭窗体
    #         self.download_finish.emit(True) # 发送信号


    def split_list(self, lst, n) -> list:
        '''列表分割方法'''
        avg = len(lst) / float(n)
        result = []
        last = 0.0
        while last < len(lst):
            result.append(lst[int(last):int(last + avg)])
            last += avg
        return result
    
    def update_all_value(self, value:float) -> None:
        '''更新下载进度到进度条'''
        self.all_value = self.all_value + value
        # print(self.all_value)
        # print(self.finish_value)
        # 计算下载进度
        self.process_value = int((self.all_value / self.finish_value) * 6.4)
        # print(self.process_value)
        self.dialog.progressBar_all.setValue(self.process_value)

    def callback(self, info) -> None:
        '''回调，更新下载信息到表格'''
        item1 = QtWidgets.QTableWidgetItem(info[0])
        item2 = QtWidgets.QTableWidgetItem(info[1])
        item3 = QtWidgets.QTableWidgetItem(info[2])
        item4 = QtWidgets.QTableWidgetItem(str(int(float(info[3]))) + "%")
        self.dialog.tableWidget.setItem(int(info[0])-1, 0, item1)
        self.dialog.tableWidget.setItem(int(info[0])-1, 1, item2)
        self.dialog.tableWidget.setItem(int(info[0])-1, 2, item3)
        self.dialog.tableWidget.setItem(int(info[0])-1, 3, item4)
        self.dialog.tableWidget.viewport().update()
        # 调用更新进度条
        self.update_all_value(float(info[3]))




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


    # def transfer_VideoInfo(self, info:any) -> None:
    #     '''传入视频信息'''
    #     self.VDinfo = info
        
    def transfer(self, VideoInfo:list, Threading_num:int) -> None:
        '''传参方法
        VideoInfo:下载相关信息
        Threading_num:使用的线程数'''
        self.VDinfo = VideoInfo
        self.Threading_num = Threading_num

class DownloadVideo(QThread):
    info = Signal(list)

    def __init__(self):
        super().__init__()
        self.thread_logo = None
        self.state = None
        self.url = None
        self.value = 0

        path = "C:\\"
        if not os.path.exists(f"{path}\\ctvd_tmp"):
            os.makedirs(f"{path}\\ctvd_tmp")

    def transfer(self, data_list):
        self.thread_logo = int(data_list[0])
        self.state = data_list[1]
        self.url = data_list[2]
        self.value = int(data_list[3])

    def run(self):
        response = requests.get(self.url, stream=True)
        chunk_size = 1024 * 1024
        size = 0
        self.state = "下载中"
        
        try:
            content_size = int(response.headers['content-length'])
            path = "C:\\"
            if response.status_code == 200:
                file_path = f"{path}\\ctvd_tmp\\{self.thread_logo}.ts"
                with open(file_path, "wb") as f:
                    for data in response.iter_content(chunk_size=chunk_size):
                        f.write(data)
                        size += len(data)
                        value = size * 100 / content_size
                        self.value = value
                        data_list = [str(self.thread_logo), self.state, self.url, str(self.value)]
                        self.info.emit(data_list)

            self.state = "完成"
            data_list = [str(self.thread_logo), self.state, self.url, str(self.value)]
            self.info.emit(data_list)
        except Exception as e:
            # 异常向上抛出，交由上层类处理
            raise

            
class ConcatThread(QThread, QObject):
    # 创建信号
    concat_finish = Signal(str)

    def __init__(self, name):
        super().__init__()
        self.name = name
        

    def run(self):
        Run = True
        while Run:
            import os, shutil, re
            path = "C:\\"
            # 获取文件列表
            file_list = os.listdir("%s/ctvd_tmp" % path)
            # print(files)
            # ['1.mp4', '10.mp4', '11.mp4', '12.mp4', '13.mp4', '14.mp4', '2.mp4', '3.mp4', '4.mp4', '5.mp4', '6.mp4', '7.mp4', '8.mp4', '9.mp4', 'video.txt']
            # 对列表进行排序,lambda提取数字
            # print(file_list)
            files = []
            for i in file_list:
                if re.match(r"\d+.mp4", i):
                    files.append(i)
            files = sorted(files, key=lambda x: int(x.split('.')[0]))
            # print(files)
            # 生成合并列表文件
            with open("%s/ctvd_tmp/video.txt" %path, "w+") as f:
                for i in files:
                    if re.match(r"\d+.mp4", i):
                        tmp = "file '" + i + "'\n"
                        f.write(tmp)

            # 合并
            os.system("cd C:\\ctvd_tmp && ffmpeg -f concat -i video.txt -c copy concat.mp4")
            # os.system("cd C:\\ctvd_tmp && ffmpeg -f concat -i video.txt -c copy concat.mp4")
            print("os")
            if not os.path.exists("C:/Video"):
                os.makedirs("C:/Video")
            
            shutil.move("C:/ctvd_tmp/concat.mp4", "C:/Video/%s.mp4" % self.name)
            shutil.rmtree("C:/ctvd_tmp")
            Run = False
            self.concat_finish.emit(self.name)