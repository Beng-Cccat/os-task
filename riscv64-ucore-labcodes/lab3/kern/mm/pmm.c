#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>

// virtual address of physical page array
// 管理物理页数组的虚拟地址
struct Page *pages;
// amount of physical memory (in pages)
// 物理内存页的数量
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// 内存开始处对应的页号
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
pde_t *boot_pgdir = NULL;
// physical address of boot-time page directory
uintptr_t boot_cr3;

// physical memory management
// 物理内存管理器
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - initialize a pmm_manager instance
// 初始化物理内存管理器
static void init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
// 为空闲内存建立页结构
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// 分配n个连续的页
// memory
struct Page *alloc_pages(size_t n) {
    // 用于存储分配的页面
    struct Page *page = NULL;
    bool intr_flag;

    while (1) {
        // 保存中断状态，进入临界区
        local_intr_save(intr_flag);
        // 分配n个页面
        { page = pmm_manager->alloc_pages(n); }
        local_intr_restore(intr_flag);

        // 如果分配成功或n>1或交换区没有初始化，跳出
        if (page != NULL || n > 1 || swap_init_ok == 0) break;

        extern struct mm_struct *check_mm_struct;
        // cprintf("page %x, call swap_out in alloc_pages %d\n",page, n);
        // 如果分配失败且页面数量为1，进行页面置换
        swap_out(check_mm_struct, n, 0);
    }
    // cprintf("n %d,get page %x, No %d in alloc_pages\n",n,page,(page-pages));
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
// 释放连续的n页
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;

    local_intr_save(intr_flag);
    { pmm_manager->free_pages(base, n); }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
// 获取当前空闲内存的页数量
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    { ret = pmm_manager->nr_free_pages(); }
    local_intr_restore(intr_flag);
    return ret;
}

/* page_init - initialize the physical memory management */
// 初始化物理内存管理
static void page_init(void) {
    extern char kern_entry[];

    // 物理地址到虚拟地址的偏移
    va_pa_offset = KERNBASE - 0x80200000;
    uint64_t mem_begin = KERNEL_BEGIN_PADDR;
    uint64_t mem_size = PHYSICAL_MEMORY_END - KERNEL_BEGIN_PADDR;
    uint64_t mem_end = PHYSICAL_MEMORY_END; //硬编码取代 sbi_query_memory()接口
    cprintf("membegin %llx memend %llx mem_size %llx\n",mem_begin, mem_end, mem_size);
    cprintf("physcial memory map:\n");
    cprintf("  memory: 0x%08lx, [0x%08lx, 0x%08lx].\n", mem_size, mem_begin,
            mem_end - 1);
    // maxpa=物理地址中的内存结束地址
    uint64_t maxpa = mem_end;

    // 不能超过虚拟地址中的内存顶？？
    if (maxpa > KERNTOP) {
        maxpa = KERNTOP;
    }

    extern char end[];

    //物理内存页的数量=物理内存结束地址/页大小
    npage = maxpa / PGSIZE;
    // BBL has put the initial page table at the first available page after the
    // kernel
    // so stay away from it by adding extra offset to end
    // BBL已经将初始页表放置在内核之后的第一个可用页面上
    // 为了避免与它冲突，我们需要在 'end' 变量的地址上添加额外的偏移量。
    // （相当于对齐一下）
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);
    for (size_t i = 0; i < npage - nbase; i++) {
        // 将虚拟地址中映射的内核地址空间的标志位设置为内核保留
        SetPageReserved(pages + i);
    }
    // 计算空闲物理内存的起始地址，其是pages结构之后的地址，确保不会覆盖前面保留的内核映射
    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));
    // 按页对齐
    mem_begin = ROUNDUP(freemem, PGSIZE);
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    if (freemem < mem_end) {
        // 如果空闲内存在内存里，初始化为页表结构
        init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE);
    }
}

static void enable_paging(void) {
    write_csr(satp, (0x8000000000000000) | (boot_cr3 >> RISCV_PGSHIFT));
}

/**
 * @brief      setup and enable the paging mechanism
 *
 * @param      pgdir  The page dir
 * @param[in]  la     Linear address of this memory need to map
 * @param[in]  size   Memory size
 * @param[in]  pa     Physical address of this memory
 * @param[in]  perm   The permission of this memory
 */
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm) {
    assert(PGOFF(la) == PGOFF(pa));
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE) {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - allocate one page using pmm->alloc_pages(1)
// return value: the kernel virtual address of this allocated page
// note: this function is used to get the memory for PDT(Page Directory
// Table)&PT(Page Table)
static void *boot_alloc_page(void) {
    struct Page *p = alloc_page();
    if (p == NULL) {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup
// paging mechanism
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other
    // size).
    // So a framework of physical memory manager (struct pmm_manager)is defined
    // in pmm.h
    // First we should init a physical memory manager(pmm) based on the
    // framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    // 检测物理内存空间，保留已经被使用的内存，然后使用pmm->init_memmap来创建空闲页面列表。
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a
    // pmm
    // 使用pmm->check函数来验证pmm中分配和释放功能的正确性。
    check_alloc_page();
    // create boot_pgdir, an initial page directory(Page Directory Table, PDT)
    // 创建 boot_pgdir，一个初始的页目录
    // boot_page_table_sv39三级页表的虚拟地址
    extern char boot_page_table_sv39[];
    // 初始页目录指向三级页表的虚拟地址
    boot_pgdir = (pte_t*)boot_page_table_sv39;
    // 计算初始页目录的物理地址
    boot_cr3 = PADDR(boot_pgdir);
    // 验证初始页目录的正确性
    check_pgdir();
    // 断言，确保 KERNBASE 和 KERNTOP 是页大小 PTSIZE 的整数倍。
    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // map all physical memory to linear memory with base linear addr KERNBASE
    // linear_addr KERNBASE~KERNBASE+KMEMSIZE = phy_addr 0~KMEMSIZE
    // But shouldn't use this map until enable_paging() & gdt_init() finished.
    //boot_map_segment(boot_pgdir, KERNBASE, KMEMSIZE, PADDR(KERNBASE),
     //                READ_WRITE_EXEC);

    // temporary map:
    // virtual_addr 3G~3G+4M = linear_addr 0~4M = linear_addr 3G~3G+4M =
    // phy_addr 0~4M
    // boot_pgdir[0] = boot_pgdir[PDX(KERNBASE)];

    //    enable_paging();

    // now the basic virtual memory map(see memalyout.h) is established.
    // check the correctness of the basic virtual memory map.
    // 现在已经建立了基本的虚拟内存映射（参见 memalyout.h）。
    // 验证基本虚拟内存映射的正确性。
    check_boot_pgdir();

}

// get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
// 寻找（必要时分配）一个页表项
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    /*
     *
     * If you need to visit a physical address, please use KADDR()
     * please read pmm.h for useful macros
     *
     * Maybe you want help comment, BELOW comments can help you finish the code
     *
     * Some Useful MACROs and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   PDX(la) = the index of page directory entry of VIRTUAL ADDRESS la.
     *   KADDR(pa) : takes a physical address and returns the corresponding
     * kernel virtual address.
     *   set_page_ref(page,1) : means the page be referenced by one time
     *   page2pa(page): get the physical address of memory which this (struct
     * Page *) page  manages
     *   struct Page * alloc_page() : allocation a page
     *   memset(void *s, char c, size_t n) : sets the first n bytes of the
     * memory area pointed by s
     *                                       to the specified value c.
     * DEFINEs:
     *   PTE_P           0x001                   // page table/directory entry
     * flags bit : Present
     *   PTE_W           0x002                   // page table/directory entry
     * flags bit : Writeable
     *   PTE_U           0x004                   // page table/directory entry
     * flags bit : User can access
     */
    // 通过使用PDX1宏计算虚拟地址la对应的一级页表项的索引
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V)) {
        // 如果一级页表项无效，表示没有相应的二级页表
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL) {
            return NULL;
        }
        // 创建新页，将分配页的引用计数设置为1
        set_page_ref(page, 1);
        // 获取新分配的页的物理地址，并存储在pa中
        uintptr_t pa = page2pa(page);
        // 将物理地址pa映射到内核地址空间
        memset(KADDR(pa), 0, PGSIZE);
        // 在虚拟地址空间中，要转化为kaddr再memset
        // 要确保物理地址和虚拟地址的偏移量始终相同
        // 创建一个新的页表项，设置标志位，存储在一级页表项中，并将页表项指向新分配的页
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    // 获取二级页表项索引
    pde_t *pdep0 = &((pde_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];// 再下一级页表
//    pde_t *pdep0 = &((pde_t *)(PDE_ADDR(*pdep1)))[PDX0(la)];
    // 如果二级页表项无效，说明没有对应的页表
    if (!(*pdep0 & PTE_V)) {
    	struct Page *page;
    	if (!create || (page = alloc_page()) == NULL) {
    		return NULL;
    	}
    	set_page_ref(page, 1);
    	uintptr_t pa = page2pa(page);
    	memset(KADDR(pa), 0, PGSIZE);
 //   	memset(pa, 0, PGSIZE);
    	*pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    // 找到输入的虚拟地址la对应的页表项的地址
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - get related Page struct for linear address la using PDT pgdir
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store) {
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL) {
        *ptep_store = ptep;
    }
    if (ptep != NULL && *ptep & PTE_V) {
        return pte2page(*ptep);
    }
    return NULL;
}

// page_remove_pte - free an Page sturct which is related linear address la
//                - and clean(invalidate) pte which is related linear address la
// note: PT is changed, so the TLB need to be invalidate
// 删除一个页表项以及它的映射
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep) {
    /*
     *
     * Please check if ptep is valid, and tlb must be manually updated if
     * mapping is updated
     *
     * Maybe you want help comment, BELOW comments can help you finish the code
     *
     * Some Useful MACROs and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   struct Page *page pte2page(*ptep): get the according page from the
     * value of a ptep
     *   free_page : free a page
     *   page_ref_dec(page) : decrease page->ref. NOTICE: ff page->ref == 0 ,
     * then this page should be free.
     *   tlb_invalidate(pde_t *pgdir, uintptr_t la) : Invalidate a TLB entry,
     * but only if the page tables being
     *                        edited are the ones currently in use by the
     * processor.
     * DEFINEs:
     *   PTE_P           0x001                   // page table/directory entry
     * flags bit : Present
     */
    // 检查页表入口是否有效
    if (*ptep & PTE_V) {  //(1) check if this page table entry is
        // 找到与该页表项有关的页
        struct Page *page =
            pte2page(*ptep);  //(2) find corresponding page to pte
        // 去掉页引用
        page_ref_dec(page);   //(3) decrease page reference
        if (page_ref(page) ==
            0) {  //(4) and free this page when page reference reachs 0
            // 当页引用为0时，释放页
            free_page(page);
        }
        // 清除页表入口
        *ptep = 0;                  //(5) clear second page table entry
        // 刷新页表
        tlb_invalidate(pgdir, la);  //(6) flush tlb
    }
}

// page_remove - free an Page which is related linear address la and has an
// validated pte
// 删除映射关系
// 
void page_remove(pde_t *pgdir, uintptr_t la) {
    // 找到页表项所在位置
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL) {
        // 删除这个页表项的映射
        page_remove_pte(pgdir, la, ptep);
    }
}

// page_insert - build the map of phy addr of an Page with the linear addr la
// paramemters:
//  pgdir: the kernel virtual base address of PDT
//  page:  the Page which need to map
//  la:    the linear address need to map
//  perm:  the permission of this Page which is setted in related pte
// return value: always 0
// note: PT is changed, so the TLB need to be invalidate
// 新增映射关系
// 参数：页表基址，物理页面，虚拟地址
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm) {
    // 先找到对应页表项的位置
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }
    // 指向这个物理页面的虚拟地址新增了一个
    page_ref_inc(page);
    if (*ptep & PTE_V) {
        // 原先存在映射
        struct Page *p = pte2page(*ptep);
        if (p == page) {
            // 如果这个映射原先就有
            page_ref_dec(page);
        } else {
            // 如果原先这个虚拟地址映射到其他物理页面
            // 那么需要删除映射
            page_remove_pte(pgdir, la, ptep);
        }
    }
    // 构造页表项
    *ptep = pte_create(page2ppn(page), PTE_V | perm);
    // 页表改变之后要刷新TLB
    tlb_invalidate(pgdir, la);
    return 0;
}

// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void tlb_invalidate(pde_t *pgdir, uintptr_t la) { flush_tlb(); }

// pgdir_alloc_page - call alloc_page & page_insert functions to
//                  - allocate a page size memory & setup an addr map
//                  - pa<->la with linear address la and the PDT pgdir
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm) {
    struct Page *page = alloc_page();
    if (page != NULL) {
        if (page_insert(pgdir, page, la, perm) != 0) {
            free_page(page);
            return NULL;
        }
        if (swap_init_ok) {
            swap_map_swappable(check_mm_struct, la, page, 0);
            page->pra_vaddr = la;
            assert(page_ref(page) == 1);
            // cprintf("get No. %d  page: pra_vaddr %x, pra_link.prev %x,
            // pra_link_next %x in pgdir_alloc_page\n", (page-pages),
            // page->pra_vaddr,page->pra_page_link.prev,
            // page->pra_page_link.next);
        }
    }

    return page;
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

static void check_pgdir(void) {
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store=nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir != NULL && (uint32_t)PGOFF(boot_pgdir) == 0);
    assert(get_page(boot_pgdir, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir, p1, 0x0, 0) == 0);
    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir[0])) == 1);

    pde_t *pd1=boot_pgdir,*pd0=page2kva(pde2page(boot_pgdir[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir[0] = 0;

    assert(nr_free_store==nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

static void check_boot_pgdir(void) {
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store=nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE) {
        assert((ptep = get_pte(boot_pgdir, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }


    assert(boot_pgdir[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1=boot_pgdir,*pd0=page2kva(pde2page(boot_pgdir[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir[0] = 0;

    assert(nr_free_store==nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// 分配至少n个连续的字节
void *kmalloc(size_t n) {
    void *ptr = NULL;
    struct Page *base = NULL;
    assert(n > 0 && n < 1024 * 0124);
    // 向上取整到整数个页
    int num_pages = (n + PGSIZE - 1) / PGSIZE;
    base = alloc_pages(num_pages);
    // 分配失败直接panic
    assert(base != NULL);
    // 分配的内存的起始位置
    ptr = page2kva(base);
    return ptr;
}

// 从某个位置开始释放n个字节
void kfree(void *ptr, size_t n) {
    assert(n > 0 && n < 1024 * 0124);
    assert(ptr != NULL);
    struct Page *base = NULL;
    int num_pages = (n + PGSIZE - 1) / PGSIZE;
    base = kva2page(ptr);
    free_pages(base, num_pages);
}
