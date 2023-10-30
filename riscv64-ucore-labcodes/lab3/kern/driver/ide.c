#include <assert.h>
#include <defs.h>
#include <fs.h>
#include <ide.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <riscv.h>

// 模拟硬盘接口初始化
void ide_init(void) {}

#define MAX_IDE 2
#define MAX_DISK_NSECS 56
static char ide[MAX_DISK_NSECS * SECTSIZE];

// 验证IDE设备的有效性
bool ide_device_valid(unsigned short ideno) { return ideno < MAX_IDE; }

// 获取IDE设备的大小
size_t ide_device_size(unsigned short ideno) { return MAX_DISK_NSECS; }

// 从IDE设备中读取扇区数据
// 参数：IDE设备编号、起始扇区号、存储数据的目标缓冲区、要读取的扇区数量
int ide_read_secs(unsigned short ideno, uint32_t secno, void *dst,
                  size_t nsecs) {
    int iobase = secno * SECTSIZE;
    memcpy(dst, &ide[iobase], nsecs * SECTSIZE);
    return 0;
}

// 将数据写入IDE设备的扇区
// 参数：IDE设备编号、要写入的起始扇区号、包含数据的源缓冲区、要写入的扇区数量
int ide_write_secs(unsigned short ideno, uint32_t secno, const void *src,
                   size_t nsecs) {
    int iobase = secno * SECTSIZE;
    memcpy(&ide[iobase], src, nsecs * SECTSIZE);
    return 0;
}
