#ifndef __CPOOL_GP_INTERNAL_H__
#define __CPOOL_GP_INTERNAL_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_core.h"
#include "cpool_com_internal.h"
#include "cpool_com_priq.h"
#include "cpool_gp_entry.h"
#include "cpool_gp_wait_internal.h"

static inline void
__cpool_gp_task_pri_queuel(cpool_gp_t *gpool, ctask_entry_t *entry, ctask_trace_t *ptask)
{
	ptask->seq = ++ gpool->seq;
	ptask->f_stat = eTASK_STAT_F_WAITING;
	
	/**
	 * Add the task into the priority queue
	 */
	__cpool_com_priq_insert(&entry->c, (ctask_t *)ptask);
	entry->top = TASK_CAST_TRACE(__cpool_com_priq_top(&entry->c));
	++ entry->npendings;
}


static inline void
__cpool_gp_entry_consumer_notifyl(cpool_gp_t *gpool, ctask_entry_t *entry)
{
	assert (!entry->paused);
	
	++ gpool->npendings;
	/**
	 * Should we update the effective tasks number ? 
	 */
	if (likely(entry->receive_benifits) ||
		((entry->nrunnings + entry->npendings_eff) < entry->limit_tasks && 
		gpool->npendings > entry->npendings_eff)) {
		++ gpool->core->npendings;
		
		if (unlikely(!entry->receive_benifits)) 
			++ entry->npendings_eff;

		if (cpool_core_need_ensure_servicesl(gpool->core) && !gpool->core->paused)
			cpool_core_ensure_servicesl(gpool->core, NULL);	
	}
		
	/**
	 * Active the entry if it is not paused 
	 */
	if (1 == entry->npendings)
		__cpool_gp_entry_activel(gpool, entry);	

	/**
	 * Update the statics report 
	 */
	if (gpool->ntasks_peak < gpool->npendings) 
		gpool->ntasks_peak = gpool->npendings;	

	/**
	 * There must be a few active threads existing in the pool
	 * if the pool is not in suspended status 
	 */
	assert (gpool->core->paused || gpool->core->n_qths_wait <= gpool->core->n_qths);
}

static inline ctask_trace_t *
__cpool_gp_get_pending_task(cpool_gp_t *gpool)
{
	int idx, benifits = 0;
	ctask_entry_t *entry;
	ctask_trace_t *ptask = NULL;
	
	assert (gpool->active_idx >= 0 &&
	        gpool->active_idx >= gpool->nactives_ok &&
			gpool->active_idx < gpool->num);
	
	assert (gpool->npendings > 0); 
reget:
	for (idx=0; idx<gpool->active_idx; idx++) {
		entry = gpool->actentry[idx];
		
		assert (!entry->paused); 
		
		/**
		 * We skip it if this entry has none active tasks
		 */
		if (likely(entry->npendings)) {
			assert (entry->receive_benifits || (entry->npendings_eff >= 0 &&
					entry->npendings_eff <= entry->npendings));

			if (entry->nrunnings < entry->limit_tasks || 
				(benifits && entry->receive_benifits)) {
				
				assert (SLOT_F_ACTIVE & entry->lflags && 
					entry->top->priq < entry->c.priq_num);
				
				assert (!benifits || entry->receive_benifits ||
						entry->npendings_eff > 1);

				if (!ptask) {
					ptask = entry->top;
					continue;
				}
				
				/**
				 * We select it if the next task has a highter priority 
				 */
				if (likely(entry->top->pri == ptask->pri)) {
					if (entry->top->seq > ptask->seq) {
						if (ep_TOP == entry->top->pri_policy) 
							ptask = entry->top;
					
					} else if (likely(ep_BACK == ptask->pri_policy))
						ptask = entry->top;

				} else if (entry->top->pri > ptask->pri)
					ptask = entry->top;
			}
		}
	}

	if (ptask) { 
		entry = gpool->entry + ptask->gid;
		
		assert (ptask->f_stat & eTASK_STAT_F_WAITING);
		assert (TASK_CAST_FAC(ptask) == __cpool_com_priq_top(&entry->c));
		
		/**
		 * Remove the task from the pending queue 
		 */
		__cpool_com_priq_pop(&entry->c);
	
#ifndef NDEBUG
		MSG_log(M_SCHEDULER, LOG_TRACE,
				"Select a task(%s/%d) from group{%s}/%2d npendings:%d RUNNING(%d,%d)\n",
				ptask->task_desc, ptask->seq, entry->name, entry->id, entry->npendings - 1,
				entry->limit_tasks, entry->nrunnings);
#endif
		/** 
		 * Update the entry's records 
		 */
		if (__cpool_com_priq_empty(&entry->c)) {
			assert (entry->npendings == 1);

			entry->npendings = 0;
			if (unlikely(!entry->receive_benifits)) {
				assert (entry->npendings_eff == 1);
				entry->npendings_eff = 0;
			}

			-- gpool->core->npendings;
			-- gpool->npendings;

			__cpool_gp_entry_inactivel(gpool, entry);
			assert (gpool->core->npendings >= 0 && gpool->npendings >= gpool->core->npendings);
			
			return ptask;
		} 
		entry->top = TASK_CAST_TRACE(__cpool_com_priq_top(&entry->c));
		assert (entry->npendings > 0);
		assert (gpool->core->npendings >= 0 && gpool->npendings >= gpool->core->npendings);
		
		-- entry->npendings;
		
		/**
		 * Try to update the effective tasks number 
		 */
		if (unlikely(!entry->receive_benifits))
			-- entry->npendings_eff;
		
		assert (!benifits || entry->receive_benifits);
		-- gpool->core->npendings;
		-- gpool->npendings;
		
	} else if (gpool->core->npendings) {
		assert (gpool->nactives_ok > 0 && !benifits);
		benifits = 1;
		goto reget;
	}
	
	return ptask;
}

static inline void
__cpool_gp_task_removel(cpool_gp_t *gpool, ctask_entry_t *entry, ctask_trace_t *ptask, struct list_head *q) 
{
	assert (eTASK_STAT_F_WAITING & ptask->f_stat);
	
	/**
	 * Move the task from the ready queue
	 */
	__cpool_com_priq_erase(&entry->c, TASK_CAST_FAC(ptask));
	
	if (!ptask->task_err_handler) {
		/**
		 * Move it from the trace queue
		 */
		list_del(&ptask->trace_link);
		-- gpool->n_qtraces;
		-- entry->n_qtraces;
		++ entry->ntasks_processed;
		/**
		 * Notify user
		 */ 
		__cpool_gp_w_wakeup_taskl(gpool, entry, ptask);
		if (eTASK_VM_F_LOCAL_CACHE & ptask->f_vmflags && !ptask->ref) 
			smcache_addl_dir(gpool->core->cache_task, ptask);
		else
			ptask->f_stat = 0;
		
	} else {
		/**
		 * Reset its status
		 */
		ptask->f_stat = eTASK_STAT_F_DISPATCHING;
		if (likely(!q)) {
			list_add_tail(&ptask->link, &gpool->core->dispatch_q);
			++ gpool->core->n_qdispatchs;
		
		} else {
			list_add_tail(&ptask->link, q);
			++ gpool->ndispatchings;
		}
		++ entry->ndispatchings;
	}
	/**
	 * Update the pendings number
	 */
	-- entry->npendings;
	if (!entry->paused) {
		-- gpool->npendings;

		if (likely(entry->receive_benifits))
			-- gpool->core->npendings;

		else if (entry->npendings_eff > entry->npendings) {
			-- gpool->core->npendings;
			-- entry->npendings_eff;
		}

		if (0 == entry->npendings)
			__cpool_gp_entry_inactivel(gpool, entry);
		
		assert (gpool->npendings >= 0 && entry->npendings_eff >= 0 &&
				gpool->core->npendings >= entry->npendings_eff &&
				gpool->n_qtraces >= gpool->ndispatchings + gpool->core->n_qdispatchs + gpool->npendings);
	}

	// Update the top entry
	if (!__cpool_com_priq_empty(&entry->c))
		entry->top = TASK_CAST_TRACE(__cpool_com_priq_top(&entry->c));
}

void __cpool_gp_task_dispatcher(cpool_gp_t *gpool, struct list_head *rmq);
long __cpool_gp_wcb_paused(ctask_t *ptask, void *opaque);
int  __cpool_gp_entry_wait_cbl(cpool_gp_t *gpool, ctask_entry_t *entry, long type, Visit_cb cb, void *arg, long ms);

#endif
