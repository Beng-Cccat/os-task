<h1><center>lab1实验报告</center></h1>

<center>组长：谢雯菲  学号：2110803</center>

<center>组员：吴静   学号：2113285</center>

<center>组员：李浩天  学号：2110133</center>

## 练习一：理解内核启动中的程序入口操作

<div>
阅读 kern/init/entry.S内容代码，结合操作系统内核启动流程，说明指令 la sp, bootstacktop 完成了什么操作，目的是什么？ tail kern_init 完成了什么操作，目的是什么？
</div>

**问题解答：**

`kern/init/entry.s`代码如下：

```
#include <mmu.h>
#include <memlayout.h>

#.text段由此开始
    .section .text,"ax",%progbits
#.globl使得编译脚本可以读取到符号kern_entry的位置，下文.global同理
    .globl kern_entry

kern_entry:
    la sp, bootstacktop

    tail kern_init
#.data段由此开始
.section .data
# .align 2^12，地址对齐
    .align PGSHIFT
#  定义符号boostack，boostacktop，完成内核栈分配
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
```

查看ucore操作系统内核所使用的链接脚本`tools/kernel.ld`中的相关代码：

```uscript
/* Simple linker script for the ucore kernel.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

OUTPUT_ARCH(riscv)
ENTRY(kern_entry)
BASE_ADDRESS = 0x80200000;

SECTIONS /*section指令指示了输出文件的所有section的布局*/
{
    /* Load the kernel at this address: "." means the current address */
    . = BASE_ADDRESS;

    .text : {
        *(.text.kern_entry)
        *(.text .stub .text.* .gnu.linkonce.t.*)
    }

    PROVIDE(etext = .); /* Define the 'etext' symbol to this value */

    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    }

    /* Adjust the address for the data segment to the next page */
    . = ALIGN(0x1000);

    /* The data segment */
    .data : {
        *(.data)
        *(.data.*)
    }
...
```

以及通过lab0.5的gdb验证，我们可以得知`kern_entry`是整个内核的入口点，这一段代码被放置在了链接输出文件的`.text`段的开头处，即`0x80200000`。

`entry.s`文件的`.data`段则定义了内核栈的大小，以及符号`bootstack`，`bootstacktop`。由链接脚本生成于最终的输出文件中。

`kern_entry`作为整个内核的入口点，作用为完成内核栈的分配：

1. `la sp,bootstacktop`：
   
   RISC-V伪指令`la rd symbol`，将符号`bootstacktop`标记的地址赋值给`sp`栈指针寄存器，初始化栈指针，为栈分配内存空间。

2. `tail kern_init`：
   
   RISC-V伪指令`tail symbol`，尾调用，设置`pc`寄存器为`kern_init`标记的地址，调用操作系统内核实际的入口点`kern_init`。使用尾调用避免了保存返回地址和对栈的操作。

## 练习二：完善中断处理

请编程完善trap.c中的中断处理函数trap，在对时钟中断进行处理的部分填写kern/trap/trap.c函数中处理时钟中断的部分，使操作系统每遇到100次时钟中断后，调用print_ticks子程序，向屏幕上打印一行文字”100 ticks”，在打印完10行后调用sbi.h中的shut_down()函数关机。

要求完成问题1提出的相关函数实现，提交改进后的源代码包（可以编译执行），并在实验报告中简要说明实现过程和定时器中断中断处理的流程。实现要求的部分代码后，运行整个系统，大约每1秒会输出一次”100 ticks”，输出10行。

**实现代码如下：**

```c
clock_set_next_event();
ticks++;
if(ticks==TICK_NUM)
    {
        print_ticks();
        ticks=0;
        num++;
    }
if(num==10)
    sbi_shutdown();
```

**实现过程：**

调用函数`clock_set_next_event()`，该函数定义于`kern/driver/clock.c`，查阅可知该函数调用接口`sbi_set_timer()`，设置了下一次时钟中断的时间（调用接口`get_cycles()`获得当前周期数，并在此基础上+100000）。之后计数器`ticks`+1。当`ticks`到100时，调用函数`print_ticks()`，向屏幕输出“100 ticks”，然后`ticks`置0，计数器`num`+1。当`num`到10时，调用函数`sbi_shutdown()`关机。

**定时器中断处理流程：**

中断发生时，CPU跳转至CSR`stvec`，当前我们采用`Direct`模式，仅有一个中断处理程序，`stvec`直接保存该中断处理程序的入口点。

`stvec`在操作系统内核初始化时被设置为该中断处理程序的入口点（`kern_init()`调用函数`idt_init()`），相关代码如下：

```c
//定义于kern/trap.c
void idt_init(void) {
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}
```

中断处理程序入口点符号`__alltraps`定义于`kern/trap/trap.h`，该文件同时定义了：

结构体`trapFrame`，用于保存通用寄存器x0到x31以及中断相关的CSR的值；

汇编宏`SAVE_ALL`，保存所有寄存器（即**上下文**）到栈顶，且符合`trapFrame`的结构；

汇编宏`RESTORE_ALL`，恢复上下文。

中断处理程序如下：

```nasm
__alltraps:
    SAVE_ALL

    move  a0, sp
    jal trap
    # sp should be the same as before "jal trap"

    .globl __trapret
__trapret:
    RESTORE_ALL
    # return from supervisor call
    sret
```

该程序保存`trapFrame`到栈后，执行`mov a0,sp`将`sp`保存到`a0`中作为函数参数，调用中断处理程序`trap()`。该函数进一步调用`trap_dispatch()`，将中断按照类型（通过`trapFrame`保存的CSR`cause`判断）分发给`interrupt_handler()`，`exception_handler()`（定时器中断由`interrupt_handler()`处理）。

`interrupt_handler()`根据CSR`cause`的值，跳转到`IRQ_S_TIMER`处执行定时器中断处理的相关代码。

## 扩展练习 Challenge1：描述与理解中断流程

回答：描述ucore中处理中断异常的流程（从异常的产生开始），其中mov a0，sp的目的是什么？SAVE_ALL中寄寄存器保存在栈中的位置是什么确定的？对于任何中断，__alltraps 中都需要保存所有寄存器吗？请说明理由。

### ucore处理中断异常的流程

1. **中断或异常触发**：调用`libs/sbi.h`中声明的`set_sbi_timer`接口，传入一个时刻，通过OpenSBI的时钟事件触发一个中断或异常。
2. **跳转到中断处理程序**：当中断或异常发生时，跳转到`kern/trap/trapentry.S`的`__alltraps`标记。
3. **保存上下文**：在进入中断处理程序之前，当前执行流的上下文（包括寄存器状态、程序计数器等）需要被保存，以便在处理完中断后能够正确恢复执行。在`__alltraps`标记中调用汇编宏`SAVE_ALL`，用来保存所有寄存器（上下文）到栈顶（内存中）。
4. **执行中断处理程序**：通过函数调用，跳转到`kern/trap/trap.c`的中断处理函数`trap()`，进入`trap()`的执行流。切换前的上下文会作为一个`trapFrame`结构体，传递给`trap()`作为函数参数。`kern/trap/trap.c`通过调用`trap_dispatch()`函数判断中断的类型是`nterrupt_handler`还是`exception_handler`，并进行分发，执行时钟中断对应的处理语句。
5. **恢复上下文**：`trap()`的执行流执行结束后返回`kern/trap/trapentry.S`，跳转到`__trapret`标记处，调用汇编宏`RESTORE_ALL`恢复原先的上下文，中断处理结束。

### mov a0, sp的目的

`mov a0, sp`汇编指令的作用是将栈指针`sp`的值复制到寄存器`a0`中。

在`trap()`函数处理中断或异常时，通常会需要原本上下文中的数据。这些上下文在进入中断处理程序之前会作为一个`trapFrame`结构体存在栈上。通过将栈指针`sp`复制到`a0`寄存器中，`a0`寄存器会把栈指针作为参数传给`trap()`函数，方便调用。

### SAVE_ALL中寄存器保存在栈中的位置

使用`addi sp, sp, -36 * REGBYTES`指令将栈指针`sp`向下移动，为寄存器的保存分配一定的栈空间。`36 * REGBYTES`表示保存这些寄存器需要占据36个REGBYTES的空间（REGBYTES是`libs/riscv.h`中定义的常量，表示一个寄存器占据几个字节，在64位RISCV架构里我们定义为64位无符号整数）。

将寄存器作为一个`trapFrame`结构体进行保存。依次保存32个通用寄存器，在保存4个和中断有关的CSR时，先将csrr读取到通用寄存器，再从通用寄存器STORE到内存。

因此SAVE_ALL中寄存器保存在栈中的位置是由中断发生前栈顶所在的位置和寄存器保存时的顺序决定的。

### __alltraps中是否需要保存所有寄存器

需要。保存寄存器后的栈顶指针需要传给`trap()`函数作为参数，如果不保存所有寄存器，函数参数将不完整，中断或异常处理将出现问题。

## 扩展练习 Challenge2：理解上下文切换机制

回答：在trapentry.S中汇编代码 csrw sscratch, sp；csrrw s0, sscratch, x0实现了什么操作，目的是什么？save all里面保存了stval scause这些csr，而在restore all里面却不还原它们？那这样store的意义何在呢？

### csrw sscratch, sp实现的操作

`csrw`是一个实现特权级别的寄存器和一个通用寄存器之间的写操作的指令。`csrw sscratch, sp`这条指令将`sp`寄存器的值写入了`sscratch`寄存器中，旨在保存原先的栈顶指针到`sscratch`寄存器。

由于之后保存上下文时要为寄存器的保存分配内存空间，栈顶指针的值会改变。为了保证中断处理程序执行完后能够恢复上下文，需要存储之前`sp`栈顶寄存器的值。

### csrrw s0, sscratch, x0实现的操作

`csrrw`是一个实现特权级别的寄存器操作的指令。`csrrw s0, sscratch, x0`这条指令从`sscratch`中读取当前值，并将该值写入通用寄存器`s0`。之后将`x0`的值写入`sscratch`寄存器，覆盖其中原有的值（在RISCV架构中，`x0`寄存器被视为零寄存器，因此这里相当于清空`sscratch`寄存器）。

由于RISCV不能直接从CSR写到内存，需要csrr把CSR读取到通用寄存器，再从通用寄存器STORE到内存。因此这里将`sscratch`中的值读入通用寄存器后再保存到内存。

### save all里面保存了stval scause这些csr，而在restore all里面却不还原它们？store的意义何在？

部分CSR寄存器中存储的信息如下：

+ `sscratch`寄存器：通常用于保存临时数据，以便在异常处理或中断处理期间保持上下文信息。
+ `sstatus`寄存器：包含了当前的处理器状态信息，如特权级别、中断使能位、用户/核心模式等。
+ `sepc`寄存器：存储了异常或中断的返回地址。
+ `sbadaddr`寄存器：存储了导致异常的虚拟地址或物理地址。
+ `scause`寄存器：包含了导致异常或中断的原因代码。

在异常处理程序中，通常会使用`stval`和`scause`等CSR寄存器来确定异常的类型和原因，并采取适当的措施来处理异常。因此，我们需要在保存上下文时将这些寄存器的值保存到内存，以便`trap()`处理函数的调用。

但部分寄存器（例如`scause`、`sbadaddr`等）的值通常是保持不变的，因此不用在`RESTORE_ALL`中恢复它们。

## 扩展练习Challenge3：完善异常中断

编程完善在mret触发一条非法指令异常，和在kern/trap/trap.c的异常处理函数中捕获，并对其进行处理，简单输出异常类型和异常指令触发地址，即“Illegal instruction caught at 0x(地址)”，“ebreak caught at 0x（地址）”与“Exception type:Illegal instruction”，“Exception type: breakpoint”。

完善部分的代码如下所示：

```c
case CAUSE_ILLEGAL_INSTRUCTION:
    // 非法指令异常处理
    /* LAB1 CHALLENGE3   2110803 :  */
    cprintf("Exception type: Illegal instruction\n");
    cprintf("Illegal instruction caught at 0x%x\n", tf->epc);
    tf->epc += 4; //更新epc寄存器
    /*(1)输出指令异常类型（ Illegal instruction）
     *(2)输出异常指令地址
     *(3)更新 tf->epc寄存器
    */
    break;
case CAUSE_BREAKPOINT:
    //断点异常处理
    /* LAB1 CHALLLENGE3   2110803 :  */
    cprintf("Exception type: breakpoint\n");
    cprintf("ebreak caught at 0x%x\n", tf->epc);
    tf->epc += 2; //更新epc寄存器
    /*(1)输出指令异常类型（ breakpoint）
     *(2)输出异常指令地址
     *(3)更新 tf->epc寄存器
    */
    break;
```

由于`ebreak`指令占2字节长，因此，在断点异常处理中，epc需要加2以更新。

修改`kern/init/init.c`中的`kern_init`函数如下：

```c
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    cons_init();  // init the console

    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);

    print_kerninfo();

    // grade_backtrace();

    idt_init();  // init interrupt descriptor table

    // rdtime in mbare mode crashes
    clock_init();  // init clock interrupt

    intr_enable();  // enable irq interrupt

    asm("mret");
    asm("ebreak");

    while (1)
        ;
}
```

在最后加入`asm("mret");`，用于在执行完`intr_enable()`函数后从M模式回到特权模式，为了之后触发非法指令异常。

在之后加入`asm("ebreak");`，这是RISCV中的断点指令，用于在程序中插入一个断点。在这里，它会触发一个断点异常，这样内核就会进入中断处理程序，运行到我们之前写的程序的位置。

运行结果如下所示：

```
OpenSBI v0.4 (Jul  2 2019 11:53:53)
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name          : QEMU Virt Machine
Platform HART Features : RV64ACDFIMSU
Platform Max HARTs     : 8
Current Hart           : 0
Firmware Base          : 0x80000000
Firmware Size          : 112 KB
Runtime SBI Version    : 0.1

PMP0: 0x0000000080000000-0x000000008001ffff (A)
PMP1: 0x0000000000000000-0xffffffffffffffff (A,R,W,X)
(THU.CST) os is loading ...


Special kernel symbols:
  entry  0x000000008020000c (virtual)
  etext  0x0000000080200a7e (virtual)
  edata  0x0000000080204010 (virtual)
  end    0x0000000080204028 (virtual)
Kernel executable memory footprint: 17KB
++ setup timer interrupts
sbi_emulate_csr_read: hartid0: invalid csr_num=0x302
Exception type: Illegal instruction
Illegal instruction caught at 0x80200050
Exception type: breakpoint
ebreak caught at 0x80200054
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
```