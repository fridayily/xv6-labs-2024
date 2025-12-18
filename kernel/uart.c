//
// low-level driver routines for 16550a UART.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html

// RHR	0	只读	接收保持寄存器 Receive Holding Register 存储刚接收到的字节
// THR	0	只写	发送保持寄存器 Transmit Holding Register 存储待发送的字节
// IER	1	读写	中断使能寄存器 Interrupt Enable Register 控制UART中断
// FCR	2	只写	FIFO控制寄存器 FIFO Control Register 配置收发FIFO
// ISR	2	只读	中断状态寄存器 Interrupt Status Register 指示中断类型
// LCR	3	读写	线路控制寄存器 Line Control Register 配置数据格式和波特率
// LSR	5	只读	线路状态寄存器 Line Status Register 指示UART状态
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  // 进入波特率设置模式
  // 进入该模式后，0 号寄存器和 1 号寄存器就会变为波特率设置寄存器
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&uart_tx_lock, "uart");
}

// 用户进程 → write() 系统调用 → sys_write() → filewrite() → consolewrite() → uartputc() → UART 硬件

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  if(panicked){
    for(;;)
      ;
  }
  // 当发送缓冲区满时，进程调用sleep()进入睡眠状态
  // 睡眠通道为&uart_tx_r，与uartstart()中的唤醒通道一致
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  
  // 字符写到缓冲区，此时缓冲区肯定非满
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  // 发送缓冲区的写指针 +1 
  uart_tx_w += 1;
  // 调用 uartstart()尝试立即发送字符，该函数会让 uart_tx_r +1
  uartstart();
  release(&uart_tx_lock);
}


// printf->consputs->uartputc_sync->WriteReg(THR, c)

// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  // 如果系统已处于panic状态，进入无限循环
  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  // 不断检查LSR（线路状态寄存器）的LSR_TX_IDLE位
  // LSR_TX_IDLE（0x20）表示UART的发送保持寄存器（THR）已准备好接收新字符
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  // 字符被写入 THR 后，UART 硬件自动将其复制到发送移位寄存器(TSR)
  // TSR 将并行数据转换为串行比特流, 串行比特流通过物理串行线路发送出去
  WriteReg(THR, c);

  pop_off();
}


// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void
uartstart()
{
  while(1){
    // 当写指针(uart_tx_w)等于读指针(uart_tx_r)时，表示缓冲区为空
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      // 读取ISR寄存器（中断状态寄存器）用于清除可能的未处理中断状态
      ReadReg(ISR);
      return;
    }
    
    // 读取LSR寄存器（线路状态寄存器），检查LSR_TX_IDLE位
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    // 1. 从循环缓冲区获取字符
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    // 2. 更新读指针（模运算实现循环）
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    // 3. 唤醒可能等待缓冲区空间的进程
    wakeup(&uart_tx_r);
    // 4. 发送字符到UART硬件
    WriteReg(THR, c);
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().
void
uartintr(void)
{
  // read and process incoming characters.
  while(1){
    // 从UART硬件读取一个字符
    int c = uartgetc();
    if(c == -1)
      break;
  // 负责回显字符、处理特殊键（如退格、换行）
    consoleintr(c);
  }

  // send buffered characters.
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}


// 输入流程
// 进程调用 uartputc() → 字符进入发送缓冲区 → uartstart()尝试发送 → 
//    若UART忙则等待 → UART发送完成触发输出中断 → uartintr() → uartstart()继续发送


// 输出流程
// 进程调用uartputc() → 字符进入发送缓冲区 → uartstart()尝试发送 → 
//    若UART忙则等待 → UART发送完成触发输出中断 → uartintr() → uartstart()继续发送