# xv6_gdb.py - 用于辅助调试 xv6 操作系统的 GDB 脚本

import gdb
import re
import datetime

syscall_names = {
    1: "fork",
    2: "exit",
    3: "wait",
    4: "pipe",
    5: "read",
    6: "kill",
    7: "exec",
    8: "fstat",
    9: "chdir",
    10: "dup",
    11: "getpid",
    12: "sbrk",
    13: "sleep",
    14: "uptime",
    15: "open",
    16: "write",
    17: "mknod",
    18: "unlink",
    19: "link",
    20: "mkdir",
    21: "close",
    # ...根据你的 xv6 版本补全...
}

key_functions = [
# "uservec",
# "usertrap",
# "usertrapret", 
# "userret",
"syscall",
"exec"
]


row_cnt =5
func_width = 12
pid_width = 3
name_width = 10
num_width = 3

def set_breakpoints_for_key_functions():
    info = gdb.execute("info functions", to_string=True)
    func_pattern = re.compile(r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\(')
    for line in info.splitlines():
        match = func_pattern.search(line)
        if match:
            func = match.group(1)
            if func in key_functions:
                try:
                    print(f"[*] Setting breakpoint for function: {func}")
                    FunctionEntryBreakpoint(func)
                except Exception:
                    print(f"[!] Error setting breakpoint for function: {func}")
                    pass

class FunctionEntryBreakpoint(gdb.Breakpoint):
    last_syscall_info = None  # (pid, num)
    def __init__(self, func_name):
        super(FunctionEntryBreakpoint, self).__init__(func_name, gdb.BP_BREAKPOINT, internal=True)
        self.func_name = func_name

    def stop(self):
        proc = gdb.parse_and_eval('myproc()').dereference()
        pid = int(proc['pid'])
        name = proc['name'].string()
        num = int(proc['trapframe']['a7'])
        if self.func_name == "exec":
            frame = gdb.newest_frame()
            path = frame.read_var('path').string()
            argv = frame.read_var('argv')
            argv_list = []
            for i in range(0, 16):  # MAXARG=16
                arg_ptr = argv[i]
                if int(arg_ptr) == 0:
                    break
                argv_list.append(arg_ptr.string())
            argv_str = ' '.join(argv_list)
            print(f'[{self.func_name:^{func_width}}] pid={pid:<{pid_width}} name={name:<{name_width}} num={num:<{num_width}} path={path} argv={argv_str}')
        elif self.func_name == 'syscall':
            scname = syscall_names.get(num, 'unknown')
            info = (pid, num)
            if FunctionEntryBreakpoint.last_syscall_info != info:
                print(f'[{self.func_name:^{func_width}}] pid={pid:<{pid_width}} name={name:<{name_width}} num={num:<{num_width}} scname={scname}')
                FunctionEntryBreakpoint.last_syscall_info = info
        else:
            print(f"[{self.func_name:^{func_width}}] pid={pid:<{pid_width}} name={name:{name_width}} num={num:<{num_width}} ")
        return False


class Xv6InfoRegs(gdb.Command):
    """显示 xv6 RISC-V 风格的寄存器信息"""
    def __init__(self):
        super(Xv6InfoRegs, self).__init__("xv6-regs", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        # RISC-V 寄存器列表
        regs = [
            "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1",
            "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
            "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
            "t3", "t4", "t5", "t6", "pc"  # pc 对应 xip
        ]
        
        print(" registers:")
        
        for i in range(0, len(regs), row_cnt):
            line_parts = []
            # 处理当前组内的寄存器
            for j in range(row_cnt):
                if i + j < len(regs):
                    reg = regs[i + j]
                    try:
                        if reg == "pc":
                            value = gdb.selected_frame().pc()
                        else:
                            value = gdb.selected_frame().read_register(reg)
                        # 格式化单个寄存器：名称(3字符) + 十六进制值(18字符)
                        line_parts.append(f"{reg:3s}: 0x{int(value):016x}")
                    except gdb.error:
                        line_parts.append(f"{reg:3s}: [unavailable]")
            # 拼接当前行并打印
            print("  " + "  ".join(line_parts))

class Xv6Backtrace(gdb.Command):
    """显示 xv6 风格的函数调用栈"""
    def __init__(self):
        super(Xv6Backtrace, self).__init__("xv6-backtrace", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        frame = gdb.newest_frame()
        if not frame:
            print("No stack.")
            return

        print("Backtrace:")
        i = 0
        while frame:
            pc = frame.pc()
            symtab_and_line = gdb.find_pc_line(pc)
            if symtab_and_line.symtab is not None:
        # Get the source file name and line number
                filename = symtab_and_line.symtab.filename
                line_number = symtab_and_line.line
            func = frame.function()
            func_name = func.name if func else f"0x{pc:x}"
       
            
            # 尝试获取函数参数
            args = []
            try:
                block = frame.block()
                for symbol in block:
                    if symbol.is_argument:
                        try:
                            value = frame.read_var(symbol)
                            args.append(f"{symbol.name}={value}")
                        except:
                            pass
            except:
                pass
            
            print(f"#{i} {filename}:{line_number} 0x{pc:x} in {func_name} ({', '.join(args)})")
            frame = frame.older()
            i += 1


class Xv6PrintProc(gdb.Command):
    """打印 xv6 进程结构体信息"""
    def __init__(self):
        super(Xv6PrintProc, self).__init__("xv6-proc", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            if arg == 'tp':
                # 如果参数是 'tp'，打印当前进程的 trapframe
                proc_ptr = gdb.parse_and_eval("myproc()")
                proc = proc_ptr.dereference()
                self.print_trapframe(proc["trapframe"])
            else:
                # 默认情况下打印当前进程信息
                proc_ptr = gdb.parse_and_eval("myproc()")
                self.print_proc(proc_ptr)
        except gdb.error as e:
            print(f"Error: {e}")
            print("Usage: xv6-proc <tp> or xv6-proc (for current process)")

    def print_proc(self, proc_ptr):
        """格式化打印进程结构体"""
        proc = proc_ptr.dereference()
        
        # 基本信息
        pid = int(proc["pid"])
        state_val = int(proc["state"])
        state_names = ["UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"]
        state = state_names[state_val] if 0 <= state_val < len(state_names) else f"UNKNOWN({state_val})"
        name = proc["name"].string()
        
        # 上下文信息
        context = proc["context"]
        if context:
            ra = int(context["ra"])
            sp = int(context["sp"])
            s0 = int(context["s0"])
            s1 = int(context["s1"])
            s2 = int(context["s2"])
            s3 = int(context["s3"])
            s4 = int(context["s4"])
            s5 = int(context["s5"])
            s6 = int(context["s6"])
            s7 = int(context["s7"])
            s8 = int(context["s8"])
            s9 = int(context["s9"])
            s10 = int(context["s10"])
            s11 = int(context["s11"])
        else:
            ra = sp = s0 = s1 = s2 = s3 = s4 = s5 = s6 = s7 = s8 = s9 = s10 = s11 = 0
        
        # 内存信息
        sz = int(proc["sz"])
        pgtbl = int(proc["pagetable"])
        
        # 打开文件信息
        ofile_count = 0
        for i in range(10):  # 假设最多10个文件描述符
            if int(proc["ofile"][i]) != 0:
                ofile_count += 1
        
        # 打印格式化信息
        print(f"Process {pid} ({name}):")
        print(f"  State: {state}")
        print(f"  Stack Pointer: 0x{sp:016x}")
        print(f"  Return Address: 0x{ra:016x}")
        print(f"  Saved Registers:")
        print(f"    s0: 0x{s0:016x}  s1: 0x{s1:016x}  s2: 0x{s2:016x}  s3: 0x{s3:016x}")
        print(f"    s4: 0x{s4:016x}  s5: 0x{s5:016x}  s6: 0x{s6:016x}  s7: 0x{s7:016x}")
        print(f"    s8: 0x{s8:016x}  s9: 0x{s9:016x}  s10: 0x{s10:016x}  s11: 0x{s11:016x}")
        print(f"  Memory:")
        print(f"    Size: {sz} bytes")
        print(f"    Page Table: 0x{pgtbl:016x}")
        print(f"  Open Files: {ofile_count}")
        parent_ptr = proc["parent"]
        if int(parent_ptr) != 0:
            parent = parent_ptr.dereference()
            parent_name = parent["name"].string()
            parent_pid = int(parent["pid"])
            print(f"  Parent Process: {parent_name} (PID: {parent_pid})")
        else:
            print("  Parent Process: None")

    def print_trapframe(self, trapframe_ptr):
        """打印 trapframe 信息，每行5个寄存器"""
        if int(trapframe_ptr) == 0:
            print("Trapframe: None")
            return
            
        try:
            tf = trapframe_ptr.dereference()
            print("Trapframe:")
            tf_type = tf.type
            if str(tf_type).startswith("struct trapframe"):
                # 遍历结构体的字段
                fields = []
                for field in tf_type.fields():
                    try:
                        field_value = tf[field.name]
                        fields.append((field.name, int(field_value)))
                    except:
                        fields.append((field.name, "unknown"))
                
                # 每行打印5个字段
                for i in range(0, len(fields), row_cnt):
                    line_parts = []
                    for j in range(row_cnt):
                        if i + j < len(fields):
                            field_name, field_value = fields[i + j]
                            if isinstance(field_value, int):
                                line_parts.append(f"{field_name}: 0x{field_value:016x}")
                            else:
                                line_parts.append(f"{field_name}: {field_value}")
                    print("  " + ", ".join(line_parts))                
        except gdb.error as e:
            print(f"Error reading trapframe: {e}")


class Xv6BreakSyscall(gdb.Command):
    """在指定系统调用处设置断点"""
    def __init__(self):
        super(Xv6BreakSyscall, self).__init__("xv6-syscall", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        if not arg:
            print("Usage: xv6-syscall <syscall_name>")
            return

        # 尝试查找系统调用函数
        syscall_name = f"sys_{arg}"
        try:
            gdb.Breakpoint(syscall_name)
            print(f"Breakpoint set at {syscall_name}")
        except gdb.error:
            print(f"Error: Could not set breakpoint at {syscall_name}")

class Xv6WatchFile(gdb.Command):
    """监视文件操作"""
    def __init__(self):
        super(Xv6WatchFile, self).__init__("xv6-watch-file", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        if not arg:
            print("Usage: xv6-watch-file <filename>")
            return

        # 在关键文件操作函数设置断点
        funcs = ["filedup", "fileclose", "fileread", "filewrite"]
        for func in funcs:
            try:
                bp = gdb.Breakpoint(func)
                bp.commands = f"""
                    if strcmp((char*)$a0, "{arg}") == 0
                        printf "File operation: {func}({arg})\\n"
                        bt 1
                        continue
                    end
                    continue
                """
                print(f"Watchpoint set for {func} on {arg}")
            except gdb.error as e:
                print(f"Error setting watchpoint on {func}: {e}")

class CommandHook(gdb.Command):
    def __init__(self):
        super(CommandHook, self).__init__("",gdb.COMMAND_USER,gdb.COMPLETE_NONE,True)

    def invoke(self, arg, from_tty):
        print(f"\n>>> {arg}")
        try:
            output = gdb.execute(arg,to_string=True)
            if output:
                print(output)
        except gdb.error as e:
            print(f"Error executing command: {e}")


class Xv6SourceLocation(gdb.Command):
    """获取当前执行位置的源文件名和行号"""
    
    def __init__(self):
        super(Xv6SourceLocation, self).__init__("xv6-location", gdb.COMMAND_USER)
    
    def invoke(self, arg, from_tty):
        """显示当前执行位置的文件名和行号"""
        filename, line_num = self.get_current_file_and_line()
        if filename and line_num:
            print(f"Current location: {filename}:{line_num}")
        else:
            print("Could not determine current file and line")
    
    def get_current_file_and_line(self):
        """
        获取当前执行位置的文件名和行号
        
        Returns:
            tuple: (filename, line_number) or (None, None) if not available
        """
        try:
            # 获取当前帧
            frame = gdb.selected_frame()
            if not frame:
                return None, None
                
            # 获取程序计数器
            pc = frame.pc()
            
            # 通过PC查找源文件和行号
            symtab_and_line = gdb.find_pc_line(pc)
            
            if symtab_and_line.symtab is not None:
                # 获取源文件名和行号
                filename = symtab_and_line.symtab.filename
                line_number = symtab_and_line.line
                return filename, line_number
            else:
                return None, None
                
        except gdb.error:
            return None, None



# 自动加载脚本
def init_xv6_gdb():
    print("Loading xv6 GDB helper functions...")
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_filename = f"xv6_debug_{timestamp}.log"
    try:
        gdb.execute(f"set logging file {log_filename}")
        gdb.execute("set logging on")
        gdb.execute("set pagination off")
        gdb.execute("set trace-commands on")
        gdb.execute("set verbose on") 
        print(f"GDB logging enabled: {log_filename}")
    except gdb.error as e:
        print(f"Warning: Could not enable GDB logging: {e}")

    set_breakpoints_for_key_functions()

    
    # 先尝试删除已存在的命令
    try:
        gdb.execute("del xv6-regs")
    except:
        pass
    try:
        gdb.execute("del xv6-backtrace")
    except:
        pass
    try:
        gdb.execute("del xv6-proc")
    except:
        pass
    try:
        gdb.execute("del xv6-syscall")
    except:
        pass
    try:
        gdb.execute("del xv6-watch-file")
    except:
        pass
    
    # 重新注册命令
    Xv6InfoRegs()
    Xv6Backtrace()
    Xv6PrintProc()
    Xv6BreakSyscall()
    Xv6WatchFile()
    Xv6SourceLocation()
    # CommandHook()
    
    # 添加常用断点命令别名
    try:
        gdb.execute("alias xv6-fork = break sys_fork")
    except gdb.error:
        pass  # Alias already exists, ignore
        
    try:
        gdb.execute("alias xv6-exec = break sys_exec")
    except gdb.error:
        pass  # Alias already exists, ignore
        
    try:
        gdb.execute("alias xv6-kill = break sys_kill")
    except gdb.error:
        pass  # Alias already exists, ignore
    
    try:
        gdb.execute("break sys_exec")
        gdb.execute("commands")
        gdb.execute("printf \"sys_exec called with path: %s\\n\", (char*)$a0")
        gdb.execute("continue")
        gdb.execute("end")
    except gdb.error as e:
        print(f"Warning: Could not set sys_exec auto-trigger: {e}")
    

    gdb.execute("break main")
    # gdb.execute("break allocproc")
    gdb.execute("break fork")
    gdb.execute("break forkret")
    # gdb.execute("break usertrap")
    # gdb.execute("break usertrapret")
    print("Xv6 GDB helpers loaded. Type 'help' to see available commands.")

# 初始化
init_xv6_gdb()