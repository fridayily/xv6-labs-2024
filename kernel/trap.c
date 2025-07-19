#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
// 调用 w_stvec() 设置 stvec 寄存器
// 将 kernelvec 的地址写入 stvec，作为内核态异常处理入口
// 定义在 kernelvec.S 中
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 修改 stvec ，以便在内核中发生陷阱时由 kernelvec 处理
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  // 区分是系统调用、设备中断还是异常
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // 系统调用路径会将保存的用户程序计数器加 4，因为在系统调用的情况下，
    // RISC-V 会让程序指针指向 ecall 指令，但用户代码需要从后续的指令继续执行。
    // 在退出时，usertrap 会检查进程是否已被终止，或者是否应该让出 CPU（如果此陷阱是定时器中断）
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    // 发生异常
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  // 检查进程是否被终止
  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  // 让出 CPU
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
// 恢复用户寄存器；
// 设置异常返回地址；
// 切换到用户栈；
// 最终通过 sret 指令跳转到用户空间继续执行

// 在用户态发生异常、中断或系统调用后，完成从内核态返回用户态的所有准备工作，
// 并最终跳转到 trampoline.S 中执行真正的切换

// 该函数会设置 RISC-V 的控制寄存器，为将来来自用户空间的陷阱做准备
//      将stvec设置为uservec，并准备好uservec所依赖的陷阱帧字段
//      将sepc设置为之前保存的用户程序计数器
//      最后，usertrapret会调用映射在用户页表和内核页表中的跳板页（trampoline page）上的userret
//      原因是userret中的汇编代码将要切换页表
void
usertrapret(void)
{
  // 获取当前进程控制块
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  // 关闭中断，防止在切换过程中被中断打断
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  // 0x80000000	 内核入口地址（由链接器脚本定义)
  // 0x80000000 ~ 0x80001000 内核初始化代码（entry.S 等）
  // 跳板页起始地址 _trampoline
  // uservec：异常处理入口标签，位于 trampoline.S 中,地址什么时候确定的？？
  // uservec - trampoline：获取其在跳板页内的偏移量
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  // 将 stvec 寄存器设置为 uservec 的地址，表示下一次异常应跳转到该位置
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  // 当前内核页表地址（用于异常返回时切换回内核态）
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  // 内核栈顶指针
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  // 异常处理函数地址（usertrap）
  p->trapframe->kernel_trap = (uint64)usertrap;
  // 当前硬件线程 ID（hartid）
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  // 设置 SPIE 位，表示在用户模式下允许中断；
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // p->trapframe->epc 保存了发生异常前的用户程序计数器（PC）；
  // w_sepc 设置 sepc 寄存器为该值，表示从哪里继续执行；这决定了用户程序从哪里恢复执行
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // 构造用于加载页表的 satp 寄存器值
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // trampoline_userret：计算 userret 在跳板页中的地址；
  // 强制转换为函数指针并调用；
  // 将 satp（新页表）作为参数传入
  // 用于实现用户态与内核态之间的上下文切换
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

