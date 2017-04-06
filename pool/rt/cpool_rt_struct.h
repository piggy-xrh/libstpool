#ifndef __CPOOL_RT_STRUCT_H__
#define __CPOOL_RT_STRUCT_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_core_struct.h"
#include "cpool_com_priq.h"

#define M_RT "RT"

typedef struct cpool_rt cpool_rt_t;

struct cpool_rt 
{
	/**
	 * The Core
	 */
	cpool_core_t *core;

	/**
	 * The function masks (CPOOL_F_RT_XX)
	 */
	long lflags;
	
	/**
	 * The reference
	 *
	 * If someone calls a WAIT interface to wait the events on 
	 * our pool, \@ref will be increased.
	 */
	int  ref_sync;
	OSPX_pthread_cond_t *cond_sync;
	
	/**
	 * Throttle
	 */
	int  throttle_on;
	long ev_wref;
	int  ev_need_notify;
	OSPX_pthread_cond_t *cond_event;
	
	/** 
	 * Priority queue 
	 */
	int priq_num;
	cpriq_t *priq;
	cpriq_container_t c;
	
	/**
	 * Task ready queue
	 */
	struct list_head ready_q;
	
	/**
	 * Remove env
	 */
	long tsks_held_by_dispatcher;
	
	/**
	 * Wait env
	 */
	long tsk_wref;
	int  tsk_need_notify;
	OSPX_pthread_cond_t *cond_task;
	
	/**
	 * A common condition variable
	 */
	OSPX_pthread_cond_t cond_com;
	
	/**
	 * Overload policy
	 */
	 int task_threshold;
	 int eoa;
};

#endif
