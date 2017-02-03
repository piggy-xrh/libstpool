#ifndef __CPOOL_GP_WAIT_H__
#define __CPOOL_GP_WAIT_H__

#include "cpool_gp_struct.h"

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

/**********************************************************/
/********************The waiting requests class************/
/**********************************************************/

/* For requests waiting for the entry */
#define WAIT_CLASS_ENTRY    0x0001

/* For requests waiting for the pool */
#define WAIT_CLASS_POOL     0x0002

#define WAIT_CLASS_ALL  (WAIT_CLASS_ENTRY|WAIT_CLASS_POOL)

/**********************************************************/
/********************The waiting requests type*************/
/**********************************************************/

/* For requests waiting for either the single task or Walk_cb */
#define WAIT_TYPE_TASK     0x0010

/* For request waiting for any tasks existing in the tasks sets */
#define WAIT_TYPE_TASK_ANY 0x0020

/* For requests waiting for all tasks */
#define WAIT_TYPE_TASK_ALL 0x0040

/* For requests on the throotle */
#define WAIT_TYPE_THROTTLE 0x0080


/* For @stpool_task_wait_any */
#define WAIT_TYPE_TASK_ANY2 0x0400

#define WAIT_TYPE_ENTRY_ALL  (WAIT_TYPE_TASK|WAIT_TYPE_TASK_ANY|WAIT_TYPE_TASK_ALL|\
							  WAIT_TYPE_THROTTLE)
#define WAIT_TYPE_POOL_ALL   (WAIT_TYPE_ENTRY_ALL|WAIT_TYPE_TASK_ANY2)
#define WAIT_TYPE_ALL        (WAIT_CLASS_ALL|WAIT_TYPE_ENTRY_ALL|WAIT_TYPE_POOL_ALL)     

/************************************************************************/
/******************* APIs about the waiting requests ********************/
/************************************************************************/

/**
 * Wake up the waiters
 */
int cpool_gp_w_wakeup(cpool_gp_t *gpool, long type, int id);

#endif
