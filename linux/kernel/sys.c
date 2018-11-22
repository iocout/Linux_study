/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

//如果返回值为-ENOSYS，表示该版本还没有实现该功能

#include <errno.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <sys/times.h>
#include <sys/utsname.h>

int sys_ftime()
{
	return -ENOSYS;
}

int sys_break()
{
	return -ENOSYS;
}

int sys_ptrace()
{
	return -ENOSYS;
}

int sys_stty()
{
	return -ENOSYS;
}

int sys_gtty()
{
	return -ENOSYS;
}

int sys_rename()
{
	return -ENOSYS;
}

int sys_prof()
{
	return -ENOSYS;
}

//设置当前任务的实际或者有效组ID
int sys_setregid(int rgid, int egid)
{
	if (rgid>0) {
		if ((current->gid == rgid) || 
		    suser())
			current->gid = rgid;
		else
			return(-EPERM);
	}
	if (egid>0) {
		if ((current->gid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    suser())
			current->egid = egid;
		else
			return(-EPERM);
	}
	return 0;
}

//设置组id
//当用户没有超级管理员权限的时候，如果想要运行有set_use_id的程序，
//可以通过这个方式来设置运行这个程序的组id，这样就可以跨组运行程序了。
//如果是超级管理员，则这个id不用改变
int sys_setgid(int gid)
{
	return(sys_setregid(gid, gid));
}

int sys_acct()
{
	return -ENOSYS;
}

int sys_phys()
{
	return -ENOSYS;
}

int sys_lock()
{
	return -ENOSYS;
}

int sys_mpx()
{
	return -ENOSYS;
}

int sys_ulimit()
{
	return -ENOSYS;
}

//返回从格林威治时间开始到现在的时间（秒）
int sys_time(long * tloc)
{
	int i;

	i = CURRENT_TIME;
	if (tloc) {
		verify_area(tloc,4);	
		put_fs_long(i,(unsigned long *)tloc);
	}
	return i;
}

/*
 * Unprivileged users may change the real user id(ruid)  to the effective uid (euid)
 * or vice versa.
 */
int sys_setreuid(int ruid, int euid)
{
	int old_ruid = current->uid;
	
	if (ruid>0) {
		if ((current->euid==ruid) ||
                    (old_ruid == ruid) ||
		    suser())
			current->uid = ruid;
		else
			return(-EPERM);
	}
	if (euid>0) {
		if ((old_ruid == euid) ||
                    (current->euid == euid) ||
		    suser())
			current->euid = euid;
		else {
			current->uid = old_ruid;
			return(-EPERM);
		}
	}
	return 0;
}

int sys_setuid(int uid)
{
	return(sys_setreuid(uid, uid));
}

//设置系统时间和日期，仅限超级用户
int sys_stime(long * tptr)
{
	if (!suser())
		return -EPERM;
	startup_time = get_fs_long((unsigned long *)tptr) - jiffies/HZ;
	return 0;
}

//获取当前项目时间
int sys_times(struct tms * tbuf)
{
	if (tbuf) {
		verify_area(tbuf,sizeof *tbuf);
		put_fs_long(current->utime,(unsigned long *)&tbuf->tms_utime);
		put_fs_long(current->stime,(unsigned long *)&tbuf->tms_stime);
		put_fs_long(current->cutime,(unsigned long *)&tbuf->tms_cutime);
		put_fs_long(current->cstime,(unsigned long *)&tbuf->tms_cstime);
	}
	return jiffies;
}

//当参数合理时，返回进程当前的数据段结尾值
int sys_brk(unsigned long end_data_seg)
{
	if (end_data_seg >= current->end_code &&	//如果参数>代码结尾
	    end_data_seg < current->start_stack - 16384)	//小于堆栈-16k
		current->brk = end_data_seg;	//将结尾值向后拓展
	return current->brk;
}

/*
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 */
//用于将同一个会话下的一个进程从一个进程组移到另一个进程组
int sys_setpgid(int pid, int pgid)
{
	int i;

	if (!pid)
		pid = current->pid;
	if (!pgid)
		pgid = current->pid;
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->pid==pid) {	//查找当前进程
			if (task[i]->leader)	//如果该进程已经是首领，返回出错
				return -EPERM;
			if (task[i]->session != current->session)	//
				return -EPERM;
			task[i]->pgrp = pgid;	//设置当前任务的gprp
			return 0;
		}
	return -ESRCH;
}

int sys_getpgrp(void)
{
	return current->pgrp;
}

//创建一个会话 setsid (set session id)
int sys_setsid(void)
{
	if (current->leader && !suser())
		return -EPERM;
	current->leader = 1;	//设置当前进程为新的会话首领
	current->session = current->pgrp = current->pid;	//设置session=pid
	current->tty = -1;	//表示当前进程没有控制终端
	return current->pgrp;
}

//获取系统信息，本版本操作系统的名称，网络节点名称，当前发行级别，版本级别和硬件类型名称
int sys_uname(struct utsname * name)
{
	static struct utsname thisname = {
		"linux .0","nodename","release ","version ","machine "
	};
	int i;

	if (!name) return -ERROR;
	verify_area(name,sizeof *name);		//验证缓冲区大小是否超限（超出以分配的内存等）
	for(i=0;i<sizeof *name;i++)
		put_fs_byte(((char *) &thisname)[i],i+(char *) name);  //复制到用户缓存
	return 0;
}

//设置当前进程穿件文件属性屏蔽码为mask & 0777，并返回原屏蔽码
int sys_umask(int mask)
{
	int old = current->umask;

	current->umask = mask & 0777;
	return (old);
}
