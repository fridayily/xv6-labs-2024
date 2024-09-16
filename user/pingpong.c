#include "kernel/types.h"
#include "user/user.h"

// ex8.c: communication between two processes

int main()
{
    int n, pid;
    //   int status;
    int fds[2];
    char buf[100];

    // create a pipe, with two FDs in fds[0], fds[1].
    pipe(fds);

    pid = fork();
    if (pid == 0)
    {
        // child
        int child_pid = getpid();
        n = read(fds[0], buf, sizeof(buf));
        if (n <= 0)
            exit(1);
        close(fds[0]);
        printf("%d: received ping\n", child_pid);
        write(fds[1], buf, n);
        close(fds[1]);
        exit(0);
    }
    else
    {
        // parent
        int parent_pid = getpid();

        write(fds[1], "a", 1);
        close(fds[1]);

        wait(0);
        n = read(fds[0], buf, sizeof(buf));
        if (n <= 0)
            exit(1);
        close(fds[0]);
        printf("%d: received pong\n", parent_pid);
    }
    exit(0);
}