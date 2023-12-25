#include <defs.h>
#include <mmu.h>
#include <sem.h>
#include <ide.h>
#include <inode.h>
#include <kmalloc.h>
#include <dev.h>
#include <vfs.h>
#include <iobuf.h>
#include <error.h>
#include <assert.h>

#define DISK0_BLKSIZE PGSIZE
#define DISK0_BUFSIZE (4 * DISK0_BLKSIZE)
#define DISK0_BLK_NSECT (DISK0_BLKSIZE / SECTSIZE)

static char *disk0_buffer;
static semaphore_t disk0_sem;

static void
lock_disk0(void)
{
    down(&(disk0_sem));
}

static void
unlock_disk0(void)
{
    up(&(disk0_sem));
}

static int
disk0_open(struct device *dev, uint32_t open_flags)
{
    return 0;
}

static int
disk0_close(struct device *dev)
{
    return 0;
}

//实际上属于设备驱动的部分

// 从磁盘0中读取指定块号范围的数据
static void
disk0_read_blks_nolock(uint32_t blkno, uint32_t nblks)
{
    int ret;
    // sectno起始扇区号,nsecs要读取的扇区数
    uint32_t sectno = blkno * DISK0_BLK_NSECT, nsecs = nblks * DISK0_BLK_NSECT;

    // 调用 ide_read_secs 函数从磁盘0中读取数据
    // DISK0_DEV_NO 是磁盘0的设备编号，disk0_buffer 是存储读取数据的缓冲区
    if ((ret = ide_read_secs(DISK0_DEV_NO, sectno, disk0_buffer, nsecs)) != 0)
    {
        panic("disk0: read blkno = %d (sectno = %d), nblks = %d (nsecs = %d): 0x%08x.\n",
              blkno, sectno, nblks, nsecs, ret);
    }
}

static void
disk0_write_blks_nolock(uint32_t blkno, uint32_t nblks)
{
    int ret;
    uint32_t sectno = blkno * DISK0_BLK_NSECT, nsecs = nblks * DISK0_BLK_NSECT;
    if ((ret = ide_write_secs(DISK0_DEV_NO, sectno, disk0_buffer, nsecs)) != 0)
    {
        panic("disk0: write blkno = %d (sectno = %d), nblks = %d (nsecs = %d): 0x%08x.\n",
              blkno, sectno, nblks, nsecs, ret);
    }
}

// 对磁盘0执行读取或写入操作
static int
disk0_io(struct device *dev, struct iobuf *iob, bool write)
{
    // 从'iob'结构体中提取偏移量和剩余数据大小
    off_t offset = iob->io_offset;
    size_t resid = iob->io_resid;

    // 根据块大小计算块号和块数
    uint32_t blkno = offset / DISK0_BLKSIZE;
    uint32_t nblks = resid / DISK0_BLKSIZE;

    /* don't allow I/O that isn't block-aligned */
    // 不允许非块对齐的I/O操作
    if ((offset % DISK0_BLKSIZE) != 0 || (resid % DISK0_BLKSIZE) != 0)
    {
        return -E_INVAL;
    }

    /* don't allow I/O past the end of disk0 */
    // 不允许超出磁盘0末尾的I/O操作
    if (blkno + nblks > dev->d_blocks)
    {
        return -E_INVAL;
    }

    /* read/write nothing ? */
    // 读/写为0?
    if (nblks == 0)
    {
        return 0;
    }

    // 锁定磁盘0以进行安全访问
    lock_disk0();

    // 处理剩余待处理数据的循环
    while (resid != 0)
    {
        size_t copied, alen = DISK0_BUFSIZE;
        // 如果是写入操作
        if (write)
        {
            // 将数据从'iob'结构体移动到'disk0_buffer'中，然后将其写入磁盘0
            iobuf_move(iob, disk0_buffer, alen, 0, &copied);
            assert(copied != 0 && copied <= resid && copied % DISK0_BLKSIZE == 0);
            nblks = copied / DISK0_BLKSIZE;
            disk0_write_blks_nolock(blkno, nblks);
        }
        // 如果是读操作
        else
        {
            if (alen > resid)
            {
                alen = resid;
            }
            // 从磁盘0读取数据到'disk0_buffer'中，然后从'disk0_buffer'中移动数据到'iob'结构体中
            nblks = alen / DISK0_BLKSIZE;
            disk0_read_blks_nolock(blkno, nblks);
            iobuf_move(iob, disk0_buffer, alen, 1, &copied);
            assert(copied == alen && copied % DISK0_BLKSIZE == 0);
        }
        // 更新剩余数据大小'resid'和块号'blkno'
        resid -= copied, blkno += nblks;
    }
    // 完成后解锁磁盘0
    unlock_disk0();
    return 0;
}

static int
disk0_ioctl(struct device *dev, int op, void *data)
{
    return -E_UNIMP;
}

static void
disk0_device_init(struct device *dev)
{
    static_assert(DISK0_BLKSIZE % SECTSIZE == 0);
    if (!ide_device_valid(DISK0_DEV_NO))
    {
        panic("disk0 device isn't available.\n");
    }
    dev->d_blocks = ide_device_size(DISK0_DEV_NO) / DISK0_BLK_NSECT;
    dev->d_blocksize = DISK0_BLKSIZE;
    dev->d_open = disk0_open;
    dev->d_close = disk0_close;
    dev->d_io = disk0_io;
    dev->d_ioctl = disk0_ioctl;
    sem_init(&(disk0_sem), 1);

    static_assert(DISK0_BUFSIZE % DISK0_BLKSIZE == 0);
    if ((disk0_buffer = kmalloc(DISK0_BUFSIZE)) == NULL)
    {
        panic("disk0 alloc buffer failed.\n");
    }
}

void dev_init_disk0(void)
{
    struct inode *node;
    if ((node = dev_create_inode()) == NULL)
    {
        panic("disk0: dev_create_node.\n");
    }
    disk0_device_init(vop_info(node, device));

    int ret;
    if ((ret = vfs_add_dev("disk0", node, 1)) != 0)
    {
        panic("disk0: vfs_add_dev: %e.\n", ret);
    }
}
