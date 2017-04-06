/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_gp_internal.h"
#include "cpool_gp_method.h"

long
__cpool_gp_wcb_paused(ctask_t *ptask, void *opaque)
{
	int *ids = opaque, idx;

	if (ptask->f_stat & (eTASK_STAT_F_DISPATCHING|eTASK_STAT_F_SCHEDULING)) {
		if (!opaque)
			return 1;
		
		/**
		 * Check whether the group id of the task is in our sets
		 */
		for (idx=1; idx<ids[0]; idx++) {
			if (ids[idx] == ptask->gid)
				return 1;
		}
	}

	return 0;
}

void 
__cpool_gp_task_dispatcher(cpool_gp_t *gpool, struct list_head *rmq)
{
	uint16_t detached = 0;
	ctask_t *p, *n;
	ctask_trace_t *ptask;
	ctask_entry_t *entry;

	assert (rmq && !list_empty(rmq));

	list_for_each_entry_safe(p, n, rmq, ctask_t, link) {
		ptask = TASK_CAST_TRACE(p);
		
		assert (ptask->task_err_handler);

		ptask->pdetached = &detached;
		ptask->task_err_handler(p, cpool_gp_core_err_reasons(TASK_CAST_CORE(p)));
		if (detached) {
			detached = 0;
			continue;
		}

		OSPX_pthread_mutex_lock(&gpool->core->mut);
		entry = gpool->entry + ptask->gid;
		
		assert (gpool->ndispatchings > 0 && entry->ndispatchings > 0);
		-- gpool->ndispatchings;
		-- entry->ndispatchings;

		if (likely(!(eTASK_STAT_F_WPENDING & ptask->f_stat))) {
			/**
			 * Remove the trace record and reset the status of the task 
			 */
			list_del(&ptask->trace_link);
			-- gpool->n_qtraces;
			-- entry->n_qtraces;
			++ entry->ntasks_processed;
			
			/**
			 * Wake up the waiters who is waiting on the task
			 */
			__cpool_gp_w_wakeup_taskl(gpool, entry, ptask);
			/**
			 * Free the temple task object if it is useless 
			 */
			if (likely((ptask->f_vmflags & eTASK_VM_F_LOCAL_CACHE) && !ptask->ref)) 
				smcache_addl_dir(gpool->core->cache_task, ptask);
			else 
				ptask->f_stat = 0;
			
		} else {
			__cpool_gp_task_pri_queuel(gpool, entry, ptask);	
			if (!entry->paused)
				__cpool_gp_entry_consumer_notifyl(gpool, entry);
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}
	
	/**
	 * we try to wake up a threads to do the GC to avoid holding so many memories 
	 */
	cpool_core_try_GC(gpool->core);
}


