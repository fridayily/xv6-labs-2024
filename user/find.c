#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void find(char *dir_name, char *file_name)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    // 只读方式打开路径
    if ((fd = open(dir_name, O_RDONLY)) < 0)
    {
        fprintf(2, "ls: cannot open %s\n", dir_name);
        return;
    }
    // 对返回的文件描述符进行统计
    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "ls: cannot stat %s\n", dir_name);
        close(fd);
        return;
    }

    switch (st.type)
    {
    case T_DEVICE:
    // 文件类型
    case T_FILE:
        fprintf(2,"not a dir");
        close(fd);
        exit(1);
    // 路径类型
    case T_DIR:
        if (strlen(dir_name) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            fprintf(2,"find: path too long\n");
            break;
        }
        strcpy(buf, dir_name);
        p = buf + strlen(buf);
        // 文件夹名后面加 /
        *p++ = '/';
        // 文件夹就是包含一系列文件结构体的结构体
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            {
                // printf("line %d de.name %s\n",__LINE__, de.name);
                continue;
            }
            // 文件夹后面加文件名
            memmove(p, de.name, DIRSIZ);

            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0)
            {
                fprintf(2,"find: cannot stat %s\n", buf);
                continue;
            }
            if (st.type == T_FILE && strcmp(de.name, file_name) == 0)
            {
                fprintf(1,"%s\n", buf);
            }
            else if (st.type == T_DIR)
            {
                find(buf, file_name);
            }
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        printf("argc is wrong");
        exit(1);
    }
    char *dir_name = argv[1];
    char *file_name = argv[2];
    find(dir_name, file_name);

    exit(0);
}