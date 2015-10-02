#ifndef __CACHE_CREATER_H__
#define __CACHE_CREATER_H__

#include "tpool_struct.h"
#include "sm_cache.h"
#include "msglog.h"

/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */
#ifndef NDEBUG
#define INIT_thread_structure(p, self) \
	do {\
		(self)->status = THREAD_STAT_INIT;\
		(self)->flags = 0;\
		(self)->ntasks_done = 0;\
		(self)->b_waked = 1;\
		(self)->n_reused = 0;\
		INIT_LIST_HEAD(&self->thq); \
		(self)->ncont_GC_counters = 0;\
		(self)->pool = (p);\
		(self)->run = 1;\
	} while (0)
#else
#define INIT_thread_structure(p, self) \
	do {\
		(self)->status = THREAD_STAT_INIT;\
		(self)->flags = 0;\
		INIT_LIST_HEAD(&self->thq); \
		(self)->ncont_GC_counters = 0;\
		(self)->b_waked = 1;\
		(self)->pool = p;\
		(self)->run = 1;\
	} while (0)
#endif

static void *
thread_obj_create(void *opaque) 
{
	struct tpool_thread_t *thread = calloc(1, sizeof(struct tpool_thread_t) + 
			sizeof(struct cond_attr_t));
	
	if (!thread) {
		MSG_log2("pool", LOG_ERR,
			"***thread_obj_create Err: no memory.");
		return NULL;
	}
	INIT_thread_structure((struct tpool_t *)opaque, thread);
	thread->cattr = (struct cond_attr_t *)(thread + 1);
	thread->structure_release = 1;
	
	return thread;
}

static void 
thread_obj_destroy(void *obj, void *opaque) 
{
	struct tpool_thread_t *thread = obj;
	
	assert (thread->structure_release); 
	if (thread->cattr->initialized)
		OSPX_pthread_cond_destroy(&thread->cattr->cond);
	free(thread);	
}

static int 
thread_obj_need_destroy(void *obj, void *opaque) 
{
	return ((struct tpool_thread_t *)obj)->structure_release;	
}

#endif
