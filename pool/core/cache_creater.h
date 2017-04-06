#ifndef __CACHE_CREATER_H__
#define __CACHE_CREATER_H__

#include "cpool_core.h"
#include "cpool_core_struct.h"
#include "sm_cache.h"
#include "msglog.h"

/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */
#ifndef NDEBUG
#define INIT_thread_structure(c, self) \
	do {\
		(self)->status = THREAD_STAT_INIT;\
		(self)->flags = 0;\
		(self)->ntasks_processed = 0;\
		(self)->b_waked = 1;\
		(self)->n_reused = 0;\
		INIT_LIST_HEAD(&(self)->thq); \
		INIT_LIST_HEAD(&(self)->dispatch_q); \
		(self)->ncont_GC_counters = 0;\
		(self)->core = (c);\
		(self)->run = 1;\
		INIT_SMLINK_Q(&(self)->qcache); \
		(self)->local_cache_limited = (c)->thread_local_cache_limited + (unsigned long)((self) + time(NULL)) % 6; \
	} while (0)
#else
#define INIT_thread_structure(c, self) \
	do {\
		(self)->status = THREAD_STAT_INIT;\
		(self)->flags = 0;\
		INIT_LIST_HEAD(&(self)->thq); \
		INIT_LIST_HEAD(&(self)->dispatch_q); \
		(self)->ncont_GC_counters = 0;\
		(self)->b_waked = 1;\
		(self)->core = (c);\
		(self)->run = 1;\
		INIT_SMLINK_Q(&(self)->qcache); \
		(self)->local_cache_limited = (c)->thread_local_cache_limited + (unsigned long)((self) + time(NULL)) % 6; \
	} while (0)
#endif

static void *
thread_obj_create(void *opaque) 
{
	cpool_core_t *core = opaque;
	thread_t *thread = calloc(1, sizeof(thread_t) + 
			sizeof(struct cond_attr));
	
	if (!thread) {
		MSG_log2(M_CORE, LOG_ERR,
			"***thread_obj_create Err: no memory.");
		return NULL;
	}
	INIT_thread_structure(core, thread);
	thread->cattr = (struct cond_attr *)(thread + 1);
	thread->structure_release = 1;

	return thread;
}

static void 
thread_obj_destroy(void *obj, void *opaque) 
{
	thread_t *thread = obj;
	
	assert (thread->structure_release); 
	if (thread->cattr->initialized)
		OSPX_pthread_cond_destroy(&thread->cattr->cond);
	free(thread);	
}

static int 
thread_obj_need_destroy(void *obj, void *opaque) 
{
	return ((thread_t *)obj)->structure_release;	
}

#endif
