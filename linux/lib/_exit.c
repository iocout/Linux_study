/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
::"a"(_NR_exit)*程序调用内核的退出系统调用函数
*/

#define __LIBRARY__
#include <unistd.h>

/*
*内核使用的程序终止函数.
*直接调用系统中断int 0x80，功能号_NR_exit.
*参数exit_code--退出码
*/

volatile void _exit(int exit_code)  
{
	/*
	* %0-eax(系统调用号_NR_exit),%1-ebx(退出码exit_code)
	*/
	__asm__("int $0x80"::"a" (__NR_exit),"b" (exit_code));
}
