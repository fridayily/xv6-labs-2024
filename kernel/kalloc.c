// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void superfreerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

//  空闲页面1 (地址: 0x80200000):
// +----------------+
// | next: 0x80201000| -> 指向下一个空闲页面
// +----------------+
// |                |
// |   (4KB - 8B)   | -> 剩余空间未使用
// |    的空间       |
// +----------------+

// 空闲页面2 (地址: 0x80201000):
// +----------------+
// | next: 0x80203000| -> 指向下一个空闲页面
// +----------------+
// |                |
// |   (4KB - 8B)   | -> 剩余空间未使用
// |    的空间       |
// +----------------+

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct
{
  struct spinlock lock;
  struct run *freelist;
} superkmem;

void kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
  // initlock(&superkmem.lock, "superkmem");
  // superfreerange(end,(void *)PHYSTOP);
}

//  0x80023000->0x80022000->0x8002100 第1页
void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

//  0x80600000->0x80400000->0x80200000 第1页
// void superfreerange(void *pa_start,void *pa_end)
// {
//   char *p;
//   p = (char *)SUPERPGROUNDUP((uint64)pa_start);
//   for(; p+ SUPERPGSIZE <= (char *)pa_end;p += SUPERPGSIZE)
//     superkfree(p);
// }

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// void superkfree(void *pa)
// {
//   struct run *r;
//   if (((uint64)pa % SUPERPGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
//     panic("superfree");

//   memset(pa, 1, SUPERPGSIZE);

//   r = (struct run *)pa;
//   acquire(&superkmem.lock);
//   r->next = superkmem.freelist;
//   superkmem.freelist = r;
//   release(&superkmem.lock);
// }

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

// void *
// superkalloc(void)
// {
//   struct run *r;

//   acquire(&superkmem.lock);
//   r = superkmem.freelist;
//   if (r)
//     superkmem.freelist = r->next;
//   release(&superkmem.lock);

//   if (r)
//     memset((char *)r, 5, SUPERPGSIZE); // fill with junk
//   printf("superkalloc %p\n",(void *)r);
//   return (void *)r;
// }

void *
superkalloc(void)
{
  struct run *r = 0;
  acquire(&kmem.lock);
  struct run *current = kmem.freelist;
  struct run **prev_ptr = &kmem.freelist;

  while (current)
  {
    uint64 start_pa = (uint64)current;
    printf("start_pa %p\n",(void *)start_pa);
    if ((start_pa % SUPERPGSIZE) == 0)
    {
      printf("in start_pa %p\n",(void *)start_pa);
      struct run *end = current;
      int i;
      for (i = 0; i < 512 && end; i++)
      {
        uint64 expected_pa = start_pa - i * PGSIZE;
        if ((uint64)end != expected_pa){
          break;
        }
        end = end->next;
        // printf("end %p freelist %p\n",end,kmem.freelist);
      }

      if (i == 512)
      {
        r = current;
        *prev_ptr = end;
        // printf("512 r %p prev_ptr %p freelist %p\n",(void *)current,(void*)*prev_ptr,(void *)kmem.freelist);
        kmem.freelist = end;
        break;
      }
    }
    prev_ptr = &(current->next);
    current = current->next;
  }
  release(&kmem.lock);
  return (void *)r;
}

void superkfree(void *pa)
{
  if (((uint64)pa % SUPERPGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("superfree");
  struct run *r=(struct run *)pa;
  for(int i=0;i<511;i++){
    r[i].next = &r[i+1];
  }
  acquire(&kmem.lock);
  r[511].next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);  
}