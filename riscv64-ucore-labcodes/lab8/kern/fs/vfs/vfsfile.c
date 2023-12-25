#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <unistd.h>
#include <error.h>
#include <assert.h>

// open file in vfs, get/create inode for file with filename path.
// 找到path指出的文件对应的基于inode数据结构的VFS索引节点node
// 完成：
// 1.通过vfs_lookup找到path对应的文件的inode
// 2.调用vop_open打开文件
int vfs_open(char *path, uint32_t open_flags, struct inode **node_store)
{
    bool can_write = 0;
    // 解析open_flags并做合法性检查
    switch (open_flags & O_ACCMODE)
    {
    case O_RDONLY:
        break;
    case O_WRONLY:
    case O_RDWR:
        can_write = 1;
        break;
    default:
        return -E_INVAL;
    }

    // 如果打开标志中包含 O_TRUNC，但不允许写入，则返回无效参数错误
    if (open_flags & O_TRUNC)
    {
        if (!can_write)
        {
            return -E_INVAL;
        }
    }

    int ret;
    struct inode *node;
    bool excl = (open_flags & O_EXCL) != 0;
    bool create = (open_flags & O_CREAT) != 0;
    // vfs_lookup根据路径构造inode
    ret = vfs_lookup(path, &node);

    if (ret != 0)
    { // 要打开的文件还不存在，可能出错，也可能需要创建新文件
        if (ret == -16 && (create))
        {
            char *name;
            struct inode *dir;
            if ((ret = vfs_lookup_parent(path, &dir, &name)) != 0)
            {
                // 需要在已经存在的目录下创建文件，目录不存在，则出错
                return ret;
            }
            // 创建新文件
            ret = vop_create(dir, name, excl, &node);
        }
        else
            return ret;
    }
    else if (excl && create)
    {
        return -E_EXISTS;
    }
    assert(node != NULL);

    // 执行节点的打开操作并增加引用计数
    if ((ret = vop_open(node, open_flags)) != 0)
    {
        vop_ref_dec(node);
        return ret;
    }

    vop_open_inc(node);
    if (open_flags & O_TRUNC || create)
    {
        if ((ret = vop_truncate(node, 0)) != 0)
        {
            vop_open_dec(node);
            vop_ref_dec(node);
            return ret;
        }
    }
    // 存储文件节点指针
    *node_store = node;
    return 0;
}

// close file in vfs
int vfs_close(struct inode *node)
{
    vop_open_dec(node);
    vop_ref_dec(node);
    return 0;
}

// unimplement
int vfs_unlink(char *path)
{
    return -E_UNIMP;
}

// unimplement
int vfs_rename(char *old_path, char *new_path)
{
    return -E_UNIMP;
}

// unimplement
int vfs_link(char *old_path, char *new_path)
{
    return -E_UNIMP;
}

// unimplement
int vfs_symlink(char *old_path, char *new_path)
{
    return -E_UNIMP;
}

// unimplement
int vfs_readlink(char *path, struct iobuf *iob)
{
    return -E_UNIMP;
}

// unimplement
int vfs_mkdir(char *path)
{
    return -E_UNIMP;
}
