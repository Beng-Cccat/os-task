#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_lru.h>
#include <list.h>

list_entry_t pra_list_head, *curr_ptr;

bool is_page_in_list(list_entry_t *head, struct Page *target_page)
{
    list_entry_t *entry;

    // 遍历链表
    for (entry = (head)->next; entry != (entry); entry = entry->next)
    {
        // 获取当前节点的 Page 指针
        struct Page *page = le2page(entry, pra_page_link);

        // 检查当前节点的 Page 是否与目标页面相同
        if (page == target_page)
        {
            return true; // 找到了目标页面
        }
    }

    return false; // 遍历完链表都没有找到目标页面
}

list_entry_t *get_list_page(list_entry_t *head, struct Page *target_page)
{
    list_entry_t *entry;
    // 遍历链表
    for (entry = (head)->next; entry != (entry); entry = entry->next)
    {
        // 获取当前节点的 Page 指针
        struct Page *page = le2page(entry, pra_page_link);

        // 检查当前节点的 Page 是否与目标页面相同
        if (page == target_page)
        {
            return entry; // 找到了目标页面
        }
    }

    return NULL; // 遍历完链表都没有找到目标页面
}

// 初始化一个名为 pra_list_head 的双向链表
static int
_lru_init_mm(struct mm_struct *mm)
{
    // 初始化pra_list_head为空链表
    list_init(&pra_list_head);

    // 初始化当前指针curr_ptr指向pra_list_head，表示当前页面替换位置为链表头
    curr_ptr = &pra_list_head;

    // 将mm的私有成员指针指向pra_list_head，用于后续的页面替换算法操作
    mm->sm_priv = &pra_list_head;

    cprintf(" mm->sm_priv %x in lru_init_mm\n", mm->sm_priv);
    cprintf("\n");
    return 0;
}

// 插入页面
static int
_lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    // 内存管理结构体，用于确定要执行页面插入操作的特定进程或任务
    // addr要插入页面的虚拟地址
    // page要插入的页面对象的指针
    // swap_in是否执行页面交换（swap in）操作
    // 输出参数 ptr_page 来返回所选择的页面帧
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    list_entry_t *entry = &(page->pra_page_link); // page链入link的指针

    assert(entry != NULL && head != NULL);

    if (is_page_in_list(head, page))
    {
        list_del(get_list_page(head, page));
        struct Page *curr_page = le2page(curr_ptr, pra_page_link);
        cprintf("\n");
        cprintf("out:curr_pra_vaddr %p\n", curr_page->pra_vaddr);
    }

    // 添加当前页面至链表头，表示最近访问
    list_add(head, entry);

    curr_ptr = entry;
    cprintf("\n");
    cprintf("in:curr_ptr %p\n", curr_ptr);

    return 0;
}

// 删除页面——删除链表尾的页面
static int
_lru_swap_out_victim(struct mm_struct *mm, struct Page **ptr_page, int in_tick)
{
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    assert(head != NULL);
    assert(in_tick == 0);

    list_entry_t *le = head->prev;

    curr_ptr = le;

    struct Page *curr_page = le2page(curr_ptr, pra_page_link);

    list_del(le);

    cprintf("out:curr_ptr %p\n\n", curr_ptr);
    assert(curr_ptr != NULL);

    *ptr_page = curr_page;
    return 0;
}

// 测试代码
static int _lru_check_swap(void)
{
#ifdef ucore_test
    int score = 0, totalscore = 5;
    cprintf("%d\n", &score);
    ++score;
    cprintf("grading %d/%d points", score, totalscore);
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 4);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 4);
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 4);
    *(unsigned char *)0x2000 = 0x0b;
    ++score;
    cprintf("grading %d/%d points", score, totalscore);
    assert(pgfault_num == 4);
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 5);
    ++score;
    cprintf("grading %d/%d points", score, totalscore);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 5);
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 5);
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 5);
    ++score;
    cprintf("grading %d/%d points", score, totalscore);
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 5);
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 6);
    ++score;
    cprintf("grading %d/%d points", score, totalscore);
#else
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 4);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 4);
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 4);
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 4);
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 5);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 5);
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 5);
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 5);
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 5);
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 6);
#endif
    return 0;
}

static int
_lru_init(void)
{
    return 0;
}
// 初始化的时候什么都不做

static int
_lru_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_lru_tick_event(struct mm_struct *mm)
{
    return 0;
}
// 时钟中断的时候什么都不做

struct swap_manager swap_manager_lru =
    {
        .name = "lru swap manager",
        .init = &_lru_init,
        .init_mm = &_lru_init_mm,
        .tick_event = &_lru_tick_event,
        .map_swappable = &_lru_map_swappable,
        .set_unswappable = &_lru_set_unswappable,
        .swap_out_victim = &_lru_swap_out_victim,
        .check_swap = &_lru_check_swap,
};