/*
 *  linux/kernel/traps.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
/*
*在程序asm.s中保存了一些状态后，本程序用来处理硬件陷阱和故障。
*目前主要用于调试目的，以后将扩展用来杀死遭损坏的进程(主要是)
*通过发送一个信号，但如果必要也会直接杀死。
*/
#include <string.h>  

#include <linux/head.h>  //段描述符的简单结构，和几个选择符常量
#include <linux/sched.h>  //调度程序头文件，
#include <linux/kernel.h>  //内核头文件，含有一些内核常用函数的原形定义
#include <asm/system.h>  //系统头文件，
#include <asm/segment.h>  //段操作头文件，定义有关段寄存器操作的嵌入式汇编函数
#include <asm/io.h>  //定义硬件端口输入/输出宏汇编语句


//以下语句定义了三个嵌入式汇编宏语句函数。
//取段seg中地址addr处的一个字节
//用圆括号括住的组合语句（花括号中的语句）可以作为表达式使用，其中最后的_res是其输出值。

#define get_seg_byte(seg,addr) ({ \  
register char __res; \    //register，寄存器变量
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \  
	:"=a" (__res):"0" (seg),"m" (*(addr))); \  //
__res;})   //输出值


//去段seg中地址addr处的一个长字（4字节）
#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

//取fs段寄存器的值（选择符）
#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})


//以下定义一些函数原型。
int do_exit(long code);    //程序退出处理。（Kernel/exit.c,102)

void page_exception(void);  //页异常，实际是page_fault(mm/page.s,14)


//中断处理原型
void divide_error(void);
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void reserved(void);
void parallel_interrupt(void);
void irq13(void);

//该子程序用来打印出错中断的名称，出错号，调用程序的EIP，EFLAGS,ESP,fs段寄存器值，
//段的基址、段的长度，进程号pid，任务号，10字节指令码。如果堆栈在用户数据段，
//则还打印16字节的堆栈长度
/*注意这里放入堆栈的元素都是四字节对齐的*/
static void die(char * str,long esp_ptr,long nr)  
{
	long * esp = (long *) esp_ptr;
	int i;

	printk("%s: %04x\n\r",str,nr&0xffff);  //str第一个字符串
	printk("EIP:\t%04x:%p\nEFLAGS:\t%p\nESP:\t%04x:%p\n",  //将当前的ip，flags，sp，fs段寄存器值打印出来
		esp[1],esp[0],esp[2],esp[4],esp[3]);
	printk("fs: %04x\n",_fs());  //打印段地址。
	printk("base: %p, limit: %p\n",get_base(current->ldt[1]),get_limit(0x17));  //段的基址，段的长度
	if (esp[4] == 0x17) {
		printk("Stack: ");
		for (i=0;i<4;i++)
			printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
		printk("\n");
	}
	str(i);
	printk("Pid: %d, process nr: %d\n\r",current->pid,0xffff & i);
	for(i=0;i<10;i++)
		printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
	printk("\n\r");
	do_exit(11);		/* play segment exception */
}


//处理对应中断的c函数
void do_double_fault(long esp, long error_code)
{
	die("double fault",esp,error_code);
}

void do_general_protection(long esp, long error_code)
{
	die("general protection",esp,error_code);
}

void do_divide_error(long esp, long error_code)
{
	die("divide error",esp,error_code);
}

void do_int3(long * esp, long error_code,
		long fs,long es,long ds,
		long ebp,long esi,long edi,
		long edx,long ecx,long ebx,long eax)
{
	int tr;

	__asm__("str %%ax":"=a" (tr):"0" (0));  //tr存任务寄存器的值
	printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
		eax,ebx,ecx,edx);
	printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
		esi,edi,ebp,(long) esp);
	printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
		ds,es,fs,tr);
	printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

void do_nmi(long esp, long error_code)
{
	die("nmi",esp,error_code);
}

void do_debug(long esp, long error_code)
{
	die("debug",esp,error_code);
}

void do_overflow(long esp, long error_code)
{
	die("overflow",esp,error_code);
}

void do_bounds(long esp, long error_code)
{
	die("bounds",esp,error_code);
}

void do_invalid_op(long esp, long error_code)
{
	die("invalid operand",esp,error_code);
}

void do_device_not_available(long esp, long error_code)
{
	die("device not available",esp,error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code)
{
	die("coprocessor segment overrun",esp,error_code);
}

void do_invalid_TSS(long esp,long error_code)
{
	die("invalid TSS",esp,error_code);
}

void do_segment_not_present(long esp,long error_code)
{
	die("segment not present",esp,error_code);
}

void do_stack_segment(long esp,long error_code)
{
	die("stack segment",esp,error_code);
}

void do_coprocessor_error(long esp, long error_code)
{
	if (last_task_used_math != current)
		return;
	die("coprocessor error",esp,error_code);
}

void do_reserved(long esp, long error_code)
{
	die("reserved (15,17-47) error",esp,error_code);
}

//异常中断程序初始化子程序，设置他们的中断调用门（中断向量）
//set_trap_gate和set_system_gate的主要区别在于前置设置特权级为0，后者是
//3，因此断点陷阱中断int3，溢出中断overflow和边界错误中断bounds可以由任
//何程序产生。
void trap_init(void)
{
	int i;

	set_trap_gate(0,&divide_error);  //设置除操作出错的中断向量值，下同
	set_trap_gate(1,&debug);
	set_trap_gate(2,&nmi);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_trap_gate(8,&double_fault);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_trap_gate(14,&page_fault);
	set_trap_gate(15,&reserved);
	set_trap_gate(16,&coprocessor_error);
	for (i=17;i<48;i++)
		set_trap_gate(i,&reserved);
	set_trap_gate(45,&irq13);
	outb_p(inb_p(0x21)&0xfb,0x21);
	outb(inb_p(0xA1)&0xdf,0xA1);
	set_trap_gate(39,&parallel_interrupt);
}
