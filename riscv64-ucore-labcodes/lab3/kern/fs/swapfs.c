#include <swap.h>
#include <swapfs.h>
#include <mmu.h>
#include <fs.h>
#include <ide.h>
#include <pmm.h>
#include <assert.h>

// 初始化虚拟内存的交换文件系统
void
swapfs_init(void) {
    // 检查页面大小是否是扇区大小的整数倍
    static_assert((PGSIZE % SECTSIZE) == 0);
    if (!ide_device_valid(SWAP_DEV_NO)) {
        panic("swap fs isn't available.\n");
    }
    // 计算交换设备中可以存储的页面数
    max_swap_offset = ide_device_size(SWAP_DEV_NO) / (PGSIZE / SECTSIZE);
}

// 用于从交换文件系统中读取数据到内存页面中
// 参数：IDE设备编号、起始扇区号、存储数据的目标缓冲区、要读取的扇区数量
int
swapfs_read(swap_entry_t entry, struct Page *page) {
    return ide_read_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}

// 用于将内存页面的数据写入到交换文件系统中
// 参数：参数：IDE设备编号、要写入的起始扇区号、包含数据的源缓冲区、要写入的扇区数量
int
swapfs_write(swap_entry_t entry, struct Page *page) {
    return ide_write_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}

