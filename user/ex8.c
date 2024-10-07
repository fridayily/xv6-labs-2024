

#include "kernel/types.h"
#include "user/user.h"

// ex8.c: communication between two processes

int
main()
{
  int n, pid;
  int fds[2];
  char buf[100];
  
  // create a pipe, with two FDs in fds[0], fds[1].
  pipe(fds);
  // 子进程写字符串到 pipe，主线程读取出来，再通过标准输出打印
  pid = fork();
  if (pid == 0) {
    // child
    // fd1 写
    write(fds[1], "this is ex8\n", 12);
  } else {
    // parent
    // fd0 读
    n = read(fds[0], buf, sizeof(buf));
    write(1, buf, n);
  }

  exit(0);
}