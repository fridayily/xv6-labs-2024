#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

void print_buf(char *buf[], int n)
{
    printf("n=%d buf: \n", n);
    for (int i = 0; i < n; ++i)
    {
        printf(" %s ", buf[i]);
    }
    printf("\n");
}

void exec_fun(const char* file_name,char **param)
{
    int pid;
    pid = fork();
    if (pid == 0)
    {

        exec(file_name, param);
        printf("exec failed!\n");
        exit(1);
    }
    else
    {
        wait((int *)0);
    }
}

int main(int argc, char *argv[])
{

    if (argc < 3)
    {
        printf("error argc");
        exit(1);
    }
    char buf[64];

    char a;
    char *param_buf[MAXARG];
    int i = 0;
    int j = 0;
    while (read(0, &a, 1) == 1)
    {
        if (a != '\n')
        {
            buf[i] = a;
            ++i;
        }
        else
        {
            param_buf[j] = malloc(i);
            memcpy(param_buf[j], buf, i);
            i = 0;
            ++j;
        }
    }

    if (strcmp(argv[1], "-n") != 0)
    {
        for (int n = 0; n < j; ++n)
        {
            char *param[MAXARG];
            int m = 0;
            for (int i = 1; i < argc; ++i)
            {
                param[m] = argv[i];
                ++m;
            }
            param[m] = param_buf[n];
            param[++m] = 0;
            exec_fun(argv[1],param);
        }
    }
    else
    {

        int cnt = atoi(argv[2]);

        for (int k = 0; k < j;)
        {
            char *param[cnt + 2]; // +2 保存命令 和 0
            param[0] = argv[3];
            int m = 0;
            for (m = 0; m < cnt && k < j; ++m)
            {
                param[m + 1] = param_buf[k];
                ++k;
            }
            param[m + 1] = 0;
            exec_fun(argv[3],param);
        }
    }
    exit(0);
}
// sh < xargstest.sh
// find . b | xargs grep hello
// echo hello too | xargs echo bye
// (echo 1 ; echo 2) | xargs -n 1 echo
// (echo 123 ; echo 456 ; echo 789) | xargs -n 2 echo