/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */
#define MINUTE 60
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define YEAR (365*DAY)

/* interestingly, we assume leap-years */
static int month[12] = {
	0,
	DAY*(31),
	DAY*(31+29),
	DAY*(31+29+31),
	DAY*(31+29+31+30),
	DAY*(31+29+31+30+31),
	DAY*(31+29+31+30+31+30),
	DAY*(31+29+31+30+31+30+31),
	DAY*(31+29+31+30+31+30+31+31),
	DAY*(31+29+31+30+31+30+31+31+30),
	DAY*(31+29+31+30+31+30+31+31+30+31),
	DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

/*只能用于表示1970到1999年*/
long kernel_mktime(struct tm * tm)
{
	long res;
	int year;

	year = tm->tm_year - 70;//年份的后两位，这里表示从现在到1970年的年份差。
/* magic offsets (y+1) needed to get leapyears right.*/
/* 下面这句含意：YEAR*year 表示这些年总供经过的秒数，DAY*((year+1)/4表示这些年
*中有多少个闰年，每个闰都需多加一天的秒数。Year+1表示计算闰年时从1970年算起*/
	res = YEAR*year + DAY*((year+1)/4);  //年所经过的秒
	res += month[tm->tm_mon];			 //月所经过的秒
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
/*
*下面这句的目的主要是判断当年是否为闰年，如果不是需减掉一天的秒数
*(因为month全局变量中默认二月份有29天)。这里判断当年是否为闰年并没有用闰年算法(参见下面的闰年算法)，
*每4年就会出去一次闰年，如：1970年不是闰年，1971年不是，1972年是，1973年不是，1974年不是，1975年不是，
*1976年是闰年…。大家有没有注意到1972,1976,1980这些闰年的规律，闰年的后两位+2都能被4整除。
*/
	if (tm->tm_mon>1 && ((year+2)%4))
		res -= DAY;
	res += DAY*(tm->tm_mday-1);
	res += HOUR*tm->tm_hour;
	res += MINUTE*tm->tm_min;
	res += tm->tm_sec;
	return res;
}
/*闰年算法：如果年份能被4整除但不能被100整除，或者年份能被400整除。*/
/* 
bool isLeapYear(long year) {
        return ((year % 4) == 0) && ((year % 100) != 0 || (year % 400) == 0);
    }
*/