/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
/*
*auto: HF
*SAY: 使用内联确保没有写时复制（copy on write），这样可以确保在最初创建
*进程0没有多余信息被放入堆栈中，这样，在任务0完成之后，0就不能被后续创建
*的程序所调用。
*/

/*
https://blog.csdn.net/u010132427/article/details/52108669
#define _syscall0(type,name) \
type name(void) \
{ \
long __res; \
__asm__ volatile ("int $0x80" \
	: "=a" (__res) \
	: "0" (__NR_##name)); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}
*/
static inline _syscall0(int,fork)//int fork（） 创建进程系统调用,最后的0表示无参数，1表示一个参数
/*fork函数翻译过来就是：

static inline int fork(void)
{
	long __res;
	__asm__ volatile ("int $0x80" \         //调用系统中断0x80
	: "=a" (__res) \  			//__res用来承载中断返回值
	: "0" (__NR_fork)); \			//输入为系统中断调用号__NR_fork ( = 2)
if (__res >= 0) \
	return (int) __res; \			//如果返回值>=0，则直接返回该值。
errno = -__res; \				//否则置出错号
return -1; \					//并返回-1
}
*/
static inline _syscall0(int,pause)//系统调用，暂停进程，知道收到一个信号
static inline _syscall1(int,setup,void *,BIOS)//用于初始化
static inline _syscall0(int,sync)//更新系统

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];//内核显示信息的缓存

extern int vsprintf();//送格式化到以字符串中（kernel/vsprintf.c)
extern void init(void);//函数原型，初始化
extern void blk_dev_init(void);//块设备初始化子程序
extern void chr_dev_init(void);//字符设备初始化子程序
extern void hd_init(void);//硬盘初始化
extern void floppy_init(void);//软盘初始化
extern void mem_init(long start, long end);//内存初始化
extern long rd_init(long mem_start, int length);//虚拟盘初始化
extern long kernel_mktime(struct tm * tm);//系统开机启动时间
extern long startup_time;//内核启动时间

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)//1M以后扩展内存的大小
#define DRIVE_INFO (*(struct drive_info *)0x90080)//硬盘参数表基址
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)//根文件系统所在设备号

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \        //这段宏读取CMOS实时时钟信息 
outb_p(0x80|addr,0x70); \          //0x70是写端口，0x80|addr是要读取的CMOS内存地址。
inb_p(0x71); \					   //0x71是读端口
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)//BCD码转换成二进制数值

static void time_init(void)   //读取cmos时钟，并设置开机时间 startup_time。
{
	struct tm time;

	//CMOS访问速度慢，为减小时间误差，把读取下面循环中所有数值后，此时如果cmos中秒值发生变化，
	//就重新读取所有值，这样内核就把时间误差控制在1秒之内。
	
	do {
		time.tm_sec = CMOS_READ(0);      
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;							//tm——mon中月份范围是0-11
	startup_time = kernel_mktime(&time);	//调用mktime.c中的函数，计算开机时间
}

static long memory_end = 0;					//机器具有的内存容量
static long buffer_memory_end = 0;			//高速缓冲区末端地址
static long main_memory_start = 0;			//主内存开始的位置

struct drive_info { char dummy[32]; } drive_info;  //硬盘参数表

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
 	ROOT_DEV = ORIG_ROOT_DEV;//跟设备号
 	drive_info = DRIVE_INFO;//硬盘参数表
	memory_end = (1<<20) + (EXT_MEM_K<<10);//内存大小1M+扩展内存（k)*1024
	memory_end &= 0xfffff000;//忽略不到4K的内存数
	if (memory_end > 16*1024*1024)		
		memory_end = 16*1024*1024;	//超过16M，按16M计
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;//设置高速缓冲末地址为4M
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;//如果大于6M，则高速缓冲地址为2M
	else
		buffer_memory_end = 1*1024*1024;//1M
	main_memory_start = buffer_memory_end;//主内存起始位置
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);//虚拟磁盘
#endif

/*men_init函数的定义
399 void mem_init(long start_mem, long end_mem)
400 {
401     int i;
402 
403     HIGH_MEMORY = end_mem;
404     for (i=0 ; i<PAGING_PAGES ; i++)
405         mem_map[i] = USED;  //内核使用的那些页初始化被使用了
406     i = MAP_NR(start_mem);//计算出开始也的索引号 
407     end_mem -= start_mem;
408     end_mem >>= 12;//除以4096(4K)一页的大小，计算页数
409     while (end_mem-->0)//最后标记所有也空闲
410         mem_map[i++]=0;
411 }
*/

	//所有方面的初始化调用，请深入源程序看。
	mem_init(main_memory_start,memory_end);//对内存页进行标记，是否已经使用
	trap_init();  //陷阱门（硬件中断向量)初始化。(kernel/traps.c,181)
	blk_dev_init();  //块设备初始化，（kernel/blk_drv/ll_rw_blk.c,157)
	chr_dev_init();  //字符设备初始化(kernel/chr_drv/tty_io.c ,347)//未定义任何函数

	//主要用于将控制台和文件流初始化
	tty_init();  //tty初始化 (kernel/chr_drv/tty_io.c ,105)

	time_init();   //设置开机启动时间
	sched_init();  //调度程序初始化（加载任务0的tr，ldtr）（kernel/sched.c ,385)
	buffer_init(buffer_memory_end);   //缓冲初始化,建内存链表等（fs/buffer.c,348)
	hd_init();   //硬盘初始化程序 （kernel/blk_drv/hd.c,343行）
	floppy_init();  //软盘初始化（kernel/blk_drv/floppy.c 457行）
	sti();  //所有初始化都做完了，开启中断位 //这个函数用来开启中断，与cli（）函数一起使用来控制中断的开启与关闭

	//下面过程在堆栈中设置的参数，利用中断返回指令启动任务0执行
	move_to_user_mode();  //移动用户模式下执行（include/asm/system.h，第一行） //移动到任务0

	//Linux的系统调用通过int 80h实现，用系统调用号来区分入口函数
	//关于80h调用的中断入口参考：https://blog.csdn.net/xiaominthere/article/details/17287965
	if (!fork()) {		/* we count on this going ok */  //创建进程0            +++++++++++++++++++++++++没看懂
		init();  //复制，注意已经在新建的子进程（任务一）中执行，
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;) pause();  //(kernel/sched.c,144) 在schedule中进行了系统的进程的调度，那个运行，如何运行
}


/*
*产生格式化信息并输出到标准输出设备stdout（1），这里是指屏幕上显示。参数*fmt 指定输出将
*采用的格式，参见各种标准c语言书籍。该子程序正好是vsprintf如何使用的一个例子。改程序使用
*vsprintf（）将格式化的字符串放入printbuf缓冲区，然后用write（）将缓冲区的内容输出到标准
*设备（1==stdout）。vsprintf（）函数的实现见kernel/vsprintf.c,91
*/
static int printf(const char *fmt, ...)  //printf的格式，格式字符串+不定参数
{
	va_list args;
	int i;

	va_start(args, fmt);  //初始化
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));  //调用vsprintf函数,注意，printbuf将放到标准输出stdout -- 1
	va_end(args);  //释放资源
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };  //调用执行程序时参数的字符串数组。
static char * envp_rc[] = { "HOME=/", NULL };  //调用执行程序时的环境字符串数组。

static char * argv[] = { "-/bin/sh",NULL };  //同上
static char * envp[] = { "HOME=/usr/root", NULL };  
//上面237行中argv[0]中的字符“-”是传递给shell程序sh的一个标志。通过识别该标志，sh程序会作为登录shell执行。
//其执行过程与在shell提示符下执行sh不太一样。

/*在main（）中已经进行了系统初始化，包括内存管理，各种硬件设备和驱动程序。init（）函数运行在任务0第一次创建的子进程
*（任务1）中，它首先对第一个将要执行的程序（shell）的环境进行初始化，然后加载该程序并执行之。
*/
void init(void)
{
	int pid,i;

//这是一个系统调用。用于读取硬盘参数包括分区表信息并加载虚拟盘（若存在的话）和安装根文件系统设备。该函数使用25行上
//的宏定义的，对应函数是sys_setup()，在(kernel/blk_drv/hd.c,71)
	setup((void *) &drive_info);
	(void) open("/dev/tty0",O_RDWR,0);  //读写方式打开终端控制台。其文件描述符是0，对应stdin
	(void) dup(0);  //复制描述符，创建1，对应stdout
	(void) dup(0);  //复制描述符，创建2，对应stdcerr
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);  //使用的内存
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);  //未使用的内存
	if (!(pid=fork())) {  //fork后，子进程返回0，父进程返回子进程号，这里为子进程（任务2）
		close(0);  //关闭stdin
		if (open("/etc/rc",O_RDONLY,0))  //
			_exit(1);  //操作未经许可，直接调用0x80退出。
		execve("/bin/sh",argv_rc,envp_rc);  //将该进程替换成shell程序。
		_exit(2);  //如果execve执行失败（即不在阻塞），就退出，2--文件或目录不存在。
	}
	if (pid>0)  //父进程执行的函数
		while (pid != wait(&i))  //等待子进程终止。如果不是子进程的进程号（pid),就继续等待
			/* nothing */;
	while (1) {  //如果执行到这里，说明子进程已经停止或返回。
		if ((pid=fork())<0) {  //重新创建一个子进程。
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {  //新创建的子进程
			close(0);close(1);close(2);  //关闭原来的文件描述符
			setsid();  //让子进程不受终端影响，即使终端退出了也能正常运行。
			(void) open("/dev/tty0",O_RDWR,0);  //重新打开终端
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));//将该进程替换成shell程序。用这种方式打开的包含登录程序,用于处理多次输入错误的密码情况
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();  //同步操作，刷新缓冲区
	}
	/*
	*exit()调用退出处理函数，清除i/O缓存，然后调用_exit()
	*_exit()直接调用系统0x80中断，清理内存空间。
	*/
	_exit(0);	/* NOTE! _exit, not exit() */
}
