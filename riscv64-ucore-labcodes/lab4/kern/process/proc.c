#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc, 
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:
                                            
  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+ 
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  + 
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
旧兄弟：
older sibling:    proc->optr    (proc is younger sibling)
年轻兄弟：
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
//进程退出
SYS_exit        : process exit,                           -->do_exit
//创建子进程，复制内存空间
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
//fork后，进程执行程序
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
//创建子线程
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
//进程标记自身需要重新调度
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
//进程休眠
SYS_sleep       : process sleep                           -->do_sleep 
//杀死进程
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
//获取进程PID                                                                 -->wakeup_proc-->do_wait-->do_exit   
SYS_getpid      : get the process's pid

*/

// the process set's list
//进程链表
list_entry_t proc_list;

//哈希表的位移值，哈希表大小为2^10
#define HASH_SHIFT          10
//哈希表的实际大小
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
//哈希函数
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// has list for process set based on pid
//进程hash表，用于根据进程的PID快速查找进程
//是一个链表节点的数组
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
//第0个内核线程（ucoreOS线程）
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

//把参数放在了a0寄存器，并跳转到s0执行我们指定的函数
void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
// 分配一个进程结构
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
    //LAB4:EXERCISE1 2113285 吴静
    /*
     * below fields in proc_struct need to be initialized
     *       enum proc_state state;                      // Process state
     *       int pid;                                    // Process ID
     *       int runs;                                   // the running times of Proces
     *       uintptr_t kstack;                           // Process kernel stack
     *       volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
     *       struct proc_struct *parent;                 // the parent process
     *       struct mm_struct *mm;                       // Process's memory management field
     *       struct context context;                     // Switch here to run process
     *       struct trapframe *tf;                       // Trap frame for current interrupt
     *       uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
     *       uint32_t flags;                             // Process flag
     *       char name[PROC_NAME_LEN + 1];               // Process name
     */

        proc->state=PROC_UNINIT;
        proc->pid=-1; //id初始化为-1
        proc->runs=0; //进程调度次数为0
        proc->kstack=0; //内核栈位置，暂未分配，初始化为0
        proc->need_resched=0; //不需要释放CPU，因为还没有分配
        proc->parent=NULL;  //当前没有父进程，初始为null
        proc->mm=NULL;     //当前未分配内存，初始为null
        //用memset将context变量中的所有成员变量置为0
        memset(&(proc -> context), 0, sizeof(struct context)); 
        proc->tf=NULL;   		//当前没有中断帧,初始为null
        proc->cr3=boot_cr3;    //内核线程，默认初始化为boot_cr3
        proc->flags=0;//当前进程的相关标志，暂无，设为0
        memset(proc -> name, 0, PROC_NAME_LEN+1);
    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name) {
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc) {
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// get_pid - alloc a unique pid for process
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

// proc_run - make process "proc" running on cpu
// NOTE: before call switch_to, should load  base addr of "proc"'s new PDT
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        // LAB4:EXERCISE3 YOUR CODE 2113285 吴静
        /*
        * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
        * MACROs or Functions:
        *   local_intr_save():        Disable interrupts
        *   local_intr_restore():     Enable Interrupts
        *   lcr3():                   Modify the value of CR3 register
        *   switch_to():              Context switching between two processes
        */
       
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
            //使用switch_to切换到新进程,(切换进程上下文)
            //a0指向原进程，a1指向目的进程
        }
        local_intr_restore(intr_flag);
        //恢复中断
    }
}

// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void) {
    forkrets(current->tf);
    //把进程的中断帧（current->tf）放在了sp，在__trapret中就可以直接从中断帧里面恢复所有的寄存器

}

// hash_proc - add proc into proc hash_list
//将proc添加到proc哈希表
static void
hash_proc(struct proc_struct *proc) {
    //使用proc的pid计算哈希值，将proc的hash_link节点添加到对应的哈希数组中
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
//根据给定的进程号 pid 在进程哈希表中查找对应的进程结构体
struct proc_struct *
find_proc(int pid) {
    if (0 < pid && pid < MAX_PID) {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list) {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid) {
                return proc;
            }
        }
    }
    return NULL;
}

// kernel_thread - create a kernel thread using "fn" function
// NOTE: the contents of temp trapframe tf will be copied to 
//       proc->tf in do_fork-->copy_thread function
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    //创建一个trapframe结构体tf用于存储线程上下文信息
    struct trapframe tf;
    //清零
    memset(&tf, 0, sizeof(struct trapframe));

    //设置内核线程的参数和函数指针
    tf.gpr.s0 = (uintptr_t)fn; //s0寄存器保存函数指针(init_main)
    tf.gpr.s1 = (uintptr_t)arg; //s1寄存器保存函数参数(helloworld)

    //设置trapframe中的status寄存器（sstatus）
    // SSTATUS_SPP：Supervisor Previous Privilege（设置为 supervisor 模式，因为这是一个内核线程）
    // SSTATUS_SPIE：Supervisor Previous Interrupt Enable（设置为启用中断，因为这是一个内核线程）
    // SSTATUS_SIE：Supervisor Interrupt Enable（设置为禁用中断，因为我们不希望该线程被中断）
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;

    // 将入口点（epc）设置为 kernel_thread_entry 函数，作用实际上是将pc指针指向它(*trapentry.S会用到)
    //kernel_thread_entry函数主要为内核线程的主体fn函数做了一个准备开始和结束运行的“壳”，并把函数fn的参数arg（保存在edx寄存器中）压栈
    //然后调用fn函数，把函数返回值eax寄存器内容压栈，调用do_exit函数退出线程执行
    tf.epc = (uintptr_t)kernel_thread_entry;

    // 使用 do_fork 创建一个新进程（内核线程），这样才真正用设置的tf创建新进程。
    //返回pid
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
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

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc) {
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
//本次实验暂未涉及
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    assert(current->mm == NULL);
    /* do nothing in this project */
    return 0;
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
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

    //将上下文中的ra（返回地址）设置为了forkret函数的入口
    proc->context.ra = (uintptr_t)forkret;
    //把trapframe放在上下文的栈顶（sp）
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork -     parent process for a new child process
 * @clone_flags: used to guide how to clone the child process
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 */
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
    //LAB4:EXERCISE2 YOUR CODE 2110803 谢雯菲
    /*
     * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 setup the kernel entry point and stack of process
     *   hash_proc:    add proc into proc hash_list
     *   get_pid:      alloc a unique pid for process
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     * VARIABLES:
     *   proc_list:    the process set's list
     *   nr_process:   the number of process set
     */

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

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int
do_exit(int error_code) {
    panic("process exit!!.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
//用于创建user_main内核线程的第二个内核线程
static int
init_main(void *arg) {
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and 
//           - create the second kernel thread init_main
void
proc_init(void) {
    int i;

    // 初始化进程链表（自身首尾相连）
    list_init(&proc_list);
    //初始化哈希数组
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    //分配一个初始的进程块给idle
    //idleproc内核线程的工作就是不停地查询，看是否有其他内核线程可以执行了，如果有，马上让调度器选择那个内核线程执行
    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    // check the proc structure
    // 检查proc结构，主要是检查context和name字段是否初始化正确
    // 通过memcmp比较内存块中是否全为0
    int *context_mem = (int*) kmalloc(sizeof(struct context));
    memset(context_mem, 0, sizeof(struct context));
    int context_init_flag = memcmp(&(idleproc->context), context_mem, sizeof(struct context));

    int *proc_name_mem = (int*) kmalloc(PROC_NAME_LEN);
    memset(proc_name_mem, 0, PROC_NAME_LEN);
    int proc_name_flag = memcmp(&(idleproc->name), proc_name_mem, PROC_NAME_LEN);

    // 检查idleproc结构的各个字段是否初始化正确
    if(idleproc->cr3 == boot_cr3 && idleproc->tf == NULL && !context_init_flag
        && idleproc->state == PROC_UNINIT && idleproc->pid == -1 && idleproc->runs == 0
        && idleproc->kstack == 0 && idleproc->need_resched == 0 && idleproc->parent == NULL
        && idleproc->mm == NULL && idleproc->flags == 0 && !proc_name_flag
    ){
        cprintf("alloc_proc() correct!\n");

    }
    
    //初始化idleproc的属性
    idleproc->pid = 0; //第0个进程
    idleproc->state = PROC_RUNNABLE; //就绪态
    idleproc->kstack = (uintptr_t)bootstack; //内核堆栈
    idleproc->need_resched = 1; //需要被调度
    set_proc_name(idleproc, "idle"); //线程名字
    //进程数量？
    nr_process ++;

    // 将idleproc设置为当前运行的进程
    current = idleproc;

    //创建initproc线程，工作就是显示“Hello World”，表明自己存在且能正常工作了
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    //根据pid查找线程结构
    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
//在 kern_init 结束后，第一个内核线程 idleproc 将执行以下工作
void
cpu_idle(void) {
    // 进入一个无限循环，等待调度器通知
    while (1) {
        // 如果当前进程需要重新调度（即有其他进程需要运行），则执行调度
        if (current->need_resched) {
            schedule();
        }
    }
}

