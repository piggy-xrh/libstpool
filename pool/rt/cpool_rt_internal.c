/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_com_internal.h"
#include "cpool_rt_internal.h"
#include "cpool_rt_method.h"

struct cpool_stat *
__cpool_rt_stat_conv(cpool_rt_t *rtp, struct cpool_core_stat *src, struct cpool_stat *dst)
{
	dst->desc = src->desc;
	dst->created = src->start;
	dst->acttimeo = src->acttimeo;
	dst->randtimeo = src->randtimeo;
	dst->ref = src->user_ref;
	dst->priq_num = rtp->priq_num;
	dst->suspended = src->paused;
	dst->maxthreads = src->max;
	dst->minthreads = src->min;
	dst->curthreads = src->n_qths;
	if (rtp->lflags & eFUNC_F_DYNAMIC_THREADS) 
		dst->curthreads_active = src->nths_running;
	else 
		dst->curthreads_active = src->n_qths - src->n_qths_waked - src->n_qths_wait;
	dst->curthreads_dying  = src->nths_dying;
	dst->threads_peak = src->nths_peak;
	dst->tasks_peak = -1;
	dst->tasks_added = -1;
	dst->tasks_processed = -1;
	dst->tasks_removed = -1;
	dst->curtasks_pending = src->n_qpendings;
	dst->curtasks_removing += src->n_qdispatchs;

	if (dst->curthreads_active < 0)
		dst->curthreads_active = 0;
	dst->curtasks_scheduling = dst->curthreads_active;
	
	return dst;
}

void
__cpool_rt_task_dispatch(cpool_rt_t *rtp, struct list_head *rmq, int dispatched_bypool)
{
	int  n;
	long task_counter = 0;
	ctask_t *ptask, *nptask;
	SMLINK_Q_HEAD(qcache);
	LIST_HEAD(q_null);
	
	/**
	 * Cut the dispatch queue into two 
	 */
	n =__cpool_com_get_err_handler_q(rmq, &q_null);
	if (!list_empty(&q_null)) 
		__cpool_com_list_to_smq(&q_null, &qcache);
	
	if (list_empty(rmq))
		goto out;

	if (dispatched_bypool) {
		OSPX_pthread_mutex_lock(&rtp->core->mut);	
		/**
		 * We just remove all tasks into the dispatching queue of
		 * the rtp->core if the user does not want to procces the error
		 * handlers directly.
		 */
		rtp->core->n_qdispatchs += n;
		list_splice(rmq, &rtp->core->dispatch_q);
		
		/**
		 * We notify the rtp->core to schedule the error handlers
		 * if it is necessary
		 */ 
		if (cpool_core_need_ensure_servicesl(rtp->core))
			cpool_core_ensure_servicesl(rtp->core, NULL);
		OSPX_pthread_mutex_unlock(&rtp->core->mut);
		/**
		 * Decrease the task counter
		 */
		OSPX_interlocked_add(&rtp->tsks_held_by_dispatcher, (long)-n);

	} else {
		list_for_each_entry_safe(ptask, nptask, rmq, ctask_t, link) {
			assert (ptask->task_err_handler); 
			
			/**
			 * Reset its status and run the error handler for the rt
			 */
			ptask->f_stat = (eTASK_STAT_F_DISPATCHING|eTASK_STAT_F_SCHEDULING);
			ptask->task_err_handler(ptask, cpool_rt_core_err_reasons(TASK_CAST_CORE(ptask)));
		
			/**
			 * We deliver the task into the queue if it is requested to be rescheduled again
			 */
			if (eTASK_STAT_F_WPENDING & ptask->f_stat) {
				if (rtp->lflags & eFUNC_F_PRIORITY)
					__cpool_rt_pri_task_queue(rtp, ptask, 1);
				else
					__cpool_rt_task_queue(rtp, ptask, 1);
				
				++ task_counter;
			} else
				smlink_q_push(&qcache, ptask);
		}

		if (task_counter)
			OSPX_interlocked_add(&rtp->tsks_held_by_dispatcher, -task_counter);
	}

out:
	/**
	 * Free all temple tasks
	 */
	if (!smlink_q_empty(&qcache)) {
		task_counter = smlink_q_size(&qcache);

		smcache_add_q(rtp->core->cache_task, &qcache);
		/**
		 * Decrease the task counter
		 */
		OSPX_interlocked_add(&rtp->tsks_held_by_dispatcher, -task_counter);
		assert (rtp->tsks_held_by_dispatcher >= 0);
		cpool_core_try_GC(rtp->core);
	}
	
	/**
	 * Check the pool status
	 */
	if (!rtp->core->npendings) 
		__cpool_rt_try_wakeup(rtp);
}
