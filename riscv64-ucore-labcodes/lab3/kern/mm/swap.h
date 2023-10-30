#ifndef __KERN_MM_SWAP_H__
#define __KERN_MM_SWAP_H__

#include <defs.h>
#include <memlayout.h>
#include <pmm.h>
#include <vmm.h>

/* *
 * swap_entry_t
 * --------------------------------------------
 * |         offset        |   reserved   | 0 |
 * --------------------------------------------
 *           24 bits            7 bits    1 bit
 * */

// 允许的最大交换区偏移量，表示交换区最大可容纳的页面数量
#define MAX_SWAP_OFFSET_LIMIT                   (1 << 24)

// 实际可用的最大交换区偏移量
extern size_t max_swap_offset;

/* *
 * swap_offset - takes a swap_entry (saved in pte), and returns
 * the corresponding offset in swap mem_map.
 * */
// 接收入口地址返回交换区偏移量
#define swap_offset(entry) ({                                       \
               size_t __offset = (entry >> 8);                        \
               if (!(__offset > 0 && __offset < max_swap_offset)) {    \
                    panic("invalid swap_entry_t = %08x.\n", entry);    \
               }                                                    \
               __offset;                                            \
          })

// 用于管理虚拟内存交换的结构体
struct swap_manager
{
     const char *name;
     // 交换管理器名称
     /* Global initialization for the swap manager */
     int (*init)            (void);
     // 全局初始化交换管理器的函数
     /* Initialize the priv data inside mm_struct */
     int (*init_mm)         (struct mm_struct *mm);
     // 初始化待定mm_struct对象的函数
     /* Called when tick interrupt occured */
     int (*tick_event)      (struct mm_struct *mm);
     // 处理时钟中断的函数
     /* Called when map a swappable page into the mm_struct */
     int (*map_swappable)   (struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in);
     // 将可交换页面映射到mm_struct的函数
     /* When a page is marked as shared, this routine is called to
      * delete the addr entry from the swap manager */
     int (*set_unswappable) (struct mm_struct *mm, uintptr_t addr);
     // 将页面标记为不可交换的函数
     /* Try to swap out a page, return then victim */
     int (*swap_out_victim) (struct mm_struct *mm, struct Page **ptr_page, int in_tick);
     // 选择需要交换出去的页面的函数
     /* check the page relpacement algorithm */
     int (*check_swap)(void);     
     // 检查页面置换算法的函数
};

extern volatile int swap_init_ok;
// 初始化虚拟内存交换
int swap_init(void);
// 初始化mm_struct对象
int swap_init_mm(struct mm_struct *mm);
// 初始化处理时钟中断事件
int swap_tick_event(struct mm_struct *mm);
// 将页面标记为可交换
int swap_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in);
int swap_set_unswappable(struct mm_struct *mm, uintptr_t addr);
// 将页面换出交换区
int swap_out(struct mm_struct *mm, int n, int in_tick);
// 将页面换入交换区
int swap_in(struct mm_struct *mm, uintptr_t addr, struct Page **ptr_result);

//#define MEMBER_OFFSET(m,t) ((int)(&((t *)0)->m))
//#define FROM_MEMBER(m,t,a) ((t *)((char *)(a) - MEMBER_OFFSET(m,t)))

#endif
