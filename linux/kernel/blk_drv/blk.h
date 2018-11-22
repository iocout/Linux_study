#ifndef _BLK_H
#define _BLK_H

#define NR_BLK_DEV	7	//块设备数量
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */
#define NR_REQUEST	32

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and 'waiting' is used to wait for
 * read/write completion.
 */
struct request {
	int dev;		/* -1 if no request */ //使用的设备号
	int cmd;		/* READ or WRITE */ //命令
	int errors;		//操作时产生的错误次数
	unsigned long sector;	//起始扇区 (1块=2扇区)
	unsigned long nr_sectors;  //读/写扇区数
	char * buffer;		//数据缓冲区
	struct task_struct * waiting;	//任务等待操作执行完成的地方
	struct buffer_head * bh;	//缓冲区头指针
	struct request * next;	//指向下一项请求
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */
#define IN_ORDER(s1,s2) \
((s1)->cmd<(s2)->cmd || (s1)->cmd==(s2)->cmd && \
((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector)))

//块设备结构
struct blk_dev_struct {
	void (*request_fn)(void);	//请求项操作的函数指针
	struct request * current_request;	//当前请求项指针
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];	//块设备表 NR_BLK_DEV=7
extern struct request request[NR_REQUEST];	//请求队列数组（NR_REQUEST=32）
extern struct task_struct * wait_for_request;	//

#ifdef MAJOR_NR

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */

#if (MAJOR_NR == 1)	 //RAM盘主设备号为1
/* ram disk */
#define DEVICE_NAME "ramdisk"	//设备名称
#define DEVICE_REQUEST do_rd_request	//设备请求函数do_rd_request()
#define DEVICE_NR(device) ((device) & 7)	//设备号（0-7）
#define DEVICE_ON(device) 	//开启设备。虚拟盘无须开启和关闭
#define DEVICE_OFF(device)	//关闭设备。

#elif (MAJOR_NR == 2)	//软驱主设备号为2
/* floppy */
#define DEVICE_NAME "floppy"	//设备名称
#define DEVICE_INTR do_floppy	//设备中断处理程序
#define DEVICE_REQUEST do_fd_request	//设备请求函数
#define DEVICE_NR(device) ((device) & 3)	//设备号(0-3)
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))	//开启设备函数
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))	//关闭设备函数

#elif (MAJOR_NR == 3)	//硬盘主设备号是3
/* harddisk */
#define DEVICE_NAME "harddisk"	//名称
#define DEVICE_INTR do_hd	//中断处理
#define DEVICE_REQUEST do_hd_request	//设备请求函数
#define DEVICE_NR(device) (MINOR(device)/5)	//设备号（0--1）。每个硬盘可以有4个分区
#define DEVICE_ON(device)	//硬盘一直在工作，无须开启和关闭
#define DEVICE_OFF(device)

#elif
/* unknown blk device */
#error "unknown blk device"		//未知设备块

#endif

#define CURRENT (blk_dev[MAJOR_NR].current_request)	//CURRENT 指定主设备号的当前请求结构
#define CURRENT_DEV DEVICE_NR(CURRENT->dev)	//当前请求的设备号

//声明两个宏为函数指针
#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
static void (DEVICE_REQUEST)(void);

//释放锁定的缓冲区(块)
extern inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)	//如果指定的缓冲区bh并没有被上锁。则显示警告信息
		printk(DEVICE_NAME ": free buffer being unlocked\n");
	bh->b_lock=0;	//否则将缓冲区解锁
	wake_up(&bh->b_wait);	//唤醒等待该缓冲区的进程
}

//结束请求
/*
*
*/

extern inline void end_request(int uptodate)
{
	DEVICE_OFF(CURRENT->dev);	//关闭设备
	if (CURRENT->bh) {		
		CURRENT->bh->b_uptodate = uptodate;		//更新标志
		unlock_buffer(CURRENT->bh);		//解锁缓冲区
	}
	if (!uptodate) {	//更新标识为0
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r",CURRENT->dev,
			CURRENT->bh->b_blocknr);
	}
	wake_up(&CURRENT->waiting);	//唤醒等待该请求项的进程
	wake_up(&wait_for_request);		//唤醒等待请求的进程
	CURRENT->dev = -1;		//释放该请求项
	CURRENT = CURRENT->next;	//指向下一个请求项
}

//定义初始化请求宏
#define INIT_REQUEST \
repeat: \
	if (!CURRENT) \  	//如果当前请求指针为null返回
		return; \		//标识本设备当前没有需要处理的请求项
	if (MAJOR(CURRENT->dev) != MAJOR_NR) \		//如果设备号不对就死机
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!CURRENT->bh->b_lock) \		//如果在请求操作时缓冲区没锁定就死机
			panic(DEVICE_NAME ": block not locked"); \	
	}

#endif

#endif
