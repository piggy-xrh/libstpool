/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "ospx_errno.h"
#include "msglog.h"
#include "cpool_factory.h"
#include "cpool_com_internal.h"
#include "cpool_rt_internal.h"
#include "cpool_rt_method.h"
#include "cpool_core_thread_status.h"

int 
cpool_rt_core_ctor(void *priv)
{
	int e;
	cpool_rt_t *rtp = priv;
	cache_attr_t attr;

	MSG_log(M_RT, LOG_INFO,
		"Creating rt pool(%s/%p) ... lflags(%p)\n",
		rtp->core->desc, priv, rtp->lflags);

	if ((e=OSPX_pthread_cond_init(&rtp->cond_com))) {
		MSG_log2(M_RT, LOG_ERR,
			"cond_init: %s",
			OSPX_sys_strerror(e));
		return -1;
	}
	rtp->cond_sync = rtp->cond_event = rtp->cond_task = &rtp->cond_com;

	/**
	 * We create the our objpool to provide cache services
	 */
	cpool_core_adjust_cachel(rtp->core, NULL, &attr);
	if (objpool_ctor2(&rtp->core->objp_task, "FObjp-rt-local-cache", sizeof(ctask_t), 0,
				attr.nGC_cache, NULL)) {
		MSG_log2(M_CORE, LOG_ERR,
			"Fail to execute objpool_ctor.");
		goto free_cond_com;
	}
	rtp->core->cache_task = objpool_get_cache(&rtp->core->objp_task);
	
	/**
	 * Initialize the priority queue
	 */
	if (rtp->lflags & eFUNC_F_PRIORITY) {
		rtp->priq_num = max(rtp->priq_num, 1);
		rtp->priq_num = min(rtp->priq_num, 100);
		
		if (!(rtp->priq = calloc(1, rtp->priq_num * sizeof(cpriq_t)))) {
			MSG_log2(M_RT, LOG_ERR,
				"System is out of memory.");

			goto free_cache;
		}
		__cpool_com_priq_init(&rtp->c, rtp->priq, rtp->priq_num, &rtp->ready_q);
	
	} else
		INIT_LIST_HEAD(&rtp->ready_q);
	
	return 0;

free_cache:
	objpool_dtor(&rtp->core->objp_task);
	rtp->core->cache_task = NULL;

free_cond_com:
	OSPX_pthread_cond_destroy(&rtp->cond_com);

	return -1;
}

void 
cpool_rt_core_dtor(void *priv)
{
	cpool_rt_t *rtp = priv;
	
	MSG_log(M_RT, LOG_INFO,
		"Destroying rt pool(%s/%p) ...\n",
		rtp->core->desc, priv);
	
	assert (!rtp->tsk_wref && !rtp->ev_wref);

	objpool_dtor(&rtp->core->objp_task);
	OSPX_pthread_cond_destroy(&rtp->cond_com);
	if (rtp->priq)
		free(rtp->priq);
}

void 
cpool_rt_core_notifyl(void *priv, eEvent_t events)
{
	cpool_rt_t *rtp = priv;

#ifndef NDEBUG
	MSG_log(M_RT, LOG_TRACE,
		"Received events(%p) from the Core.\n",
		events);
#endif
	
	if (events & eEvent_F_free) {
		assert (cpool_core_all_donel(rtp->core));
	
		if (rtp->tsk_need_notify && !rtp->tsks_held_by_dispatcher) {
			rtp->tsk_need_notify = 0;
			OSPX_pthread_cond_broadcast(rtp->cond_task);
		}

		if (events == eEvent_F_free)
			return;
	}
	
	if (events & eEvent_F_core_resume) {
		if (!(rtp->lflags & eFUNC_F_DYNAMIC_THREADS) && rtp->core->npendings) 
			cpool_core_wakeup_n_sleeping_threadsl(rtp->core, rtp->core->npendings);
	}
	
	if (events & eEvent_F_thread) {
		if (!(rtp->lflags & eFUNC_F_DYNAMIC_THREADS)) 
			rtp->core->minthreads = rtp->core->maxthreads;
	}

	if (events & eEvent_F_destroying) {
		LIST_HEAD(rmq);
		
		/**
		 * Wake up all EVENT waiters
		 */
		OSPX_pthread_cond_broadcast(rtp->cond_event);
		/**
		 * The Core is going to be destroyed, we should check our pending 
		 * queue, if the Core has been marked suspened, we should remove
		 * all of the pending tasks.
		 */
		if (rtp->core->paused && rtp->core->npendings) {
			rtp->core->n_qdispatchs += rtp->core->npendings;

			if (rtp->lflags & eFUNC_F_PRIORITY)
				__cpool_rt_priq_remove_all(rtp, &rmq); 
			else
				__cpool_rt_remove_all(rtp, &rmq); 

			__cpool_com_task_mark_append(&rmq, eTASK_VM_F_POOL_DESTROYING|eTASK_VM_F_REMOVE_BYPOOL);
			list_splice(&rmq, &rtp->core->dispatch_q);
			cpool_core_ensure_servicesl(rtp->core, NULL);
		}
	}

	if (events & eEvent_F_shutdown) {
		/**
		 * Wake up all the waiters
		 */
		OSPX_pthread_cond_broadcast(&rtp->cond_com);
		for (;rtp->tsk_wref || rtp->ev_wref;) {
			++ rtp->ref_sync;
			OSPX_pthread_cond_wait(rtp->cond_sync, &rtp->core->mut);
			-- rtp->ref_sync;
		}
	}
}

static inline void
__cpool_rt_gettask_post(cpool_rt_t *rtp, thread_t *self, ctask_t *ptask)
{
	/**
	 * Set the running env
	 */
	ptask->f_stat = eTASK_STAT_F_SCHEDULING;
	self->current_task = TASK_CAST_CORE(ptask);
	self->task_type = TASK_TYPE_NORMAL;

#if !defined(NDEBUG) && defined(HAS_PRCTL)
	if (ptask->task_desc)
		prctl(PR_SET_NAME, ptask->task_desc);
#endif

}

int
cpool_rt_core_dynamic_gettask(void *priv, thread_t *self)
{
	ctask_t *ptask; 
	cpool_rt_t *rtp = priv;
	
	/**
	 * We schedule the dispatching tasks firstly 
	 */
	OSPX_pthread_mutex_lock(&rtp->core->mut);
	if (rtp->core->n_qdispatchs) 
		return __cpool_com_get_dispatch_taskl2(rtp->core, self, rtp->core->max_tasks_qdispatching);
	
	/**
	 * If there are none active tasks to be retreived by us,
	 * we return 0.
	 */
	if (rtp->core->paused || !rtp->core->npendings) 
		return 0;

	/**
	 * The pending queue must not be empty if this interface
	 * is called by the Core
	 */
	assert (!list_empty(&rtp->ready_q));
	-- rtp->core->npendings;
	
	/**
	 * Pop up a task from the pending queue and update the
	 * pendings number
	 */
	ptask = list_first_entry(&rtp->ready_q, ctask_t, link);
	__list_del(ptask->link.prev, ptask->link.next);
	
	/**
	 * We set the current task here for the debug messages
	 */
#ifndef NDEBUG
	__curtask = TASK_CAST_CORE(ptask);
#endif
	/**
	 * Notify the Core that we are ready now
	 */
	cpool_core_thread_status_changel(rtp->core, self, THREAD_STAT_RUN);
	OSPX_pthread_mutex_unlock(&rtp->core->mut);
	
	__cpool_rt_gettask_post(rtp, self, ptask);
	return 1;
}

long 
cpool_rt_core_err_reasons(basic_task_t *ptask)
{
	assert (TASK_CAST_FAC(ptask)->f_vmflags & eTASK_VM_F_REMOVE_FLAGS);
	
	if (likely(!(TASK_CAST_FAC(ptask)->f_vmflags & eTASK_VM_F_POOL_DESTROYING)))
		return eErr_removed_byuser;

	/**
	 * We remove the flags
	 */
	TASK_CAST_FAC(ptask)->f_vmflags &= ~eTASK_VM_F_POOL_DESTROYING;
	return eErr_pool_destroying;
}

int
cpool_rt_core_gettask(void *priv, thread_t *self)
{
	ctask_t *ptask; 
	cpool_rt_t *rtp = priv;
	
	/**
	 * We schedule the dispatching tasks firstly 
	 */
	OSPX_pthread_mutex_lock(&rtp->core->mut);
	if (rtp->core->n_qdispatchs) {
		__cpool_com_get_dispatch_taskl(rtp->core, self, rtp->core->max_tasks_qdispatching);
		OSPX_pthread_mutex_unlock(&rtp->core->mut);

		self->task_type = TASK_TYPE_DISPATCHED;
		return 1;
	}
	
	/**
	 * If there are none active tasks to be retreived by us,
	 * we return 0.
	 */
	if (rtp->core->paused || !rtp->core->npendings) 
		return 0;
	/**
	 * The pending queue must not be empty if this interface
	 * is called by the Core
	 */
	assert (!list_empty(&rtp->ready_q));
	-- rtp->core->npendings;
	
	/**
	 * Pop up a task from the pending queue and update the
	 * pendings number
	 */
	ptask = list_first_entry(&rtp->ready_q, ctask_t, link);
	__list_del(ptask->link.prev, ptask->link.next);
	OSPX_pthread_mutex_unlock(&rtp->core->mut);
	
	__cpool_rt_gettask_post(rtp, self, ptask);
	return 1;
}

int
cpool_rt_core_pri_gettask(void *priv, thread_t *self)
{
	ctask_t *ptask; 
	cpool_rt_t *rtp = priv;
	
	/**
	 * We schedule the dispatching tasks firstly 
	 */
	OSPX_pthread_mutex_lock(&rtp->core->mut);
	if (rtp->core->n_qdispatchs) {
		__cpool_com_get_dispatch_taskl(rtp->core, self, rtp->core->max_tasks_qdispatching);
		OSPX_pthread_mutex_unlock(&rtp->core->mut);
		self->task_type = TASK_TYPE_DISPATCHED;
		return 1;
	}
	
	/**
	 * If there are none active tasks to be retreived by us,
	 * we return 0. 
	 */
	 if (rtp->core->paused || !rtp->core->npendings)
	 	return 0;
	 -- rtp->core->npendings;

	/**
	 * Pop up a task from the task queue 
	 */
	ptask = __cpool_com_priq_pop(&rtp->c);
	OSPX_pthread_mutex_unlock(&rtp->core->mut);
	
	__cpool_rt_gettask_post(rtp, self, ptask);
	return 1;
}

int
cpool_rt_core_dynamic_pri_gettask(void *priv, thread_t *self)
{
	ctask_t *ptask; 
	cpool_rt_t *rtp = priv;
	
	/**
	 * We schedule the dispatching tasks firstly 
	 */
	OSPX_pthread_mutex_lock(&rtp->core->mut);
	if (rtp->core->n_qdispatchs) 
		return __cpool_com_get_dispatch_taskl2(rtp->core, self, rtp->core->max_tasks_qdispatching);
	
	/**
	 * If there are none active tasks to be retreived by us,
	 * we return 0.
	 */
	if (rtp->core->paused || !rtp->core->npendings) 
		return 0;
	-- rtp->core->npendings;
	
	/**
	 * Pop up a task from the task queue 
	 */
	ptask = __cpool_com_priq_pop(&rtp->c);
	/**
	 * We set the current task here for the debug messages
	 */
#ifndef NDEBUG
	__curtask = TASK_CAST_CORE(ptask);
#endif

	/**
	 * Notify the Core that we are ready now
	 */
	cpool_core_thread_status_changel(rtp->core, self, THREAD_STAT_RUN);
	OSPX_pthread_mutex_unlock(&rtp->core->mut);
	
	__cpool_rt_gettask_post(rtp, self, ptask);
	return 1;
}

void 
cpool_rt_core_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons)
{	
	if (likely(!(eTASK_STAT_F_WPENDING & TASK_CAST_FAC(ptask)->f_stat))) 
		cpool_core_objs_local_store(self, ptask);
	else
		__cpool_rt_task_queue(priv, TASK_CAST_FAC(ptask), 0);
}

void 
cpool_rt_core_pri_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons)
{
	if (likely(!(eTASK_STAT_F_WPENDING & TASK_CAST_FAC(ptask)->f_stat))) 
		cpool_core_objs_local_store(self, ptask);
	else {
		__cpool_com_task_nice_adjust(TASK_CAST_FAC(ptask));
		__cpool_rt_pri_task_queue(priv, TASK_CAST_FAC(ptask), 0);
	} 
}

void 
cpool_rt_core_dynamic_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons)
{
	/**
	 * We should call \@cpool_core_thread_status_change to update the Core status
	 * if the pool supports creating and destroying the threads dynamically
	 */
	assert (eFUNC_F_DYNAMIC_THREADS & ((cpool_rt_t *)priv)->lflags);
	
	if (list_empty(&self->dispatch_q))
		cpool_core_thread_status_change(((cpool_rt_t *)priv)->core, self, THREAD_STAT_COMPLETE);
	
	if (likely(!(eTASK_STAT_F_WPENDING & TASK_CAST_FAC(ptask)->f_stat)))
		cpool_core_objs_local_store(self, ptask);
	else
		__cpool_rt_task_queue(priv, TASK_CAST_FAC(ptask), 0);
}

void 
cpool_rt_core_dynamic_pri_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons)
{
	/**
	 * We should call \@cpool_core_thread_status_change to update the Core status
	 * if the pool supports creating and destroying the threads dynamically
	 */
	assert (eFUNC_F_DYNAMIC_THREADS & ((cpool_rt_t *)priv)->lflags);
	
	if (list_empty(&self->dispatch_q))
		cpool_core_thread_status_change(((cpool_rt_t *)priv)->core, self, THREAD_STAT_COMPLETE);
	
	if (likely(!(eTASK_STAT_F_WPENDING & TASK_CAST_FAC(ptask)->f_stat)))
		cpool_core_objs_local_store(self, ptask);
	else {
		__cpool_com_task_nice_adjust(TASK_CAST_FAC(ptask));
		__cpool_rt_pri_task_queue(priv, TASK_CAST_FAC(ptask), 0);
	}
}

