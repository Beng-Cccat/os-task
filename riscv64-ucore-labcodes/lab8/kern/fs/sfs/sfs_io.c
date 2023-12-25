#include <defs.h>
#include <string.h>
#include <dev.h>
#include <sfs.h>
#include <iobuf.h>
#include <bitmap.h>
#include <assert.h>

//Basic block-level I/O routines

/* sfs_rwblock_nolock - Basic block-level I/O routine for Rd/Wr one disk block,
 *                      without lock protect for mutex process on Rd/Wr disk block
 * @sfs:   sfs_fs which will be process
 * @buf:   the buffer uesed for Rd/Wr
 * @blkno: the NO. of disk block
 * @write: BOOL: Read or Write
 * @check: BOOL: if check (blono < sfs super.blocks)
 * 
 * sfs_rwblock_noblock->dop_io->disk0_io->disk0_read_blks_nolock->ide_read_secs完成对磁盘的操作
 */
static int
sfs_rwblock_nolock(struct sfs_fs *sfs, void *buf, uint32_t blkno, bool write, bool check) {
    assert((blkno != 0 || !check) && blkno < sfs->super.blocks);
    struct iobuf __iob, *iob = iobuf_init(&__iob, buf, SFS_BLKSIZE, blkno * SFS_BLKSIZE);
    return dop_io(sfs->dev, iob, write);
}

/* sfs_rwblock - Basic block-level I/O routine for Rd/Wr N disk blocks ,
 *               with lock protect for mutex process on Rd/Wr disk block
 * @sfs:   sfs_fs which will be process
 * @buf:   the buffer uesed for Rd/Wr
 * @blkno: the NO. of disk block
 * @nblks: Rd/Wr number of disk block
 * @write: BOOL: Read - 0 or Write - 1
 */
/* 
 * sfs_rwblock - 用于读取/写入 N 个磁盘块的基本块级 I/O 例程，
 *               并在 Rd/Wr 磁盘块时进行互斥处理的锁保护
 * @sfs:    将要进行处理的 sfs_fs 结构体
 * @buf:    用于读取/写入的缓冲区
 * @blkno:  磁盘块的编号
 * @nblks:  读取/写入的磁盘块数量
 * @write:  布尔值: 读取 - 0 或写入 - 1
 */

static int
sfs_rwblock(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks, bool write) {
    int ret = 0;
    //锁定SFS文件系统
    lock_sfs_io(sfs);
    {
        //循环处理每个磁盘块，直到读取/写入完指定数量的磁盘块
        while (nblks != 0) {
            //调用 sfs_rwblock_nolock 函数来读取或写入一个磁盘块的内容。
            //如果读取/写入操作失败（返回值不为0），则退出循环。
            if ((ret = sfs_rwblock_nolock(sfs, buf, blkno, write, 1)) != 0) {
                break;
            }
            //更新磁盘块编号、剩余磁盘块数量和缓冲区指针，以处理下一个磁盘块。
            blkno ++, nblks --;
            buf += SFS_BLKSIZE;
        }
    }
    //解锁
    unlock_sfs_io(sfs);
    return ret;
}

/* sfs_rblock - The Wrap of sfs_rwblock function for Rd N disk blocks ,
 *
 * @sfs:   sfs_fs which will be process
 * @buf:   the buffer uesed for Rd/Wr
 * @blkno: the NO. of disk block
 * @nblks: Rd/Wr number of disk block
 */
int
sfs_rblock(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks) {
    return sfs_rwblock(sfs, buf, blkno, nblks, 0);
}

/* sfs_wblock - The Wrap of sfs_rwblock function for Wr N disk blocks ,
 *
 * @sfs:   sfs_fs which will be process
 * @buf:   the buffer uesed for Rd/Wr
 * @blkno: the NO. of disk block
 * @nblks: Rd/Wr number of disk block
 */
int
sfs_wblock(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks) {
    return sfs_rwblock(sfs, buf, blkno, nblks, 1);
}

/* sfs_rbuf - The Basic block-level I/O routine for  Rd( non-block & non-aligned io) one disk block(using sfs->sfs_buffer)
 *            with lock protect for mutex process on Rd/Wr disk block
 * @sfs:    sfs_fs which will be process
 * @buf:    the buffer uesed for Rd
 * @len:    the length need to Rd
 * @blkno:  the NO. of disk block
 * @offset: the offset in the content of disk block
 */
/* 
 * sfs_rbuf - 用于读取一个磁盘块的基本块级I/O例程（非块对齐和非块I/O），使用 sfs->sfs_buffer，
 *            并在Rd/Wr磁盘块时进行互斥处理的锁保护
 * @sfs:    将要进行处理的 sfs_fs 结构体
 * @buf:    用于读取的缓冲区
 * @len:    需要读取的长度
 * @blkno:  磁盘块的编号
 * @offset: 磁盘块内容中的偏移量
 */

int
sfs_rbuf(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset) {
    //使用断言确保参数合法性，即偏移量在有效范围内
    assert(offset >= 0 && offset < SFS_BLKSIZE && offset + len <= SFS_BLKSIZE);
    int ret;
    //锁定SFS文件系统，确保在执行I/O操作时互斥
    lock_sfs_io(sfs);
    {
        //调用 sfs_rwblock_nolock 函数来读取指定磁盘块的内容到 SFS 文件系统结构体中的缓冲区 sfs_buffer。
        //如果读取成功（返回值为0），则将缓冲区的内容从指定偏移量开始复制到目标缓冲区 buf 中。
        if ((ret = sfs_rwblock_nolock(sfs, sfs->sfs_buffer, blkno, 0, 1)) == 0) {
            memcpy(buf, sfs->sfs_buffer + offset, len);
        }
    }
    //解锁
    unlock_sfs_io(sfs);
    return ret;
}

/* sfs_wbuf - The Basic block-level I/O routine for  Wr( non-block & non-aligned io) one disk block(using sfs->sfs_buffer)
 *            with lock protect for mutex process on Rd/Wr disk block
 * @sfs:    sfs_fs which will be process
 * @buf:    the buffer uesed for Wr
 * @len:    the length need to Wr
 * @blkno:  the NO. of disk block
 * @offset: the offset in the content of disk block
 */
int
sfs_wbuf(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset) {
    assert(offset >= 0 && offset < SFS_BLKSIZE && offset + len <= SFS_BLKSIZE);
    int ret;
    lock_sfs_io(sfs);
    {
        if ((ret = sfs_rwblock_nolock(sfs, sfs->sfs_buffer, blkno, 0, 1)) == 0) {
            memcpy(sfs->sfs_buffer + offset, buf, len);
            ret = sfs_rwblock_nolock(sfs, sfs->sfs_buffer, blkno, 1, 1);
        }
    }
    unlock_sfs_io(sfs);
    return ret;
}

/*
 * sfs_sync_super - write sfs->super (in memory) into disk (SFS_BLKN_SUPER, 1) with lock protect.
 */
int
sfs_sync_super(struct sfs_fs *sfs) {
    int ret;
    lock_sfs_io(sfs);
    {
        memset(sfs->sfs_buffer, 0, SFS_BLKSIZE);
        memcpy(sfs->sfs_buffer, &(sfs->super), sizeof(sfs->super));
        ret = sfs_rwblock_nolock(sfs, sfs->sfs_buffer, SFS_BLKN_SUPER, 1, 0);
    }
    unlock_sfs_io(sfs);
    return ret;
}

/*
 * sfs_sync_freemap - write sfs bitmap into disk (SFS_BLKN_FREEMAP, nblks)  without lock protect.
 */
int
sfs_sync_freemap(struct sfs_fs *sfs) {
    uint32_t nblks = sfs_freemap_blocks(&(sfs->super));
    return sfs_wblock(sfs, bitmap_getdata(sfs->freemap, NULL), SFS_BLKN_FREEMAP, nblks);
}

/*
 * sfs_clear_block - write zero info into disk (blkno, nblks)  with lock protect.
 * @sfs:   sfs_fs which will be process
 * @blkno: the NO. of disk block
 * @nblks: Rd/Wr number of disk block
 */
int
sfs_clear_block(struct sfs_fs *sfs, uint32_t blkno, uint32_t nblks) {
    int ret;
    lock_sfs_io(sfs);
    {
        memset(sfs->sfs_buffer, 0, SFS_BLKSIZE);
        while (nblks != 0) {
            if ((ret = sfs_rwblock_nolock(sfs, sfs->sfs_buffer, blkno, 1, 1)) != 0) {
                break;
            }
            blkno ++, nblks --;
        }
    }
    unlock_sfs_io(sfs);
    return ret;
}

