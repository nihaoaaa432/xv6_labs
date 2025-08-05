#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
  char buf[512];
  char *args[MAXARG];
  int i;

  // 把原始命令参数复制到 args 数组中
  for (i = 1; i < argc; i++) {
    args[i - 1] = argv[i];
  }

  int n = i - 1; // 初始参数数量
  int index = 0;
  char ch;

  while (read(0, &ch, 1) == 1) {
    if (ch == '\n') {
      buf[index] = 0; // 添加字符串终止符
      args[n] = buf;
      args[n + 1] = 0;

      int pid = fork();
      if (pid == 0) {
        exec(args[0], args);
        fprintf(2, "exec failed\n");
        exit(1);
      } else {
        wait(0);
      }

      index = 0; // 清空 buffer，读取下一行
    } else {
      if (index < sizeof(buf) - 1) {
        buf[index++] = ch;
      }
    }
  }

  exit(0);
}

