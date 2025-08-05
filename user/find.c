#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *target) {
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // 打开目录
  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  // 如果是文件，直接比较
  if (st.type == T_FILE) {
    char *filename = path + strlen(path);
    while (filename >= path && *filename != '/') {
      filename--;
    }
    filename++;
    if (strcmp(filename, target) == 0) {
      printf("%s\n", path);
    }
    close(fd);
    return;
  }

  // 是目录则递归进入
  if (st.type != T_DIR) {
    close(fd);
    return;
  }

  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
    printf("find: path too long\n");
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0) continue;

    if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
      continue;

    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;

    if (stat(buf, &st) < 0) {
      printf("find: cannot stat %s\n", buf);
      continue;
    }

    if (st.type == T_DIR) {
      find(buf, target);  // 递归处理子目录
    } else if (st.type == T_FILE) {
      if (strcmp(de.name, target) == 0) {
        printf("%s\n", buf);
      }
    }
  }

  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(2, "Usage: find <path> <filename>\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}

