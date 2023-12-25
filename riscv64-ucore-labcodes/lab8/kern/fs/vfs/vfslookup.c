#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <error.h>
#include <assert.h>

/*
 * get_device- Common code to pull the device name, if any, off the front of a
 *             path and choose the inode to begin the name lookup relative to.
 */
/*
 * 1. 遍历路径，寻找冒号 ':' 或斜杠 '/' 的位置，以确定是否存在设备名称。
 * 2. 如果不存在冒号 ':' 并且斜杠 '/' 不在开头，表示未指定设备名称，且路径是相对路径或仅是文件名。
 *    从当前目录开始搜索，并将整个路径作为子路径 'subpath'。
 * 3. 如果存在冒号 ':'，表示指定了设备名称。
 *    a. 'device:path' - 获取设备文件系统的根目录。
 *    b. 'device:/path' - 跳过斜杠 '/', 将路径视为 'device:path'。
 * 4. 如果路径是斜杠 '/' 或冒号 ':'，则表示相对于“引导文件系统”的根目录。
 *    如果是斜杠 '/', 获取引导文件系统的根目录 inode。
 *    如果是冒号 ':', 获取当前文件系统的根目录 inode。
 * 5. 存储剩余的子路径 'subpath'，并返回相应的错误码。
 */
static int
get_device(char *path, char **subpath, struct inode **node_store)
{
    int i, slash = -1, colon = -1;
    // 遍历路径寻找冒号 ':' 或斜杠 '/' 的位置
    for (i = 0; path[i] != '\0'; i++)
    {
        if (path[i] == ':')
        {
            colon = i;
            break;
        }
        if (path[i] == '/')
        {
            slash = i;
            break;
        }
    }
    if (colon < 0 && slash != 0)
    {
        // 未指定设备名称，且路径不是以斜杠开头，是相对路径或文件名
        /* *
         * No colon before a slash, so no device name specified, and the slash isn't leading
         * or is also absent, so this is a relative path or just a bare filename. Start from
         * the current directory, and use the whole thing as the subpath.
         * */
        *subpath = path;
        // 从当前目录开始搜索
        return vfs_get_curdir(node_store);
    }
    if (colon > 0)
    {
        // 存在设备名称，获取设备文件系统的根目录
        /* device:path - get root of device's filesystem */
        path[colon] = '\0';

        // 如果路径是 'device:/path'，跳过斜杠 '/'
        /* device:/path - skip slash, treat as device:path */
        while (path[++colon] == '/')
            ;
        *subpath = path + colon;
        return vfs_get_root(path, node_store);
    }

    // 处理绝对路径 '/path' 或相对于当前文件系统的路径 ':path'
    /* *
     * we have either /path or :path
     * /path is a path relative to the root of the "boot filesystem"
     * :path is a path relative to the root of the current filesystem
     * */
    int ret;
    if (*path == '/')
    {
        // 路径以斜杠 '/' 开头，获取引导文件系统的根目录
        if ((ret = vfs_get_bootfs(node_store)) != 0)
        {
            return ret;
        }
    }
    else
    {
        assert(*path == ':');
        struct inode *node;
        // 获取当前目录的 inode
        if ((ret = vfs_get_curdir(&node)) != 0)
        {
            return ret;
        }
        // 当前目录可能不是设备，必须具有文件系统
        /* The current directory may not be a device, so it must have a fs. */
        assert(node->in_fs != NULL);
        *node_store = fsop_get_root(node->in_fs);
        vop_ref_dec(node);
    }

    // 跳过多余的斜杠 '/'
    /* ///... or :/... */
    while (*(++path) == '/')
        ;
    *subpath = path;
    return 0;
}

/*
 * vfs_lookup - get the inode according to the path filename
 */
// 针对目录的操作函数
/*
调用vop_lookup来找到SFS文件系统下的目录中的文件
首先调用get_device->vfs_get_bootfs->找到根目录“/”对应的inode
这个inode是位于vfs.c中的inode变量bootfs_node
这个变量在init_main函数（kern/process/proc.c）执行时获得了赋值
调用vop_lookup函数来查找到根目录“/”下对应文件sfs_filetest1的索引节点，如果找到就返回此索引节点。
*/
int vfs_lookup(char *path, struct inode **node_store)
{
    int ret;
    struct inode *node;
    //首先调用get_device->vfs_get_bootfs->找到根目录“/”对应的inode
    //这个inode是位于vfs.c中的inode变量bootfs_node
    //这个变量在init_main函数（kern/process/proc.c）执行时获得了赋值
    if ((ret = get_device(path, &path, &node)) != 0)
    {
        return ret;
    }
    if (*path != '\0')
    {
        //调用vop_lookup函数来查找到根目录“/”下对应文件sfs_filetest1的索引节点，如果找到就返回此索引节点。
        ret = vop_lookup(node, path, node_store);
        vop_ref_dec(node);
        return ret;
    }
    *node_store = node;
    return 0;
}

/*
 * vfs_lookup_parent - Name-to-vnode translation.
 *  (In BSD, both of these are subsumed by namei().)
 */
/*
 * 该函数用于查找路径中父目录中的文件节点。
 * 根据提供的路径 'path' 执行以下步骤：
 *
 * 1. 调用 'get_device' 函数解析路径并获取设备信息。
 * 2. 存储路径中的结束部分 'endp' 和文件系统节点 'node_store'。
 * 3. 返回相应的错误码表示成功或失败。
 */
int vfs_lookup_parent(char *path, struct inode **node_store, char **endp)
{
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0)
    {
        return ret;
    }
    *endp = path;
    *node_store = node;
    return 0;
}
