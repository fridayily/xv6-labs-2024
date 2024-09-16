#include "kernel/types.h"
#include "user/user.h"

void prime(int rd_fd)
{

    int fds[2];

    pipe(fds);
    int first_num;
    int n = read(rd_fd, &first_num, sizeof(first_num));
    if (n > 0)
    {
        fprintf(1, "prime %d\n", first_num);

        int pid = fork();
        if (pid == 0)
        {
            close(rd_fd);
            exit(0);
        }
        else
        {
            int other_num;
            while ((n = read(rd_fd, &other_num, sizeof(other_num)) != 0))
            {
                if (other_num % first_num != 0)
                {
                    write(fds[1], &other_num, sizeof(other_num));
                }
            }
            close(rd_fd);
            close(fds[1]);
            prime(fds[0]);
            wait(0);
            exit(0);
        }
    }
    else
    {
        close(rd_fd);
        exit(0);
    }
}

int main()
{
    int cnt = 36;
    int fds[2];
    pipe(fds);
    for (int i = 2; i < cnt; ++i)
    {
        write(fds[1], &i, sizeof(i));
    }
    close(fds[1]);
    prime(fds[0]);
    exit(0);
}