#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

//唤醒进程，将其状态设置为可运行态
void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//进行进程调度，选择下一个要执行的进程
void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;
    // 保存当前中断状态
    local_intr_save(intr_flag);
    {
        //设置当前内核线程current->need_resched为0，表示不需要重新调度
        current->need_resched = 0;
        // 确定当前进程在进程链表中的位置
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        // 在进程链表中循环查找下一个可运行的进程
        do {
            if ((le = list_next(le)) != &proc_list) {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE) {
                    break;
                }
            }
        } while (le != last);
        // 如果没有找到可运行的进程，或者下一个进程不可运行，则选择 idleproc
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }
        // 增加下一个进程的运行次数统计
        next->runs ++;
        // 如果下一个进程不是当前进程，则切换到下一个进程
        if (next != current) {
            proc_run(next);
        }
    }
    // 恢复之前保存的中断状态
    local_intr_restore(intr_flag);
}

