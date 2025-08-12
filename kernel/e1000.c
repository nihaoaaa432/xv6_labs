#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int e1000_transmit(struct mbuf *m) {
  acquire(&e1000_lock);  // 获取锁，保证对发送环操作的互斥
  uint32 tdt = regs[E1000_TDT];  // 当前发送描述符索引
  // 判断发送环是否已满（DD位未置位表示硬件尚未处理该描述符）
  if ((tx_ring[tdt].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);  // 解锁
    return -1;             // 发送失败，环已满
  }
  // 释放先前存储在该描述符位置的 mbuf，防止内存泄漏
  if (tx_mbufs[tdt] != 0) {
    mbuffree(tx_mbufs[tdt]);
  }
  // 填充当前描述符，设置数据地址和长度
  tx_ring[tdt].addr = (uint64)m->head;
  tx_ring[tdt].length = m->len;
  // 设置命令标志，表示数据包结束(EOP)并要求状态更新(RS)
  tx_ring[tdt].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  // 保存当前 mbuf 指针，用于后续释放
  tx_mbufs[tdt] = m;
  // 更新描述符索引，环形递增
  regs[E1000_TDT] = (tdt + 1) % TX_RING_SIZE;
  release(&e1000_lock);  // 释放锁
  return 0;              // 发送成功
}

static void e1000_recv(void) {
  uint32 rdt = (regs[E1000_RDT] + 1) % RX_RING_SIZE;  // 下一个待处理描述符索引
  while (1) {
    // 若描述符状态中的 DD 位未置位，说明无新包，跳出循环
    if ((rx_ring[rdt].status & E1000_RXD_STAT_DD) == 0) {
      break;
    }
    // 检查数据包长度是否超过 mbuf 可承载最大值，防止溢出
    if (rx_ring[rdt].length > MBUF_SIZE) {
      panic("MBUF_SIZE OVERFLOW!");
    }
    struct mbuf *m = rx_mbufs[rdt];     // 获取当前 mbuf
    m->len = rx_ring[rdt].length;       // 更新 mbuf 长度
    net_rx(m);                          // 将数据包交给网络栈处理
    struct mbuf *new_m = mbufalloc(0);  // 重新分配 mbuf 作为接收缓冲区
    if (new_m == 0) {
      panic("e1000_recv: mbufalloc failed");
    }
    // 将新 mbuf 地址写入描述符，并清除状态位
    rx_ring[rdt].addr = (uint64)new_m->head;
    rx_ring[rdt].status = 0;
    rx_mbufs[rdt] = new_m;              // 更新指针数组
    rdt = (rdt + 1) % RX_RING_SIZE;    // 环形递增索引
  }
  // 更新寄存器，通知网卡已处理至该描述符
  if (rdt == 0) {
    regs[E1000_RDT] = RX_RING_SIZE - 1;
  } else {
    regs[E1000_RDT] = rdt - 1;
  }
}


void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}



