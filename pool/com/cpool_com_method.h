#ifndef __CPOOL_COMMON_METHOD_H__
#define __CPOOL_COMMON_METHOD_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_method.h"

/** Basic Method */
void  cpool_com_atexit(void * ins, void (*__atexit)(void *), void *opaque);
void  cpool_com_adjust(void * ins, int max, int min);
void  cpool_com_adjust_abs(void * ins, int max, int min);
int   cpool_com_flush(void * ins);
void  cpool_com_resume(void * ins);
long  cpool_com_addref(void * ins);
long  cpool_com_release(void * ins);
void  cpool_com_setattr(void * ins, struct thread_attr *attr);
void  cpool_com_getattr(void * ins, struct thread_attr *attr);
void  cpool_com_set_schedulingattr(void * ins, struct scheduling_attr *attr); 
void  cpool_com_get_schedulingattr(void * ins, struct scheduling_attr *attr); 
void  cpool_com_set_activetimeo(void * ins, long acttimeo, long randtimeo);

ctask_t *cpool_com_cache_get(void * ins);
void  cpool_com_cache_put(void * ins, ctask_t *ptask);

#endif
