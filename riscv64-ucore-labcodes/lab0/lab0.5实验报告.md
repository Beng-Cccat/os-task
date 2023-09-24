<h1><center>lab0.5实验报告</center></h1>

<center>组长：谢雯菲  学号：2110803</center>

<center>组员：吴静   学号：2113285</center>

<center>组员：李浩天  学号：2110133</center>



##  实验过程

进入`labcodes/lab0`，打开两个终端，其中一个终端输入`make dubug`，另外一个终端输入`make gdb`，然后继续后续操作。

### 一、复位地址

**首先**，输入指令`x/10i $pc`显示当前状态下即将执行的10条汇编指令，此时`0x1000`是复位地址，输出结果如下。

```
(gdb) x/10i $pc
=> 0x1000:	auipc	t0,0x0
   0x1004:	addi	a1,t0,32
   0x1008:	csrr	a0,mhartid
   0x100c:	ld	t0,24(t0)
   0x1010:	jr	t0
   0x1014:	unimp
   0x1016:	unimp
   0x1018:	unimp
   0x101a:	0x8000
   0x101c:	unimp
```

这里我们将对这几条汇编代码进行分析：

- `auipc rd, imm`

rd是目标寄存器，表示计算结果将被存储在哪个寄存器中；`imm`是一个立即数，这个立即数将会左移12位，然后加到当前`pc`的值上。于是原文中的`auipc t0,0x0`即表示`t0=(0x0<<12)+pc=0x1000`;`a1=t0+32(d)=0x1020`;`a0=mhartid=0`;

- `ld t0,24(t0)`

表示一个加载指令，其中24是偏移量，于是`t0=[t0+24]=[0x1a]=0x8000`；最后再由`jr t0`表示跳转至`t0`地址`0x8000`。

于是我们进行验证：

输入`si`或者`ni`单步执行，然后在相应位置使用`info r t0`查看寄存器的值，具体结果如下：

```
(gdb) si
0x0000000000001004 in ?? ()
(gdb) info r t0
t0             0x0000000000001000	4096
```

执行第一步，代码跳转至`0x1004`，`t0`寄存器的值不变。

```
(gdb) si
0x0000000000001008 in ?? ()
(gdb) info r a1
a1             0x0000000000001020	4128
(gdb) info r t0
t0             0x0000000000001000	4096
```

执行第二步，代码跳转至`0x1008`，`t0`寄存器的值不变；`a1`寄存器的值变为`0x1020`。

```
(gdb) si
0x000000000000100c in ?? ()
(gdb) info r t0
t0             0x0000000000001000	4096
(gdb) info r a0
a0             0x0000000000000000	0
```

执行第三步，代码跳转至`0x1010`，`t0`寄存器的值不变；`a0`寄存器的值变为`0x0000`。

```
(gdb) si
0x0000000000001010 in ?? ()
(gdb) info r t0
t0             0x0000000080000000	2147483648
```

执行第四步，代码跳转至`0x1010`，`t0`寄存器的值变为`0x80000000`。

```
(gdb) si
0x0000000080000000 in ?? ()
```

执行第五步，代码跳转至`0x80000000`。

### 二、kern_entry

**此时 **地址为`0x80000000`，继续输入`x/10i $pc`查看当前汇编代码，结果如下：

```
(gdb) x/10i $pc
=> 0x80000000:	csrr	a6,mhartid
   0x80000004:	bgtz	a6,0x80000108
   0x80000008:	auipc	t0,0x0
   0x8000000c:	addi	t0,t0,1032
   0x80000010:	auipc	t1,0x0
   0x80000014:	addi	t1,t1,-16
   0x80000018:	sd	t1,0(t0)
   0x8000001c:	auipc	t0,0x0
   0x80000020:	addi	t0,t0,1020
   0x80000024:	ld	t0,0(t0)
```

继续分析汇编指令：

`a6=mhartid`；

`bgtz`是一个分支指令，表示若`a6>0`，则跳转至`0x80000108`；

`t0=(0x0<<12)+pc=0x80000008`；

`t0=t0+1032(d)=0x80000410`；

`t1=pc+(0x0<<12)=0x80000010`；

`t1=t1+(-16)(d)=0x80000000`；

`sd`是一个存储指令，表示`[t0+0]=t1`，即地址`0x80000410`中存储数据`0x80000000`。

`t0=pc+(0x0<<12)=0x8000001c`；

`t0=t0+1020(d)=0x80000418`；

`ld`表示加载指令，即`t0=[t0+0]=[0x80000418]`；

```
(gdb) si
0x0000000080000004 in ?? ()
(gdb) info r a6
a6             0x0000000000000000	0
(gdb) si
0x0000000080000008 in ?? ()
(gdb) si
0x000000008000000c in ?? ()
(gdb) info r t0
t0             0x0000000080000008	2147483656
(gdb) si
0x0000000080000010 in ?? ()
(gdb) info r t0
t0             0x0000000080000410	2147484688
(gdb) si
0x0000000080000014 in ?? ()
(gdb) info r t1
t1             0x0000000080000010	2147483664
(gdb) si
0x0000000080000018 in ?? ()
(gdb) info r t1
t1             0x0000000080000000	2147483648
(gdb) si
0x000000008000001c in ?? ()
(gdb) info r t1
t1             0x0000000080000000	2147483648
(gdb) si
0x0000000080000020 in ?? ()
(gdb) info r t0
t0             0x000000008000001c	2147483676
(gdb) si
0x0000000080000024 in ?? ()
(gdb) info r t0
t0             0x0000000080000418	2147484696
(gdb) si
0x0000000080000028 in ?? ()
(gdb) info r t0
t0             0x0000000080000000	2147483648
```

具体不再阐述；

需要注意的是在最后加载`[0x80000418]`内的数据时，由汇编代码运行得出，此处加载的数据是`0x80000000`。

**紧接着**，设置断点，输入`b *0x80200000`，结果如下：

```
(gdb) break *0x80200000
Breakpoint 1 at 0x80200000: file kern/init/entry.S, line 7.
```

输入`c`，debug窗口如下所示，OpenSBI运行成功。

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
```

同时gdb窗口显示：

```
(gdb) c
Continuing.

Breakpoint 1, kern_entry () at kern/init/entry.S:7
7	    la sp, bootstacktop
```

在这行代码中，执行了一个 `la` 指令，用于将栈指针寄存器 `sp` 设置为 `bootstacktop` 的值。

查看`0x80200000`处的10条指令：

```
(gdb) x/10i $pc
=> 0x80200000 <kern_entry>:	auipc	sp,0x3
   0x80200004 <kern_entry+4>:	mv	sp,sp
   0x80200008 <kern_entry+8>:	j	0x8020000c <kern_init>
   0x8020000c <kern_init>:	auipc	a0,0x3
   0x80200010 <kern_init+4>:	addi	a0,a0,-4
   0x80200014 <kern_init+8>:	auipc	a2,0x3
   0x80200018 <kern_init+12>:	addi	a2,a2,-12
   0x8020001c <kern_init+16>:	addi	sp,sp,-16
   0x8020001e <kern_init+18>:	li	a1,0
   0x80200020 <kern_init+20>:	sub	a2,a2,a0
```

可以看到，此时`kern_entry`的地址是`0x80200000`，同时在`0x80200008 <kern_entry+8>`处执行代码`j 0x8020000c <kern_init>`跳转至`kern_init`，由此可知，`kern_entry`后面就是`kern_init`；同时，我们也知道了`kern_init`的地址为`0x8020000c`。

这一点我们在源文件`entry.S`中也可以找到印证：

```
#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    #指示汇编器将接下来的代码部分标记为代码段（.text）
    #%progbits 是一个节标志，表示该段包含可执行代码
    .globl kern_entry
    #将 kern_entry 标记为全局符号，使之可以从其他代码文件中访问
kern_entry:
    la sp, bootstacktop
	#将栈指针 sp 的值设置为 bootstacktop 的地址
    tail kern_init
    #调用名为 kern_init 的函数，启动代码将控制权转移给内核的初始化函数

.section .data
#指示接下来的代码部分标记为数据段
    # .align 2^12
    .align PGSHIFT
    .global bootstack
    #将 bootstack 标记为全局符号，使其可以在其他代码文件中访问
bootstack:
    .space KSTACKSIZE
    #为 bootstack 分配了一些空间，大小由 KSTACKSIZE 定义
    .global bootstacktop
bootstacktop:
```

***由上面分析可知，我们在链接脚本里面将程序的入口点定义为`kern_entry`，所以在`kern/init/entry.S`中定义了一段汇编代码作为整个程序的入口点，但是该入口点的作用是分配好内核栈然后跳转到`kern_init`进行初始化***，于是我们还要对`kern_init`进行分析。

### 三、kern_init

**接下来**，输入`disassemble kern_init`查看`kern_init`的反汇编代码：

```
(gdb) disassemble kern_init
Dump of assembler code for function kern_init:
   0x000000008020000c <+0>:	auipc	a0,0x3
   0x0000000080200010 <+4>:	addi	a0,a0,-4 # 0x80203008
   0x0000000080200014 <+8>:	auipc	a2,0x3
   0x0000000080200018 <+12>:	addi	a2,a2,-12 # 0x80203008
   0x000000008020001c <+16>:	addi	sp,sp,-16
   0x000000008020001e <+18>:	li	a1,0
   0x0000000080200020 <+20>:	sub	a2,a2,a0
   0x0000000080200022 <+22>:	sd	ra,8(sp)
   0x0000000080200024 <+24>:	jal	ra,0x802004ce <memset>
   0x0000000080200028 <+28>:	auipc	a1,0x0
   0x000000008020002c <+32>:	addi	a1,a1,1208 # 0x802004e0
   0x0000000080200030 <+36>:	auipc	a0,0x0
   0x0000000080200034 <+40>:	addi	a0,a0,1232 # 0x80200500
   0x0000000080200038 <+44>:	jal	ra,0x80200058 <cprintf>
   0x000000008020003c <+48>:	j	0x8020003c <kern_init+48>
End of assembler dump.
```

这里可以看见在最后一句话有个死循环，`0x000000008020003c <+48>:j 0x8020003c <kern_init+48>`不断跳向自己，至此，程序陷入死循环。对于前面的反汇编代码，可以查看源代码`init.c`，如下所示。

```
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);
   while (1)
        ;
}
```

发现他是输出了一个`message`：`(THU.CST) os is loading ...\n`后陷入死循环。

输入`c`继续程序进行验证，发现debug窗口中确实输出了`(THU.CST) os is loading ...`，验证成功。

## 问题解答

Q：模拟的RISC-V计算机从加电开始运行到执行应用程序的第一条指令（即跳转到`0x80200000`）这个阶段的执行过程。

A：该计算机有一个内置的ROM，这个ROM用于存放复位代码，所以CPU在上电的时候先跳到`0x1000`来执行代码；在这段代码中指定将PC跳转到`0x80000000`处（将`bootloader`加载到了`0x80000000`处），这个地址下的代码包含一条跳转指令如`j`，可将控制权转移到应用程序的入口地址 `0x80200000` ，即`kern_entry`，在这里可以执行应用程序的第一条指令，分配好内核栈，然后可以跳转到kern_init。

Q：说明RISC-V硬件加电后的几条指令在哪里，完成了哪些功能？

A：分析见上。

## 重要知识点

- 一些`gdb`指令比如`si`；`x/10i $pc`；`b *address`等.
- `Qemu`服务器的启动流程
- `bootloader`：把操作系统加载到内存并把CPU的控制权交给操作系统
- `kern/init/entry.S`: OpenSBI 启动之后将要跳转到的一段汇编代码，进行内核栈的分配，然后转
  入C 语言编写的内核初始化函数`kern/init/init.c`
  `kern/init/init.c`：C 语言编写的内核入口点。





