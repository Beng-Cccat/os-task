#ifndef __KERN_MM_MEMLAYOUT_H__
#define __KERN_MM_MEMLAYOUT_H__

/* This file contains the definitions for memory management in our OS. */

/* *
 * Virtual memory map:                                          Permissions
 *                                                              kernel/user
 *
 *     4G ------------------> +---------------------------------+
 *                            |                                 |
 *                            |         Empty Memory (*)        |
 *                            |                                 |
 *                            +---------------------------------+ 0xFB000000
 *                            |   Cur. Page Table (Kern, RW)    | RW/-- PTSIZE
 *     VPT -----------------> +---------------------------------+ 0xFAC00000
 *                            |        Invalid Memory (*)       | --/--
 *     KERNTOP -------------> +---------------------------------+ 0xF8000000
 *                            |                                 |
 *                            |    Remapped Physical Memory     | RW/-- KMEMSIZE
 *                            |                                 |
 *     KERNBASE ------------> +---------------------------------+ 0xC0000000
 *                            |                                 |
 *                            |                                 |
 *                            |                                 |
 *                            ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.
 *
 * */

/* All physical memory mapped at this address */
// = 0x80200000(物理内存里内核的起始位置, KERN_BEGIN_PADDR) + 0xFFFFFFFF40000000(偏移量, PHYSICAL_MEMORY_OFFSET)
#define KERNBASE            0xFFFFFFFFC0200000
//把原有内存映射到虚拟内存空间的最后一页
#define KMEMSIZE            0x7E00000          // the maximum amount of physical memory
// 0x7E00000 = 0x8000000 - 0x200000 可用物理内存的最大值
// QEMU 缺省的RAM为 0x80000000到0x88000000，默认大小是0x8000000
// 128MiB, 0x80000000到0x80200000被OpenSBI占用
#define KERNTOP             (KERNBASE + KMEMSIZE) // 0x88000000对应的虚拟地址

// 物理内存结束地址
#define PHYSICAL_MEMORY_END         0x88000000
// 物理内存到虚拟内存的偏移量
#define PHYSICAL_MEMORY_OFFSET      0xFFFFFFFF40000000
// 物理内存中内核的起始位置
#define KERNEL_BEGIN_PADDR          0x80200000
// 虚拟内存中内核的起始位置
#define KERNEL_BEGIN_VADDR          0xFFFFFFFFC0200000

//内核栈
#define KSTACKPAGE          2                           // # of pages in kernel stack
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // sizeof kernel stack

#ifndef __ASSEMBLER__

#include <defs.h>
#include <atomic.h>
#include <list.h>

typedef uintptr_t pte_t;
typedef uintptr_t pde_t;
typedef pte_t swap_entry_t; //the pte can also be a swap entry

/* *
 * struct Page - Page descriptor structures. Each Page describes one
 * physical page. In kern/mm/pmm.h, you can find lots of useful functions
 * that convert Page to other data types, such as physical address.
 * */
// 页结构定义，每页描述了一个物理页
// pmm.h中描述了将页转换为其他数据类型的函数
struct Page {
    // 页框引用计数
    int ref;                        // page frame's reference counter
    // 描述页框状态的标志数组的指针
    uint_t flags;                 // array of flags that describe the status of the page frame
    uint_t visited;
    // 空闲块的数量
    unsigned int property;          // the num of free block, used in first fit pm manager
    // 空闲块链表
    list_entry_t page_link;         // free list link
    // 用于页面替换算法
    list_entry_t pra_page_link;     // used for pra (page replace algorithm)
    uintptr_t pra_vaddr;            // used for pra (page replace algorithm)
};

/* Flags describing the status of a page frame */
// 如果reserved字段为1，表示被内核保留
#define PG_reserved                 0       // if this bit=1: the Page is reserved for kernel, cannot be used in alloc/free_pages; otherwise, this bit=0 
// 如果property字段为1，表示页面是空闲内存块的开头；如果为0，要么该页面不是空闲块的开头，要么已经被分配
#define PG_property                 1       // if this bit=1: the Page is the head page of a free memory block(contains some continuous_addrress pages), and can be used in alloc_pages; if this bit=0: if the Page is the the head page of a free memory block, then this Page and the memory block is alloced. Or this Page isn't the head page.

// 对标志位操作的函数
#define SetPageReserved(page)       set_bit(PG_reserved, &((page)->flags))
#define ClearPageReserved(page)     clear_bit(PG_reserved, &((page)->flags))
#define PageReserved(page)          test_bit(PG_reserved, &((page)->flags))
#define SetPageProperty(page)       set_bit(PG_property, &((page)->flags))
#define ClearPageProperty(page)     clear_bit(PG_property, &((page)->flags))
#define PageProperty(page)          test_bit(PG_property, &((page)->flags))

// convert list entry to page
#define le2page(le, member)                 \
    to_struct((le), struct Page, member)

/* free_area_t - maintains a doubly linked list to record free (unused) pages */
// 保留一个双重链表记录空闲页
typedef struct {
    //空闲页的开头和空闲页的数量
    list_entry_t free_list;         // the list header
    unsigned int nr_free;           // # of free pages in this free list
} free_area_t;

#endif /* !__ASSEMBLER__ */

#endif /* !__KERN_MM_MEMLAYOUT_H__ */

