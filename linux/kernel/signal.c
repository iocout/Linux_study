/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

//信号是一种软件中断的处理机制。

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

volatile void do_exit(int error_code);	//不优化

int sys_sgetmask()	//获取屏蔽位图
{
	return current->blocked;	//表示当前进程要阻塞的信号，每个信号对应一位
}

int sys_ssetmask(int newmask)	//设置新的信号屏蔽位图，SIGKILL不能被屏蔽。返回值是原信号屏蔽位图。
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));	//最后一位不能屏蔽
	return old;
}

//将信号结构体从 from 复制到 to
static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction));	//验证to处的内存是否足够
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);		//（include/asm/segment.h)
		from++;		
		to++;
	}
}
 
 //有什么区别
static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

//为指定的信号安装新的信号句柄，这个调用会发生信号的丢失
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	tmp.sa_handler = (void (*)(int)) handler; //信号处理句柄
	tmp.sa_mask = 0;	//执行时屏蔽码
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;	//该句柄只使用一次就恢复默认值，并允许信号在字节的处理句柄中收到。
	tmp.sa_restorer = (void (*)(void)) restorer;		//保存恢复处理函数指针
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	return handler;	//返回原信号句柄
}

//同上，不会发生信号丢失
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
		
	//在信号的sigaction结构中设置新的操作（动作）
	tmp = current->sigaction[signum-1];
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));

	//如果oldaction指针不为空的话，则将原操作指针保存到oldaction所指的位置
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}

//系统调用中断处理程序中真正的信号处理程序（在kernel/system_call.s,119)
//将信号的处理句柄插入到用户程序堆栈，并在本系统结束返回后立刻执行句柄程序，
//然后继续执行用户的程序
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;	
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;

	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler==1)
		return;
	if (!sa_handler) {
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1));
	}
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	*(&eip) = sa_handler;
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	*(&esp) -= longs;
	verify_area(esp,longs*4);
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
}
