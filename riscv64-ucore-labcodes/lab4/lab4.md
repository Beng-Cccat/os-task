# <center>  Lab4实验报告  </center>

<center>组长：谢雯菲  学号：2110803</center>

<center>组员：吴静   学号：2113285</center>

<center>组员：李浩天  学号：2110133</center>

## exercise1：分配并初始化一个进程控制块

`alloc_proc`函数负责分配并返回一个新的`struct proc_struct`结构，用于存储新建立的内核线程的管理信息。

首先可以看到，他返回的是一个`proc_struct`类型的指针，而我们需要做的是初始化这个结构体的成员变量，所以我们先看到这个结构体的定义如下：

```c
struct proc_struct {
    enum proc_state state;                      // Process state
    int pid;                                    // Process ID
    int runs;                                   // the running times of Proces
    uintptr_t kstack;                           // Process kernel stack
    volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
    struct proc_struct *parent;                 // the parent process
    struct mm_struct *mm;                       // Process's memory management field
    struct context context;                     // Switch here to run process
    struct trapframe *tf;                       // Trap frame for current interrupt
    uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
    uint32_t flags;                             // Process flag
    char name[PROC_NAME_LEN + 1];               // Process name
    list_entry_t list_link;                     // Process link list 
    list_entry_t hash_link;                     // Process hash list
};
```

### 初始化

- `state`：`proc_state`类型的变量

```c
enum proc_state {
    PROC_UNINIT = 0,  // uninitialized
    PROC_SLEEPING,    // sleeping
    PROC_RUNNABLE,    // runnable(maybe running)
    PROC_ZOMBIE,      // almost dead, and wait parent proc to reclaim his resource
};
```

这是一个枚举类型（enum），用来表示进程的不同状态，解释如下：

| 状态          | 含义           | 出现原因                                                     |
| ------------- | -------------- | ------------------------------------------------------------ |
| PRC_UNINIT    | 未初始化       | alloc_proc分配进程控制块                                     |
| PROC_SLEEPING | 睡眠阻塞       | try_free_pages,尝试释放页面，do_wait,等待子进程结束，do_sleep主动睡眠 |
| PROC_RUNNABLE | 可运行，就绪态 | proc_init,进程初始化，wakeup_proc唤醒进程                    |
| PROC_ZOMIBIE  | 僵死           | do_exit，进程退出但父进程未对其进行资源回收使                |

此时应该定义为`PRC_UNINIT`，即0；

- `pid`：父进程id，由于是一个未初始化的进程，所以设置为-1；

- `runs`：进程运行的时间，由于这是刚初始化的内存块，所以运行时间为0；

- `kstack`：`uintptr_t`类型成员变量，表示内核栈位置，因为还没有被执行，所以分配的地址应该为0；

- `need_resched`：是否需要调度，设置为1表示需要进行调度，此时因为不需要调度所以设置为0；

- `parent`：父进程，此时还没有被初始化所以父进程依旧为空；

***注意以上的值只是起了一个“分配内存并进行初步初始化，即把各个成员变量清零”的作用，具体的初始化还要在`proc_init`中进一步进行***

- `mm`：`struct mm_struct *`类型的变量，表示进程的虚拟内存，此时初始化为空即可；

- `context`：上下文，原型如下：

```c
struct context {
    uintptr_t ra;
    uintptr_t sp;
    uintptr_t s0;
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};
```

可以看到，其中只包括了ra，sp，s0-s11共14个寄存器，这里不需要保存所有寄存器的原因是由于线程切换在一个函数中，编译器在调用函数时会自动保存和恢复调用者的寄存器的代码，所以此时只需要保存被调用者的代码即可。

由于之后在`proc_init`中要对当前执行的上下文进行初始化，所以此时要进行内存空间的分配：初始化相应大小的内存空间为0即可；

- `tf`：指向`trapframe`的指针，保存了进程的中断帧。当进程从用户空间跳进内核空间的时候，进程的执行状态被保存在了中断帧中，此时初始化中断帧指针为空；

- `cr3`：当前页表地址，此时使用内核页目录表的基址`boot_cr3`，从这里可以得到，由于所有内核线程的内核虚地址空间是相同的，即所有的内核线程公用一个映射内核空间的页表，说明内核空间对所有内核线程都是可见的，几这些内核线程都从属于一个内核进程——`ucore`内核；

- `flags`：标志位，这里由于没有找到有什么特殊的含义，所以初始化为0；

- `name`：数组，长度为`PROC_NAME_LEN + 1`，最后加上1是因为还有一个零休止符，`PROC_NAME_LEN`长度定义如下：

```c
#define PROC_NAME_LEN               15
```

由于还没有进一步初始化，所以分配内存空间后初始化为0即可；

- 两个list_entry_t变量的指针，这两个指针不爱进程初始化的时候进行赋值或者初始化，而是在进程被插入到链表或者哈希表时通过相应的操作进行赋值或者初始化，所以这里不用管他即可。

### alloc_proc()函数实现

于是可以得到如下代码：

```c
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        proc->state = PROC_UNINIT;
        //给进程设置为未初始化状态
        proc->pid = -1;
        //未初始化的进程，其pid为-1
        proc->runs = 0;
        //初始化运行时间,刚刚初始化的进程，运行时间为0
        proc->kstack = 0;
        //内核栈地址,该进程分配的地址为0
        proc->need_resched = 0;
        //不需要调度
        proc->parent = NULL;
        //父进程为空
        proc->mm = NULL;
        //虚拟内存为空
        memset(&(proc->context), 0, sizeof(struct context));
        //初始化上下文
        proc->tf = NULL;
        //中断帧指针为空
        proc->cr3 = boot_cr3;
        //页目录为内核页目录表的基址
        proc->flags = 0;
        //标志位为0
        memset(proc->name, 0, PROC_NAME_LEN);
        //进程名为0
    }
    return proc;
}
```

这一段初始化在`proc_init()`函数中后面的一个部分可以得到印证：

```c
………………
        if(idleproc->cr3 == boot_cr3 && idleproc->tf == NULL && !context_init_flag
        && idleproc->state == PROC_UNINIT && idleproc->pid == -1 && idleproc->runs == 0
        && idleproc->kstack == 0 && idleproc->need_resched == 0 && idleproc->parent == NULL
        && idleproc->mm == NULL && idleproc->flags == 0 && !proc_name_flag
    ){
        cprintf("alloc_proc() correct!\n");

    }
………………
```

这一段是显示`alloc_proc()`函数执行成功的输出。

### 思考

Q：请说明`proc_struct`中`struct context context`和`struct trapframe *tf`成员变量含义和在本实验中的作用是啥？

A：

- context：
  - 进程的上下文，用于进程切换
  - 主要保存了被调用者的进程的寄存器状态
  - 在`switch_to`中利用`context`进行上下文的切换
- tf：
  - 中断帧指针，总是指向内核栈的某个位置
  - 当进程从用户空间跳到内核空间时，中断帧记录了进程在被中断前的状态；于是当内核需要跳回用户空间时，可以恢复保存的寄存器值

## exercise2：为新创建的内核线程分配资源

### `do_fork()`函数执行步骤

`do_fork()`函数的主要处理过程如下所示：

1. 调用`alloc_proc()`，首先获得一块用户信息块。

2. 为进程分配一个内核栈。

3. 复制原进程的内存管理信息到新进程（但内核线程不必做此事）

4. 复制原进程上下文到新进程

5. 将新进程添加到进程列表

6. 唤醒新进程

7. 返回新进程号

### 所需调用函数

#### 分配进程结构

使用`alloc_proc()`函数为进程分配一个进程块结构，具体实现见练习1。

#### 分配并初始化内核栈

调用`setup_kstack()`函数为进程分配并初始化内核栈，函数实现及注释如下所示：

```C++
//分配页面作为内核栈
static int
setup_kstack(struct proc_struct *proc) {
    //分配KSTACKPAGE个页作为内核栈
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        //如果分配成功，将进程的内核栈指针设置为分配页面的虚拟地址
        proc->kstack = (uintptr_t)page2kva(page);
        return 0; //返回0表示成功
    }
    return -E_NO_MEM; //返回错误码
}
```

#### 复制内存管理信息

使用`copy_mm()`函数复制原进程的内存管理信息到新进程，但由于内核线程都在内核的地址空间中运行，所以没有做什么实际的操作。函数实现及注释如下所示：

```C++
//本次实验暂未涉及
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    assert(current->mm == NULL);
    /* do nothing in this project */
    return 0;
}
```

#### 复制父进程的中断帧和上下文信息

使用`copy_thread()`函数复制原进程的上下文到新进程，其实就是复制父进程的中断帧和上下文信息。函数实现及注释如下所示：

```C++
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    //在进程的内核栈顶分配空间保存trapframe
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    //将传入的trapframe复制到进程的trapframe结构体中
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    //将trapframe中的a0寄存器设置为0，说明这个进程是一个子进程
    proc->tf->gpr.a0 = 0;
    //设置trapframe的栈指针(sp)
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    //将上下文中的ra设置为了forkret函数的入口
    proc->context.ra = (uintptr_t)forkret;
    //把trapframe放在上下文的栈顶
    proc->context.sp = (uintptr_t)(proc->tf);
}
```

#### 将进程加入链表

将创建的`proc`块插入`hash_list`和`proc_list`链表中，首先使用`get_pid()`函数为进程分配一个pid（具体实现在思考题中分析），插入哈希表中时调用`hash_proc()`函数：

```C++
//将proc添加到proc哈希表
static void
hash_proc(struct proc_struct *proc) {
    //使用proc的pid计算哈希值，将proc的hash_link节点添加到对应的哈希数组中
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}
```

使用`list_add()`函数将进程的`list_link`直接插入`proc_list`的开头。

#### 将进程的状态改为就绪态

使用`wakeup_proc()`函数将进程的状态改为就绪态，函数注释如下所示：

```C++
//唤醒进程，将其状态设置为可运行态
void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}
```

### `do_fork()`函数实现

`do_fork()`函数具体实现如下所示：

```C++
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    // 返回值
    int ret = -E_NO_FREE_PROC;
    // 创建的进程块
    struct proc_struct *proc;
    // 如果进程数大于最大进程数，返回
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;

    //    1. call alloc_proc to allocate a proc_struct
    //分配并初始化进程控制块
    //调用alloc_proc函数申请内存块
    if((proc=alloc_proc())==NULL){
        //如果没有分配成功
        goto fork_out;
    }
    //将子进程的父节点设置为当前进程
    proc->parent=current;

    //    2. call setup_kstack to allocate a kernel stack for child process
    //分配并初始化内核栈
    if(setup_kstack(proc)!=0){
        //如果没有分配成功
        goto bad_fork_cleanup_kstack;
    }

    //    3. call copy_mm to dup OR share mm according clone_flag
    //根据clone_flags决定是复制还是共享内存管理系统（copy_mm函数）,本次实验暂未涉及
    if(copy_mm(clone_flags,proc)!=0){
        goto bad_fork_cleanup_proc;
    }

    //    4. call copy_thread to setup tf & context in proc_struct
    //设置进程的中断帧和上下文(copy_thread函数)(复制父进程的中断帧和上下文信息)
    copy_thread(proc,stack,tf);

    //    5. insert proc_struct into hash_list && proc_list
    //把设置好的进程加入链表
    //为proc申请一个pid
    proc->pid=get_pid();
    //加入哈希表(需要调用proc的pid)
    hash_proc(proc);
    //加入proc_list
    list_add(&proc_list,&proc->list_link);
    nr_process++; //全局线程数目+1

    //    6. call wakeup_proc to make the new child process RUNNABLE
    //将新建的进程设为就绪态
    wakeup_proc(proc);

    //    7. set ret vaule using child proc's pid
    //将返回值设为线程id
    ret=proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}
```

### 思考

**请说明ucore是否做到给每个新fork的线程一个唯一的id？请说明你的分析和理由。**

查看并分析分配pid的函数`get_pid()`：

```C++
//用于为进程分配唯一的PID
static int
get_pid(void) {
    //确保最大PID>最大进程数
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    //声明了两个指针变量 list 和 le，并将 list 初始化为指向全局变量 proc_list 的地址
    list_entry_t *list = &proc_list, *le;
    //静态变量，先将上一个安全的pid和下一个安全的pid都设置为最大pid号
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        //将上一个安全的pid设为1
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        //遍历进程链表
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            //如果找到了相同的pid，尝试增加pid，直到找到一个未分配的pid
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}
```

由第23行开始的循环可知，该函数遍历了整个进程链表，在34行处以后的代码不断缩小可用pid的范围，直到遍历完整个链表，使用`last_pid`记录上一个找到的可用pid。如果遍历时`last_pid`与已经存在的pid相等，则会继续向后查找可用的pid；如果超出了最大可用的pid的范围，就重新进行范围的逼近，直到找到一个可用的pid。

由以上代码可知，`get_pid()`函数不会为进程分配重复的pid，所以ucore能做到给每个新fork的线程一个唯一的id。

## exercise3：编写proc_run函数

`proc_run()`函数的作用是保存当前进程`current`的执行现场，恢复新进程的执行现场，完成进程切换。具体流程如下：

1. 将当前运行的进程设置为要切换过去的进程
2. 将页表换成新进程的页表
3. 使用`switch_to()`切换到新进程

### 1. local_intr_save()

首先，要确保在调度函数执行期间，不会被中断打断，可以调用`local_intr_save()`函数，函数原型如下：

```c
#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
```

其中调用了一个函数`__intr_save()`，函数原型如下：

```c
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}
```

其中又调用了一个函数`intr_disable()`，函数原型如下：

```c
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
```

从如上代码看出，函数 `intr_disable()` 通过清除 `sstatus` 寄存器中的 `SIE` 位来实现中断的禁用。

于是可以往前推出`__intr_save()`函数的作用是读取并保存当前中断状态，并在读取前将中断禁用。具体实现如下：

1. 通过 `read_csr(sstatus)` 读取控制和状态寄存器 `sstatus` 的值，并使用按位与操作符 `&` 与 `SSTATUS_SIE` 进行位运算
2. 如果 `read_csr(sstatus) & SSTATUS_SIE` 的结果为真，即当前的中断使能位为 1，中断是允许的。于是调用 `intr_disable()` 函数来禁用中断以确保函数运行期间不会被中断。
3. 如果返回为0的话，表示当前中断已经被禁用。
4. 该函数返回值标识了当前中断在被禁用前是否处于使能状态：返回值为1表示禁用前当前中断处于使能状态；为0表示禁用前当前状态处于禁止状态。

再往前推的话可以得出`local_intr_save(x)`的作用是禁用中断。其中`do { ... } while (0)` 结构通常被用作宏的包裹结构，为了确保在使用宏时，不会因为宏的展开而导致语法错误。

于是可以得到如下代码：

```c
local_intr_save(intr_flag);
```

其中`intr_flag`是定义的`bool`值，用于标识当前中断在被禁用前是否处于使能状态。

### 2. 切换当前运行的进程

在禁用中断后，可以开始进程进程的切换了。此时要找两个参数：

- 当前运行的进程
- 要切换的进程

要切换的进程很好找，`proc_run()`函数的参数是`struct proc_struct *proc`，即我们要找的切换过去的进程控制块指针

至于当前运行的进程，`ucore`定义了一个全局变量`current`，存储当前占用CPU且处于“运行”状态进程控制块指针。

至此，两个需要的参数都找到了，将当前运行的进程指向要切换的进程即可，代码如下：

```c
	struct proc_struct *prev = current; 
	struct proc_struct *next = proc;
	current = proc;
```

### 3. 将页表换成新进程的页表

需要将页表换成新进程的页表，该页表信息肯定在进程控制块中可以找到，于是找到进程控制块`proc_struct`的定义如下：

```c
struct proc_struct {
    enum proc_state state;                      // Process state
    int pid;                                    // Process ID
    int runs;                                   // the running times of Proces
    uintptr_t kstack;                           // Process kernel stack
    volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
    struct proc_struct *parent;                 // the parent process
    struct mm_struct *mm;                       // Process's memory management field
    struct context context;                     // Switch here to run process
    struct trapframe *tf;                       // Trap frame for current interrupt
    uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
    uint32_t flags;                             // Process flag
    char name[PROC_NAME_LEN + 1];               // Process name
    list_entry_t list_link;                     // Process link list 
    list_entry_t hash_link;                     // Process hash list
};
```

其中有个成员变量`cr3`表示`CR3`寄存器的值，即页目录表的基地址，标识进程的地址空间。

于是我们只需要将页表指向要切换到的进程的进程控制块的`cr3`即可。

使用函数`lcr3()`，函数原型如下：

```c
static inline void
lcr3(unsigned int cr3) {
    write_csr(sptbr, SATP32_MODE | (cr3 >> RISCV_PGSHIFT));
}
```

该函数用于设置当前进程的页表基址，传递的参数即为需要设置的页表基址，于是”将页表切换到新进程的页表“这一部分代码如下：

```c
lcr3(next->cr3);
```

### 4. 使用switch_to切换到新进程

目前，已经实现了”当前进程指针指向了要切换的进程指针“和”将页表换成新进程的页表“这两项准备工作，最后调用`switch_to()`函数切换到新进程即可，依旧是先看函数原型：

```asm
# void switch_to(struct proc_struct* from, struct proc_struct* to)
.globl switch_to
switch_to:
    # save from's registers
    STORE ra, 0*REGBYTES(a0)
    STORE sp, 1*REGBYTES(a0)
    STORE s0, 2*REGBYTES(a0)
    STORE s1, 3*REGBYTES(a0)
    STORE s2, 4*REGBYTES(a0)
    STORE s3, 5*REGBYTES(a0)
    STORE s4, 6*REGBYTES(a0)
    STORE s5, 7*REGBYTES(a0)
    STORE s6, 8*REGBYTES(a0)
    STORE s7, 9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)

    # restore to's registers
    LOAD ra, 0*REGBYTES(a1)
    LOAD sp, 1*REGBYTES(a1)
    LOAD s0, 2*REGBYTES(a1)
    LOAD s1, 3*REGBYTES(a1)
    LOAD s2, 4*REGBYTES(a1)
    LOAD s3, 5*REGBYTES(a1)
    LOAD s4, 6*REGBYTES(a1)
    LOAD s5, 7*REGBYTES(a1)
    LOAD s6, 8*REGBYTES(a1)
    LOAD s7, 9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)

    ret
```

它使用汇编语言编写的函数，主要执行两个操作：

- 使用STORE保存当前进程（from）的寄存器状态
- 使用LOAD恢复即将执行的进程（to）的寄存器状态

函数参数是进程控制块指针，于是我们可以得到如下代码：

```c
switch_to(&(prev->context), &(next->context));
```

### 5. local_intr_restore()

最后，在函数结束前，需要调用`local_intr_restore()`函数恢复中断。函数原型如下：

```c
#define local_intr_restore(x) __intr_restore(x);
```

其中调用了一个函数`__intr_restore()`，函数原型如下：

```c
static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}
```

其中又调用了一个函数`intr_enable()`，函数原型如下：

```c
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }
```

从如上代码看出，函数 `intr_enable()` 通过设置 `sstatus` 寄存器中的 `SIE `位来实现中断的恢复。

于是可以往前推出`__intr_restore()`函数的作用是根据传入的参数值来恢复中断状态。具体实现如下：

1. 如果传入的参数值flag为真，则调用 `intr_enable()` 函数启用中断
2. 如果 `flag` 的值为假，则不进行任何操作，即不恢复中断

再往前推的话可以得出`local_intr_save(x)`的作用是根据传入的x的值恢复中断。

于是可以得到如下代码：

```c
local_intr_restore(intr_flag);
```

`intr_flag`为之前在`local_intr_save()`中使用的参数，调用后被赋值，表示当前中断在被禁用前是否处于使能状态，此时作为参数传入`local_intr_restore()`，可以判断是否恢复中断。

### 6. 总结

#### proc_run()函数实现

```c
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        bool intr_flag;
        struct proc_struct *prev = current; 
        //用于标识当前进程的进程控制块
        struct proc_struct *next = proc;
        //用于标识要切换的进程的进程控制块
        local_intr_save(intr_flag);
        //确保在调度函数执行期间，不会被中断打断
        {
            current = proc;
            //将当前运行的进程设置为要切换过去的进程
            lcr3(next->cr3);
            //将页表换成新进程的页表
            switch_to(&(prev->context), &(next->context));
            //使用switch_to切换到新进程
        }
        local_intr_restore(intr_flag);
        //恢复中断
    }
}
```

#### 思考

Q：在本实验的执行过程中，创建且运行了几个内核线程？

A：创建并运行了两个线程。

1. idleproc：第0个内核进程
   - 用于表示空闲进程，主要目的是在系统没有其他任务要执行的时候，占用CPU时间，同时便于进程调度的统一化。
   - 完成内核中各个子系统的初始化，之后立即执行调度`schedule()`，执行其他进程。

2. initproc：第1个内核进程

   - 从`kernel_thread_entry`开始执行

   - 完成其他工作

## challenge

Q：说明语句`local_intr_save(intr_flag);`和`local_intr_restore(intr_flag);` 是如何实现开关中断的？

A：具体分析在exercise3中有提及。