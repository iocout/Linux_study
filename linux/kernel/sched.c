/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

#define _S(nr) (1<<((nr)-1))  //取信号nr在信号位图中对应位的二进制数值。信号编号1-32
							  //比如信号5的位图数值=1 << (5-1)=16=00010000b;
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))	//除了SIGKILL 和 SIGSTOP 都是可阻塞的

//显示任务号nr的进程号，进程状态和内核堆栈空闲字节数（大约）
void show_task(int nr,struct task_struct * p)
{
	int i,j = 4096-sizeof(struct task_struct);  

	printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);  
	i=0;
	while (i<j && !((char *)(p+1))[i])  //检测指定任务数据结构以后等于0的字节数。
		i++;
	printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}

//显示所有任务的任务号，进程号，进程状态和内核堆栈空闲字节数（大约）
void show_stat(void)
{
	int i;

	for (i=0;i<NR_TASKS;i++)  //最大进程数量64
		if (task[i])			//定义在include、kernel/sched.h 4
			show_task(i,task[i]);
}

#define LATCH (1193180/HZ)  //定义每个时间片的滴答数

extern void mem_use(void);  //未使用

extern int timer_interrupt(void);  //时钟中断
extern int system_call(void);	//系统调用中断处理

union task_union {  //定义任务联合
	struct task_struct task;
	char stack[PAGE_SIZE];
};

static union task_union init_task = {INIT_TASK,};  //定义初始任务的数据

long volatile jiffies=0;  //从开机开始算起的滴答数时间值（10ms/滴答）。
long startup_time=0;	//开机时间。从格林威治时间到现在的秒数。
struct task_struct *current = &(init_task.task);	//当前任务指针，初始化为初始任务。
struct task_struct *last_task_used_math = NULL;		//使用过协处理器任务的指针

struct task_struct * task[NR_TASKS] = {&(init_task.task), };  //定义任务指针数组

long user_stack [ PAGE_SIZE>>2 ] ;		//定义用户堆栈，4k，指向指在最后一项。

//该结构用于设置堆栈ss：esp（数据段选择符，指针）。
struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */

//任务调度时，保存原来的协处理器状态，并恢复新任务的协处理器状态
void math_state_restore()
{
	if (last_task_used_math == current)  //如果任务没变则返回。
		return;
	__asm__("fwait");		
	if (last_task_used_math) {		//如果上个任务使用了协处理器，保存其状态。
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}
	last_task_used_math=current;	//指向当前任务
	if (current->used_math) {		//当前是否用过协处理器，则恢复其状态。
		__asm__("frstor %0"::"m" (current->tss.i387));
	} else {					//否则是第一次使用
		__asm__("fninit"::);	//发送初始化命令。
		current->used_math=1;	//并设置使用了协处理器标志
	}
}


//参考：https://www.cnblogs.com/joey-hua/p/5596830.html
/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */

/*
 * 'schedule()'是调度函数。这是个很好的代码！没有任何理由对它进行修改，因为它可以在所有的
 * 环境下工作（比如能够对IO-边界处理很好的响应等）。只有一件事值得留意，那就是这里的信号
 * 处理代码。
 * 注意！！任务0 是个闲置('idle')任务，只有当没有其它任务可以运行时才调用它。它不能被杀
 * 死，也不能睡眠。任务0 中的状态信息'state'是从来不用的。
 */

//current 指向当前的任务。
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;  //任务结构指针的指针。

/* check alarm, wake up any interruptible tasks that have got a signal */
/* 检测alarm（进程的报警定时值），唤醒任何已得到信号的可中断任务 */

//从任务数组中最后一个任务开始检测alarm
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
		// 如果设置过任务的定时值alarm，并且已经过期(alarm<jiffies),则在信号位图中置SIGALRM 信号，
        // 即向任务发送SIGALARM 信号。然后清alarm。该信号的默认操作是终止进程。
        // jiffies 是系统从开机开始算起的滴答数（10ms/滴答）。定义在sched.h 第139 行。
			if ((*p)->alarm && (*p)->alarm < jiffies) {
					(*p)->signal |= (1<<(SIGALRM-1));
					(*p)->alarm = 0;
				}
	// 如果信号位图中除被阻塞的信号外还有其它信号，并且任务处于可中断状态，则置任务为就绪状态。
    // 其中'~(_BLOCKABLE & (*p)->blocked)'用于忽略被阻塞的信号，但SIGKILL 和SIGSTOP 不能被阻塞。
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
			(*p)->state==TASK_INTERRUPTIBLE)
				(*p)->state=TASK_RUNNING;  //置为就绪态（可执行态）
		}

/* this is the scheduler proper: */
 /* 这里是调度程序的主要部分 */

	while (1) {
		c = -1;
		next = 0;
		i = NR_TASKS;
		p = &task[NR_TASKS];
	  // 这段代码也是从任务数组的最后一个任务开始循环处理，并跳过不含任务的数组槽。比较每个就绪
      // 状态任务的counter（任务运行时间的递减滴答计数）值，哪一个值大，运行时间还不长，next 就
      // 指向哪个的任务号。

	  //找出任务数组中时间片最长的任务进行运行
		while (--i) {
			if (!*--p)  //是否含有任务
				continue;
			//在运行的任务中哪个的时间片最大，则其运行时间最短。
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				c = (*p)->counter, next = i;
		}
		//跳出后的c指向时间片最长的，next指向这个任务在数组槽中的位置
		//如果c大于0，跳出执行该任务。
		if (c) break;

		//如果所有的时间片都是0，那么久给这些任务重新分配时间片的值，
		//然后进行下一轮循环。计算方法：counter/2+priority（优先级)
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}
   // 切换到任务号为next 的任务运行。在142 行next 被初始化为0。因此若系统中没有任何其它任务
   // 可运行时，则next 始终为0。因此调度函数会在系统空闲时去执行任务0。此时任务0 仅执行
   // pause()系统调用，并又会调用本函数
	switch_to(next);    //切换到任务号为next的任务进行执行。
}

int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;//设置当前任务状态：可中断
	schedule();
	return 0;
}

//当请求的资源正忙，暂时切换出去。将当前任务设为不可响应中断，并隐式的创建一个
//
void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))  //若当前任务是任务0 则死机
		panic("task[0] trying to sleep");
	tmp = *p;						//让tmp指向已经在等待队列上的任务
	*p = current;					//将睡眠列头的等待指针指向当前。
	current->state = TASK_UNINTERRUPTIBLE;   //将当前设为不可响应中断
	schedule();				
	//只有当这个等待任务被换形时，调度程序才又返回这里，
	if (tmp)
		tmp->state=0;
}

//将当前任务放入可中断的等待状态，并放入*p指定的队列中
void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp=*p;
	*p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;  //将当前设为可中断的等待状态
	schedule();	//执行调度
	//可运行的任务不是当前任务，说明等待队列中还有别的任务要先执行，该任务是新插入队列的新任务。
	//将所有的任务置可中断的等待状态，然后重新调度。
	if (*p && *p != current) {	
		(**p).state=0;	//将该等待任务置为可运行的就绪态。然后重新调度。
		goto repeat;
	}
	*p=tmp; //此处应为tmp，如果为null会抹去所有新翻入的任务
	if (tmp)
		tmp->state=0;
}

//唤醒任务。
void wake_up(struct task_struct **p)
{
	if (p && *p) {
		(**p).state=0;	//state为0即为可运行态
		//*p=NULL;
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
//软驱A-D，不用管这个
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
static int  mon_timer[4]={0,0,0,0};
static int moff_timer[4]={0,0,0,0};
unsigned char current_DOR = 0x0C;

int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

	if (nr>3)
		panic("floppy_on: nr>3");
	moff_timer[nr]=10000;		/* 100 s = very big :-) */
	cli();				/* use floppy_off to turn it off */
	mask |= current_DOR;
	if (!selected) {
		mask &= 0xFC;
		mask |= nr;
	}
	if (mask != current_DOR) {
		outb(mask,FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
			mon_timer[nr] = HZ/2;
		else if (mon_timer[nr] < 2)
			mon_timer[nr] = 2;
		current_DOR = mask;
	}
	sti();
	return mon_timer[nr];
}

void floppy_on(unsigned int nr)
{
	cli();
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti();
}

void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ;
}

void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i=0 ; i<4 ; i++,mask <<= 1) {
		if (!(mask & current_DOR))
			continue;
		if (mon_timer[i]) {
			if (!--mon_timer[i])
				wake_up(i+wait_motor);
		} else if (!moff_timer[i]) {
			current_DOR &= ~mask;
			outb(current_DOR,FD_DOR);
		} else
			moff_timer[i]--;
	}
}

#define TIME_REQUESTS 64	 //最多可以有64个定时器链表
	//定时器链表结构和定时器数组
static struct timer_list {
	long jiffies;	//定时滴答数
	void (*fn)();	//定时处理程序
	struct timer_list * next;	//下一个定时器
} timer_list[TIME_REQUESTS], * next_timer = NULL;

//添加定时器，输入参数为指定的定时器
void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

	if (!fn)	//定时处理函数指针为空
		return;
	cli();	//禁止中断发生
	if (jiffies <= 0)	//立即处理该程序
		(fn)();	
	else {
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
			if (!p->fn)	//寻找一个空闲项
				break;
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free"); //数组用完，系统崩溃
		p->fn = fn;		//将当前定时器接入链表中
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;
		while (p->next && p->next->jiffies < p->jiffies) {	//将任务按照定时器值从小到大的顺序排列。
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti();  //开中断
}

//时钟中断c函数处理程序。cpl是特权级0或3,0是内核态
void do_timer(long cpl)
{
	extern int beepcount;  //扬声器
	extern void sysbeepstop(void);		//关闭扬声器

	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

	if (cpl)
		current->utime++;	//
	else
		current->stime++;	//超级用户的时间

	if (next_timer) {
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();
		}
	}
	if (current_DOR & 0xf0)
		do_floppy_timer();
	if ((--current->counter)>0) return;
	current->counter=0;
	if (!cpl) return;
	schedule();
}

int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

int sys_getpid(void)
{
	return current->pid;
}

int sys_getppid(void)
{
	return current->father;
}

int sys_getuid(void)
{
	return current->uid;
}

int sys_geteuid(void)
{
	return current->euid;
}

int sys_getgid(void)
{
	return current->gid;
}

int sys_getegid(void)
{
	return current->egid;
}

int sys_nice(long increment)
{
	if (current->priority-increment>0)
		current->priority -= increment;
	return 0;
}

void sched_init(void)
{
	int i;
	struct desc_struct * p;

	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));
	p = gdt+2+FIRST_TSS_ENTRY;
	for(i=1;i<NR_TASKS;i++) {
		task[i] = NULL;
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
/* Clear NT, so that we won't have troubles with that later on */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
	ltr(0);
	lldt(0);
	outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	set_intr_gate(0x20,&timer_interrupt);
	outb(inb_p(0x21)&~0x01,0x21);
	set_system_gate(0x80,&system_call);
}
