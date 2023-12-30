# @author letr
import os, shutil
import requests
import re


class XwzkDownload():
    def __init__(self):
        pass

    def getVideoList(self):
        '''
        调用getVideoListByColumn?idAPI获取节目列表信息
        并对返回数据处理
        :return:
        '''
        api = "https://api.cntv.cn/NewVideo/getVideoListByColumn?id=TOPC1451559180488841&n=20&sort=desc&p=1&mode=0&serviceId=tvcctv"
        resp = requests.get(api)
        # print(resp.status_code)
        # print(json.dumps(resp.json(), indent=4))
        # 对返回的数据进行处理
        recv = resp.json()
        v_list = recv["data"]["list"]
        l_info = []
        l_index = []
        num = 0
        for i in v_list:
            title = i["title"]
            guid = i["guid"]
            l_1 = [title, guid]
            l_info.append(l_1)
            num += 1
            l_index.append(num)
        dict1 = dict(zip(l_index, l_info))
        # print(dict1)
        return dict1

    def getHttpVideoInfo(self, pid):
        '''
        调取getHttpVideoInfo?doAPI获取视频源链接
        :param pid:
        :return VideoInfo:
        '''
        ghv_url = "https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do?" + "pid=" + pid
        ghv_resp = requests.get(ghv_url)
        # print(ghv_resp.status_code)
        # print(ghv_resp.json())
        VideoInfo = ghv_resp.json()
        return VideoInfo

    def getDownloadUrls(self, VideoInfo):
        '''
        获取视频源下载链接
        返回一个包含链接的列表
        :param VideoInfo:
        :return urls:
        '''
        chapters = VideoInfo["video"]["chapters4"]
        # print(json.dumps(chapters, indent=4))
        urls = []
        for i in chapters:
            url = i["url"]
            urls.append(url)
        # print(urls)
        return urls

    def DownloadVideo(self, urls):
        if not os.path.exists("./xwzk_tmp"):
            # print("创建临时目录...")
            os.makedirs("./xwzk_tmp")
        n = 0
        for i in urls:
            n += 1
            self.download_index = n
            # print("正在下载第%s个视频文件..." % n, end="")
            recv = requests.get(i)
            if recv.status_code == 200:
                if n <= 9:
                    n = "0" + str(n)
                with open("./xwzk_tmp/%s.mp4" % n, "wb") as f:
                    f.write(recv.content)
                    n = int(n)
                    # print("done")
            else:
                # print("返回错误")
                break

    def VideoConcat(self):
        files = os.listdir("./xwzk_tmp")
        files.sort()
        # print(files)
        # print("创建内容文件...")
        with open("./xwzk_tmp/video.txt", "w+") as f:
            for i in files:
                if re.match(r"\d+.mp4", i):
                    tmp = "file '" + i + "'\n"
                    # print(tmp)
                    f.write(tmp)
        # print("准备合并...")
        try:
            os.system("cd xwzk_tmp && ffmpeg -f concat -i video.txt -c copy concat.mp4")
            # print("合并完成")
        except Exception as e:
            print(e)
        # print("复制文件...")
        if not os.path.exists("./Video"):
            os.makedirs("./Video")
        try:
            shutil.move("./xwzk_tmp/concat.mp4", "./Video/新闻周刊.mp4")
        except Exception as e:
            pass
            # print(e)
        # print("清理...")
        shutil.rmtree("./xwzk_tmp")


if __name__ == "__main__":
    main = XwzkDownload()
    dict1 = main.getVideoList()
    print("Copyright ©letr")
    print("获取视频列表...")
    print("-" * 50)
    num = 0
    for i in dict1:
        num += 1
        print(num, ") ", end="")
        print(dict1[i][0])
    print("-" * 50)
    a = input("请选择要下载的期数:")
    print("你的选择是：", a)
    print("-" * 50)
    print("获取节目链接...")
    info = main.getHttpVideoInfo(dict1[int(a)][1])
    # print(json.dumps(info, indent=4))
    urls = main.getDownloadUrls(info)
    main.DownloadVideo(urls)
    main.VideoConcat()
