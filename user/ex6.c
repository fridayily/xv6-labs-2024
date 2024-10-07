#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ex6.c: run a command with output redirected

int
main()
{
  int pid;

  pid = fork();
  if(pid == 0){
    // 子进程
    close(1);
    open("out", O_WRONLY | O_CREATE | O_TRUNC);

    char *argv[] = { "echo", "this", "is", "redirected", "echo", 0 };
    exec("echo", argv);
    printf("exec failed!\n");
    exit(1);
  } else {
    // 等待子进程结束
    // 子进程结束后 wait 返回子进程的 PID，并通过 status 参数传递子进程的退出状态 
    // 当传递 (int *) 0 时，实际上传递的是 NULL 指针
    // 如果传递 NULL 指针,父进程不关心子进程的退出状态
    wait((int *) 0);
  }

  exit(0);
}