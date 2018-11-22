/*
 *  linux/kernel/vsprintf.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
 */

#include <stdarg.h>
#include <string.h>

/* we use this so that we can do without the ctype library */
#define is_digit(c)	((c) >= '0' && (c) <= '9')  //判断是否是数字

static int skip_atoi(const char **s)  //跳过一个个字符，取出数字，这里const的含义是最终的字符是const
{
	int i=0;

	while (is_digit(**s))
		i = i*10 + *((*s)++) - '0';  //i的10倍+（*（*s) ）的值到 ‘0’的位置
	return i;
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SPECIAL	32		/* 0x */
#define SMALL	64		/* use 'abcdef' instead of 'ABCDEF' */

#define do_div(n,base) ({ \
int __res; \
__asm__("divl %4":"=a" (n),"=d" (__res):"0" (n),"1" (0),"r" (base)); \
__res; })

static char * number(char * str, int num, int base, int size, int precision
	,int type)
{
	char c,sign,tmp[36];
	const char *digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int i;

	if (type&SMALL) digits="0123456789abcdefghijklmnopqrstuvwxyz";
	if (type&LEFT) type &= ~ZEROPAD;
	if (base<2 || base>36)
		return 0;
	c = (type & ZEROPAD) ? '0' : ' ' ;
	if (type&SIGN && num<0) {
		sign='-';
		num = -num;
	} else
		sign=(type&PLUS) ? '+' : ((type&SPACE) ? ' ' : 0);
	if (sign) size--;
	if (type&SPECIAL)
		if (base==16) size -= 2;
		else if (base==8) size--;
	i=0;
	if (num==0)
		tmp[i++]='0';
	else while (num!=0)
		tmp[i++]=digits[do_div(num,base)];  //it's ok
	if (i>precision) precision=i;
	size -= precision;
	if (!(type&(ZEROPAD+LEFT)))
		while(size-->0)
			*str++ = ' ';
	if (sign)
		*str++ = sign;
	if (type&SPECIAL)
		if (base==8)
			*str++ = '0';
		else if (base==16) {
			*str++ = '0';
			*str++ = digits[33];
		}
	if (!(type&LEFT))
		while(size-->0)
			*str++ = c;
	while(i<precision--)
		*str++ = '0';
	while(i-->0)
		*str++ = tmp[i];
	while(size-->0)
		*str++ = ' ';
	return str;
}


//格式化输出：%[flags][width][.precision][|h|l|L][type]
int vsprintf(char *buf, const char *fmt, va_list args)
{
	int len;
	int i;
	char * str;
	char *s;
	int *ip;

	int flags;		/* flags to number() */

	int field_width;	/* width of output field */
	int precision;		/* min. # of digits for integers; max
				   number of chars for from string */
	int qualifier;		/* 'h', 'l', or 'L' for integer fields 限定符*/

	for (str=buf ; *fmt ; ++fmt) {   //扫描格式说明符
		if (*fmt != '%') {  //如果没有格式说明符，说明是普通的字符，直接写入str指向的buffer即可
			*str++ = *fmt;
			continue;
		}
			
		//到这一步，说明遇到了格式说明符
		/* process flags */
		flags = 0;  //
		repeat:  //do
			++fmt;		/* this also skips first '%' */  //进入%后的第一个字符
			switch (*fmt) {  //取格式说明符%后面的一个字符，如果是 + - 空格 # 0
				case '-': flags |= LEFT; goto repeat;  //左对齐  %-
				case '+': flags |= PLUS; goto repeat;  //头对齐  %+
				case ' ': flags |= SPACE; goto repeat;  //空格   % 
				case '#': flags |= SPECIAL; goto repeat;  //输出的进制  %#
				case '0': flags |= ZEROPAD; goto repeat;  //输出长度不够，用0填充  %0
				}
		
		/* get field width */
		// 紧接着判断下一个字符是不是数字，如果是则表示域宽
  		//否则
		field_width = -1;  //格式输出宽度
		if (is_digit(*fmt))   //如果是个数字
			field_width = skip_atoi(&fmt);  //把格式输出宽度由字符转换成数字
		else if (*fmt == '*') {   //如果是*，表示下一个参数指定域宽，用va_arg来取得
			/* it's the next argument */
			//这里有个bug，插入++fmt
			++fmt;
			field_width = va_arg(args, int);   
			if (field_width < 0) {  //如果是个负数，左对齐
				field_width = -field_width;
				flags |= LEFT;
			}
		}

		/* get the precision */
		//下一位判断小数点
		precision = -1;  
		if (*fmt == '.') {  //如果格式说明符号后面跟了'.' ,说明对输出的精度有要求
			++fmt;	
			if (is_digit(*fmt))
				precision = skip_atoi(&fmt);
			else if (*fmt == '*') {  //如果是*，取args参数做为进度要求
				/* it's the next argument */
				precision = va_arg(args, int);  //读取不定参数列表的值
			}
			if (precision < 0)  //精度必须大于0
				precision = 0;
		}

		/* get the conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
			qualifier = *fmt;
			++fmt;
		}

		switch (*fmt) {
		case 'c':     //如果格式说明符是个c,把此时的参数当作ASCII码对待,说明将参数以字符的形式打印
			if (!(flags & LEFT))  //没有左对齐进入if
				while (--field_width > 0)  //用空格代替宽度
					*str++ = ' ';
			*str++ = (unsigned char) va_arg(args, int);  //将不定参数的值转化为char型 eg:printf("%c",3);
			while (--field_width > 0) 
				*str++ = ' ';
			break;

		case 's':  //说明是个字符串
			s = va_arg(args, char *);  //获取不定参数列表中的char
			len = strlen(s);
			if (precision < 0)  //蜜汁精度，用来控制字符串精度？？？
				precision = len;
			else if (len > precision)
				len = precision;

			if (!(flags & LEFT))  //没有要求左对齐，就进入if
				while (len < field_width--)
					*str++ = ' ';  //放入空格
			for (i = 0; i < len; ++i)
				*str++ = *s++;  //将string写入str
			while (len < field_width--)
				*str++ = ' ';  //写入空格
			break;

		case 'o':  //八进制输出数
			str = number(str, va_arg(args, unsigned long), 8,
				field_width, precision, flags);
			break;

		case 'p':  //指针输出
			if (field_width == -1) {
				field_width = 8;
				flags |= ZEROPAD;
			}
			str = number(str,
				(unsigned long) va_arg(args, void *), 16,
				field_width, precision, flags);
			break;

		case 'x':  //十六进制输出
			flags |= SMALL;
		case 'X':
			str = number(str, va_arg(args, unsigned long), 16,
				field_width, precision, flags);
			break;

		case 'd':  //有符号整形输出
		case 'i':
			flags |= SIGN;
		case 'u':  //无符号整形输出
			str = number(str, va_arg(args, unsigned long), 10,
				field_width, precision, flags);
			break;

		case 'n':  //把到目前位置转换输出字符串数字保存到对应参数指针指定的位置
			ip = va_arg(args, int *);
			*ip = (str - buf);
			break;

		default:
			if (*fmt != '%')  //== ？？
				*str++ = '%';
			if (*fmt)
				*str++ = *fmt;
			else
				--fmt;
			break;
		}
	}
	*str = '\0';
	return str-buf;	//返回转换好的字符串长度
}
