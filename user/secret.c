#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"


int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("Usage: secret the-secret\n");
    exit(1);
  }
  char *end = sbrk(PGSIZE*32);
  end = end + 9 * PGSIZE;
  strcpy(end, "my very very very secret pw is:   ");
  strcpy(end+32, argv[1]);
  exit(0);
}

// 构建该程序过程
// 用户内存空间 data text stack_guard stack 32_page
// 真实物理分配 5(page_table) + 4(data text stack_guard stack） + 32 (sbrk(32)) 
// 释放时从低地址开始释放 
// data text stack_guard stack 32_page + 5(page_table)
// 此时可用的内存链表为 5(page_table) 32_page  stack_guard stack text data
// secret 的数据在 5 , 32th,31th,30th,...10th(密码),...1th,  stack  stack_guard text data
// attack 时
//    fork 占用 11 页 (5,32th,31th,30th,29th,28th，27th)
//    attack 的参数分配 1 页 (26 th)
//    exec 分配 9 (25th,24th,...,17th) 释放 10 (即新增9个可用page 到 kmem_list)
//    还需要 10 + (16th,...,10th)
