

#include "kernel/types.h"
#include "user/user.h"

// ex8.c: communication between two processes
// 将标准输入链接到 pipe 的读区端，来执行 wc 程序
// echo "hello world" | wc
int main()
{
  int p[2];
  char *argv[2];
  // wc 从 0 号描述符获取数据
  argv[0] = "wc";
  argv[1] = 0;

  // 对写端和读端进行初始化
  pipe(p);
  if (fork() == 0)
  {
    // 0 号标准输入
    close(0);
    // dup 在进程中创建新的文件描述符，从系统中选择最小可用的返回，由于上面关闭了0号描述符
    // 这里将0号和p[0] 进行关联
    dup(p[0]);
    // 关闭读端
    close(p[0]);
    // 关闭写端
    close(p[1]);
    //当你在管道（pipe）上执行读取操作时，如果没有数据可用，读取操作会阻塞直到满足以下条件之一：
        // 1.有数据被写入管道。
        // 2.所有指向管道写端的文件描述符都被关闭。
    exec("/bin/wc", argv);
  }
  else
  {
    // 关闭读端
    close(p[0]);
    // 写数据
    write(p[1], "hello world\n", 12);
    // 关闭写端
    close(p[1]);
  }

  exit(0);
}