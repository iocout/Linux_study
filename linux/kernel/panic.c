/*
 *  linux/kernel/panic.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (includeinh mm and fs)
 * to indicate a major problem.
 */

//painc，惊恐，用于严重出错调用该函数。
#include <linux/kernel.h>
#include <linux/sched.h>

void sys_sync(void);	/* it's really int */

volatile void panic(const char * s)
{
	printk("Kernel panic: %s\n\r",s);
	if (current == task[0])
		printk("In swapper task - not syncing\n\r");
	else
		sys_sync();
	for(;;);	//直接死机
}
