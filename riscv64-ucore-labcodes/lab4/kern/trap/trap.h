#ifndef __KERN_TRAP_TRAP_H__
#define __KERN_TRAP_TRAP_H__

#include <defs.h>

//用于保存中断发生时的寄存器状态
struct pushregs {
    uintptr_t zero;  // Hard-wired zero
    uintptr_t ra;    // Return address
    uintptr_t sp;    // Stack pointer
    uintptr_t gp;    // Global pointer
    uintptr_t tp;    // Thread pointer
    uintptr_t t0;    // Temporary
    uintptr_t t1;    // Temporary
    uintptr_t t2;    // Temporary
    uintptr_t s0;    // Saved register/frame pointer
    uintptr_t s1;    // Saved register
    uintptr_t a0;    // Function argument/return value
    uintptr_t a1;    // Function argument/return value
    uintptr_t a2;    // Function argument
    uintptr_t a3;    // Function argument
    uintptr_t a4;    // Function argument
    uintptr_t a5;    // Function argument
    uintptr_t a6;    // Function argument
    uintptr_t a7;    // Function argument
    uintptr_t s2;    // Saved register
    uintptr_t s3;    // Saved register
    uintptr_t s4;    // Saved register
    uintptr_t s5;    // Saved register
    uintptr_t s6;    // Saved register
    uintptr_t s7;    // Saved register
    uintptr_t s8;    // Saved register
    uintptr_t s9;    // Saved register
    uintptr_t s10;   // Saved register
    uintptr_t s11;   // Saved register
    uintptr_t t3;    // Temporary
    uintptr_t t4;    // Temporary
    uintptr_t t5;    // Temporary
    uintptr_t t6;    // Temporary
};

// 保存中断处理时的整个中断帧
struct trapframe {
    struct pushregs gpr; //寄存器信息
    uintptr_t status; //中断状态寄存器
    uintptr_t epc; //程序计数器
    uintptr_t badvaddr; //发生异常的虚拟地址
    uintptr_t cause; //中断原因
};

void trap(struct trapframe *tf); //中断处理的入口函数
void idt_init(void); //初始化中断描述符表
void print_trapframe(struct trapframe *tf); //打印中断帧信息
void print_regs(struct pushregs* gpr); //打印寄存器内容
bool trap_in_kernel(struct trapframe *tf); //判断中断是否发生在内核态

#endif /* !__KERN_TRAP_TRAP_H__ */
