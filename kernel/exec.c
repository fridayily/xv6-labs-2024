#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// 标记转权限
int flags2perm(int flags)
{
  int perm = 0;
  if (flags & 0x1)
    perm = PTE_X;
  if (flags & 0x2)
    perm |= PTE_W;
  return perm;
}

// exec 会将 ELF 文件中的字节加载到该文件所指定地址的内存中。
// 用户或进程可以在 ELF 文件中放入任意想要的地址。
// 因此，exec 存在风险，因为 ELF 文件中的地址可能会意外或故意指向内核
// xv6 会执行多项检查以规避这些风险。例如，if(ph.vaddr + ph.memsz < ph.vaddr) 这一检查是为了判断两者之和是否溢出 64 位整数
int exec(char *path, char **argv)
{
  DEBUG("exec begin");
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  // 为即将进行的一组磁盘写操作预留日志空间，并标记日志事务的开始
  begin_op();

  // namei 将路径名解析为对应的 inode 结构体指针。
  // 它会逐级解析路径（如 /a/b/c），并返回最终的 inode 节点，表示该路径所指向的文件或目录
  if ((ip = namei(path)) == 0)
  {
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  if (elf.magic != ELF_MAGIC)
    goto bad;

  // proc_pagetable 分配一个没有用户映射的新页表
  // 为进程 p 分配一个新的页表
  // 到现在为止，该页表的 trapframe 没有改变
  // exec 替换了整个用户空间
  if ((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  // 一个 ELF 文件中有多个 proghdr
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph))
  {
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    // 检查 ph.vaddr + ph.memsz 是否会溢出 64 bit integer

    // 这里的危险在于，用户可能会构造一个 ELF 二进制文件，
    // 使其 ph.vaddr 指向一个用户选定的地址，
    // 且 ph.memsz 足够大，导致两者之和溢出，而这一结果看起来会是一个有效值
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;
    // 通过 uvmalloc 为每个 ELF 段分配内存，这里是准备构建一个新的程序，所以从 0 虚拟空间地址开始映射
    // 将用户程序段（如 .text, .data）映射到进程的虚拟地址空间，并确保足够的内存空间。
    DEBUG("uvmalloc progrom memory %lu", sz);
    if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    // 通过 loadseg 将每个段加载到内存中
    // 将可执行文件中的一个段（segment）从 inode 对应的磁盘位置读取到用户虚拟地址空间中。
    // ph.vaddr	ELF 段的虚拟地址起始位置
    // ip	指向可执行文件的 inode 结构体
    // ph.off	段在文件中的偏移
    // ph.filesz	段在文件中的大小
    DEBUG("load seg %lu", ph.filesz);
    if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.

  // 杠从ELF文件读取的段的大小， 然后对齐到页边界
  sz = PGROUNDUP(sz);
  uint64 sz1;
  // 分配用户栈空间，返回新的用户虚拟地址空间上限（即更新后的 sz）
  DEBUG("alloc user stack %p", pagetable);
  if ((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK + 1) * PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1; // 假如这里从ELF 文件读取两个段，sz=2*4k，然后这里会分配用户栈空间2*4k。sz= 4*4k
  // 设置栈底防护页，实际能用的用户栈空间为 1*4k
  uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);
  // sp：栈顶指针
  sp = sz;
  // stackbase 用户栈的起始地址
  // sp 用户栈指针（stack pointer）
  // sz 用户栈顶，即一般的 Frame Point 或者 Base Point
  // 特别注意这里的 stackbase 不是栈顶指针而是栈的起始地址，超过这个触发错误
  stackbase = sp - USERSTACK * PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for (argc = 0; argv[argc]; argc++)
  {
    if (argc >= MAXARG)
      goto bad;
    // 为参数字符串分配栈空间，注意这里是参数的长度，不是地址
    sp -= strlen(argv[argc]) + 1;
    // 确保栈指针对齐
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if (sp < stackbase)
      goto bad;
    // 将参数复制到用户栈
    DEBUG("copy argv to user stack%p", pagetable);
    if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    // 记录参数地址，而不是真实的参数
    ustack[argc] = sp;
  }
  // 添加 NULL 指针作为结束标志
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    goto bad;
  // sp 用户空间的虚拟地址，ustack 内核空间的虚拟地址
  DEBUG("copy arg addr to user stack%p", pagetable);
  if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
    goto bad;

  // char *argv[] = {"echo", "hello", "world", 0 };
  // exec("echo", argv);
  // 执行如上程序，得到的用户栈空间

  // echo
  // hello
  // world
  // &echo
  // &hello
  // &world
  // 0

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  // a0 寄存器用于传递第一个参数（即 argc）,由系统调用返回值自动设置
  // a1 寄存器用于传递第二个参数（即 argv 指针）
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  // path 是传入的可执行文件路径（如 /user/bin/echo）
  // s 是遍历路径字符的指针
  // last 用于记录最后一个 '/' 后的内容（即文件名）
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  // 使用 safestrcpy() 安全地将文件名复制到进程结构体的 .name 字段 ，替换了原有的进程名
  DEBUG("old p->name=%s",p->name);
  safestrcpy(p->name, last, sizeof(p->name));

  // Commit to the user image.
  // 获取当前进程 p 的旧页表；
  oldpagetable = p->pagetable;
  // 将新构建的用户页表（pagetable）赋值给进程结构体
  p->pagetable = pagetable;
  // 设置进程的虚拟地址空间上限为 sz，即用户栈顶部地址；
  p->sz = sz;
  // 设置 trapframe.epc，表示用户程序入口地址
  // 来自 ELF 文件头中的 .entry 字段
  p->trapframe->epc = elf.entry; // initial program counter = main
  // 设置 trapframe.sp，指向用户栈顶地址
  p->trapframe->sp = sp; // initial stack pointer
  // 释放旧页表资源；
  DEBUG("free old pagetable begin %p", oldpagetable);
  proc_freepagetable(oldpagetable, oldsz);
  DEBUG("free old pagetable begin end");

  DEBUG("exec end");
  return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
  if (pagetable)
    proc_freepagetable(pagetable, sz);
  if (ip)
  {
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
// 将 ELF 可执行文件中的一个段（segment）从磁盘读取到用户虚拟地址空间中。
// 该函数被 exec() 调用，用于在进程启动时加载 .text、.data 等段内容
// offset	段在文件中的偏移
// sz	段在文件中的大小
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for (i = 0; i < sz; i += PGSIZE)
  {
    // 获取虚拟地址对应的物理地址
    pa = walkaddr(pagetable, va + i);
    if (pa == 0)
      panic("loadseg: address should exist");
    if (sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    // 从 inode 的偏移 offset+i 处读取 n 字节到物理地址 pa
    if (readi(ip, 0, (uint64)pa, offset + i, n) != n)
      return -1;
  }

  return 0;
}
