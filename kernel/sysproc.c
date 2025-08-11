#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
extern pte_t* walk(pagetable_t pagetable, uint64 va, int alloc);

int sys_pgaccess(void)
{
  uint64 base;      // 起始虚拟地址
  int len;          // 检查的页数
  uint64 user_mask; // 用户空间结果存放地址

  // 参数解析与校验
  if (argaddr(0, &base) < 0 ||
      argint(1, &len) < 0 ||
      argaddr(2, &user_mask) < 0)
    return -1;
  if (len <= 0 || len > 64)
    return -1;

  struct proc *p = myproc();
  uint64 mask = 0;

  // 遍历每一页并检查访问位
  for (int i = 0; i < len; i++) {
    uint64 va = base + i * PGSIZE;
    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte && (*pte & PTE_V)) {
      if (*pte & PTE_A) {
        mask |= (1UL << i);   // 记录访问过的页
        *pte &= ~PTE_A;       // 清除访问标志
      }
    }
  }

  // 将结果返回到用户空间
  if (copyout(p->pagetable, user_mask, (char *)&mask, sizeof(mask)) < 0)
    return -1;

  return 0;
}
#endif


uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
