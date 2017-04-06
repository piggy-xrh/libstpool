#ifndef __TIMER_H__
#define __TIMER_H__
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

#include "ospx_type.h"

void us_start();
unsigned long us_end();

uint64_t us_startr();
unsigned long us_endr(uint64_t clock);

uint64_t ms_startr();
unsigned long ms_endr(uint64_t clock);
#endif
