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

/* stpool is just a simple Wrapper of the tpool */

#include <assert.h>

#include "ospx.h"
#include "stpool.h"
#include "tpool.h"
#include "objpool.h"

const size_t g_const_TASK_SIZE = sizeof(struct task_t);
static smcache_t *___smc = NULL;

EXPORT const char *
stpool_version() 
{
	return "2015/09/21-2.6.8-libstpool-FObjp-desc";
}

static void
tpool_hook_atexit(struct tpool_t *pool, void *arg) 
{
	free(pool);
}

EXPORT void   
stpool_task_init(struct sttask_t *ptsk, 
				const char *name, int (*run)(struct sttask_t *ptsk),
				void (*complete)(struct sttask_t *ptsk, long vmflags, int code),
				void *arg) 
{
	struct task_t *nptsk = (struct task_t *)ptsk;
	
	tpool_task_init2(nptsk, name, (int (*)(struct task_t *))run, 
			(void (*)(struct task_t *, long , int))complete, arg);	
}

EXPORT struct sttask_t *
stpool_task_new(const char *name,
				int (*run)(struct sttask_t *ptsk),
				void (*complete)(struct sttask_t *ptsk, long vmflags, int code),
				void *arg) 
{
	struct task_t  *ptsk;
	static int ___dummy_boolean = 0;
	static long volatile ___dummy_ref = 0;

	/* Create a global object pool. */
	if (!___dummy_boolean) {
		static objpool_t ___dummy_objp;
		
		if (!___dummy_ref) {
			/* Only one thread can get the chance to initialize
			 * the object pool. */
			if (1 == OSPX_interlocked_increase(&___dummy_ref)) {
				if (!objpool_ctor(&___dummy_objp, "FObjp-C-Global-task", stpool_task_size(), 0))
					___smc = objpool_get_cache(&___dummy_objp);	
				___dummy_boolean = 1;
			}
		}
		
		/* Set a barrier here to synchronize the env */
		while (!___dummy_boolean) ;
	}
	
	/* Create a task object and initialzie it */
	if (___smc && (ptsk = smcache_get(___smc, 1))) 
		 tpool_task_init(ptsk, 
		 	              name, 
						 (int (*)(struct task_t *))run, 
						 (void (*)(struct task_t *, long , int))complete, 
						 arg);
	
	return (struct sttask_t *)ptsk;
}

EXPORT struct sttask_t *
stpool_task_clone(struct sttask_t *ptsk, int clone_schattr) 
{
	struct sttask_t *nptsk;
	struct schattr_t attr;

	nptsk = stpool_task_new(ptsk->task_name,
		ptsk->task_run, ptsk->task_complete, ptsk->task_arg);
	
	if (nptsk && clone_schattr) {
		stpool_task_getschattr(ptsk, &attr);
		stpool_task_setschattr(nptsk, &attr);
	}

	return nptsk;
}

EXPORT void 
stpool_task_delete(struct sttask_t *ptsk) 
{	
	assert (ptsk && ___smc);
	smcache_add_limit(___smc, ptsk, -1);
}

EXPORT long  
stpool_task_set_userflags(struct sttask_t *ptsk, long uflags) 
{
	return tpool_task_set_userflags((struct task_t *)ptsk, uflags);
}

EXPORT long  
stpool_task_get_userflags(struct sttask_t *ptsk) 
{
	return tpool_task_get_userflags((struct task_t *)ptsk);
}

EXPORT void 
stpool_task_setschattr(struct sttask_t *ptsk, struct schattr_t *attr) 
{
	tpool_task_setschattr((struct task_t *)ptsk, 
						(struct xschattr_t *)attr);
}

EXPORT void 
stpool_task_getschattr(struct sttask_t *ptsk, struct schattr_t *attr) 
{
	tpool_task_getschattr((struct task_t *)ptsk, 
						(struct xschattr_t *)attr);
}

EXPORT int
stpool_task_is_free(struct sttask_t *ptsk)
{
	return tpool_task_is_free((struct task_t *)ptsk);
}

EXPORT HPOOL 
stpool_create2(const char *desc, int maxthreads, int minthreads, int suspend, int pri_q_num) 
{
	struct tpool_t *pool;
	
	/* It does not need to load the ospx library since 
     * we do not call any APIs who must use the TLS datas.
	 */
	pool = malloc(sizeof(struct tpool_t));
	if (pool) {
		if (tpool_create(pool, desc, pri_q_num, maxthreads, minthreads, suspend)) {
			free(pool);
			pool = NULL;
		} else 
			tpool_atexit(pool, tpool_hook_atexit, NULL);			
	}

	return (HPOOL)pool;
}

EXPORT const char *
stpool_desc(HPOOL hp)
{
	return ((struct tpool_t *)hp)->desc;
}

EXPORT void  
stpool_thread_setscheattr(HPOOL hp, struct stpool_thattr_t *attr) 
{
	OSPX_pthread_attr_t att = {0};
	struct tpool_t *pool = hp;

	tpool_thread_getscheattr(pool, &att);
	att.stack_size = attr->stack_size;
	att.sche_policy = (enum ep_POLICY)attr->ep_schep;
	att.sche_priority = attr->sche_priority;
	tpool_thread_setscheattr(pool, &att);
}

EXPORT struct stpool_thattr_t *
stpool_thread_getscheattr(HPOOL hp, struct stpool_thattr_t *attr) 
{
	struct tpool_t *pool = hp;
	OSPX_pthread_attr_t att = {0};
	
	tpool_thread_getscheattr(pool, &att);
	attr->stack_size = att.stack_size;
	attr->ep_schep = (enum ep_SCHE)att.sche_policy;
	attr->sche_priority = att.sche_priority;
	
	return attr;
}

EXPORT long 
stpool_addref(HPOOL hp) 
{	
	return tpool_addref(hp);
}

EXPORT long 
stpool_release(HPOOL hp) 
{
	long ref;

	/* We do not waste our time on waiting for 
	 * the pool's being destroyed completely */
	if (!(ref = tpool_release(hp, 0)) && ___smc)
		smcache_flush(___smc, 0);
	
	return ref;
}

EXPORT void 
stpool_set_activetimeo(HPOOL hp, long acttimeo, long randtimeo) 
{
	tpool_set_activetimeo(hp, acttimeo, randtimeo); 
}

EXPORT void 
stpool_adjust_abs(HPOOL hp, int maxthreads, int minthreads) 
{
	tpool_adjust_abs(hp, maxthreads, minthreads);
}

EXPORT void 
stpool_adjust(HPOOL hp, int maxthreads, int minthreads) 
{
	tpool_adjust(hp, maxthreads, minthreads);
}

EXPORT int
stpool_flush(HPOOL hp) 
{
	return tpool_flush(hp);
}

EXPORT void 
stpool_adjust_wait(HPOOL hp) 
{
	tpool_adjust_wait(hp);
}

EXPORT struct stpool_stat_t *
stpool_getstat(HPOOL hp, struct stpool_stat_t *stat) 
{
	return (struct stpool_stat_t *)tpool_getstat(hp, 
					 		 	(struct tpool_stat_t *)stat);	
}

EXPORT const char *
stpool_status_print(HPOOL hp, char *buffer, size_t bufferlen) 
{
	return tpool_status_print((struct tpool_t *)hp, 
							buffer, 
							bufferlen);
}

EXPORT long
stpool_gettskstat(HPOOL hp, struct stpool_tskstat_t *stat) 
{
	return tpool_gettskstat(hp, (struct tpool_tskstat_t *)stat);
}

EXPORT long
stpool_mark_task(HPOOL hp, struct sttask_t *ptsk, long lflags) 
{
	if (ptsk)
		return tpool_mark_task(hp, (struct task_t *)ptsk, lflags);
	else
		return tpool_mark_task_cb(hp, NULL, (void *)lflags);
}

EXPORT int  
stpool_mark_task_cb(HPOOL hp, Walk_cb wcb, void *wcb_arg)
{
	return tpool_mark_task_cb(hp, (TWalk_cb)wcb, wcb_arg);
}

EXPORT void 
stpool_throttle_enable(HPOOL hp, int enable) 
{
	tpool_throttle_enable(hp, enable);
}

EXPORT int  
stpool_throttle_wait(HPOOL hp, long ms) 
{
	return tpool_throttle_wait(hp, ms);
}

EXPORT void 
stpool_suspend(HPOOL hp, int wait) 
{
	tpool_suspend(hp, wait);
}

EXPORT void 
stpool_resume(HPOOL hp) 
{
	tpool_resume(hp);
}

EXPORT int  
stpool_add_task(HPOOL hp, struct sttask_t *tsk) 
{
	return tpool_add_task(hp, (struct task_t *)tsk);
}

EXPORT int 
stpool_add_routine(HPOOL hp, 
		const char *name, int (*run)(struct sttask_t *), 
		void (*complete)(struct sttask_t *, long, int),
		void *arg, struct schattr_t *attr) 
{	
	return tpool_add_routine(hp, name, 
			(int (*)(struct task_t*))run,
			(void (*)(struct task_t *, long, int))complete,
			arg,  (struct xschattr_t *)attr);
}

EXPORT int  
stpool_remove_pending_task(HPOOL hp, struct sttask_t *ptsk, int dispatched_by_pool) 
{
	if (ptsk) 
		return tpool_mark_task(hp, (struct task_t *)ptsk, dispatched_by_pool ? 
					TASK_VMARK_REMOVE_BYPOOL : TASK_VMARK_REMOVE_DIRECTLY);
	else 
		return tpool_remove_pending_task(hp, dispatched_by_pool);
}

EXPORT void 
stpool_detach_task(HPOOL hp, struct sttask_t *tsk) 
{
	tpool_detach_task(hp, (struct task_t *)tsk);
}

EXPORT long 
stpool_wkid() 
{
	return (long)OSPX_pthread_id();
}

EXPORT int  
stpool_task_wait(HPOOL hp, struct sttask_t *ptsk, long ms) 
{
	return stpool_task_wait_entry(hp, ptsk ? &ptsk : NULL, ptsk ? 1 : 0, 1, ms);
}

EXPORT int  
stpool_task_wait_entry(HPOOL hp, 
					   struct sttask_t *entry[], 
					   int n, 
					   int wait_all,
					   long ms) 
{
	return tpool_task_wait_entry(hp, 
							    (struct task_t **)entry, 
							    n, 
							    wait_all, 
							    ms);
}

EXPORT int  
stpool_task_wait_cb(HPOOL hp, Walk_cb wcb, void *wcb_arg, long ms)
{
	return tpool_task_wait_cb(hp, (TWalk_cb)wcb, wcb_arg, ms);
}

EXPORT int  
stpool_status_wait(HPOOL hp, int n_max_pendings, long ms) 
{
	return tpool_status_wait(hp, n_max_pendings, ms);
}

EXPORT void 
stpool_wakeup(HPOOL hp, long wakeup_id) 
{
	tpool_wakeup(hp, wakeup_id);
}
