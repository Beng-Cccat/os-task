#include <console.h>
#include <defs.h>
#include <sbi.h>
#include <sync.h>

#define CONSBUFSIZE 512

static struct
{
    uint8_t buf[CONSBUFSIZE];
    uint32_t rpos;
    uint32_t wpos; //控制台的输入缓冲区是一个队列
} cons;

/* *
 * cons_intr - called by device interrupt routines to feed input
 * characters into the circular console input buffer.
 * 被设备中断例程调用以将输入字符传递到循环控制台输入缓冲区
 * */
void cons_intr(int (*proc)(void))
{
    int c;
    while ((c = (*proc)()) != -1)
    {
        // 如果获取的字符不是空字符
        if (c != 0)
        {
            // 将字符放入控制台缓冲区的写位置（cons.wpos），并将写位置加1
            cons.buf[cons.wpos++] = c;
            // 若写位置达到缓冲区大小CONSBUFSIZE，则重置写位置为缓冲区开头
            if (cons.wpos == CONSBUFSIZE)
            {
                cons.wpos = 0;
            }
        }
    }
}

/* kbd_intr - try to feed input characters from keyboard */
void kbd_intr(void)
{
    serial_intr();
}

/* serial_proc_data - get data from serial port */
// 从串口获取数据
int serial_proc_data(void)
{
    int c = sbi_console_getchar();
    if (c < 0)
    {
        return -1;
    }
    if (c == 127)
    {
        c = '\b';
    }
    return c;
}

/* serial_intr - try to feed input characters from serial port */
// 作为一个中间函数，尝试从串口中获取输入字符
void serial_intr(void)
{
    cons_intr(serial_proc_data);
}

/* serial_putc - print character to serial port */
void serial_putc(int c)
{
    if (c != '\b')
    {
        sbi_console_putchar(c);
    }
    else
    {
        sbi_console_putchar('\b');
        sbi_console_putchar(' ');
        sbi_console_putchar('\b');
    }
}

/* cons_init - initializes the console devices */
void cons_init(void)
{
    sbi_console_getchar();
}

/* cons_putc - print a single character @c to console devices */
void cons_putc(int c)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        serial_putc(c);
    }
    local_intr_restore(intr_flag);
}

/* *
 * cons_getc - return the next input character from console,
 * or 0 if none waiting.
 * */
// 从控制台获取下一个输入字符。如果没有等待的字符，则返回 0
int cons_getc(void)
{
    int c = 0;
    bool intr_flag;
    local_intr_save(intr_flag);
    // 保存当前中断状态
    {
        // poll for any pending input characters,
        // so that this function works even when interrupts are disabled
        // (e.g., when called from the kernel monitor).
        // 调用串口中断处理函数，尝试获取串口输入字符
        serial_intr();

        // 从输入缓冲区中获取下一个字符。
        // grab the next character from the input buffer.
        if (cons.rpos != cons.wpos)
        {
            // 如果读位置不等于写位置，表示有字符可读
            c = cons.buf[cons.rpos++]; // 读取字符并移动读位置
            if (cons.rpos == CONSBUFSIZE)
            {
                // 如果读位置达到缓冲区末尾，重置为缓冲区开头
                cons.rpos = 0;
            }
        }
    }
    local_intr_restore(intr_flag); // 恢复之前保存的中断状态
    return c;
}
