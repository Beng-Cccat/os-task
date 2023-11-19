#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>


// process's state in his life cycle
// 进程生命周期中的状态
enum proc_state {
    PROC_UNINIT = 0,  // uninitialized 未初始化
    PROC_SLEEPING,    // sleeping 睡眠
    PROC_RUNNABLE,    // runnable(maybe running) 可运行态（可能正在运行）
    PROC_ZOMBIE,      // almost dead, and wait parent proc to reclaim his resource 几乎死亡的进程，等待父进程回收资源
};

// 上下文结构体，保存了一些寄存器的值，用于进程上下文的切换
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

//进程名字的最大长度
#define PROC_NAME_LEN               15
//最大进程数
#define MAX_PROCESS                 4096
//最大进程ID
#define MAX_PID                     (MAX_PROCESS * 2)

extern list_entry_t proc_list;

struct proc_struct {
    enum proc_state state;                      // Process state 进程状态
    int pid;                                    // Process ID PID
    int runs;                                   // the running times of Proces 进程运行次数
    uintptr_t kstack;                           // Process kernel stack 进程内核栈
    volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?  是否需要重新调度
    struct proc_struct *parent;                 // the parent process 父进程指针
    struct mm_struct *mm;                       // Process's memory management field 进程内存管理空间
    struct context context;                     // Switch here to run process  进程上下文（即寄存器的内容）
    struct trapframe *tf;                       // Trap frame for current interrupt 保存中断信息的结构
    uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT) 页表地址
    uint32_t flags;                             // Process flag
    char name[PROC_NAME_LEN + 1];               // Process name
    list_entry_t list_link;                     // Process link list 
    list_entry_t hash_link;                     // Process hash list
};

#define le2proc(le, member)         \
    to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);

#endif /* !__KERN_PROCESS_PROC_H__ */

