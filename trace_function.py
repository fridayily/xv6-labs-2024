# trace_call.py

import gdb
import datetime
import re

now_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")


log_filename = f"function_trace_{now_str}.log"
# 打开日志文件
log_file = open(log_filename, "w")

# 存储调用栈信息
call_stack = []

# 关键函数白名单
whitelist = [
    "main",
    "userinit"
    # Trap 相关
    "uservec", "usertrap", "usertrapret", "userret",
    # "kerneltrap", "kernelvec",
    
    # 进程管理
    "fork","allocproc", "forkret","exit", "wait", "exec",
    # "sleep", "wakeup", "yield",
    
    # 调度器
    # "scheduler", "sched", "swtch",
    
    # 内存管理
    # "kalloc", "kfree",
      "uvmalloc", "uvmdealloc",
    "walkaddr", 
    # "mappages",
    #  "uvmunmap",
      "uvminit",
    
    # 文件系统
    # "filealloc", "fileclose", "fileread", "filewrite",
    # "namei", "dirlookup", "ialloc", "iupdate",
    
    # 系统调用
    "sys_fork", "sys_exit", "sys_wait", "sys_exec",
    "sys_kill", "sys_sbrk", "sys_sleep", "sys_uptime",
    
    # 硬件相关
    # "consoleintr", "timerintr", "diskintr"
]

class FunctionEntryBreakpoint(gdb.Breakpoint):
    def __init__(self, func_name):
        super(FunctionEntryBreakpoint, self).__init__(func_name, gdb.BP_BREAKPOINT, internal=True)
        self.func_name = func_name

    def stop(self):
        # 记录进入函数的信息到文件
        log_file.write(f"->: {self.func_name}\n")
        log_file.flush()
        call_stack.append(self.func_name)
        FunctionExitBreakpoint(self.func_name,gdb.newest_frame())
        return False

class FunctionExitBreakpoint(gdb.FinishBreakpoint):
    def __init__(self, func_name,frame):
        super(FunctionExitBreakpoint, self).__init__(frame, internal=True)
        self.func_name = func_name

    def stop(self):
        # 记录离开函数的信息到文件
        log_file.write(f"<-: {self.func_name}\n")
        log_file.flush()
        if call_stack and call_stack[-1] == self.func_name:
            call_stack.pop()
        return False

def set_breakpoints_for_key_functions():
    info = gdb.execute("info functions", to_string=True)
    func_pattern = re.compile(r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\(')
    for line in info.splitlines():
        match = func_pattern.search(line)
        if match:
            func = match.group(1)
            if func in whitelist:
                try:
                    print(f"[*] Setting breakpoint for function: {func}")
                    FunctionEntryBreakpoint(func)
                except Exception:
                    print(f"[!] Error setting breakpoint for function: {func}")
                    pass

# 启动时设置断点并继续执行
set_breakpoints_for_key_functions()
log_file.write(f"[*] Function tracing started at {datetime.datetime.now()}\n")
log_file.write(f"[*] Tracing {len(whitelist)} key functions\n")
log_file.flush()
print(f"[*] Function tracing started. Tracing {len(whitelist)} key functions.")
print("[*] Output to function_trace.log")
# gdb.execute("continue")