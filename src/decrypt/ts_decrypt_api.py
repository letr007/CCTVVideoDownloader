import os
import sys
import subprocess
import tempfile
from typing import List, Dict
import shutil
import logging

class TSDecryptor:
    def __init__(self, js_script_path: str = None):
        """
        初始化解密器
        :param js_script_path: ts_decrypt.js 的路径，如果为None则假设在同目录下
        """
        self._logger = logging.getLogger(__name__)
        self.js_script_path = js_script_path or os.path.join(os.path.dirname(__file__), 'ts_decrypt.js')
        if not os.path.exists(self.js_script_path):
            raise FileNotFoundError(f"找不到解密脚本: {self.js_script_path}")

    def decrypt_files(self, file_list: List[str], output_dir: str = None) -> Dict[str, str]:
        """
        解密多个TS文件
        :param file_list: TS文件路径列表 (可以是相对路径或绝对路径)
        :param output_dir: 输出目录，如果为None则使用临时目录
        :return: 字典，键为输入文件路径，值为解密后文件路径
        """
        # 将所有输入路径转换为绝对路径
        abs_file_list = [os.path.abspath(f) for f in file_list]
        
        # 验证输入文件是否存在
        for file_path in abs_file_list:
            if not os.path.exists(file_path):
                raise FileNotFoundError(f"输入文件不存在: {file_path}")

        # 处理输出目录
        if output_dir is None:
            output_dir = tempfile.mkdtemp()
        else:
            output_dir = os.path.abspath(output_dir)
            os.makedirs(output_dir, exist_ok=True)

        try:
            # 获取js脚本的绝对路径
            js_script_abs_path = os.path.abspath(self.js_script_path)
            
            # 构建命令 - 使用绝对路径
            cmd = ['node', js_script_abs_path] + abs_file_list + ['-o', output_dir]
            
            self._logger.info(f"执行解密命令: {' '.join(cmd)}")
            
            # 执行解密
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            stdout, stderr = process.communicate()

            # 检查是否执行成功
            if process.returncode != 0:
                self._logger.error(f"解密失败，错误输出: {stderr}")
                raise RuntimeError(f"解密失败: {stderr}")

            # 构建结果字典
            result = {}
            for input_file, abs_input_file in zip(file_list, abs_file_list):
                output_path = os.path.join(output_dir, os.path.basename(input_file))
                if os.path.exists(output_path):
                    result[input_file] = output_path
                else:
                    raise FileNotFoundError(f"解密后的文件未找到: {output_path}")

            return result

        except Exception as e:
            self._logger.error(f"解密过程发生错误: {str(e)}")
            # 如果使用临时目录且发生错误，清理临时目录
            if output_dir is None:
                shutil.rmtree(output_dir, ignore_errors=True)
            raise e

def decrypt_ts_files(file_list: List[str], output_dir: str = None) -> Dict[str, str]:
    """
    便捷函数，用于直接解密TS文件
    :param file_list: TS文件路径列表
    :param output_dir: 输出目录，如果为None则使用临时目录
    :return: 字典，键为输入文件路径，值为解密后文件路径
    """
    decryptor = TSDecryptor()
    return decryptor.decrypt_files(file_list, output_dir)

# 使用示例
if __name__ == "__main__":
    # 命令行使用示例
    if len(sys.argv) < 2:
        print("使用方法: python ts_decrypt_api.py <ts文件1> [ts文件2 ...] [-o 输出目录]")
        sys.exit(1)

    # 解析命令行参数
    files = []
    output = None
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "-o":
            if i + 1 < len(sys.argv):
                output = sys.argv[i + 1]
                i += 2
            else:
                print("错误: -o 选项需要指定输出目录")
                sys.exit(1)
        else:
            files.append(sys.argv[i])
            i += 1

    try:
        # 执行解密
        print(files, output)
        result = decrypt_ts_files(files, output)
        print("\n解密结果:")
        for input_file, output_file in result.items():
            print(f"{input_file} -> {output_file}")
    except Exception as e:
        print(f"错误: {str(e)}", file=sys.stderr)
        sys.exit(1) 