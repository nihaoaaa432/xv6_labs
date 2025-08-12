// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int refcnt[PHYSTOP / PGSIZE];  // 每个物理页的引用数量
} pageref;

// 根据物理地址计算索引
static int
page_idx(void *pa)
{
  return ((uint64)pa - (uint64)end) / PGSIZE;
}

void
initref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("initref");
  acquire(&pageref.lock);
  pageref.refcnt[page_idx(pa)] = 1;
  release(&pageref.lock);
}

// 增加引用数并返回更新后的值
int
addref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("addref");
  acquire(&pageref.lock);
  int val = ++pageref.refcnt[page_idx(pa)];
  release(&pageref.lock);
  return val;
}

// 减少引用数并返回结果
int
subref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("subref");
  acquire(&pageref.lock);
  int idx = page_idx(pa);
  if(pageref.refcnt[idx] <= 0)
    panic("subref: already zero");
  int val = --pageref.refcnt[idx];
  release(&pageref.lock);
  return val;
}

// 获取引用次数
int
readref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("readref");
  acquire(&pageref.lock);
  int val = pageref.refcnt[page_idx(pa)];
  release(&pageref.lock);
  return val;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pageref.lock, "pageref");
  memset(pageref.refcnt, 0, sizeof(pageref.refcnt));
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *start, void *end_addr)
{
  char *p = (char*)PGROUNDUP((uint64)start);
  for(; p + PGSIZE <= (char*)end_addr; p += PGSIZE) {
    initref((void*)p);
    kfree(p);
  }
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  if(subref(pa) > 0) {
    return; // 仍有引用，不释放
  }
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *
kalloc(void)
{
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    initref((void*)r);
  }
  release(&kmem.lock);
  if(r)
    memset((char*)r, 5, PGSIZE);
  return (void*)r;
}
