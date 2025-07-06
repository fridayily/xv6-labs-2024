#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  // 清除 MPP
  x &= ~MSTATUS_MPP_MASK;
  // 设置 supervisor 模式
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  // 这条指令将 mepc 寄存器设置为 main 函数的起始地址，确保在执行 mret 指令时，程序会从 main 函数开始执行。
  w_mepc((uint64)main);

  // disable paging for now.
  // 禁用分页，使处理器直接使用物理地址进行内存访问
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  // medeleg 寄存器用于控制哪些类型的异常可以被委托给supervisor模式处理
  w_medeleg(0xffff);
  // 寄存器用于控制哪些类型的中断可以被委托给supervisor模式处理
  w_mideleg(0xffff);
  // SIE_SEIE、SIE_STIE 和 SIE_SSIE 是常量，分别表示监督模式下的外部中断使能、定时器中断使能和软件中断使能
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  // xv6使用tp寄存器来存储每个CPU的hartid。这为操作系统提供了一个快速访问当前CPU ID的方法
  w_tp(id);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// ask each hart to generate timer interrupts.
void
timerinit()
{
  // enable supervisor-mode timer interrupts.
  w_mie(r_mie() | MIE_STIE);
  
  // enable the sstc extension (i.e. stimecmp).
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // allow supervisor to use stimecmp and time.
  w_mcounteren(r_mcounteren() | 2);
  
  // ask for the very first timer interrupt.
  w_stimecmp(r_time() + 1000000);
}
