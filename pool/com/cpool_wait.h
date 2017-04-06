#ifndef __CPOOL_WAIT_H__
#define __CPOOL_WAIT_H__

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
#include "list.h"

#define M_WAIT "WAIT"

struct WWAKE_requester 
{
	int id;
	int b_interrupted;
	struct list_head link;
	void (*wakeup)(struct WWAKE_requester *r);
	void *opaque;
};
typedef int (*WWAKE_walk)(struct WWAKE_requester *r, void *walk_arg);

#define DECLARE_WWAKE_REQUEST(name, id, invoke, opaque) \
	struct WWAKE_requester name = { \
		id, 0, {0, 0}, invoke, opaque \
	}

/*----------------------------------------------------------*/
/*-------------APIs about the waiting requests -------------*/
/*----------------------------------------------------------*/

/** Get the global WAKE id */
#define WWAKE_id() ((int)OSPX_pthread_id())

void WWAKE_add(struct WWAKE_requester *r);
void WWAKE_erase(int id, struct WWAKE_requester **r);
void WWAKE_erase_direct(struct WWAKE_requester *r);
int  WWAKE_invoke(int id);
void WWAKE_cb(WWAKE_walk cb, void *cb_arg);

#endif
