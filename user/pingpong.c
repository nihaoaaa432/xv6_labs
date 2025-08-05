#include "kernel/types.h"
#include "user/user.h"

int main() {
  int p1[2]; // 父 -> 子
  int p2[2]; // 子 -> 父

  pipe(p1);
  pipe(p2);

  int pid = fork();

  if (pid > 0) {
    // 父进程
    char byte = 'A';
    write(p1[1], &byte, 1);    // 写给子进程
    wait(0);                   // 等子进程结束

    read(p2[0], &byte, 1);     // 从子进程读回来
    printf("%d: received pong\n", getpid());

    exit(0);

  } else if (pid == 0) {
    // 子进程
    char byte;
    read(p1[0], &byte, 1);     // 读取父进程发来的数据
    printf("%d: received ping\n", getpid());

    write(p2[1], &byte, 1);    // 写回给父进程
    exit(0);
  }

  exit(1); // 不应到达
}

