#include <defs.h>
#include <stdio.h>
#include <dev.h>
#include <vfs.h>
#include <iobuf.h>
#include <inode.h>
#include <unistd.h>
#include <error.h>
#include <assert.h>

static int
stdout_open(struct device *dev, uint32_t open_flags) {
    if (open_flags != O_WRONLY) {
        return -E_INVAL;
    }
    return 0;
}

static int
stdout_close(struct device *dev) {
    return 0;
}

static int
stdout_io(struct device *dev, struct iobuf *iob, bool write) {
    //对应struct device的d_io()
    if (write) {
        char *data = iob->io_base;
        for (; iob->io_resid != 0; iob->io_resid --) {
            //调用cputchar()把字符打印到控制台
            cputchar(*data ++);
        }
        return 0;
    }
    //如果不是写操作会报错
    return -E_INVAL;
}

static int
stdout_ioctl(struct device *dev, int op, void *data) {
    return -E_INVAL;
}

static void
stdout_device_init(struct device *dev) {
    dev->d_blocks = 0;
    dev->d_blocksize = 1;
    dev->d_open = stdout_open;
    dev->d_close = stdout_close;
    dev->d_io = stdout_io;
    dev->d_ioctl = stdout_ioctl;
}

//完成对具体设备的初始化
void
dev_init_stdout(void) {
    struct inode *node;
    //抽象成设备文件并建立对应的inode数据结构
    if ((node = dev_create_inode()) == NULL) {
        panic("stdout: dev_create_node.\n");
    }
    stdout_device_init(vop_info(node, device));

    int ret;
    //将它们链入到vdev_list中
    if ((ret = vfs_add_dev("stdout", node, 0)) != 0) {
        panic("stdout: vfs_add_dev: %e.\n", ret);
    }
}

