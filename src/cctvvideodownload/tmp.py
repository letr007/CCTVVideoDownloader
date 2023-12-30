def split_list(lst, n):
    avg = len(lst) / float(n)
    result = []
    last = 0.0
    while last < len(lst):
        result.append(lst[int(last):int(last + avg)])
        last += avg
    return result

my_list = [['1', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-1.mp4', '0'], ['2', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-2.mp4', '0'], ['3', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-3.mp4', '0'], ['4', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-4.mp4', '0'], ['5', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-5.mp4', '0'], ['6', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-6.mp4', '0'], ['7', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-7.mp4', '0'], ['8', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-8.mp4', '0'], ['9', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-9.mp4', '0'], ['10', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-10.mp4', '0'], ['11', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-11.mp4', '0'], ['12', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-12.mp4', '0'], ['13', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-13.mp4', '0'], ['14', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-14.mp4', '0'], ['15', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-15.mp4', '0'], ['16', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-16.mp4', '0'], ['17', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-17.mp4', '0'], ['18', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-18.mp4', '0'], ['19', '等待', 'https://vod.cntv.myhwcdn.cn/flash/mp4video63/TMS/2023/12/23/ca5e790ff3044539877a90816122d069_h2642000000nero_aac16-19.mp4', '0']]
split_result = split_list(my_list, 3)
print(split_result[0])
print(split_result[1])
print(split_result[2])

l2 = []
if len(l2) == 0:
    print("a")
