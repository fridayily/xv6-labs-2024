#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  // myproc()->sz = myproc()->sz+ n;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  // 获取要睡眠的时钟周期数
  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  // 保存当前时钟周期数
  ticks0 = ticks;
  // 检查是否已达到指定的睡眠周期数
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  backtrace();
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 
sys_sigalarm(void)
{
  // DEBUG("sys_sigalarm");
  int ticks;
  uint64 handler;
  DEBUG("sys_sigalarm");
  // 获取第一个系统调用参数
  argint(0, &ticks);
  // 获取第二个系统调用参数（地址类型), 这个参数是闹钟触发时要执行的处理函数地址
  argaddr(1,&handler);
  // DEBUG("ticks %d handler %lu",ticks,handler);
  procsigalarm(ticks,(void*)handler);
  return 0;
}

uint64 
sys_sigreturn(void)
{
  // DEBUG("sys_sigreturn");
  // acquire(&tickslock);
  int i = procsigreturn();
  // release(&siglock);
  return i;
}