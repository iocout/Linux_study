/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);	//

long last_pid=0;	//最新进程号

//进程空间区域写前验证函数
//这段看原本的注释
void verify_area(void * addr,int size)
{
	unsigned long start;	//页的左边界位置

	start = (unsigned long) addr;	//
	size += start & 0xfff;	//获取所在页的偏移值	//size=size+start%(4k),调整起始地址后的长度
	start &= 0xfffff000;	//调整成所在页的区域大小 //重置开始地址，将地址整成整4k字节开始，即从整字节开始验证
	start += get_base(current->ldt[2]);		//下面把start 加上进程数据段在线性地址空间中的起始基址，
											//变成系统整个线性空间中的地址位置。
	while (size>0) {
		size -= 4096;			// 一次验证一页内存
		write_verify(start);	//写页面验证。若页面不可写，则复制页面
		start += 4096;
	}
}

//copy_mem()设置新任务的代码和数据段基址、限长并复制页表。 nr 为新任务号；p 是新任务数据结构的指针。
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	new_data_base = new_code_base = nr * 0x4000000;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *) get_free_page();		//申请一页空闲内存
	if (!p)
		return -EAGAIN;
	task[nr] = p;			//放入指针数组
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */
	p->state = TASK_UNINTERRUPTIBLE;	//先不可中断
	p->pid = last_pid;	
	p->father = current->pid;
	p->counter = p->priority;		//设置时间片
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);		//设置新进程的全局描述符
	p->tss.trace_bitmap = 0x80000000;		//
	if (last_task_used_math == current)	//协处理器
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	if (copy_mem(nr,p)) {		//返回不为0 表示出错
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	for (i=0; i<NR_OPEN;i++)	//引用次数+1
		if (f=p->filp[i])
			f->f_count++;
	if (current->pwd)	
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
	return last_pid;
}

//为新进程取得不重复的进程号last_pid,并返回在任务数组中的任务号（数组index）
int find_empty_process(void)
{
	int i;

	repeat:
		if ((++last_pid)<0) last_pid=1;  //如果last_pid加1后超出正数表示范围，重新从1开始
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;	//在任务数组中搜索pid号是否已经被任何任务使用，如果是则重新获取pid号
	for(i=1 ; i<NR_TASKS ; i++)  //注意任务0不在这里边
		if (!task[i])
			return i;	//返回空的任务
	return -EAGAIN;	//如果64个全部占用完，返回出错
}
