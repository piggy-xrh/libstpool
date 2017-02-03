/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_core.h"
#include "cpool_core_gc.h"

static inline void cpool_core_GC_timer_startl(cpool_core_t *core)
{
	core->us_last_gcclock = us_startr();
	core->us_gc_left_timeo = core->cattr.rest_timeo * 1000;
	
	if (core->b_GC_delay) {
		core->b_GC_delay = 0;
		core->us_gc_left_timeo += core->cattr.rest_timeo * 1000;
	}
}
#define cpool_core_GC_timer_start(core) cpool_core_GC_timer_startl(core)

static void 
cpool_core_GC_run(basic_task_t *ptsk) 
{
	thread_t *thread = ptsk->task_arg;
	
#if !defined(NDEBUG) && !defined(_WIN) && !defined(OS_T_OSX)
	prctl(PR_SET_NAME, ptsk->task_desc);
#endif
	if (thread->ncache_limit < 0) {
		smlink_q_t dummy_q;
		
		/**
		 * If @ncache_limit is negative, it tell us that
		 * we should reset the GC cache 
		 */
		smcache_reset(thread->core->cache_task, &dummy_q);
	
	} else
		smcache_flush(thread->core->cache_task, thread->ncache_limit);	
	/**
	 * Restart the timer
	 */
	cpool_core_GC_timer_start(thread->core);	
}

int
cpool_core_GC_init(cpool_core_t *core)
{
	/**
	 * Initialize the GC task 
	 */
	core->sys_GC_task.task_desc = "sys/GC";
	core->sys_GC_task.task_run  = cpool_core_GC_run;
	return 0;
}

void 
cpool_core_GC_deinit(cpool_core_t *core)
{
}

static inline int
cpool_core_GC_accessl(cpool_core_t *core, thread_t *self)
{
	return !core->GC || core->GC == self;
}

inline int
cpool_core_GC_expire(cpool_core_t *core, thread_t *self)
{
	long us = us_endr(core->us_last_gcclock);
	
	return (us + 50000) >= core->us_gc_left_timeo || !us;
}

inline int
cpool_core_GC_joinl(cpool_core_t *core, thread_t *self) 
{
	assert (!(self->flags & THREAD_STAT_GC));
	
	core->GC = self;
	self->flags |= THREAD_STAT_GC;
	self->ncont_GC_counters = 0;
	
	cpool_core_event_free_try_notifyl(core);
#ifndef NDEBUG
	MSG_log(M_SCHEDULER, LOG_TRACE,
			"Thread(%d) is selected to do the GC ... ncached(%d)\n",
			self, smcache_nl(core->cache_task));
#endif
	
	if (!core->us_last_gcclock) {
		core->us_last_gcclock = us_startr();
		core->us_gc_left_timeo = 0;
	}
	assert (core->us_gc_left_timeo >= 0);
	
	return 1;
}

inline int
cpool_core_GC_continuel(cpool_core_t *core, thread_t *self)
{
	int ncached = smcache_nl(core->cache_task);

	if (!ncached || ncached < core->cattr.nGC_cache) {
		if (THREAD_STAT_GC & self->flags)
			cpool_core_GC_leavel(core, self);
		return 0;
	}
	
	/**
	 * We try to assign the thread to do the GC jobs 
	 */
	if (THREAD_STAT_GC & self->flags) {
		if (core->b_GC_delay && ncached < 400) {
			cpool_core_GC_timer_startl(core);	
			self->ncont_GC_counters = 0;
			return 0;
		}
	} else if (!cpool_core_GC_joinl(core, self))
		return 0;
	
	if (cpool_core_GC_expire(core, self)) {
		++ self->ncont_GC_counters; 
		self->ncache_limit = ncached - core->cattr.nGC_one_time;	
		
		return 1;
	}
	self->ncont_GC_counters = 0;
	return 0;
}

/**
 * The lock should have been held 
 */
int
cpool_core_GC_gettaskl(cpool_core_t *core, thread_t *self) 
{	
	/**
	 * Currently, we only allowe one thread to do the GC 
	 */
	if (!core->cache_task || !cpool_core_GC_accessl(core, self))
		return 0;
	
	/**
	 * We should quit ourself imediately if we get the event that 
	 * pool is being destroyed and user is responsible for cleaning
	 * the GC 
	 */
	if (!(CORE_F_created & core->status) && core->release_cleaning)
		return 0;
	
	cpool_core_objs_local_flushl(self);
	if (!smcache_nl(core->cache_task)) 
		/**
		 * Flush the objects cached by threads
		 */
		cpool_core_objs_local_flushl_all(core);
	
	/**
	 * Check the cache configurations to determine whether we should
	 * sleep for a while before our doing the GC 
	 */
	if (!cpool_core_GC_continuel(core, self))
		return 0;
	OSPX_pthread_mutex_unlock(&core->mut);
	
	self->task_type = TASK_TYPE_GC;	
	++ self->ncont_GC_counters;
	
	__curtask = &core->sys_GC_task;
	__curtask->task_arg = (void *)self;
			
	return 1;
}


