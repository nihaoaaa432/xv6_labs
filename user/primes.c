#include "kernel/types.h"
#include "user/user.h"

// 每个进程的任务：读来自左边的管道，筛选掉被当前prime整除的数，把其余写到右边的管道
void sieve(int p[2]) {
  close(p[1]); // 只读，不写
  int prime;
  if (read(p[0], &prime, sizeof(int)) != sizeof(int)) {
    close(p[0]);
    exit(0); // 管道读不到了
  }
  printf("prime %d\n", prime);

  int next;
  int newpipe[2];
  pipe(newpipe);
  int pid = fork();
  if (pid == 0) {
    // 子进程递归继续处理
    sieve(newpipe);
  } else {
    close(newpipe[0]); // 父进程写，子进程读
    while (read(p[0], &next, sizeof(int)) == sizeof(int)) {
      if (next % prime != 0) {
        write(newpipe[1], &next, sizeof(int));
      }
    }
    close(p[0]);
    close(newpipe[1]);
    wait(0); // 等待子进程
    exit(0);
  }
}

int main() {
  int p[2];
  pipe(p);
  int pid = fork();

  if (pid == 0) {
    // 子进程开始筛选
    sieve(p);
  } else {
    // 父进程写入 2 ~ 35
    close(p[0]);
    for (int i = 2; i <= 35; i++) {
      write(p[1], &i, sizeof(int));
    }
    close(p[1]); // 写完关闭写端
    wait(0);     // 等子进程结束
  }
    exit(0);
}
