/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "ospx_errno.h"
#include "cpool_factory.h"
#include "cpool_core_struct.h"
#include "cpool_gp_method.h"
#include "cpool_gp_internal.h"
#include "cpool_gp_wait_internal.h"

int  
cpool_gp_core_ctor(void *priv)
{
	cpool_gp_t *gpool = priv;
	
	MSG_log(M_GROUP, LOG_INFO,
		"Creating group pool {\"%s\"/%p} ...\n",
		gpool->core->desc, priv);

	gpool->entry_idx_max = MAX_WAIT_ENTRY;
	
	if ((errno=OSPX_pthread_cond_init(&gpool->cond_task))) 
		return eERR_errno;

	if ((errno=OSPX_pthread_cond_init(&gpool->cond_task_entry))) 
		goto free_cond_task;
	
	if ((errno=OSPX_pthread_cond_init(&gpool->cond_ev))) 
		goto free_cond_entry;
	
	if ((errno=OSPX_pthread_cond_init(&gpool->cond_sync))) 
		goto free_cond_ev;
	
	/**
	 * Create the default entry
	 */
	if ((0 > cpool_gp_entry_create(gpool, "sys00", gpool->priq_num, 0))) { 
		errno = ENOMEM;
		goto free_cond_sync;
	}
	gpool->n = 4;
	INIT_LIST_HEAD(&gpool->wq);

	return 0;
	
free_cond_task:
	OSPX_pthread_cond_destroy(&gpool->cond_task);
free_cond_sync:
	OSPX_pthread_cond_destroy(&gpool->cond_sync);
free_cond_entry:
	OSPX_pthread_cond_destroy(&gpool->cond_task_entry);
free_cond_ev:
	OSPX_pthread_cond_destroy(&gpool->cond_ev);
	
	MSG_log(M_GROUP, LOG_ERR,
			"Failed to create the default group: %s\n",
			OSPX_sys_strerror(errno));
	
	return eERR_errno;
}

void 
cpool_gp_core_dtor(void *priv)
{
	int idx;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = priv;
	
	MSG_log(M_GROUP, LOG_INFO,
		"Destroying group pool {\"%s\"/%p} ...\n",
		gpool->core->desc, priv);
	
	/*
	 * Delete all entries
	 */
	for (idx=0; idx<gpool->num; idx++) {
		entry = gpool->entry + idx;

		if (!(SLOT_F_FREE & entry->lflags))
			cpool_gp_entry_delete(gpool, entry->id);

		assert (entry->lflags & SLOT_F_FREE);
		if (entry->c.priq)
			free(entry->c.priq);
	}

	if (gpool->entry) {
		free(gpool->entry);
		free(gpool->actentry);
	}

	OSPX_pthread_cond_destroy(&gpool->cond_sync);
	OSPX_pthread_cond_destroy(&gpool->cond_task);
	OSPX_pthread_cond_destroy(&gpool->cond_task_entry);
	OSPX_pthread_cond_destroy(&gpool->cond_ev);
}

void cpool_gp_core_notifyl(void *priv, eEvent_t events)
{
	int idx = 0;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = priv;

#ifndef NDEBUG
	MSG_log(M_GROUP, LOG_TRACE,
		"{\"%s\"/%p} Received events(%p) from Core\n",
		gpool->core->desc, priv, events);
#endif
	
	if (eEvent_F_thread & events) 
		__cpool_gp_entry_update_all_envl(priv);

	if (eEvent_F_destroying & events) {
		__cpool_gp_entry_notifyl(gpool, NULL, eTASK_VM_F_POOL_DESTROYING);
		

		/**
		 * We create service threads to dispatch the task in the background if 
		 * there are one more swapped tasks existing in the pool. 
		 */
		if (gpool->core->paused) 
			__cpool_gp_entry_mark_cbl(gpool, NULL, NULL, (void *)eTASK_VM_F_REMOVE_BYPOOL, NULL);
		
		for (; idx<gpool->num; idx++) {
			entry = gpool->entry + idx;
			
			if (IS_VALID_ENTRY(entry) && entry->paused)
				__cpool_gp_entry_mark_cbl(gpool, entry, NULL, (void *)eTASK_VM_F_REMOVE_BYPOOL, NULL);
		
			/**
			 * Wake up all throttle
			 */
			__cpool_gp_w_wakeup_entry_throttlel(entry);
		}
	} 

	if (eEvent_F_shutdown & events) {
		/**
		 * We can not make sure that all WAIT functions have returned 
	 	 * completely, so we wake them up 
		 */
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
		cpool_gp_w_wakeup(gpool, WAIT_TYPE_ALL, -1);
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		/**
		 * Synchronize the env 
		 */
		for (;gpool->tsk_wref || gpool->ev_wref;) {
			MSG_log(M_GROUP, LOG_INFO,
					"{\"%s\"/%p} tsk_wref(%d) ev_wref(%d) Synchronizing ...\n",
					gpool->core->desc, gpool, gpool->tsk_wref, gpool->ev_wref);
			OSPX_pthread_cond_wait(&gpool->cond_sync, &gpool->core->mut);
		}
	} 
}

long 
cpool_gp_core_err_reasons(basic_task_t *ptask)
{
	long e = 0;

	assert (TASK_CAST_FAC(ptask)->f_vmflags & eTASK_VM_F_REMOVE_FLAGS);

	if (TASK_CAST_FAC(ptask)->f_vmflags & eTASK_VM_F_POOL_DESTROYING)
		e |= eErr_pool_destroying;
	
	if (TASK_CAST_FAC(ptask)->f_vmflags & eTASK_VM_F_GROUP_DESTROYING)
		e |= eErr_group_destroying;
	
	if (TASK_CAST_FAC(ptask)->f_vmflags & eTASK_VM_F_DRAINED)
		e |= eErr_group_overloaded;
	/**
	 * We remove the flags
	 */
	TASK_CAST_FAC(ptask)->f_vmflags &= ~(eTASK_VM_F_POOL_DESTROYING|eTASK_VM_F_GROUP_DESTROYING|eTASK_VM_F_DRAINED);
	if (!e)
		return eErr_removed_byuser;
	
	return e;
}

int  
cpool_gp_core_gettask(void *priv, thread_t *self)
{
	ctask_trace_t *ptask; 
	cpool_gp_t *gpool = priv;
	
	self->task_type = TASK_TYPE_NORMAL;
	/**
	 * We schedule the dispatching tasks firstly 
	 */
	OSPX_pthread_mutex_lock(&gpool->core->mut);
	if (gpool->core->n_qdispatchs) {
		gpool->ndispatchings += __cpool_com_get_dispatch_taskl3(gpool->core, self, gpool->n);
	#ifndef NDEBUG	
		self->task_type = TASK_TYPE_DISPATCHED;
	#endif	
		cpool_core_thread_status_changel(gpool->core, self, THREAD_STAT_RUN);
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	
		/**
		 * Set the trace env 
		 */
		list_for_each_entry(ptask, &self->dispatch_q, ctask_trace_t, link) {
			TASK_CAST_TRACE(ptask)->thread = self;
			TASK_CAST_TRACE(ptask)->pdetached = (uint16_t*)&self->trace_args;
		}
		self->task_type = TASK_TYPE_DISPATCHED;

		return 1;
	}

	/**
	 * If there are none active tasks to be retreived by us,
	 * we return 0.
	 */
	if (gpool->core->paused || !gpool->core->npendings) 
		return 0;
	
	ptask = __cpool_gp_get_pending_task(gpool);
	ptask->f_stat = eTASK_STAT_F_SCHEDULING;
#ifndef NDEBUG
	self->current_task = TASK_CAST_CORE(ptask);
#endif
	++ gpool->entry[ptask->gid].nrunnings;
	cpool_core_thread_status_changel(gpool->core, self, THREAD_STAT_RUN);
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
	
	/**
	 * Set the trace env for the task before our's executing it
	 */
	if (ptask) {
		ptask->thread = self;
		ptask->pdetached = (uint16_t *)&self->trace_args;
		self->current_task = TASK_CAST_CORE(ptask);
		
		return 1;
	}

	return 0;
}

void 
cpool_gp_core_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons)
{
	int reschedule = 1;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = priv;
	ctask_trace_t *ptask0 = TASK_CAST_TRACE(ptask);

	assert (self->task_type != TASK_TYPE_GC);
	/**
	 * Has the \@task_detach been called by user ?
	 */
	if (self->trace_args) {
		self->trace_args = 0;
		return;
	}
	assert (ptask0->thread == self && __curtask == ptask);
	/**
	 * We reset the attached thread 
	 */
	ptask0->thread = NULL;
	__cpool_com_task_nice_adjust(TASK_CAST_FAC(ptask));
	
	OSPX_pthread_mutex_lock(&gpool->core->mut);	
	entry = gpool->entry + ptask0->gid;
	/**
	 * We decrease the @ndispatchings if the task has been 
	 * requested to be removed from the pending queue 
	 */
	if (likely(TASK_TYPE_NORMAL == self->task_type)) {
		-- entry->nrunnings;
		/**
		 * If the task is scheduled by our core, we sholud try to update 
		 * the core's effective tasks number 
		 */
		if (!entry->receive_benifits && !entry->paused && 
			(entry->npendings_eff + entry->nrunnings) < entry->limit_tasks &&
			entry->npendings_eff < entry->npendings) {
			++ gpool->core->npendings;
			++ entry->npendings_eff;
		}
	} else {
		assert (entry->ndispatchings > 0 && gpool->ndispatchings > 0);
		-- entry->ndispatchings;
		-- gpool->ndispatchings;
	} 
	assert (gpool->core->npendings >= 0 && gpool->core->n_qdispatchs >= 0 &&
			gpool->n_qtraces >= gpool->ndispatchings + gpool->core->n_qdispatchs + gpool->npendings);
	
	if (likely(!(eTASK_STAT_F_WPENDING & ptask0->f_stat))) {
		reschedule = 0;
		
		assert (entry->n_qtraces > 0 && gpool->n_qtraces >= entry->n_qtraces);

		/**
		 * Remove the trace record and reset the status of the task 
		 */
		list_del(&ptask0->trace_link);
		-- gpool->n_qtraces;
		-- entry->n_qtraces;
		++ entry->ntasks_processed;
		
		/**
		 * Wake up the waiters who is waiting on the task
		 */
		__cpool_gp_w_wakeup_taskl(gpool, entry, ptask0);
	
		/**
		 * Free the temple task object if it is useless 
		 */
		if (likely((ptask0->f_vmflags & eTASK_VM_F_LOCAL_CACHE) && !ptask0->ref)) 
			smcache_addl_dir(gpool->core->cache_task, ptask0);
		else 
			ptask0->f_stat = 0;
	}
	if (list_empty(&self->dispatch_q))
		cpool_core_thread_status_changel(gpool->core, self, THREAD_STAT_COMPLETE);		
			
	/**
	 * We deliver the task into the pending queue if
	 * the user wants to reschedule it again 
	 */
	if (unlikely(reschedule)) {
		__cpool_gp_task_pri_queuel(gpool, entry, ptask0);	
		if (!entry->paused)
			__cpool_gp_entry_consumer_notifyl(gpool, entry);
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);				
}

