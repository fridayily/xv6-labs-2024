        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        # la 加载地址或标号到寄存器中, stack0[4096 * NCPU]，即每个CPU 4k 栈空间
        la sp, stack0
        # li 加载立即数到寄存器中，设置每个CPU的栈空间大小 4096 byte
        li a0, 1024*4
        # csrr 获取当前 CPU 核心的ID  machine hard id
        # 这里没懂？ mhartid 不等于核数，而且不一定数值上连续，但至少有一个必须为0
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        // sp 开始指向 stack0, a0= 4K*(mhartid+1)
        add sp, sp, a0
        # jump to start() in start.c
        call start
spin:
        j spin
