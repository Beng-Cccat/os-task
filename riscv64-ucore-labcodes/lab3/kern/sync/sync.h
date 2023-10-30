#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <intr.h>
#include <mmu.h>
#include <riscv.h>

// 定义了一些用于中断处理的宏和内联函数
// 主要用于管理中断的开启和关闭状态

// 保存当前中断状态并禁用中断
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

// 恢复中断状态
static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}


// 该宏用于在局部作用域内保存当前中断状态
#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)

// 该宏用于在局部作用域内根据之前保存的中断状态x来恢复中断状态
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */
