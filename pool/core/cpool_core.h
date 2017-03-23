#ifndef __CPOOL_CORE_H__
#define __CPOOL_CORE_H__

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

#include "ospx_config.h"
#ifdef HAS_PRCTL
#include <sys/prctl.h> 
#endif
#include "msglog.h"
#include "cpool_core_struct.h"

#define M_CORE "Core"
#define M_SCHEDULER "Scheduler"

#ifndef NDEBUG 
void VERIFY(cpool_core_t *core, thread_t *self); 
#else
#define VERIFY(core, thread) 
#endif

/***************************Interfaces about the Core ******************************/
int  cpool_core_ctor(cpool_core_t *core, const char *desc, const cpool_core_method_t *const me, 
					int maxthreads, int minthreads, int suspend, long lflags); 
void cpool_core_atexit(cpool_core_t *core, void (*atexit_func)(void *arg), void *arg);
void cpool_core_dtor(cpool_core_t *core);

static inline long cpool_core_addrefl(cpool_core_t *core, int increase_user) 
{
	++ core->ref; 
	if (increase_user) 
		return ++ core->user_ref;
	
	return core->ref;
}

static inline long cpool_core_releasel(cpool_core_t *core, int decrease_user) 
{
	assert (core->user_ref >= 0 && core->ref >= core->user_ref);
	
	-- core->ref;
	if (decrease_user) 
		return -- core->user_ref;	
	
	return core->ref;
}

static inline long cpool_core_statusl(cpool_core_t *core) {return core->status;}

long cpool_core_release_ex(cpool_core_t *core, int decrease_user, int clean_wait);
void cpool_core_load_envl(cpool_core_t *core); 
void cpool_core_adjust_cachel(cpool_core_t *core, cache_attr_t *attr, cache_attr_t *oattr); 
void cpool_core_ensure_servicesl(cpool_core_t *core, thread_t *self);
int  cpool_core_add_threadsl(cpool_core_t *core, thread_t *self, int nthreads); 
int  cpool_core_dec_threadsl(cpool_core_t *core, int nthreads);
void cpool_core_adjust_abs_l(cpool_core_t *core, int maxthreads, int minthreads); 
int  cpool_core_GC_gettaskl(cpool_core_t *core, thread_t *self);
void cpool_core_suspendl(cpool_core_t *core);
void cpool_core_resume(cpool_core_t *core);

#define cpool_core_waitq_sizel(core) ((core)->n_qths_wait)
static inline void cpool_core_wakeup_n_sleeping_threadsl(cpool_core_t *core, int nwake) 
{
	/**
	 * Scan the sleeping threads queue to wake up a few threads
	 * to provide services 
	 */
	while (!list_empty(&core->ths_waitq)) {
		thread_t *thread = container_of(core->ths_waitq.next,
				thread_t, run_link); 
		
		assert (!thread->b_waked && core->n_qths_wait > 0);
		
		thread->b_waked = 1;
		list_del(&thread->run_link);
		OSPX_pthread_cond_signal(&thread->cattr->cond);
		
		-- core->n_qths_wait;
		++ core->n_qths_waked;
		if (!-- nwake)
			break;
	}	
	
	VERIFY(core, NULL);
}

/**
 * NOTE:
 * 	To improve the perfermance of the scheduler, the underlying context may not call 
 * @cpool_core_thread_status_changel(self, THREAD_STAT_RUN) to update the Core's status
 */
static inline int cpool_core_all_donel(cpool_core_t *core)
{
	int ok = 0;
	assert (core->n_qths >= core->n_qths_wait + core->n_qths_waked + core->nthreads_running +
			core->nthreads_real_free);
	
	if ((core->paused || !core->npendings) && !core->n_qdispatchs) {
		if (CORE_F_dynamic & core->lflags)
			ok = !core->nthreads_running; 
	
		else if (!core->GC || !core->GC->b_waked)
			ok = (core->n_qths == (core->n_qths_wait + core->n_qths_waked));
		else
			ok = (core->n_qths == (core->n_qths_wait + core->n_qths_waked + 
				((core->GC->status == THREAD_STAT_WAIT) ? 0 : 1))); 
	}
	if (!ok && core->event_free_notified)
		core->event_free_notified = 0;
	return ok;
}

static inline void cpool_core_event_free_try_notifyl(cpool_core_t *core)
{
	if (cpool_core_all_donel(core) && !core->event_free_notified) {
		core->event_free_notified = 1;
		if (core->me->notifyl) 
			core->me->notifyl(core->priv, eEvent_F_free);
	}
}

/**
 * Wake up working threads to schedule tasks.
 *  .Are there any FREE threads ?
 *  .Has the threads number arrived at the limit ?
 */
static inline int cpool_core_need_ensure_servicesl(cpool_core_t *core) 
{
	return (!core->nthreads_real_free && !core->n_qths_waked && 
			(core->maxthreads != core->nthreads_real_pool || core->n_qths_wait));
}

/**
 * Dump the status of the Core
 */
static inline struct cpool_core_stat *cpool_core_statl(cpool_core_t *core, struct cpool_core_stat *stat)
{
	static struct cpool_core_stat __stat;

	if (!stat)
		stat = &__stat;
	
	stat->desc = core->desc;
	stat->start = core->start;
	stat->status = cpool_core_statusl(core);
	stat->max = core->maxthreads;
	stat->min = core->minthreads;
	stat->user_ref = core->user_ref;
	stat->acttimeo = core->acttimeo;
	stat->randtimeo = core->randtimeo;
	stat->paused = core->paused;
	stat->ncaches = core->cache_task ? smcache_nl(core->cache_task) : 0;
	stat->n_qpendings = core->npendings;
	stat->n_qdispatchs = core->n_qdispatchs;
	stat->n_qths = core->n_qths;
	stat->n_qths_wait = core->n_qths_wait;
	stat->n_qths_waked = core->n_qths_waked;
	stat->nths_dying = core->nthreads_dying;
	stat->nths_running = core->nthreads_running;
	stat->nths_dying_run = core->nthreads_dying_run;
	stat->nths_free_effective = core->nthreads_real_free;
	stat->nths_effective = core->nthreads_real_pool;
	stat->nths_peak = core->nthreads_peak;
	
	return stat;
}

/*------------------------Interfaces about the cache-------------------------------*/
static inline void cpool_core_objs_local_flushl(thread_t *self)
{
	if (smlink_q_size(&self->qcache)) {
		if (smcache_auto_lock(self->core->cache_task))
			smcache_add_q_dir(self->core->cache_task, &self->qcache);
		else
			smcache_add_ql_dir(self->core->cache_task, &self->qcache);
	}
	self->flags &= ~THREAD_STAT_FLUSH;
}

static inline void cpool_core_objs_local_flushl_all(cpool_core_t *core)
{
	thread_t *thread;
		
	/**
	 * Notify all threads to flush its cache
	 */
	list_for_each_entry(thread, &core->ths, thread_t, link) {
		if (!smlink_q_empty(&thread->qcache))
			thread->flags |= THREAD_STAT_FLUSH;
	}
}

static inline void cpool_core_objs_local_flush(thread_t *self)
{
	if (smlink_q_size(&self->qcache)) 
		smcache_add_q_dir(self->core->cache_task, &self->qcache);
	
	if (THREAD_STAT_FLUSH & self->flags) {
		OSPX_pthread_mutex_lock(&self->core->mut);
		self->flags &= ~THREAD_STAT_FLUSH;
		OSPX_pthread_mutex_unlock(&self->core->mut);
	}
}

static inline void cpool_core_objs_local_flush_all(cpool_core_t *core)
{
	thread_t *thread;
		
	/**
	 * Notify all threads to flush their caches
	 */
	OSPX_pthread_mutex_lock(&core->mut);
	list_for_each_entry(thread, &core->ths, thread_t, link) {
		if (!smlink_q_empty(&thread->qcache))
			thread->flags |= THREAD_STAT_FLUSH;
	}
	OSPX_pthread_mutex_unlock(&core->mut);
}

static inline void cpool_core_objs_local_store(thread_t *self, basic_task_t *ptask) 
{
	smlink_q_push(&self->qcache, (void *)ptask);
	
	if (self->local_cache_limited <= smlink_q_size(&self->qcache)) 
		cpool_core_objs_local_flush(self);
}

#endif
