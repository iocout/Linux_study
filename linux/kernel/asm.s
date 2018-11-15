/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */
 /*包含大部分硬件故障（或出错）处理的底层次代码。异常页是由内存管理程序mm处理的
 *所以不再这里。
 */
 
 //本代码文件主要涉及对Intel保留的中断int0-int16的处理（in17-int31留作今后使用）。
 //以下是一些全局函数的名的声明，其原型在traps.c中说明

.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved


//标号_divide_error实际上是c语言函数divide_error()编译后所生成模块中对应的名称。
//_do_divide_error函数在traps.c中
//零除错误  int0
_divide_error:
	pushl $_do_divide_error  //将_do_divide_error函数地址入栈，在traps.c中
no_error_code:  //无出错号处理的入口。
	xchgl %eax,(%esp)  //ax入栈，sp的值入ax，注意xchgl是一个宏而非一个原生指令
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl $0		# "error code"
	lea 44(%esp),%edx  //取源调用返回地址处堆栈指针位置，并压入堆栈。
	pushl %edx
	movl $0x10,%edx  //内核代码数据段选择符
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs  //下行上的‘*’号表示的是绝对调用操作数，与程序指针pc无关
	call *%eax  //调用do_divide_error().
	addl $8,%esp  //让指针重新指向寄存器fs入栈处。
	pop %fs  //
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

_debug:  //debug模式  int1 调试中断入口点
	pushl $_do_int3		# _do_debug
	jmp no_error_code

_nmi:  //int2 非屏蔽中断调用入口点
	pushl $_do_nmi  //
	jmp no_error_code

_int3:  //同_debug
	pushl $_do_int3
	jmp no_error_code

_overflow:
	pushl $_do_overflow
	jmp no_error_code

_bounds:
	pushl $_do_bounds
	jmp no_error_code

_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code

_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code

//int15 保留
_reserved:
	pushl $_do_reserved
	jmp no_error_code

//int45（=0x20+13）数学协处理器发出的中断
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20
	jmp 1f
1:	jmp 1f
1:	outb %al,$0xA0
	popl %eax
	jmp _coprocessor_error

//以下中断调用时会在中断返回地址之后将出错号压入堆栈，因此返回时也需要将出错号弹出。
//int 8--双出错中断。
_double_fault:
	pushl $_do_double_fault  //
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax
	xchgl %ebx,(%esp)		# &function <-> %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code
	lea 44(%esp),%eax		# offset
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx
	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

_general_protection:
	pushl $_do_general_protection
	jmp error_code

