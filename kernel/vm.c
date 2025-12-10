#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the one kernel_pagetable
void kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--)
  {
    pte_t *pte = &pagetable[PX(level, va)]; // 从 pagetable 中获取指定的 pte 地址，从最高级开始
    if (*pte & PTE_V)
    { // 如果标记位设置是有效
      // 获取 pte 的物理地址
      pagetable = (pagetable_t)PTE2PA(*pte);
      if (*pte & PTE_S)
      {
        DEBUG("pte %p\n", pte);
        return pte;
      }
#ifdef LAB_PGTBL
      if (PTE_LEAF(*pte))
      {
        return pte;
      }
#endif
    }
    else
    { // PTE 标记无效
      // alloc 不等于0 ，新分配一页内存，成功后初始化分配内存为0
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      // 第level 的物理地址转为 PTE，并设置为有效
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)]; // 虚拟地址对应的 PTE 地址，即 L0 级的 PTE 地址
}

pte_t *
superwalk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("superwalk: virtual address out of range");

  // 第2级页表的的 PTE,存储的是第1级页表的物理地址
  pte_t *pte = &pagetable[PX(2, va)];
  if (*pte & PTE_V)
  {
    // 如果从上面获取的 PTE 有效，则获取第1级页表的物理地址，该页表包含 512 个 PTE
    pagetable = (pagetable_t)PTE2PA(*pte);
  }
  else
  {
    // 如果第2级页表的 PTE 无效，则新分配一页物理页作为第1级页表
    if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
      return 0;  
    memset(pagetable, 0, SUPERPGSIZE);
    // 这里的 pte 还是第2级页表中的值，这里修改这个值
    *pte = PA2PTE(pagetable) | PTE_V;
  }

  return &pagetable[PX(1, va)];
}

// int is_mapped_as_superpage(pagetable_t pagetable, uint64 va)
// {
//   pte_t *pte = &pagetable[PX(2, va)];
//   if (*pte & PTE_V)
//   {
//     pagetable = (pagetable_t)PTE2PA(*pte);
//     pte = &pagetable[PX(1, va)];
//     return PTE_LEAF(*pte);
//   }
//   else
//   {
//     panic("is_mapped_as_superpage");
//   }
// }

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0); // 从pagetable 中获取对应的 pte 地址
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// 映射物理地址到 PTES
// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if (size == 0)
    panic("mappages: size");

  a = va; // 从 va 开始创建 PTE
  last = va + size - PGSIZE;
  for (;;)
  {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    // 从物理地址获得 PTE
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
    // printf("a %p pa %p last %p\n", (void *)a, (void *)pa, (void *)last);
  }
  // printf("mappages end\n");
  return 0;
}

int super_mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if ((va % SUPERPGSIZE) != 0)
    panic("super_mappages: va not aligned to SUPERPGSIZE");

  if ((size % SUPERPGSIZE) != 0)
    panic("super_mappages: pa not aligned to SUPERPGSIZE");

  if (size == 0)
    panic("super_mappages: size is zero");

  a = va; // 从 va 开始创建 PTE
  last = va + size - SUPERPGSIZE;
  for (a = va; a <= last; a += SUPERPGSIZE, pa += SUPERPGSIZE)
  {
    if ((pte = superwalk(pagetable, a, 1)) == 0)
      return -1;

    if (*pte & PTE_V)
      panic("super_mappages: virtual address already mapped");

    // 设置超级页表项
    *pte = PA2PTE(pa) | perm | PTE_U | PTE_V | PTE_S;
  }
  return 0;
}

// int mappages_superpage(pagetable_t pagetable, uint64 va, uint64 pa, int perm)
// {
//   pte_t *pte;

//   if ((va % SUPERPGSIZE) != 0)
//   {
//     panic("mappages_superpage: va not superpage aligned");
//   }

//   if ((pa % SUPERPGSIZE) != 0)
//   {
//     panic("mappages_superpage: pa not superpage aligned");
//   }

//   pte_t *l2_pte = &pagetable[PX(2, va)];
//   pagetable_t l1_pagetable;
//   if ((*l2_pte & PTE_V) == 0)
//   {
//     l1_pagetable = (pagetable_t)kalloc();
//     if (l1_pagetable == 0)
//       return -1;
//     memset(l1_pagetable, 0, PGSIZE);
//     *l2_pte = PA2PTE(l1_pagetable) | PTE_V;
//   }
//   else
//   {
//     l1_pagetable = (pagetable_t)PTE2PA(*l2_pte);
//   }
//   pte = &l1_pagetable[PX(1, va)];
//   if (*pte & PTE_V)
//   {
//     panic("mappages_superpage: remap");
//   }
//   *pte = PA2PTE(pa) | perm | PTE_V;
//   return 0;
// }

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  int sz;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += sz)
  {
    sz = PGSIZE;
    if ((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk failed");
    if ((*pte & PTE_V) == 0)
    {
      printf("va=%ld pte=%ld\n", a, *pte);
      panic("uvmunmap: not mapped");
    }
    if (PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");

    sz = (*pte & PTE_S) ? SUPERPGSIZE : PGSIZE;

    if (do_free)
    {
      uint64 pa = PTE2PA(*pte);
      if (sz == SUPERPGSIZE)
      {
        DEBUG("superfree: pa=%p\n", (void *)pa);
        superfree((void *)pa);
      }
      else
      {
        kfree((void *)pa);
      }
    }
    *pte = 0;
  }
  // 清空 PTE 地址
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  // 将 src 的数据复制到 mem
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;
  int sz;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += sz)
  {
    if (a % SUPERPGSIZE == 0 && a + SUPERPGSIZE <= newsz)
    {
      sz = SUPERPGSIZE;
      mem = superalloc();
      DEBUG("superalloc %p\n", mem);
      if (mem == 0)
      {
        goto regular_pages;
      }
      memset(mem, 0, sz);
      if (super_mappages(pagetable, a, sz, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
      {
        superfree(mem);
        uvmdealloc(pagetable, a, oldsz);
        return 0;
      }
    }
    else
    {
    regular_pages:
      sz = PGSIZE;
      mem = kalloc();
      DEBUG_EVERY_N(10,"kalloc %p\n", mem);
      if (mem == 0)
      {
        uvmdealloc(pagetable, a, oldsz);
        return 0;
      }
      memset(mem, 0, sz);
      if (mappages(pagetable, a, sz, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
      {
        kfree(mem);
        uvmdealloc(pagetable, a, oldsz);
        return 0;
      }
    }
  }
  return newsz;
}

// 调整进程内存大小，从 oldsz 到 newdz ，以 page 为单位
// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;
  int szinc;

  for (i = 0; i < sz; i += szinc)
  {
    szinc = PGSIZE;
    if ((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((*pte & PTE_S) == 0)
    {
      if ((mem = kalloc()) == 0)
        goto err;
      memmove(mem, (char *)pa, PGSIZE);
      if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
      {
        kfree(mem);
        goto err;
      }
    }
    else
    {
      szinc = SUPERPGSIZE;
      if ((mem = superalloc()) == 0)
        goto err;
      memmove(mem, (char *)pa, SUPERPGSIZE);
      if (super_mappages(new, i, SUPERPGSIZE, (uint64)mem, flags) != 0)
      {
        superfree(mem);
        goto err;
      }
    }

    // if (PTE_LEAF(*pte) && (i % SUPERPGSIZE) == 0 && (pa % SUPERPGSIZE) == 0)
    // {
    //   szinc = SUPERPGSIZE;
    //   mem = superkalloc();
    //   if (mem == 0)
    //   {
    //     goto err;
    //   }
    //   memmove(mem, (char *)pa, SUPERPGSIZE);
    //   if (mappages_superpage(new, i, (uint64)mem, flags) != 0)
    //   {
    //     superkfree(mem);
    //     goto err;
    //   }
    // }
    // else
    // {
    //   szinc = PGSIZE;
    //   if ((mem = kalloc()) == 0)
    //     goto err;
    //   // 拷贝1页数据到新分配的地址
    //   memmove(mem, (char *)pa, PGSIZE);
    //   // 将新分配的物理地址与虚拟地址建立关系
    //   if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
    //   {
    //     kfree(mem);
    //     goto err;
    //   }
    // }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// 设置一个 PTE 用户无效
// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
// 从 src 拷贝 len 字节到指定 page table 的虚拟地址
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0)
  {
    // 向下取最接近的对齐的虚拟地址
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;
    if ((pte = walk(pagetable, va0, 0)) == 0)
    {
      // printf("copyout: pte should exist 0x%x %d\n", dstva, len);
      return -1;
    }

    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0)
      return -1;
    // 虚拟地址对应的物理地址，后续要将 src 数据拷贝到对应物理地址
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    // 第一次拷贝时 n 不是 PGSIZE 倍数
    // 之后按页拷贝
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// -- 4096     == n = PGSIZE - (dstva - va0);
// -- ******
// -- dstva    == pa0 + (dstva - va0)
// -- *******
// -- va0      == pa0

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    // 物理地址
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    // 从用户虚拟地址拷贝到内核的dst
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0)
    {
      if (*p == '\0')
      {
        *dst = '\0';
        got_null = 1;
        break;
      }
      else
      {
        *dst = *p; // 复制一个字符
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null)
  {
    return 0;
  }
  else
  {
    return -1;
  }
}

void print_dots(int n)
{
  for (int i = 0; i < n; i++)
  {
    printf(" ..");
  }
}

// 0*512*512
// 1*512*512
//
void vmprintwalk(pagetable_t pagetable)
{
  uint64 index = 0;
  for (int i = 0; i < 512; ++i)
  {
    pte_t pte_1 = pagetable[i];
    if (pte_1 & PTE_V)
    {
      print_dots(1);
      uint64 child_1 = PTE2PA(pte_1);
      printf("--> index = %ld\n", index / PGSIZE);
      printf("%p: pte %p pa %p\n", (void *)index, (void *)pte_1, (void *)child_1);
      for (int j = 0; j < 512; ++j)
      {
        pte_t pte_2 = ((pagetable_t)child_1)[j];
        if (pte_2 & PTE_V)
        {
          print_dots(2);
          uint64 child_2 = PTE2PA(pte_2);
          printf("%p: pte %p pa %p\n", (void *)index, (void *)pte_2, (void *)child_2);
          for (int k = 0; k < 512; ++k)
          {
            pte_t pte_3 = ((pagetable_t)child_2)[k];
            if (pte_3 & PTE_V)
            {
              print_dots(3);
              uint64 child_3 = PTE2PA(pte_3);
              printf("%p: pte %p pa %p\n", (void *)index, (void *)pte_3, (void *)child_3);
            }
            index += PGSIZE;
          }
        }
        else
        {
          index += 512 * PGSIZE;
        }
      }
    }
    else
    {
      index += 512 * 512 * PGSIZE;
    }
    if (index > MAXVA)
    {
      return;
    }
  }
}

void vmprinthelper(pagetable_t pagetable, int level, uint64 va)
{
  // 遍历当前页表的所有PTE
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if (pte & PTE_V)
    { // 如果PTE有效
      // 计算当前PTE对应的虚拟地址
      // L2 L1 L0 级, 最高级为 L2
      // i 是索引
      uint64 child_va = va | ((uint64)i << (9 * (2 - level) + 12));

      // 打印缩进
      for (int j = 0; j < level + 1; j++)
      {
        printf(" ..");
      }

      uint64 pa = PTE2PA(pte);
      printf("%p: pte %p pa %p\n", (void *)child_va, (void *)pte, (void *)pa);

      // 如果这是一个中间节点（指向下一级页表）
      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0)
      {
        // 递归处理下一级页表
        if (level < 2)
        {
          vmprinthelper((pagetable_t)pa, level + 1, child_va);
        }
      }
    }
  }
}

#ifdef LAB_PGTBL
void vmprint(pagetable_t pagetable)
{
  // your code here
  printf("page table %p\n", pagetable);
  vmprinthelper(pagetable, 0, 0);
}
#endif

#ifdef LAB_PGTBL
pte_t *
pgpte(pagetable_t pagetable, uint64 va)
{
  return walk(pagetable, va, 0);
}
#endif