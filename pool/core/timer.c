/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "timer.h"
#include "ospx.h"
#include "ospx_compatible.h"

uint64_t ___clock = 0;

static inline uint64_t
us_now()
{
	struct timeval tv;

	OSPX_gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

uint64_t 
us_startr()
{
	return us_now();
}

unsigned long
us_endr(uint64_t clock)
{
	uint64_t clock_now = us_now();
	
	if (clock_now <= clock)
		return 0;
	
	return (unsigned long)(clock_now - clock);
}

void 
us_start()
{
	___clock = us_startr();
}

unsigned long
us_end()
{
	return us_endr(___clock);
}


uint64_t
ms_startr()
{
	return us_startr() / 1000;
}

unsigned long 
ms_endr(uint64_t clock)
{
	return us_endr(clock * 1000) / 1000;
}
