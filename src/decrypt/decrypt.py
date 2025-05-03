from wasmtime import Store, Module, Instance, MemoryType, Limits, Memory, FuncType, Func, ValType, GlobalType, Global, Table, TableType, Val
from struct import pack, unpack
import ctypes
import os
import sys
import argparse

def resource_path(relative_path):
    """获取资源文件的绝对路径"""
    # 如果程序是打包后的（比如使用 PyInstaller 打包）
    if getattr(sys, 'frozen', False):
        # 获取打包后的可执行文件路径
        base_path = os.path.dirname(sys.executable)
        # 由于auto-py-to-exe的改动，添加以下目录
        base_path = os.path.join(base_path, "_internal")
    else:
        # 获取未打包时的脚本文件路径
        base_path = os.path.dirname(os.path.abspath(__file__))
    
    # 返回资源文件的绝对路径
    return os.path.join(base_path, relative_path)

# 加载 wasm 模块
wasm_path = resource_path('decrypt.wasm')
store = Store()
module = Module.from_file(store.engine, wasm_path)

# 内存配置
MEMORY_SIZE = 64 * 1024 * 1024  # 64MB
PAGE_SIZE = 64 * 1024  # 64KB per page
required_pages = (MEMORY_SIZE + PAGE_SIZE - 1) // PAGE_SIZE

# 设置内存限制
limits = Limits(min=required_pages, max=required_pages)
memory_type = MemoryType(limits)
memory = Memory(store, memory_type)

# 验证内存分配
actual_pages = memory.size(store)
actual_size = actual_pages * PAGE_SIZE

if actual_size < MEMORY_SIZE:
    raise MemoryError(f"内存分配不足: {actual_size} < {MEMORY_SIZE}")

# 分配内存的基地址
ALLOC_BASE = 0x1000  # 4KB 对齐
alloc_ptr = ALLOC_BASE
allocations = []

def jsmalloc(size: int) -> int:
    """分配内存，确保对齐和边界检查"""
    global alloc_ptr
    size = (size + 3) & ~3
    addr = alloc_ptr
    if addr + size > MEMORY_SIZE - 0x1000:
        raise MemoryError(f"内存不足: 请求 {size} 字节，但只有 {MEMORY_SIZE - addr} 字节可用")
    alloc_ptr += size
    allocations.append((addr, size))
    return addr

def jsfree(ptr: int):
    """释放内存"""
    global allocations
    allocations = [(addr, size) for addr, size in allocations if addr != ptr]

# 定义导入函数
def dummy_func(*args): return 0

# 构造 imports
__table_base = Global(store, GlobalType(ValType.i32(), False), Val.i32(0))
DYNAMICTOP_PTR = Global(store, GlobalType(ValType.i32(), False), Val.i32(0))
table = Table(store, TableType(ValType.funcref(), Limits(160, None)), None)

f_type = [
    FuncType([ValType.i32(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32()], []),
    FuncType([ValType.i32(), ValType.f64(), ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32()], []),
    FuncType([], []),
    FuncType([ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32(),ValType.i64(), ValType.i32()], [ValType.i64()]),
    FuncType([ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.f64(), ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32()], []),
    FuncType([], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32(), ValType.i64()], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.f64(), ValType.i32()], [ValType.f64()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], []),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.f64(), ValType.i32(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.f64(), ValType.i32()], [ValType.i32()]),
    FuncType([ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32(), ValType.i32()], []),
    FuncType([ValType.i32(), ValType.i32(), ValType.i64(), ValType.i32()], [ValType.i64()]),
]

imports = [
    Func(store, f_type[3], dummy_func),
    Func(store, f_type[7], dummy_func),
    Func(store, f_type[8], dummy_func),
    Func(store, f_type[9], dummy_func),
    Func(store, f_type[3], dummy_func),
    Func(store, f_type[1], dummy_func),
    Func(store, f_type[10], dummy_func),
    Func(store, f_type[3], dummy_func),
    Func(store, f_type[7], dummy_func),
    Func(store, f_type[7], dummy_func),
    Func(store, f_type[7], dummy_func),
    Func(store, f_type[7], dummy_func),
    Func(store, f_type[3], dummy_func),
    Func(store, f_type[7], dummy_func),
    Func(store, f_type[11], dummy_func),
    Func(store, f_type[11], dummy_func),
    Func(store, f_type[0], dummy_func),
    Func(store, f_type[5], dummy_func),
    Func(store, f_type[3], dummy_func),
    Func(store, f_type[5], dummy_func),
    Func(store, f_type[3], lambda x: 0),
    Func(store, f_type[11], dummy_func),
    Func(store, f_type[12], dummy_func),
    __table_base,
    DYNAMICTOP_PTR,
    memory,
    table
]

# 实例化模块
instance = Instance(store, module, imports)

# 获取 wasm 内存句柄
mem_ptr = memory.data_ptr(store)

# 获取函数引用
tea_func = instance.exports(store)["func60_TEA"]

class EmscriptenMemoryManager:
    def __init__(self, memory, base=0x1000):
        self.memory = memory
        self.base = base
        self.alloc_ptr = base
        self.allocations = []
        self.memory_size = memory.size(store) * PAGE_SIZE
        
        self.heap8 = ctypes.cast(
            ctypes.addressof(memory.data_ptr(store).contents),
            ctypes.POINTER(ctypes.c_uint8)
        )
        self.heap32 = ctypes.cast(
            ctypes.addressof(memory.data_ptr(store).contents),
            ctypes.POINTER(ctypes.c_uint32)
        )
    
    def malloc(self, size):
        """分配内存，确保 4 字节对齐"""
        size = (size + 3) & ~3
        size = size + 0x1000
        addr = self.alloc_ptr
        
        if addr + size > self.memory_size - 0x1000:
            if size <= self.memory_size - self.base - 0x1000:
                self.alloc_ptr = self.base
                addr = self.alloc_ptr
            else:
                raise MemoryError(f"内存不足: 请求 {size} 字节，可用 {self.memory_size - addr - 0x1000} 字节")
        
        self.alloc_ptr += size
        self.allocations.append((addr, size))
        return addr
    
    def free(self, ptr):
        """释放内存"""
        self.allocations = [(addr, size) for addr, size in self.allocations if addr != ptr]
    
    def write8(self, ptr, data):
        """写入 8 位数据"""
        if ptr + len(data) > self.memory_size - 0x1000:
            raise MemoryError(f"写入越界: 地址=0x{ptr:x}, 大小={len(data)}字节")
        for i, byte in enumerate(data):
            self.heap8[ptr + i] = byte
    
    def read8(self, ptr, size):
        """读取 8 位数据"""
        if ptr + size > self.memory_size - 0x1000:
            raise MemoryError(f"读取越界: 地址=0x{ptr:x}, 大小={size}字节")
        return bytes(self.heap8[ptr:ptr + size])
    
    def write32(self, ptr, value):
        """写入 32 位数据"""
        if ptr + 4 > self.memory_size - 0x1000:
            raise MemoryError(f"写入越界: 地址=0x{ptr:x}, 大小=4字节")
        self.heap32[ptr >> 2] = value
    
    def read32(self, ptr):
        """读取 32 位数据"""
        if ptr + 4 > self.memory_size - 0x1000:
            raise MemoryError(f"读取越界: 地址=0x{ptr:x}, 大小=4字节")
        return self.heap32[ptr >> 2]

# 使用内存管理器
memory_manager = EmscriptenMemoryManager(memory)

def get_binary_from_file(file_path):
    """读取二进制文件"""
    try:
        with open(file_path, 'rb') as f:
            return bytearray(f.read())
    except Exception as e:
        raise IOError(f"读取文件 {file_path} 时出错: {e}")

def save_binary(data, file_path):
    """保存二进制数据到文件"""
    try:
        with open(file_path, 'wb') as f:
            f.write(data)
    except Exception as e:
        raise IOError(f"保存文件 {file_path} 时出错: {e}")

def find_nalu_start(buf, pos, total):
    """查找 NAL 单元起始位置"""
    while pos + 2 < total:
        if buf[pos+2] == 0:
            if pos + 3 < total and buf[pos+1] == 0 and buf[pos+3] == 1:
                return pos + 1
            pos += 2
        elif buf[pos+2] == 1:
            if buf[pos] == 0 and buf[pos+1] == 0:
                return pos
            pos += 3
        else:
            pos += 3
    return total

def parse_nal_array(buf):
    """解析和解密 NAL 单元"""
    begin = 0
    total = len(buf)
    
    while begin < total:
        begin += 3
        end = find_nalu_start(buf, begin+1, total)
        size = end - begin
        
        if size <= 0:
            begin = end
            continue
            
        nal_unit_type = buf[begin] & 0x1f
        
        if nal_unit_type in [1, 5, 25]:
            try:
                nalu_data = buf[begin:begin+size]
                buffer_size = len(nalu_data) + 0x1000
                buffer_ptr = memory_manager.malloc(buffer_size)
                memory_manager.write8(buffer_ptr, nalu_data)
                result_len = tea_func(store, len(nalu_data), buffer_ptr, buffer_ptr, 0)
                
                if result_len > 0:
                    decrypted_data = memory_manager.read8(buffer_ptr, result_len)
                    for i in range(min(len(decrypted_data), size)):
                        buf[begin+i] = decrypted_data[i]
                
                memory_manager.free(buffer_ptr)
                
            except Exception as e:
                raise RuntimeError(f"解密 NAL 单元时出错: {e}")
                
        begin = end

def scatter_pes(buf, ctx):
    """将处理后的 PES 数据分散回原始 TS 包"""
    k = 0
    for i in range(len(ctx['offarray'])):
        for j in range(ctx['offarray'][i], ctx['indexarray'][i] + 188):
            if k < len(ctx['PES']):
                buf[j] = ctx['PES'][k]
                k += 1

def parse_ts_packet(buf, index, ctx):
    """解析单个 TS 包"""
    PID = ((buf[index+1] & 0x1f) << 8) + buf[index+2]
    PUSI = (buf[index+1] & 0x40) >> 6
    
    if PID != 0x100:
        return
        
    adaptation_field_control = (buf[index+3] & 0x30) >> 4
    
    if PUSI == 1:
        if ctx['tscount'] > 0:
            parse_nal_array(ctx['PES'])
            scatter_pes(buf, ctx)
            ctx['tscount'] = 0
            
    payload_index = index + 4
    payload = None
    
    if adaptation_field_control == 1:
        payload = buf[payload_index:index+188]
    elif adaptation_field_control == 2:
        return
    elif adaptation_field_control == 3:
        adaptation_field_length = buf[index+4]
        payload_index = index + 4 + 1 + adaptation_field_length
        payload = buf[payload_index:index+188]
    else:
        return
        
    if payload:
        if PUSI == 1:
            ctx['indexarray'] = [index]
            ctx['PES'] = bytearray(payload)
            ctx['offarray'] = [payload_index]
            ctx['tscount'] = 1
        else:
            ctx['indexarray'].append(index)
            ctx['PES'].extend(payload)
            ctx['offarray'].append(payload_index)
            ctx['tscount'] += 1

def parse_ts(buf):
    """解析整个 TS 流"""
    ctx = {
        'indexarray': [],
        'PES': bytearray(),
        'offarray': [],
        'tscount': 0
    }
    
    for index in range(0, len(buf), 188):
        if buf[index] == 0x47:
            parse_ts_packet(buf, index, ctx)
        else:
            raise ValueError(f"在偏移量 {index} 处发现无效的 TS 包同步字节")
            
    if ctx['tscount'] > 0:
        parse_nal_array(ctx['PES'])
        scatter_pes(buf, ctx)

def decrypt_files(input_file, output_dir):
    """
    解密单个 TS 文件
    
    Args:
        input_file: 输入文件路径
        output_dir: 输出目录路径
    
    Returns:
        bool: 处理是否成功
    """
    try:
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
        
        buf = get_binary_from_file(input_file)
        buffer_size = len(buf) + 0x1000
        buffer_ptr = memory_manager.malloc(buffer_size)
        memory_manager.write8(buffer_ptr, buf)
        parse_ts(buf)
        memory_manager.free(buffer_ptr)
        
        output_file = os.path.join(output_dir, os.path.basename(input_file))
        save_binary(buf, output_file)
        return True
        
    except Exception as e:
        return False

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='解密 TS 文件中的加密视频流')
    parser.add_argument('input_file', help='输入 TS 文件路径')
    parser.add_argument('-o', '--output-dir', required=True, help='输出目录')
    args = parser.parse_args()
    
    success = decrypt_files(args.input_file, args.output_dir)
    if success:
        print(f"成功处理: {args.input_file} -> {os.path.join(args.output_dir, os.path.basename(args.input_file))}")
    else:
        print(f"处理失败: {args.input_file}") 