#include <defs.h>
#include <string.h>
#include <stdlib.h>
#include <list.h>
#include <stat.h>
#include <kmalloc.h>
#include <vfs.h>
#include <dev.h>
#include <sfs.h>
#include <inode.h>
#include <iobuf.h>
#include <bitmap.h>
#include <error.h>
#include <assert.h>

static const struct inode_ops sfs_node_dirops;  // dir operations
static const struct inode_ops sfs_node_fileops; // file operations

/*
 * lock_sin - lock the process of inode Rd/Wr
 */
static void
lock_sin(struct sfs_inode *sin)
{
    down(&(sin->sem));
}

/*
 * unlock_sin - unlock the process of inode Rd/Wr
 */
static void
unlock_sin(struct sfs_inode *sin)
{
    up(&(sin->sem));
}

/*
 * sfs_get_ops - return function addr of fs_node_dirops/sfs_node_fileops
 */
static const struct inode_ops *
sfs_get_ops(uint16_t type)
{
    switch (type)
    {
    case SFS_TYPE_DIR:
        return &sfs_node_dirops;
    case SFS_TYPE_FILE:
        return &sfs_node_fileops;
    }
    panic("invalid file type %d.\n", type);
}

/*
 * sfs_hash_list - return inode entry in sfs->hash_list
 */
static list_entry_t *
sfs_hash_list(struct sfs_fs *sfs, uint32_t ino)
{
    return sfs->hash_list + sin_hashfn(ino);
}

/*
 * sfs_set_links - link inode sin in sfs->linked-list AND sfs->hash_link
 */
static void
sfs_set_links(struct sfs_fs *sfs, struct sfs_inode *sin)
{
    list_add(&(sfs->inode_list), &(sin->inode_link));
    list_add(sfs_hash_list(sfs, sin->ino), &(sin->hash_link));
}

/*
 * sfs_remove_links - unlink inode sin in sfs->linked-list AND sfs->hash_link
 */
static void
sfs_remove_links(struct sfs_inode *sin)
{
    list_del(&(sin->inode_link));
    list_del(&(sin->hash_link));
}

/*
 * sfs_block_inuse - check the inode with NO. ino inuse info in bitmap
 */
static bool
sfs_block_inuse(struct sfs_fs *sfs, uint32_t ino)
{
    if (ino != 0 && ino < sfs->super.blocks)
    {
        return !bitmap_test(sfs->freemap, ino);
    }
    panic("sfs_block_inuse: called out of range (0, %u) %u.\n", sfs->super.blocks, ino);
}

/*
 * sfs_block_alloc -  check and get a free disk block
 */
static int
sfs_block_alloc(struct sfs_fs *sfs, uint32_t *ino_store)
{
    int ret;
    if ((ret = bitmap_alloc(sfs->freemap, ino_store)) != 0)
    {
        return ret;
    }
    assert(sfs->super.unused_blocks > 0);
    sfs->super.unused_blocks--, sfs->super_dirty = 1;
    assert(sfs_block_inuse(sfs, *ino_store));
    return sfs_clear_block(sfs, *ino_store, 1);
}

/*
 * sfs_block_free - set related bits for ino block to 1(means free) in bitmap, add sfs->super.unused_blocks, set superblock dirty *
 */
static void
sfs_block_free(struct sfs_fs *sfs, uint32_t ino)
{
    assert(sfs_block_inuse(sfs, ino));
    bitmap_free(sfs->freemap, ino);
    sfs->super.unused_blocks++, sfs->super_dirty = 1;
}

/*
 * sfs_create_inode - alloc a inode in memroy, and init din/ino/dirty/reclian_count/sem fields in sfs_inode in inode
 */
static int
sfs_create_inode(struct sfs_fs *sfs, struct sfs_disk_inode *din, uint32_t ino, struct inode **node_store)
{
    struct inode *node;
    if ((node = alloc_inode(sfs_inode)) != NULL)
    {
        vop_init(node, sfs_get_ops(din->type), info2fs(sfs, sfs));
        struct sfs_inode *sin = vop_info(node, sfs_inode);
        sin->din = din, sin->ino = ino, sin->dirty = 0, sin->reclaim_count = 1;
        sem_init(&(sin->sem), 1);
        *node_store = node;
        return 0;
    }
    return -E_NO_MEM;
}

/*
 * lookup_sfs_nolock - according ino, find related inode
 *
 * NOTICE: le2sin, info2node MACRO
 */
static struct inode *
lookup_sfs_nolock(struct sfs_fs *sfs, uint32_t ino)
{
    struct inode *node;
    list_entry_t *list = sfs_hash_list(sfs, ino), *le = list;
    while ((le = list_next(le)) != list)
    {
        struct sfs_inode *sin = le2sin(le, hash_link);
        if (sin->ino == ino)
        {
            node = info2node(sin, sfs_inode);
            if (vop_ref_inc(node) == 1)
            {
                sin->reclaim_count++;
            }
            return node;
        }
    }
    return NULL;
}

/*
 * sfs_load_inode - If the inode isn't existed, load inode related ino disk block data into a new created inode.
 *                  If the inode is in memory alreadily, then do nothing
 */
int sfs_load_inode(struct sfs_fs *sfs, struct inode **node_store, uint32_t ino)
{
    lock_sfs_fs(sfs);
    struct inode *node;
    if ((node = lookup_sfs_nolock(sfs, ino)) != NULL)
    {
        goto out_unlock;
    }

    int ret = -E_NO_MEM;
    struct sfs_disk_inode *din;
    if ((din = kmalloc(sizeof(struct sfs_disk_inode))) == NULL)
    {
        goto failed_unlock;
    }

    assert(sfs_block_inuse(sfs, ino));
    if ((ret = sfs_rbuf(sfs, din, sizeof(struct sfs_disk_inode), ino, 0)) != 0)
    {
        goto failed_cleanup_din;
    }

    assert(din->nlinks != 0);
    if ((ret = sfs_create_inode(sfs, din, ino, &node)) != 0)
    {
        goto failed_cleanup_din;
    }
    sfs_set_links(sfs, vop_info(node, sfs_inode));

out_unlock:
    unlock_sfs_fs(sfs);
    *node_store = node;
    return 0;

failed_cleanup_din:
    kfree(din);
failed_unlock:
    unlock_sfs_fs(sfs);
    return ret;
}

/*
 * sfs_bmap_get_sub_nolock - according entry pointer entp and index, find the index of indrect disk block
 *                           return the index of indrect disk block to ino_store. no lock protect
 * @sfs:      sfs file system
 * @entp:     the pointer of index of entry disk block
 * @index:    the index of block in indrect block
 * @create:   BOOL, if the block isn't allocated, if create = 1 the alloc a block,  otherwise just do nothing
 * @ino_store: 0 OR the index of already inused block or new allocated block.
 */
/*
 * 此函数用于在没有锁保护的情况下，根据 entry 指针 entp 和索引 index，在间接磁盘块中查找索引的索引。
 * 如果指定的 entry 块已经存在，则将 entry 块的内容读取到 sfs->sfs_buffer 中。
 * 如果指定的 entry 块中已经存在索引，或者 create = 0，则什么也不做。
 * 如果 entry 块不存在且 create = 0，则什么也不做。
 * 如果 entry 块不存在且 create = 1，则分配一个 entry 块（用于间接块）。
 * 如果需要，还会分配一个新的块，将其写入到 entry 块中，以表示间接块的索引。
 *
 * 如果成功分配了间接块或 entry 块，将更新相应的块索引并存储到 ino_store 中。
 * 返回 0 表示成功，其他值表示发生错误。
 */
static int
sfs_bmap_get_sub_nolock(struct sfs_fs *sfs, uint32_t *entp, uint32_t index, bool create, uint32_t *ino_store)
{
    assert(index < SFS_BLK_NENTRY);
    int ret;
    uint32_t ent, ino = 0;
    off_t offset = index * sizeof(uint32_t); // the offset of entry in entry block

    // 如果 entry 块存在，读取 entry 块的内容到 sfs->sfs_buffer 中
    // if entry block is existd, read the content of entry block into  sfs->sfs_buffer
    if ((ent = *entp) != 0)
    {
        if ((ret = sfs_rbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0)
        {
            return ret;
        }
        if (ino != 0 || !create)
        {
            goto out;
        }
    }
    else
    {
        if (!create)
        {
            goto out;
        }
        // if entry block isn't existd, allocated a entry block (for indrect block)
        //  如果 entry 块不存在且不需要创建，直接退出
        //  如果 entry 块不存在且需要创建，分配一个 entry 块（用于间接块）
        if ((ret = sfs_block_alloc(sfs, &ent)) != 0)
        {
            return ret;
        }
    }

    // 如果需要，分配一个新的块，并将其写入到 entry 块中
    if ((ret = sfs_block_alloc(sfs, &ino)) != 0)
    {
        goto failed_cleanup;
    }
    if ((ret = sfs_wbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0)
    {
        sfs_block_free(sfs, ino);
        goto failed_cleanup;
    }

out:
    // 更新 entp 指向的 entry 块的索引
    if (ent != *entp)
    {
        *entp = ent;
    }
    *ino_store = ino; // 存储分配的块的索引
    return 0;

failed_cleanup:
    // 失败时释放已分配的块
    if (ent != *entp)
    {
        sfs_block_free(sfs, ent);
    }
    return ret;
}

/*
 * sfs_bmap_get_nolock - according sfs_inode and index of block, find the NO. of disk block
 *                       no lock protect
 * @sfs:      sfs file system
 * @sin:      sfs inode in memory
 * @index:    the index of block in inode
 * @create:   BOOL, if the block isn't allocated, if create = 1 the alloc a block,  otherwise just do nothing
 * @ino_store: 0 OR the index of already inused block or new allocated block.
 */
// 函数目的：在没有加锁的情况下，根据给定的索引从 inode 中获取磁盘块的编号。
// 将第index个索引指向的block的索引值取出存到相应的指针指向的单元(ino_store)
static int
sfs_bmap_get_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index, bool create, uint32_t *ino_store)
{
    // 获取指向该 inode 的磁盘表示的指针
    struct sfs_disk_inode *din = sin->din;
    int ret;
    uint32_t ent, ino;

    // the index of disk block is in the fist SFS_NDIRECT  direct blocks
    // 如果索引小于 SFS_NDIRECT，说明磁盘块的索引在直接块中
    // SFS_NDIRECT表示直接块的数量
    if (index < SFS_NDIRECT)
    {
        // 如果相应索引处的直接块为空且需要创建，分配一个新的块给它
        if ((ino = din->direct[index]) == 0 && create)
        {
            if ((ret = sfs_block_alloc(sfs, &ino)) != 0)
            {
                return ret;
            }
            din->direct[index] = ino;
            sin->dirty = 1; // 标记 inode 已被修改
        }
        goto out;
    }

    // the index of disk block is in the indirect blocks.
    // 如果索引在 SFS_NDIRECT 之后，但小于 SFS_BLK_NENTRY，说明磁盘块的索引在间接块中
    index -= SFS_NDIRECT;
    if (index < SFS_BLK_NENTRY)
    {
        ent = din->indirect;
        // 在间接块中获取相应索引处的块号，如果有则直接返回，如果没有则分配一个然后返回块号
        if ((ret = sfs_bmap_get_sub_nolock(sfs, &ent, index, create, &ino)) != 0)
        {
            return ret;
        }
        // 如果间接块的编号发生变化，更新到 inode 中
        // 原因：
        // 分配新的间接块 || 重新分配间接块
        if (ent != din->indirect)
        {
            assert(din->indirect == 0);
            din->indirect = ent;
            sin->dirty = 1; // 标记 inode 已被修改
        }
        goto out;
    }
    else
    {
        panic("sfs_bmap_get_nolock - index out of range"); // 索引超出范围，触发 panic
    }

out:
    // 确保获取到的块号为空或者已经在使用中
    assert(ino == 0 || sfs_block_inuse(sfs, ino));
    *ino_store = ino; // 将获取到的块号存储到指定的指针中
    return 0;         // 返回成功
}

/*
 * sfs_bmap_free_sub_nolock - set the entry item to 0 (free) in the indirect block
 * 在间接块中将条目项置0
 */
static int
sfs_bmap_free_sub_nolock(struct sfs_fs *sfs, uint32_t ent, uint32_t index)
{
    assert(sfs_block_inuse(sfs, ent) && index < SFS_BLK_NENTRY);
    int ret;
    uint32_t ino, zero = 0;
    off_t offset = index * sizeof(uint32_t);
    // 验证给定的间接块仍在使用中且索引未超出间接块的有效范围
    // sfs_rbuf 函数的作用是从文件系统中读取指定块的数据到内存缓冲区
    if ((ret = sfs_rbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0)
    {
        return ret;
    }
    // 如果读取到的条目项不为 0，则将该条目项设置为 0，表示释放对应的块
    if (ino != 0)
    {
        // sfs_wbuf将数据写入到文件系统的指定块中
        if ((ret = sfs_wbuf(sfs, &zero, sizeof(uint32_t), ent, offset)) != 0)
        {
            return ret;
        }
        // 释放原始的块
        sfs_block_free(sfs, ino);
    }
    return 0;
}

/*
 * sfs_bmap_free_nolock - free a block with logical index in inode and reset the inode's fields
 * 释放 inode 中逻辑索引对应的一个块，并重置 inode 的字段
 */
/*
 * 首先，获取文件对应的 on-disk inode（din）。
 * 然后，根据逻辑索引的值判断磁盘块的存储位置：
 *   - 如果索引小于 SFS_NDIRECT，表示磁盘块存储在直接块中，释放相应的直接块。
 *   - 如果索引大于等于 SFS_NDIRECT 且小于 SFS_BLK_NENTRY，表示磁盘块存储在间接块中，释放相应的间接块项。
 * 返回 0 表示成功释放磁盘块，其他值表示发生错误。
 */
static int
sfs_bmap_free_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index)
{
    struct sfs_disk_inode *din = sin->din;
    int ret;
    uint32_t ent, ino;

    // 如果逻辑索引小于 SFS_NDIRECT，表示磁盘块存储在直接块中
    if (index < SFS_NDIRECT)
    {
        if ((ino = din->direct[index]) != 0)
        {
            // free the block
            // 如果 direct 数组中的磁盘块编号不为 0，则释放该磁盘块
            sfs_block_free(sfs, ino);
            // 重置对应的 direct 数组项为 0
            din->direct[index] = 0;
            // 标记 inode 已被修改
            sin->dirty = 1;
        }
        return 0;
    }

    index -= SFS_NDIRECT;
    // 磁盘存储在间接块中
    if (index < SFS_BLK_NENTRY)
    {
        if ((ent = din->indirect) != 0)
        {
            // set the entry item to 0 in the indirect block
            // 获取间接块中索引处的项，并释放对应的磁盘块
            if ((ret = sfs_bmap_free_sub_nolock(sfs, ent, index)) != 0)
            {
                return ret;
            }
        }
        return 0;
    }
    // 索引超出范围，不执行任何操作，返回成功
    return 0;
}

/*
 * sfs_bmap_load_nolock - according to the DIR's inode and the logical index of block in inode, find the NO. of disk block.
 * @sfs:      sfs file system
 * @sin:      sfs inode in memory
 * @index:    the logical index of disk block in inode
 * @ino_store:the NO. of disk block
 *
 * sfs_bmap_load_nolock - 根据目录的 inode 和 inode 中块的逻辑索引，查找磁盘块的编号。
 * @sfs:        SFS 文件系统
 * @sin:        内存中的 SFS inode
 * @index:      inode 中磁盘块的逻辑索引
 * @ino_store:  存储磁盘块编号的指针
 */
static int
sfs_bmap_load_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index, uint32_t *ino_store)
{
    // 获取指向该 inode 的磁盘表示的指针
    struct sfs_disk_inode *din = sin->din;

    // 断言所请求的索引不超过当前 inode 的块数
    assert(index <= din->blocks);

    int ret;
    uint32_t ino;
    // 确定是否需要创建新块（写入新的块？）
    bool create = (index == din->blocks);

    // 获取给定索引处的块映射，如果需要创建块，则创建新块
    if ((ret = sfs_bmap_get_nolock(sfs, sin, index, create, &ino)) != 0)
    {
        return ret;
    }

    // 断言获取到的块号确实在使用中
    assert(sfs_block_inuse(sfs, ino));

    // 如果需要创建块，则增加当前 inode 的块数
    if (create)
    {
        din->blocks++;
    }

    // 如果 ino_store 指针不为空，则将获取到的块号存储到指针指向的位置
    if (ino_store != NULL)
    {
        *ino_store = ino;
    }

    // 返回成功加载块映射的结果
    return 0;
}

/*
 * sfs_bmap_truncate_nolock - free the disk block at the end of file
 */
// 将多级数据索引表中的最后一个entry释放掉
/*
 * 首先，获取文件对应的 on-disk inode（din）。
 * 然后，检查文件的 blocks 数量，确保文件至少有一个磁盘块。
 * 接着，调用 sfs_bmap_free_nolock 函数释放文件末尾的磁盘块。
 * 释放成功后，更新 on-disk inode（din）中记录的 blocks 数量，并标记 sfs_inode 为已修改（dirty）。
 * 最后，返回操作结果：0 表示成功，其他值表示发生错误。
 */
static int
sfs_bmap_truncate_nolock(struct sfs_fs *sfs, struct sfs_inode *sin)
{
    struct sfs_disk_inode *din = sin->din;
    // 断言：确保文件至少有一个磁盘块，即 blocks 数量不为 0
    assert(din->blocks != 0);
    int ret;
    // 调用 sfs_bmap_free_nolock 函数释放文件末尾的磁盘块
    if ((ret = sfs_bmap_free_nolock(sfs, sin, din->blocks - 1)) != 0)
    {
        // 释放磁盘块失败，返回错误码
        return ret;
    }
    // 更新 on-disk inode 中记录的 blocks 数量（文件大小减小一个磁盘块）
    din->blocks--;
    // 标记 sfs_inode 为已修改，表示对该 inode 进行了修改
    sin->dirty = 1;
    // 返回成功
    return 0;
}

/*
 * sfs_dirent_read_nolock - read the file entry from disk block which contains this entry
 * 从包含文件条目的磁盘块中读取指定索引处的文件条目
 * @sfs:      sfs file system
 * @sin:      sfs inode in memory
 * @slot:     the index of file entry
 * @entry:    file entry
 */
/*
 * 首先，断言：验证 inode 类型为 SFS_TYPE_DIR（表示为目录）且索引在有效范围内。
 * 然后，根据目录的 inode 和文件条目的索引，查找包含该文件条目的磁盘块的索引。
 * 接着，读取磁盘块中文件条目的内容。
 * 最后，将读取的文件名以 null 结尾，并返回操作结果：0 表示成功读取文件条目，其他值表示发生错误。
 */
static int
sfs_dirent_read_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_disk_entry *entry)
{
    assert(sin->din->type == SFS_TYPE_DIR && (slot >= 0 && slot < sin->din->blocks));
    int ret;
    uint32_t ino;
    // according to the DIR's inode and the slot of file entry, find the index of disk block which contains this file entry
    // 根据目录的 inode 和文件条目的索引，找到包含该文件条目的磁盘块的索引
    if ((ret = sfs_bmap_load_nolock(sfs, sin, slot, &ino)) != 0)
    {
        return ret;
    }
    assert(sfs_block_inuse(sfs, ino));
    // 从磁盘块中读取文件条目的内容
    // read the content of file entry in the disk block
    if ((ret = sfs_rbuf(sfs, entry, sizeof(struct sfs_disk_entry), ino, 0)) != 0)
    {
        return ret;
    }
    entry->name[SFS_MAX_FNAME_LEN] = '\0';
    return 0;
}

#define sfs_dirent_link_nolock_check(sfs, sin, slot, lnksin, name)             \
    do                                                                         \
    {                                                                          \
        int err;                                                               \
        if ((err = sfs_dirent_link_nolock(sfs, sin, slot, lnksin, name)) != 0) \
        {                                                                      \
            warn("sfs_dirent_link error: %e.\n", err);                         \
        }                                                                      \
    } while (0)

#define sfs_dirent_unlink_nolock_check(sfs, sin, slot, lnksin)             \
    do                                                                     \
    {                                                                      \
        int err;                                                           \
        if ((err = sfs_dirent_unlink_nolock(sfs, sin, slot, lnksin)) != 0) \
        {                                                                  \
            warn("sfs_dirent_unlink error: %e.\n", err);                   \
        }                                                                  \
    } while (0)

/*
 * sfs_dirent_search_nolock - read every file entry in the DIR, compare file name with each entry->name
 *                            If equal, then return slot and NO. of disk of this file's inode
 *                            在目录中逐个读取文件条目，将文件名与每个条目的名称进行比较
 *                            如果相等，则返回文件条目的索引和该文件的磁盘节点号
 * @sfs:        sfs file system
 * @sin:        sfs inode in memory
 * @name:       the filename
 * @ino_store:  NO. of disk of this file (with the filename)'s inode
 * @slot:       logical index of file entry (NOTICE: each file entry ocupied one  disk block)
 * @empty_slot: the empty logical index of file entry.
 */
/**
 * SFS 实现里文件的数据页是连续的，不存在任何空洞；
 * 而对于目录，数据页不是连续的，当某个 entry 删除的时候，SFS 通过设置 entry->ino 为 0 将该 entry 所在的 block 标记为 free，
 * 在需要添加新 entry 的时候，SFS 优先使用这些 free 的 entry，其次才会去在数据页尾追加新的 entry。
 */
static int
sfs_dirent_search_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, const char *name, uint32_t *ino_store, int *slot, int *empty_slot)
{
    // empty_slot表示一个输出参数，用于存储函数执行过程中找到的空文件条目的逻辑索引

    assert(strlen(name) <= SFS_MAX_FNAME_LEN);
    struct sfs_disk_entry *entry;
    // 分配内存以存储一个文件条目的信息
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL)
    {
        return -E_NO_MEM;
    }

#define set_pvalue(x, v) \
    do                   \
    {                    \
        if ((x) != NULL) \
        {                \
            *(x) = (v);  \
        }                \
    } while (0)
    // 检查参数 x 是否为非空指针，如果是，则将参数 v 的值赋给指针 x 所指向的变量

    int ret, i, nslots = sin->din->blocks;
    // 初始化空文件条目的索引为目录中的文件条目总数
    set_pvalue(empty_slot, nslots);
    // 遍历目录中的每个文件条目，比较文件名与条目名称
    for (i = 0; i < nslots; i++)
    {
        // 读取文件条目的内容
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0)
        {
            goto out;
        }
        if (entry->ino == 0)
        {
            // 如果当前条目的磁盘节点号为 0，表示为空条目
            set_pvalue(empty_slot, i);
            continue;
        }
        if (strcmp(name, entry->name) == 0)
        {
            // 找到匹配的文件条目，设置对应的索引值
            set_pvalue(slot, i);
            // 设置对应文件的磁盘节点号
            set_pvalue(ino_store, entry->ino);
            goto out;
        }
    }
// 清除宏定义
#undef set_pvalue
    ret = -E_NOENT;
out:
    kfree(entry);
    return ret;
}

/*
 * sfs_dirent_findino_nolock - read all file entries in DIR's inode and find a entry->ino == ino
 */

static int
sfs_dirent_findino_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t ino, struct sfs_disk_entry *entry)
{
    int ret, i, nslots = sin->din->blocks;
    for (i = 0; i < nslots; i++)
    {
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0)
        {
            return ret;
        }
        if (entry->ino == ino)
        {
            return 0;
        }
    }
    return -E_NOENT;
}

/*
 * sfs_lookup_once - find inode corresponding the file name in DIR's sin inode
 * @sfs:        sfs file system
 * @sin:        DIR sfs inode in memory
 * @name:       the file name in DIR
 * @node_store: the inode corresponding the file name in DIR
 * @slot:       the logical index of file entry
 */
static int
sfs_lookup_once(struct sfs_fs *sfs, struct sfs_inode *sin, const char *name, struct inode **node_store, int *slot)
{
    int ret;
    uint32_t ino;
    lock_sin(sin);
    { // find the NO. of disk block and logical index of file entry
        //调用sfs_dirent_search_nolock函数来查找与路径名匹配的目录项，
        //如果找到目录项，则根据目录项中记录的inode所处的数据块索引值找到路径名对应的SFS磁盘inode，
        //并读入SFS磁盘inode的内容，创建SFS内存inode
        ret = sfs_dirent_search_nolock(sfs, sin, name, &ino, slot, NULL);
    }
    unlock_sin(sin);
    if (ret == 0)
    {
        // load the content of inode with the the NO. of disk block
        ret = sfs_load_inode(sfs, node_store, ino);
    }
    return ret;
}

// sfs_opendir - just check the opne_flags, now support readonly
static int
sfs_opendir(struct inode *node, uint32_t open_flags)
{
    switch (open_flags & O_ACCMODE)
    {
    case O_RDONLY:
        break;
    case O_WRONLY:
    case O_RDWR:
    default:
        return -E_ISDIR;
    }
    if (open_flags & O_APPEND)
    {
        return -E_ISDIR;
    }
    return 0;
}

// sfs_openfile - open file (no use)
static int
sfs_openfile(struct inode *node, uint32_t open_flags)
{
    return 0;
}

// sfs_close - close file
static int
sfs_close(struct inode *node)
{
    return vop_fsync(node);
}

/*
 * sfs_io_nolock - Rd/Wr a file contentfrom offset position to offset+ length  disk blocks<-->buffer (in memroy)
 * @sfs:      sfs file system
 * @sin:      sfs inode in memory
 * @buf:      the buffer Rd/Wr
 * @offset:   the offset of file
 * @alenp:    the length need to read (is a pointer). and will RETURN the really Rd/Wr lenght
 * @write:    BOOL, 0 read, 1 write
 * 
 * sfs_io_nolock - 从偏移位置到偏移+长度的磁盘块<-->缓冲区（在内存中）读取/写入文件内容
 * @sfs:      SFS 文件系统
 * @sin:      内存中的 SFS inode
 * @buf:      用于读取/写入的缓冲区
 * @offset:   文件的偏移量
 * @alenp:    需要读取/写入的长度（是一个指针）。将返回实际读取/写入的长度
 * @write:    布尔值，0 为读取，1 为写入
 */
static int
sfs_io_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, void *buf, off_t offset, size_t *alenp, bool write)
{
    //获取SFS inode对应的磁盘inode结构体
    struct sfs_disk_inode *din = sin->din;
    assert(din->type != SFS_TYPE_DIR);
    //计算读写结束位置以及起始块内偏移
    off_t endpos = offset + *alenp, blkoff;
    *alenp = 0;
    // calculate the Rd/Wr end position
    //对于非法的偏移，或者偏移等于结束位置，或者结束位置超过文件大小，返回相应的错误码。
    if (offset < 0 || offset >= SFS_MAX_FILE_SIZE || offset > endpos)
    {
        return -E_INVAL;
    }
    if (offset == endpos)
    {
        return 0;
    }
    if (endpos > SFS_MAX_FILE_SIZE)
    {
        endpos = SFS_MAX_FILE_SIZE;
    }
    if (!write)
    {
        if (offset >= din->size)
        {
            return 0;
        }
        if (endpos > din->size)
        {
            endpos = din->size;
        }
    }

    //根据操作类型，设置相应的函数指针，如 sfs_buf_op 和 sfs_block_op 分别指向读取缓冲区和读取块的函数，或者写入缓冲区和写入块的函数。
    int (*sfs_buf_op)(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);
    int (*sfs_block_op)(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);
    if (write)
    {
        sfs_buf_op = sfs_wbuf, sfs_block_op = sfs_wblock;
    }
    else
    {
        sfs_buf_op = sfs_rbuf, sfs_block_op = sfs_rblock;
    }

    int ret = 0;
    size_t size, alen = 0;
    uint32_t ino;
    //起始块号
    uint32_t blkno = offset / SFS_BLKSIZE;         // The NO. of Rd/Wr begin block
    //要读取/写入的块数
    uint32_t nblks = endpos / SFS_BLKSIZE - blkno; // The size of Rd/Wr blocks

    // LAB8:EXERCISE1 YOUR CODE HINT: 2110803
    //call sfs_bmap_load_nolock, sfs_rbuf, sfs_rblock,etc. read different kind of blocks in file
    /*
     * (1) If offset isn't aligned with the first block, Rd/Wr some content from offset to the end of the first block
     *       NOTICE: useful function: sfs_bmap_load_nolock, sfs_buf_op
     *               Rd/Wr size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset)
     * (2) Rd/Wr aligned blocks
     *       NOTICE: useful function: sfs_bmap_load_nolock, sfs_block_op
     * (3) If end position isn't aligned with the last block, Rd/Wr some content from begin to the (endpos % SFS_BLKSIZE) of the last block
     *       NOTICE: useful function: sfs_bmap_load_nolock, sfs_buf_op
     */
    // 实验8：练习1 你的代码提示：调用 sfs_bmap_load_nolock、sfs_rbuf、sfs_rblock 等函数，读取文件中的不同类型块
    /*
    * (1) 如果偏移量与第一个块不对齐，从偏移位置读取/写入一些内容直到第一个块的末尾
    *       注意：有用的函数：sfs_bmap_load_nolock，sfs_buf_op
    *               读/写大小 = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset)
    * (2) 读/写对齐的块
    *       注意：有用的函数：sfs_bmap_load_nolock，sfs_block_op
    * (3) 如果结束位置与最后一个块不对齐，从开始位置读取/写入一些内容到最后一个块的 (endpos % SFS_BLKSIZE) 处
    *       注意：有用的函数：sfs_bmap_load_nolock，sfs_buf_op
    */
    //(1)blkoff为非对齐的起始块中需要操作的偏移量
    //判断是否需要操作
    if ((blkoff = offset % SFS_BLKSIZE) != 0){
        //得到起始块中要进行操作的数据长度
        //nblks为0说明为最后一块
        //如果不是最后一块计算：块大小-操作偏移量
        //如果是最后一块计算：结束长度-总偏移量
        size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos-offset);
        //获取这些数据块对应到磁盘上的数据块的inode号
        if((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0){
            goto out;
        }
        //对缓冲区进行读或写操作
        if((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0){
            goto out;
        }
        //已经完成读写的数据长度
        alen += size;
        //如果这是最后操作的块，结束
        if(nblks == 0){
            goto out;
        }
        //否则，更新缓冲区
        buf += size;
        blkno++;
        nblks--;
    }
    //（2）读取起始块后对齐块的数据
    //将中间部分的数据分为一块一块的大小，一块一块操作
    size = SFS_BLKSIZE;
    //从起始块后一块开始到对齐的最后一块
    while(nblks != 0){
        //获取磁盘上块的编号
        if((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0){
            goto out;
        }
        //对数据块进行读或写操作
        if((ret = sfs_block_op(sfs, buf, ino, 1)) != 0){
            goto out;
        }
        //更新
        alen += size;
        buf += size;
        blkno ++;
        nblks --;
    }
    //（3）如果有非对齐操作的最后一块
    if((size = endpos % SFS_BLKSIZE) != 0){
        //获取磁盘上的块号
        if((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0){
            goto out;
        }
        //缓冲区读写操作
        if((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0){
            goto out;
        }
        //更新
        alen += size;
    }
    

out:
    *alenp = alen;
    //如果操作导致文件大小增加，更新SFS inode的大小和标记为脏（dirty）
    if (offset + alen > sin->din->size)
    {
        sin->din->size = offset + alen;
        sin->dirty = 1;
    }
    return ret;
}

/*
 * sfs_io - Rd/Wr file. the wrapper of sfs_io_nolock
            with lock protect
sfs_read函数调用sfs_io函数。
node：对应文件的inode
iob：缓存
write：是读还是写的布尔值（0表示读，1表示写）
 */
static inline int
sfs_io(struct inode *node, struct iobuf *iob, bool write)
{
    //先找到inode对应sfs和sin
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret;
    lock_sin(sin);
    {
        size_t alen = iob->io_resid;
        //调用sfs_io_nolock函数进行读取文件操作
        ret = sfs_io_nolock(sfs, sin, iob->io_base, iob->io_offset, &alen, write);
        if (alen != 0)
        {
            //调用iobuf_skip函数调整iobuf的指针。
            iobuf_skip(iob, alen);
        }
    }
    unlock_sin(sin);
    return ret;
}

// sfs_read - read file
static int
sfs_read(struct inode *node, struct iobuf *iob)
{
    return sfs_io(node, iob, 0);
}

// sfs_write - write file
static int
sfs_write(struct inode *node, struct iobuf *iob)
{
    return sfs_io(node, iob, 1);
}

/*
 * sfs_fstat - Return nlinks/block/size, etc. info about a file. The pointer is a pointer to struct stat;
 */
static int
sfs_fstat(struct inode *node, struct stat *stat)
{
    int ret;
    memset(stat, 0, sizeof(struct stat));
    if ((ret = vop_gettype(node, &(stat->st_mode))) != 0)
    {
        return ret;
    }
    struct sfs_disk_inode *din = vop_info(node, sfs_inode)->din;
    stat->st_nlinks = din->nlinks;
    stat->st_blocks = din->blocks;
    stat->st_size = din->size;
    return 0;
}

/*
 * sfs_fsync - Force any dirty inode info associated with this file to stable storage.
 */
static int
sfs_fsync(struct inode *node)
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret = 0;
    if (sin->dirty)
    {
        lock_sin(sin);
        {
            if (sin->dirty)
            {
                sin->dirty = 0;
                if ((ret = sfs_wbuf(sfs, sin->din, sizeof(struct sfs_disk_inode), sin->ino, 0)) != 0)
                {
                    sin->dirty = 1;
                }
            }
        }
        unlock_sin(sin);
    }
    return ret;
}

/*
 *sfs_namefile -Compute pathname relative to filesystem root of the file and copy to the specified io buffer.
 *
 */
static int
sfs_namefile(struct inode *node, struct iobuf *iob)
{
    struct sfs_disk_entry *entry;
    if (iob->io_resid <= 2 || (entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL)
    {
        return -E_NO_MEM;
    }

    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int ret;
    char *ptr = iob->io_base + iob->io_resid;
    size_t alen, resid = iob->io_resid - 2;
    vop_ref_inc(node);
    while (1)
    {
        struct inode *parent;
        if ((ret = sfs_lookup_once(sfs, sin, "..", &parent, NULL)) != 0)
        {
            goto failed;
        }

        uint32_t ino = sin->ino;
        vop_ref_dec(node);
        if (node == parent)
        {
            vop_ref_dec(node);
            break;
        }

        node = parent, sin = vop_info(node, sfs_inode);
        assert(ino != sin->ino && sin->din->type == SFS_TYPE_DIR);

        lock_sin(sin);
        {
            ret = sfs_dirent_findino_nolock(sfs, sin, ino, entry);
        }
        unlock_sin(sin);

        if (ret != 0)
        {
            goto failed;
        }

        if ((alen = strlen(entry->name) + 1) > resid)
        {
            goto failed_nomem;
        }
        resid -= alen, ptr -= alen;
        memcpy(ptr, entry->name, alen - 1);
        ptr[alen - 1] = '/';
    }
    alen = iob->io_resid - resid - 2;
    ptr = memmove(iob->io_base + 1, ptr, alen);
    ptr[-1] = '/', ptr[alen] = '\0';
    iobuf_skip(iob, alen);
    kfree(entry);
    return 0;

failed_nomem:
    ret = -E_NO_MEM;
failed:
    vop_ref_dec(node);
    kfree(entry);
    return ret;
}

/*
 * sfs_getdirentry_sub_noblock - get the content of file entry in DIR
 */
static int
sfs_getdirentry_sub_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_disk_entry *entry)
{
    int ret, i, nslots = sin->din->blocks;
    for (i = 0; i < nslots; i++)
    {
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0)
        {
            return ret;
        }
        if (entry->ino != 0)
        {
            if (slot == 0)
            {
                return 0;
            }
            slot--;
        }
    }
    return -E_NOENT;
}

/*
 * sfs_getdirentry - according to the iob->io_offset, calculate the dir entry's slot in disk block,
                     get dir entry content from the disk
 */
static int
sfs_getdirentry(struct inode *node, struct iobuf *iob)
{
    struct sfs_disk_entry *entry;
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL)
    {
        return -E_NO_MEM;
    }

    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int ret, slot;
    off_t offset = iob->io_offset;
    if (offset < 0 || offset % sfs_dentry_size != 0)
    {
        kfree(entry);
        return -E_INVAL;
    }
    if ((slot = offset / sfs_dentry_size) > sin->din->blocks)
    {
        kfree(entry);
        return -E_NOENT;
    }
    lock_sin(sin);
    if ((ret = sfs_getdirentry_sub_nolock(sfs, sin, slot, entry)) != 0)
    {
        unlock_sin(sin);
        goto out;
    }
    unlock_sin(sin);
    ret = iobuf_move(iob, entry->name, sfs_dentry_size, 1, NULL);
out:
    kfree(entry);
    return ret;
}

/*
 * sfs_reclaim - Free all resources inode occupied . Called when inode is no longer in use.
 */
static int
sfs_reclaim(struct inode *node)
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int ret = -E_BUSY;
    uint32_t ent;
    lock_sfs_fs(sfs);
    assert(sin->reclaim_count > 0);
    if ((--sin->reclaim_count) != 0 || inode_ref_count(node) != 0)
    {
        goto failed_unlock;
    }
    if (sin->din->nlinks == 0)
    {
        if ((ret = vop_truncate(node, 0)) != 0)
        {
            goto failed_unlock;
        }
    }
    if (sin->dirty)
    {
        if ((ret = vop_fsync(node)) != 0)
        {
            goto failed_unlock;
        }
    }
    sfs_remove_links(sin);
    unlock_sfs_fs(sfs);

    if (sin->din->nlinks == 0)
    {
        sfs_block_free(sfs, sin->ino);
        if ((ent = sin->din->indirect) != 0)
        {
            sfs_block_free(sfs, ent);
        }
    }
    kfree(sin->din);
    vop_kill(node);
    return 0;

failed_unlock:
    unlock_sfs_fs(sfs);
    return ret;
}

/*
 * sfs_gettype - Return type of file. The values for file types are in sfs.h.
 */
static int
sfs_gettype(struct inode *node, uint32_t *type_store)
{
    struct sfs_disk_inode *din = vop_info(node, sfs_inode)->din;
    switch (din->type)
    {
    case SFS_TYPE_DIR:
        *type_store = S_IFDIR;
        return 0;
    case SFS_TYPE_FILE:
        *type_store = S_IFREG;
        return 0;
    case SFS_TYPE_LINK:
        *type_store = S_IFLNK;
        return 0;
    }
    panic("invalid file type %d.\n", din->type);
}

/*
 * sfs_tryseek - Check if seeking to the specified position within the file is legal.
 */
static int
sfs_tryseek(struct inode *node, off_t pos)
{
    if (pos < 0 || pos >= SFS_MAX_FILE_SIZE)
    {
        return -E_INVAL;
    }
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    if (pos > sin->din->size)
    {
        return vop_truncate(node, pos);
    }
    return 0;
}

/*
 * sfs_truncfile : reszie the file with new length
 */
static int
sfs_truncfile(struct inode *node, off_t len)
{
    if (len < 0 || len > SFS_MAX_FILE_SIZE)
    {
        return -E_INVAL;
    }
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    struct sfs_disk_inode *din = sin->din;

    int ret = 0;
    // new number of disk blocks of file
    uint32_t nblks, tblks = ROUNDUP_DIV(len, SFS_BLKSIZE);
    if (din->size == len)
    {
        assert(tblks == din->blocks);
        return 0;
    }

    lock_sin(sin);
    // old number of disk blocks of file
    nblks = din->blocks;
    if (nblks < tblks)
    {
        // try to enlarge the file size by add new disk block at the end of file
        while (nblks != tblks)
        {
            if ((ret = sfs_bmap_load_nolock(sfs, sin, nblks, NULL)) != 0)
            {
                goto out_unlock;
            }
            nblks++;
        }
    }
    else if (tblks < nblks)
    {
        // try to reduce the file size
        while (tblks != nblks)
        {
            if ((ret = sfs_bmap_truncate_nolock(sfs, sin)) != 0)
            {
                goto out_unlock;
            }
            nblks--;
        }
    }
    assert(din->blocks == tblks);
    din->size = len;
    sin->dirty = 1;

out_unlock:
    unlock_sin(sin);
    return ret;
}

/*
 * sfs_lookup - Parse path relative to the passed directory
 *              DIR, and hand back the inode for the file it
 *              refers to.
 * 用于解析相对于传入的目录 DIR 的路径，并返回该路径对应文件的 inode
 * 为了简化代码，没有实现对多级目录进行查找的控制逻辑
 */
/*
node：表示传入的目录对应的 inode 结构体。
path：表示相对于目录 DIR 的路径。
node_store：是一个指向指针的指针，用于存储找到的文件对应的 inode。
*/
static int
sfs_lookup(struct inode *node, char *path, struct inode **node_store)
{
    //获取文件系统结构体，并确保其为 SFS 文件系统。
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    //断言路径不为空且不以斜杠开头，确保路径是相对路径。
    //查找的过程相当于一个递归的过程，所以只要搜索相对路径就可以
    assert(*path != '\0' && *path != '/');
    //增加传入目录的引用计数，防止在函数执行期间目录被释放。
    vop_ref_inc(node);
    //获取传入目录对应的 SFS inode 结构体。
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    //检查传入目录是否为目录类型，如果不是，则减少引用计数并返回错误码 
    if (sin->din->type != SFS_TYPE_DIR)
    {
        vop_ref_dec(node);
        return -E_NOTDIR;
    }
    struct inode *subnode;
    //调用 sfs_lookup_once 函数来查找路径对应的子节点 inode
    int ret = sfs_lookup_once(sfs, sin, path, &subnode, NULL);

    vop_ref_dec(node);
    if (ret != 0)
    {
        return ret;
    }
    //将找到的子节点 inode 存储到 node_store 指向的位置。
    *node_store = subnode;
    //返回0表示成功完成查找
    return 0;
}

// The sfs specific DIR operations correspond to the abstract operations on a inode.
// Inode的目录操作函数
/**
 * 相对于 sfs_open，sfs_opendir 只是完成一些 open 函数传递的参数判断，没做其他更多的事情。
 * 目录的 close 操作与文件的 close 操作完全一致。
 * 由于目录的内容数据与文件的内容数据不同，所以读出目录的内容数据的函数是 sfs_getdirentry()，其主要工作是获取目录下的文件 inode 信息。
 */
static const struct inode_ops sfs_node_dirops = {
    .vop_magic = VOP_MAGIC,
    .vop_open = sfs_opendir,
    .vop_close = sfs_close,
    .vop_fstat = sfs_fstat,
    .vop_fsync = sfs_fsync,
    .vop_namefile = sfs_namefile,
    .vop_getdirentry = sfs_getdirentry,
    .vop_reclaim = sfs_reclaim,
    .vop_gettype = sfs_gettype,
    .vop_lookup = sfs_lookup,
};
/// The sfs specific FILE operations correspond to the abstract operations on a inode.
/** 分别对应用户进程发出的 open、close、read、write 操作
 * 其中 sfs_openfile 不用做什么事；
 * sfs_close 需要把对文件的修改内容写回到硬盘上，这样确保硬盘上的文件内容数据是最新的；
 * sfs_read 和 sfs_write 函数都调用了一个函数 sfs_io，并最终通过访问硬盘驱动来完成对文件内容数据的读写。
 */
static const struct inode_ops sfs_node_fileops = {
    .vop_magic = VOP_MAGIC,
    .vop_open = sfs_openfile,
    .vop_close = sfs_close,
    .vop_read = sfs_read,
    .vop_write = sfs_write,
    .vop_fstat = sfs_fstat,
    .vop_fsync = sfs_fsync,
    .vop_reclaim = sfs_reclaim,
    .vop_gettype = sfs_gettype,
    .vop_tryseek = sfs_tryseek,
    .vop_truncate = sfs_truncfile,
};

/**
 * inode结构体把关于inode的操作接口，集中在一个结构体里， 通过这个结构体，
 * 我们可以把Simple File System的接口（如sfs_openfile())提供给上层的VFS使用。
 * 可以想象我们除了Simple File System, 还在另一块磁盘上使用完全不同的文件系统Complex File System，
 * 显然vop_open(),vop_read()这些接口的实现都要不一样了。对于同一个文件系统这些接口都是一样的，
 * 所以我们可以提供”属于SFS的文件的inode_ops结构体”, “属于CFS的文件的inode_ops结构体”。
 */