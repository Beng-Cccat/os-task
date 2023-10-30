#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <sync.h>

//pre define
struct mm_struct;

// the virtual continuous memory area(vma), [vm_start, vm_end), 
// addr belong to a vma means  vma.vm_start<= addr <vma.vm_end 
// 虚拟内存区域的数据结构
// 每个vma代表了一段连续的虚拟内存空间
struct vma_struct {
    // 指向使用相同页目录表的VMA集合的控制结构
    struct mm_struct *vm_mm; // the set of vma using the same PDT 
    // vma起始地址
    uintptr_t vm_start;      // start addr of vma      
    // vma结束地址（不包含在范围内）
    uintptr_t vm_end;        // end addr of vma, not include the vm_end itself
    // vma标志，表示访问权限（读、写、执行等）
    uint_t vm_flags;       // flags of vma
    // 用于将vma结构连接到线性链表中，按照起始地址排序
    list_entry_t list_link;  // linear list link which sorted by start addr of vma
};

// 用于将链表节点转换为vma_struct结构
#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

// 定义了表示VMA权限的数
#define VM_READ                 0x00000001
#define VM_WRITE                0x00000002
#define VM_EXEC                 0x00000004

// the control struct for a set of vma using the same PDT
// 管理一组VMA的控制结构
struct mm_struct {
    // 线性链表，用于存储vma结构
    list_entry_t mmap_list;        // linear list link which sorted by start addr of vma
    // 指向当前访问的vma结构，缓存加速访问
    struct vma_struct *mmap_cache; // current accessed vma, used for speed purpose
    // 指向页目录表的指针，用于管理vma的地址映射
    pde_t *pgdir;                  // the PDT of these vma
    // 当前vma集合的vma数量
    int map_count;                 // the count of these vma
    // 与交换管理器相关的私有数据
    void *sm_priv;                   // the private data for swap manager
};

//查找vma结构指针的函数
struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);
// 创建并返回一个新的vma结构的函数（起始地址，结束地址，权限标志）
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint_t vm_flags);
// 将vma结构插入到mm_struct控制的vma列表中，按照起始地址排序
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);

// 创建并返回一个新的mm_struct结构的函数，用于管理一组vma
struct mm_struct *mm_create(void);
// 销毁一个mm_struct结构
void mm_destroy(struct mm_struct *mm);

// 初始化虚拟内存管理器
void vmm_init(void);

// 处理页面故障异常的函数
int do_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr);

extern volatile unsigned int pgfault_num;
extern struct mm_struct *check_mm_struct;

#endif /* !__KERN_MM_VMM_H__ */

