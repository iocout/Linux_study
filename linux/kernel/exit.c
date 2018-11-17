/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);


//释放指定进程占用的任务槽和内存
void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i]==p) {
			task[i]=NULL;
			free_page((long)p);
			schedule();
			return;
		}
	panic("trying to release non-existent task");	//指定任务不存在就死机
}

//sig - 信号值
//p  -  指定任务的指针
//priv  -  强制发送信号的标志，即不需要考虑用户属性或级别而能发送信号的权利
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
	if (!p || sig<1 || sig>32)
		return -EINVAL;
	//suser()定义为（current->euid==p->euid) ,用于判断是否是超级用户
	if (priv || (current->euid==p->euid) || suser())	
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

//终止会话
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task;	//指向任务数组最末端
	
	while (--p > &FIRST_TASK) {
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1);	//发送挂断进程信号
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;

	if (!pid) //进程号为0，向所有进程发送信号。
	while (--p > &FIRST_TASK)
	{
		if (*p && (*p)->pgrp == current->pid) 
			if (err=send_sig(sig,*p,1))		// 1 强制发送
				retval = err;
	} else if (pid>0) while (--p > &FIRST_TASK)	//pid>0
	{
		if (*p && (*p)->pid == pid) 	//向当前进程发送
			if (err=send_sig(sig,*p,0))
				retval = err;
	} else if (pid == -1)	
	 while (--p > &FIRST_TASK)	//发送给处第一个进程的所有进程
		if (err = send_sig(sig,*p,0))
			retval = err;
	else 	while (--p > &FIRST_TASK)	//pid <-1
				if (*p && (*p)->pgrp == -pid) 	//给所有-pid发送信号
					if (err = send_sig(sig,*p,0))
						retval = err;
	return retval;
}

//通知父进程--向进程pid发送SIGCHLD，默认情况下子进程将停止或终止。
//如果没有找到父进程，则自己释放，但根据POSIX.1要求，若父进程已先
//行终止，则子进程应该被初始进程 1 收容
static void tell_father(int pid)
{
	int i;

	if (pid)
		for (i=0;i<NR_TASKS;i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1));	//向父进程发送
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
//没找到父进程，自己释放。注意此处应被1进程收容
	printk("BAD BAD - no father found\n\r");
	release(current);
}

//出错退出程序，在sys_exit()中被调用。
int do_exit(long code)	//code，出错码
{
	int i;

	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {	//当前进程的子进程
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)	//子进程的状态是僵死状态（ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);	//强制发送，此处进程1为init
		}
	for (i=0 ; i<NR_OPEN ; i++)	//关闭当前进程打开的所有文件。
		if (current->filp[i])
			sys_close(i);	 //同样是调用中断，切换到关闭函数。
	iput(current->pwd);		//同步pwd root exe并置空
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	if (current->leader && current->tty >= 0)	//是leader且有终端
		tty_table[current->tty].pgrp = 0;	
	if (last_task_used_math == current)		//数学协处理器
		last_task_used_math = NULL;
	if (current->leader)	//是leader
		kill_session();		//终止会话
	current->state = TASK_ZOMBIE;	//将当前进程置为僵死状态
	current->exit_code = code;	
	tell_father(current->father);
	schedule();
	return (-1);	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}

//挂起当前进程，知道指定的进程退出
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);
repeat:
	flag=0;
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p || *p == current)
			continue;
		if ((*p)->father != current->pid)	//指定进程一定是当前进程的子进程
			continue;
		if (pid>0) {
			if ((*p)->pid != pid)	//别的子进程
				continue;
		} else if (!pid) {		//pid=0
			if ((*p)->pgrp != current->pgrp)	//当前进程组号
				continue;
		} else if (pid != -1) {		//筛选出pid ==-1
			if ((*p)->pgrp != -pid)
				continue;
		}
		switch ((*p)->state) {	//此时，p->pid ==pid，如果pid==-1，表示在等待其任何子进程
			case TASK_STOPPED:
				if (!(options & WUNTRACED))
					continue;
				put_fs_long(0x7f,stat_addr);	//
				return (*p)->pid;
			case TASK_ZOMBIE:		//子进程处于僵死状态
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid;
				code = (*p)->exit_code;
				release(*p);
				put_fs_long(code,stat_addr);	//置状态信息为退出码
				return flag;	//返回子进程id
			default:
				flag=1;		//找到过一个符合要求的子进程，但是它处于运行态或睡眠态
				continue;
		}
	}
	if (flag) {
		if (options & WNOHANG)	//没有上述条件的子进程
			return 0;
		current->state=TASK_INTERRUPTIBLE;	
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))	//如果没有收到除SIGCHLD以外的信号，重复处理。
			goto repeat;	
		else
			return -EINTR;	//否则，返回出错码。
	}
	return -ECHILD;	//没有找到符合要求的子进程
}


