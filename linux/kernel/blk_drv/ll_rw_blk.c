/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL }		/* dev lp */
};

//上锁
static inline void lock_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);	//睡眠，直到缓冲区解锁
	bh->b_lock=1;	//立刻锁定该缓冲区
	sti();
}

//解锁
static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)	//未锁定，错误
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;	//解锁
	wake_up(&bh->b_wait);	//唤醒等待该缓冲区的任务
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */
//向链表中插入一项请求，它会关闭中断
//然后就能安全的处理请求链表了
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	req->next = NULL;
	cli();
	if (req->bh)
		req->bh->b_dirt = 0;
	if (!(tmp = dev->current_request)) {	//列表中没有请求项
		dev->current_request = req;
		sti();
		(dev->request_fn)();
		return;
	}
	for ( ; tmp->next ; tmp=tmp->next)	//如果已经有请求项，用电梯算法分析出磁盘移动的最小距离。
		if ((IN_ORDER(tmp,req) ||
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	req->next=tmp->next;
	tmp->next=req;
	sti();
}

//创建请求项并插入请求队列，参数是：major主设备号，rw 命令，bh 存放数据缓冲区头指针
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
//READA /WRITEA 提前预读
	if (rw_ahead = (rw == READA || rw == WRITEA)) {	 //
		if (bh->b_lock)	//是否上锁
			return;	//已经上锁就放弃预读
		if (rw == READA)	//如果没有上锁就按照普通读写命令来执行
			rw = READ;
		else
			rw = WRITE;
	}
	if (rw!=READ && rw!=WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");
	lock_buffer(bh);	//锁定缓冲区
	//如果命令是写，单缓冲区数据不脏(未被修改过),或者命令是读，缓冲区未更新
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);	//解锁，放弃执行
		return;
	}
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */

//注意，在任务队列中，写只能占2/3，因为要给读预留1/3，读可以全占
	if (rw == READ)
		req = request+NR_REQUEST;	//读请求直接指向队列尾
	else
		req = request+((NR_REQUEST*2)/3);	//写请求最多指向2/3
/* find an empty request */
//从后向前搜索，当请求结构request的dev字段值为-1时，表示该项未被占用
	while (--req >= request)
		if (req->dev<0)
			break;
/* if none found, sleep on new requests: check for rw_ahead */

	if (req < request) {	//如果没有找到
		if (rw_ahead) {	//看是否预读
			unlock_buffer(bh);	
			return;
		}
		sleep_on(&wait_for_request);	//睡眠，再次请求
		goto repeat;
	}
/* fill up the request-info, and add it to the queue */
	req->dev = bh->b_dev;
	req->cmd = rw;
	req->errors=0;
	req->sector = bh->b_blocknr<<1;
	req->nr_sectors = 2;	//读写扇区数
	req->buffer = bh->b_data;	//数据缓冲区
	req->waiting = NULL;
	req->bh = bh;
	req->next = NULL;
	add_request(major+blk_dev,req);	 //将请求项加入队列中
}

void ll_rw_block(int rw, struct buffer_head * bh)
{
	unsigned int major;

	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
	!(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major,rw,bh);
}

//块设备初始化，request的dev字段为-1表示空闲请求
void blk_dev_init(void)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {
		request[i].dev = -1;//初始化为空闲请求
		request[i].next = NULL;
	}
}
