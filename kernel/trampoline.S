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
        # 对 ecall 瞬间的状态做快照
        # 填充 struct trapframe （trap.h）中的寄存器
        # 利用 $sscratch(S-mode scratch) 保持所有寄存器
        # 切换到内核栈（相当于切换到进程对应的内核线程）
        # 切换到内核地址空间
        #    修改 #satp 寄存器 (S-mode address translation and protection)
        #    sfence.vma (flush TLB)
        # 跳转到 tf->kernel_trap
#include "riscv.h"
#include "memlayout.h"

# 内核会在系统启动时，将一个内核空间的栈地址写入 sscratch ，这个地址专门用于存放数据，不会与用户寄存器冲突
# 陷阱发生后， 可以将 a0 寄存器的值保存到 sscratch 中，确保在内核处理过程中不会丢失该值，
# 处理完成后可以通过 csrc sscratch, a0 恢复该值


# 上下文切换流程
#     用户程序执行 → 触发异常（如系统调用、中断）
#     CPU 进入 Supervisor 模式，跳转到 uservec；
#     执行 C 函数 usertrap() 处理异常
#     最终通过 userret 返回用户态


# 指定接下来的代码放入名为 trampsec 的段
.section trampsec
# 声明全局符号 trampoline 和 usertrap
.globl trampoline
.globl usertrap
#  定义 trampoline 标签 .align 4 表示按 16 字节对齐
trampoline:
.align 4
# 声明全局符号 uservec, 是用户中断向量表的入口地址,通常用于处理时钟中断、系统调用等异常
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
        # 将 a0 与 sscratch 的值交换
        csrw sscratch, a0

        # each process has a separate p->trapframe memory area,
        # but it's mapped to the same virtual address
        # (TRAPFRAME) in every process's user page table.
        # load immediate  将 TRAPFRAME 的地址加载到 a0 中
        li a0, TRAPFRAME
        
        # save the user registers in TRAPFRAME
        # 将寄存器 ra（返回地址寄存器）的值保存到 trapframe 中偏移量为 40 的位置
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
        # uservec的第一步操作是通过sscratch暂存用户a0的值
        # 从 sscratch 寄存器中读取值并保存到 t0
        # 将 t0 的值 存到 TRAPFRAME 的偏移量为 112 的位置，即 a0
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
        # 刷新当前地址空间下的所有 TLB 条目，
        # 确保后续的地址转换使用最新的页表映射
        sfence.vma zero, zero

        # install the kernel page table.
        # 用于从用户页表切换回内核页表
        csrw satp, t1

        # flush now-stale user entries from the TLB.
        # 由于刚刚切换了页表（通过 csrw satp, t1），之前缓存的用户态 TLB 条目已经失效
        # 再次刷新 TLB。
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
        #  Recall that the
        # user page table maps both the trampoline page and TRAPFRAME, but nothing else from the kernel.
        sfence.vma zero, zero
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
