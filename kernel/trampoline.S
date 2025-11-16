        #
        # low-level code to handle traps from user space into
        # the kernel, and returns from kernel to user.
        #
        # the kernel maps the page holding this code
        # at the same virtual address (TRAMPOLINE)
        # in user and kernel space so that it continues
        # to work when it switches page tables.
        # kernel.ld causes this code to start at 
        # a page boundary.
        #

#include "riscv.h"
#include "memlayout.h"


# 整体工作流程：
#   当用户空间发生陷阱时，硬件跳转到uservec
#   uservec 保存用户寄存器并最终跳转到 usertrap() 函数
#   usertrap() 函数处理陷阱（系统调用、中断等）
#   处理完成后调用usertrapret()返回到用户空间



# 用于定义一个名为trampsec的代码段（section）
# 在xv6中，这个特殊的段被链接器放置在一个独立的内存页中，
# 这个页在虚拟地址空间中有特殊的用途：它需要同时映射在内核空间和用户空间的相同虚拟地址（TRAMPOLINE）上。
.section trampsec
# 声明trampoline为全局符号，使其在其他文件中可见和可访问。这使得C代码（如trap.c）可以引用这个标签的地址。
.globl trampoline
# 将 usertrap 声明为全局符号，但trampoline.S 文件本身并没有包含 usertrap 的实际实现代码。
# 实际的 usertrap 实现位于 trap.c 文件中，它是一个C语言函数
# 这个全局符号声明的作用是建立链接：
#    在 uservec 汇编代码的末尾, 有一条指令jr t0跳转到 usertrap() 函数
#    这里 t0 寄存器的值是从陷阱帧（TRAPFRAME）中加载的usertrap函数地址
#    为了让链接器能够正确解析这个引用，需要将 usertrap 声明为全局符号
.globl usertrap
# 定义 trampoline 标签，这是跳板页的起始点。整个跳板页包含了用于用户空间和内核空间之间切换的关键代码
trampoline:
# 指令告诉汇编器将后续代码按照16字节边界对齐（2^4=16）
.align 4
# 声明uservec为全局符号，使其可以在其他文件中被引用。uservec是处理来自用户空间陷阱的入口点。
.globl uservec
uservec:    
	#
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        #

        # save user a0 in sscratch so
        # a0 can be used to get at TRAPFRAME.
        csrw sscratch, a0

        # each process has a separate p->trapframe memory area,
        # but it's mapped to the same virtual address
        # (TRAPFRAME) in every process's user page table.
        li a0, TRAPFRAME
        
        # save the user registers in TRAPFRAME
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0
        csrr t0, sscratch
        sd t0, 112(a0)

        # initialize kernel stack pointer, from p->trapframe->kernel_sp
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # load the address of usertrap(), from p->trapframe->kernel_trap
        ld t0, 16(a0)

        # fetch the kernel page table address, from p->trapframe->kernel_satp.
        ld t1, 0(a0)

        # wait for any previous memory operations to complete, so that
        # they use the user page table.
        sfence.vma zero, zero

        # install the kernel page table.
        csrw satp, t1

        # flush now-stale user entries from the TLB.
        sfence.vma zero, zero

        # jump to usertrap(), which does not return
        jr t0

.globl userret
userret:
        # userret(pagetable)
        # called by usertrapret() in trap.c to
        # switch from kernel to user.
        # a0: user page table, for satp.

        # switch to the user page table.
        # 这是第一条内存屏障指令（在设置新页表前执行），用于：
        #   确保切换页表前的所有内存访问操作都已完成
        #   刷新 TLB（Translation Lookaside Buffer）缓存中与当前页表相关的条目
        #   防止 CPU 重排序导致的内存访问错误    
        sfence.vma zero, zero
        # 将系统切换到用户页表，使得后续的内存访问使用用户空间的地址映射。
        # 在RISC-V架构的调用约定中，第一个函数参数通过 a0 寄存器传递。因此，
        # 当 trampoline_userret 函数被调用时，satp变量的值会被自动放入a0寄存器中
        csrw satp, a0
        sfence.vma zero, zero

        li a0, TRAPFRAME

        # restore all but a0 from TRAPFRAME
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0
        ld a0, 112(a0)
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.
        sret
