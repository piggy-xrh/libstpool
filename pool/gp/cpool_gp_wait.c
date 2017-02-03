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
#include "timer.h"
#include "cpool_core.h"
#include "cpool_wait.h"
#include "cpool_gp_wait.h"
#include "cpool_gp_wait_internal.h"
#include "cpool_gp_entry.h"

typedef struct cpool_gp_wrequester cpool_gp_wrequester_t;

struct cpool_gp_wrequester {
	/**
	 * Global requesters
	 */
	struct WWAKE_requester glbr;
	
	/**
	 * Local requesters 
	 */
	struct list_head llink;
	long type;
	int  id;
	cpool_gp_t *gpool;
};

static void 
__cpool_gp_event_invoke(struct WWAKE_requester *r)
{
	cpool_gp_wrequester_t *wr = (cpool_gp_wrequester_t *)r;
	ctask_entry_t *entry = wr->gpool->entry + wr->id;
	
	if (r->b_interrupted)
		return;
	
	r->b_interrupted = 1;
	
	if (wr->type & WAIT_CLASS_ENTRY) 
		if (wr->type & WAIT_TYPE_THROTTLE)
			__cpool_gp_w_wakeup_entry_throttlel(entry);
		else
			__cpool_gp_w_wakeup_entry_taskl(entry);
	else {
		assert (wr->type & WAIT_CLASS_POOL);
		
		if (wr->type & WAIT_TYPE_THROTTLE)
			__cpool_gp_w_wakeup_pool_throttlel(wr->gpool);
		else
			__cpool_gp_w_wakeup_pool_taskl(wr->gpool);
	}
}

#define DECLARE_WREQUEST(r, wtype, pool, entry_id) \
	struct cpool_gp_wrequester r = { \
		{ \
			WWAKE_id(), \
			0, \
			{0, 0}, \
			__cpool_gp_event_invoke, \
			NULL \
		}, \
		{0, 0}, \
		wtype, \
		entry_id, \
		pool\
	} 

/**
 * Push our request into the wait queue and wait for the notifications from the pool 
 */
int 
__cpool_gp_w_waitl(cpool_gp_t *gpool, long type, int id, void *arg, long ms)
{
	int e, idx, ntsks = id, sync = 0;
	ctask_trace_t **ptasks = arg, *ptask = type & WAIT_TYPE_TASK ? arg : NULL;
	ctask_entry_t *entry  = NULL;
	OSPX_pthread_cond_t *cond = NULL;

	/**
	 * Stack varariables for requests type: WAIT_CLASS_POOL|WAIT_TYPE_TASK_ANY2 
	 */
	int idx0, ids[100] = {0}, ids_idx = 0;

	if (ms == 0)
		return eERR_TIMEDOUT;

	assert (!(type & WAIT_CLASS_POOL && type & WAIT_CLASS_ENTRY));
	assert (type & (~(WAIT_CLASS_POOL|WAIT_CLASS_ENTRY)));
	
	/**
	 * NOTE: Eventually, this type request(Walk_cb) will be mapped to the entry 
	 */
	assert (!(type & WAIT_CLASS_POOL && type & WAIT_TYPE_TASK));
	
	if (type & WAIT_CLASS_POOL) {
		if (type & WAIT_TYPE_THROTTLE) {
			++ gpool->ev_wref;
			gpool->ev_need_notify = 1;
			cond = &gpool->cond_ev;

		} else if (type & (WAIT_TYPE_TASK_ANY|WAIT_TYPE_TASK_ALL)) {
			gpool->tsk_need_notify = 1;
			if (WAIT_TYPE_TASK_ANY & type)
				gpool->tsk_any_wait = 1;
			cond = &gpool->cond_task;
		
		} else if (type & WAIT_TYPE_TASK_ANY2) {
			for (idx=0; idx<ntsks; idx++) {
				/**
				 * Skip the null entry 
				 */
				if (!ptasks[idx])  
					continue;
				
				/**
				 * Is the task free now ? 
				 */
				if (!ptasks[idx]->f_stat)
					return 0;

				/**
				 * If there are any task who does not belong to our pool, we 
				 * return an error imediately 
				 */
				if (ptasks[idx]->pool->ins != (void *)gpool)
					return eTASK_ERR_DESTINATION;
	
				/**
				 * Find the entry according to the task's group id 
				 */
				if (SLOT_F_FREE & gpool->entry[id].lflags) 
					return eERR_GROUP_NOT_FOUND;
				entry = gpool->entry + ptasks[idx]->gid;
				/**
				 * The caller must be sure that the task objects wont be destroyed
				 * before our returing from this function if its reference is not 
				 * zero 
				 */
				assert (ptasks[idx]->ref || !(eTASK_VM_F_CACHE & ptasks[idx]->f_vmflags));
				
				++ ptasks[idx]->ref;
				ptasks[idx]->f_global_wait = 1;
				
				assert (ids_idx < sizeof(ids)/sizeof(*ids));

				/**
				 * Increase the entry's reference and push its id into the array 
				 */
				for (idx0=0; idx0<ids_idx && ids[idx0] != ptasks[idx]->gid; idx0++);
				if (idx0 == ids_idx) {
					++ entry->tsk_wref;
					entry->tsk_need_notify = 1; 
					ids[ids_idx++] = ptasks[idx]->gid;
				}
				
				/**
				 * Push the task into the waiting queue, @WWAKE_task will be responsible
				 * for clearing it 
				 */					
				if (gpool->entry_idx + 1 <= gpool->entry_idx_max) {
					gpool->glbentry[gpool->entry_idx ++] = ptasks[idx];
					if (gpool->entry_idx == gpool->entry_idx_max)
						MSG_log(M_WAIT, LOG_WARN,
								"{\"%s\"/%p} WAIT entry is full. entry_num(%d)\n",
								gpool->core->desc, gpool, gpool->entry_idx_max);
				}
			}
			
			/**
			 * We wait for the tasks on the global condition var 
			 */
			gpool->tsk_need_notify = 1;
			cond = &gpool->cond_task;
		}

	} else if (type & WAIT_CLASS_ENTRY) {
		/**
		 * Find the entry according to the task's group id 
		 */
		if (SLOT_F_FREE & gpool->entry[id].lflags)
			return eERR_GROUP_NOT_FOUND;
		entry = gpool->entry + id;
		
		
		if (type & WAIT_TYPE_THROTTLE) {
			++ entry->ev_wref;
			*entry->ev_need_notify = 1;
			cond = entry->cond_ev;
		
		} else if (type & (WAIT_TYPE_TASK|WAIT_TYPE_TASK_ANY|WAIT_TYPE_TASK_ALL)) {
			entry->tsk_need_notify = 1;
			++ entry->tsk_wref;
			if (WAIT_TYPE_TASK_ANY & type)
				entry->tsk_any_wait = 1;
			cond = entry->cond_task;
		
			/**
			 * To make sure that the tasks object is always valid, we increase the 
			 * tasks' references before our's starting waiting for them 
			 */
			if (type & WAIT_TYPE_TASK)
				++ ptask->ref;
			
			/**
		 	 * Increase the reference of the entry 
		 	 */
			ids[ids_idx ++] = entry->id;
		}
	} 

	if (!cond) {
		MSG_log(M_WAIT, LOG_ERR,
			"Invalid WAIT request: pool(%s/%p) type(%p) id(%d).\n",
			gpool->core->desc, gpool, type, id);
		return eERR_NSUPPORT;
	}

	if (type & (WAIT_TYPE_TASK|WAIT_TYPE_TASK_ALL|WAIT_TYPE_TASK_ANY|WAIT_TYPE_TASK_ANY2))
		++ gpool->tsk_wref;
				
	{
		DECLARE_WREQUEST(r, type, gpool, id);
		
		/**
		 * Add the request into the global queue 
		 */
		WWAKE_add(&r.glbr);

		list_add_tail(&r.llink, &gpool->wq); 
		if (ms > 0)
			e = OSPX_pthread_cond_timedwait(cond, &gpool->core->mut, ms);
		else
			e = OSPX_pthread_cond_wait(cond, &gpool->core->mut);	
		list_del(&r.llink);
		
		WWAKE_erase_direct(&r.glbr);

		/**
		 * Analysic the WAIT result 
		 */
		if (r.glbr.b_interrupted)
			e = eERR_INTERRUPTED;
		
		else if (ETIMEDOUT == e)
			e = eERR_TIMEDOUT;

		else if (e) {
			MSG_log(M_WAIT, LOG_ERR,
				"***Err: cond_wait:%s\n",
				OSPX_sys_strerror(e));
			e = eERR_OTHER;
		}
	}
	if (type & (WAIT_TYPE_TASK|WAIT_TYPE_TASK_ALL|WAIT_TYPE_TASK_ANY|WAIT_TYPE_TASK_ANY2))
		-- gpool->tsk_wref;
		
	/**
	 * WARNING: If the tasks come from different entries share the same condition var, 
	 * once that one task from one entry has been done, it'll invoke the condition, and 
	 * the variable @tsk_need_notify of other entires may leave unchanged 
	 */
	if (type & WAIT_CLASS_POOL) {
		
		if (type & WAIT_TYPE_THROTTLE) 
			-- gpool->ev_wref;

		else if (type & WAIT_TYPE_TASK_ANY2) {
			for (idx=0; idx<ntsks; idx++) {
				if (!ptasks[idx]) 
					continue;
					
				ptask = ptasks[idx];
				
				/**
				 * If there are free tasks, we reset the error code 
				 */
				if (!ptask->f_stat)
					e = 0;

				assert (ptask->pool->ins == (void *)gpool && ptask->ref > 0);
				
				/**
				 * We decrease the references of the task if it belongs to our pool. 
				 */
				sync += (0 == -- ptask->ref && ptask->f_vmflags & eTASK_VM_F_DETACHED);
			}
		}
	
	} else if (type & WAIT_CLASS_ENTRY) {
		entry = gpool->entry + id;

		if (type & WAIT_TYPE_THROTTLE) 
			-- entry->ev_wref;
	
		else if (type & WAIT_TYPE_TASK) {
			assert (ptask->ref > 0);
			/**
			 * If the task is detaching, we should give it a notification
			 * that it is free now 
			 */	
			 sync += (0 == -- ptask->ref && ptask->f_vmflags & eTASK_VM_F_DETACHED);
		}
	}

	for (idx0=0; idx0<ids_idx; idx0++) {
		assert (ids[idx0] >= 0 && ids[idx0] < gpool->num);

		entry = gpool->entry + ids[idx0];
		
		assert (entry->tsk_wref > 0);
		-- entry->tsk_wref;
	}
	
	/**
	 * If the entry is being destroyed, we should give it a notification
	 * to tell the library that the entry can be destroyed safely now 
	 */
	if (type & WAIT_CLASS_ENTRY)
		sync += !entry->tsk_wref && !entry->ev_wref && SLOT_F_DESTROYING & entry->lflags;
	
	/**
	 * If the pool is being destroyed, we give it a notification to 
	 * tell the library that the pool can be destroyed safely now
	 */
	else if (type & WAIT_CLASS_POOL)
		sync += !gpool->ev_wref && !gpool->tsk_wref && cpool_core_statusl(gpool->core) & CORE_F_destroying;

	if (sync) 
		OSPX_pthread_cond_broadcast(&gpool->cond_sync);
	
	return e;
}

static void
__cpool_gp_w_invoke_r(cpool_gp_wrequester_t *r)
{
	int idx = 0;
	cpool_gp_t *gpool = r->gpool;

	if (r->glbr.b_interrupted)
		return;
	
	r->glbr.b_interrupted = 1;
	
	if (r->type & WAIT_CLASS_ENTRY) {
		ctask_entry_t *entry = NULL;
		
		/**
		 * Find the entry according to the task's group id 
		 */
		for (idx=0; idx<gpool->num; idx++) {
			if (SLOT_F_FREE & gpool->entry[idx].lflags)
				continue;

			if (gpool->entry[idx].id == r->id) {
				entry = gpool->entry + idx;
				break;
			}
		}
		assert (idx != gpool->num);

		if (r->type & WAIT_TYPE_THROTTLE)
			__cpool_gp_w_wakeup_entry_throttlel(entry);
		
		else if (r->type & (WAIT_TYPE_TASK|WAIT_TYPE_TASK_ANY|WAIT_TYPE_TASK_ALL)) 
			__cpool_gp_w_wakeup_entry_taskl(entry);

	} else if (r->type & WAIT_CLASS_POOL) {
		if (WAIT_TYPE_THROTTLE & r->type)
			__cpool_gp_w_wakeup_pool_throttlel(r->gpool);
		
		else if ((WAIT_TYPE_ALL|WAIT_TYPE_TASK_ANY|WAIT_TYPE_TASK_ANY2) & r->type) 
			__cpool_gp_w_wakeup_pool_taskl(r->gpool);
	}
}

int
cpool_gp_w_wakeup(cpool_gp_t *gpool, long type, int id)
{
	int n = 0;
	cpool_gp_wrequester_t *r, *nr;
	
	if (type == WAIT_TYPE_ALL) {
		MSG_log(M_WAIT, LOG_INFO,
			"{\"%s\"/%p} Wake up all waiters ...\n",
			gpool->core->desc, gpool);
	
	} else if (type & WAIT_CLASS_POOL && type & WAIT_TYPE_TASK) {
		MSG_log(M_WAIT, LOG_WARN,
				"{\"%s\"/%p} library can not process this request currently. type(%p) id(%d)\n",
				gpool->core->desc, gpool, type, id);
		return 0;
	}


#define MATCH(r, type)        (WAIT_TYPE_ALL == type)  
#define ENTRY_MATCH(r, type)  (type & WAIT_CLASS_ENTRY && r->id == id && \
							  (type & (WAIT_TYPE_ENTRY_ALL & ~WAIT_CLASS_ENTRY))) 
#define POOL_MATCH(r, type)   (type & WAIT_CLASS_POOL && type &  \
					 		  (WAIT_TYPE_POOL_ALL &~(WAIT_CLASS_POOL|WAIT_TYPE_TASK))) 

	/**
	 * We scan the requester queue to find the waiter who is 
	 * requested to return from the wait functions imediately 
	 */
	OSPX_pthread_mutex_lock(&gpool->core->mut);
	list_for_each_entry_safe(r, nr, &gpool->wq, cpool_gp_wrequester_t, llink) {
		if (MATCH(r, type) || 
			(r->type & WAIT_CLASS_ENTRY && ENTRY_MATCH(r, type)) ||
			(r->type & WAIT_CLASS_POOL  && POOL_MATCH(r, type))) {
			assert (r->gpool == gpool);
			__cpool_gp_w_invoke_r(r);
			++ n;
		}
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
		
	return n;
}

int 
__cpool_gp_w_wait_cbl(cpool_gp_t *gpool, int entry_id, long type, Visit_cb cb, void *arg, long ms)
{
	int idx, e = 0, times = 0, ok = 0;
	int entry_destroying_warn = 0;
	int pool_destroying_warn = 0;
	uint64_t ntasks_processed_prev = 0;
	ctask_trace_t *ptask, *peek = NULL, **ptasks = NULL;
	ctask_entry_t *entry = NULL;
	
	/**
	 * Arguments for \@__cpool_gp_waitl
	 */
	void *warg = NULL;
	int  wtype = type;
	
	/**
	 * Record the begining clock 
	 */
	uint64_t us_start_clock = us_startr();

	/**
	 * Parse the requests 
	 */
	if (type & WAIT_TYPE_TASK) {
		ok   = (arg || cb != NULL);
		warg = peek = arg;
	
	} else if (type & WAIT_TYPE_TASK_ANY2) {
		assert (type == (WAIT_CLASS_POOL|WAIT_TYPE_TASK_ANY2) && arg);
	
		warg = ptasks = arg;
	}

	do {	
		/**
		 * If the pool is being destroyed, we show a log message
		 */
		if (!(CORE_F_created & cpool_core_statusl(gpool->core))) {
			if (CORE_F_destroying & cpool_core_statusl(gpool->core)) {
				if (!pool_destroying_warn) {
					MSG_log(M_WAIT, LOG_INFO,
						   "{\"%s\"/%p} Exception: Pool is being destroyed ! wtype(%p)\n",
						   gpool->core->desc, gpool, entry_id, type);
					pool_destroying_warn = 1;
				}

			} else if (CORE_F_destroyed & cpool_core_statusl(gpool->core)) {
				MSG_log(M_WAIT, LOG_INFO,
						"library detects that the pool{\"%s\"/%p} has been destroyed ! wtype(%p)\n",
						gpool->core->desc, gpool, type);

				return 0;

			} else {
				MSG_log(M_WAIT, LOG_ERR,
						"library detects an unrecoverable exception. (The pool(%p) is not alive !) wtype(%p)\n",
						gpool, type);

				return eERR_OTHER;
			}
		}
		/**
		 * Deal with the exceptions 
		 */
		if (WAIT_CLASS_ENTRY & type) {
			entry = gpool->entry + entry_id;

			if (entry_id > gpool->num || entry->lflags & SLOT_F_FREE) {
				MSG_log(M_WAIT, LOG_WARN,
					   "{\"%s\"/%p} library detects that the group(%d) disappears suddenly !\n%s\n",
					   gpool->core->desc, gpool, entry_id, __cpool_gp_entry_dumpl(gpool, NULL, 0));
				return 0;
			}
						
			if (times && SLOT_F_DESTROYING & entry->lflags) {
				if (!entry_destroying_warn) {
					MSG_log(M_WAIT, LOG_WARN,
					   	"{\"%s\"/%p} : library detects that the group (\"%s\"/%d) is being destroyed. type(%d)\n",
					   	gpool->core->desc, gpool, entry->name, entry_id, type);
					entry_destroying_warn = 1;
				}
				
				/**
				 * We continue excuting our wait operation 
				 */
			}
		}
		/**
		 * Check the task's status 
		 */
		if (WAIT_TYPE_TASK & type && peek && !peek->f_stat) {
			/**
			 * Are we responsible for freeing the task object ? 
			 */
			if (!peek->ref && peek->f_vmflags & eTASK_VM_F_LOCAL_CACHE)
				smcache_addl(gpool->core->cache_task, peek);
			if (!cb)
				return 0;
			peek = NULL;
		}
		
		if (!peek) {
			wtype = type;
			switch (type) {
			case (WAIT_CLASS_ENTRY|WAIT_TYPE_TASK):
				/**
				 * Scan the task queue and @cb will peek a propriate a task 
				 */
				list_for_each_entry(ptask, entry->trace_q, ctask_trace_t, trace_link) {
					if (cb(TASK_CAST_FAC(ptask), arg)) {
						warg = peek = ptask;
						break;
					}
				}
				break;
			case (WAIT_CLASS_ENTRY|WAIT_TYPE_TASK_ALL): 
				if (!entry->n_qtraces) 
					return 0;
				break;
			case (WAIT_CLASS_ENTRY|WAIT_TYPE_TASK_ANY):
				if (!times)
					ntasks_processed_prev = entry->ntasks_processed;
				else if (ntasks_processed_prev != entry->ntasks_processed)
					return 0;
				break;
			case (WAIT_CLASS_POOL|WAIT_TYPE_TASK): 
				/**
				 * Scan all of the tasks who has not been finished 
				 */
				for (idx=0; idx<gpool->num && !peek; idx++) {
					entry = gpool->entry + idx;

					if (entry->lflags & SLOT_F_FREE || !entry->n_qtraces) 
						continue;
					
					list_for_each_entry(ptask, entry->trace_q, ctask_trace_t, trace_link) {
						if (cb(TASK_CAST_FAC(ptask), arg)) {
							warg = peek = ptask;
							entry_id = peek->gid;
							break;
						}
					}
				}
				wtype = WAIT_CLASS_ENTRY|WAIT_TYPE_TASK;
				
				/**
				 * If we have not found one, we break 
				 */
				if (!peek)
					return 0;
				break;
			case (WAIT_CLASS_POOL|WAIT_TYPE_TASK_ANY): {
				uint64_t ntasks_processed = 0;

				/**
				 * Scan all of the entries to get the sum
				 * of the done tasks 
				 */
				for (idx=0; idx<gpool->num && !peek; idx++) {
					entry = gpool->entry + idx;

					if (entry->lflags & SLOT_F_FREE) 
						continue;
					assert (entry->ntasks_processed >= 0);
					ntasks_processed += entry->ntasks_processed;
				}
				ntasks_processed += gpool->ntasks_processed0;
				if (!times)
					ntasks_processed_prev = ntasks_processed;
				else if (ntasks_processed_prev != ntasks_processed)
					return 0;
				break;
			}
			case (WAIT_CLASS_POOL|WAIT_TYPE_TASK_ANY2): 
				for (idx=0, ok=0; idx<entry_id; idx++) {
					if (!ptasks[idx] || (ptasks[idx]->f_stat && ptasks[idx]->pool->ins != (void *)gpool))
						continue;

					if (!ptasks[idx]->f_stat)
						return 0;
					
				 	/**
					 * If the entry is not avaliable, we just return 0 imediately 
					 */
					entry = gpool->entry + ptasks[idx]->gid;
					if (ptasks[idx]->gid < 0 || ptasks[idx]->gid > gpool->num ||
						!IS_VALID_ENTRY(entry)) {
						MSG_log(M_WAIT, LOG_WARN,
							   "{\"%s\"/%p} library detects that the the task's gid is invalid. "
							   "task(%s/%p) gid(%d)\n",
								gpool->core->desc, gpool, ptasks[idx]->task_desc, ptasks[idx], ptasks[idx]->gid);
						return eERR_GROUP_NOT_FOUND;
					}
					++ ok;
				}

				if (!ok) { 
					MSG_log(M_WAIT, LOG_WARN,
							"{\"%s\"/%p} library has not gotten any tasks avaliable. times(%d)\n",
							gpool->core->desc, gpool, times);
					return 0;
				}
				break;
			case (WAIT_CLASS_POOL|WAIT_TYPE_TASK_ALL):
				/**
				 * NOTE: 
				 *    We just return 0 no matter whatever the user is
				 * waitting for if there are none any tasks existing 
				 * in the pool 
				 */
				if (!gpool->n_qtraces)
					return 0;
				break;
			default:
				assert (0);
			}
		}
		++ times;
		
		/**
		 * Check the timeout value
		 */
		if (ms >= 0) {
			if (ms > 0)
				ms -= us_endr(us_start_clock) / 1000;
			if (ms <= 0)
				e = eERR_TIMEDOUT;
		}
		
		if (e) 
			return e;
		e = __cpool_gp_w_waitl(gpool, wtype, entry_id, warg, ms);
	} while (1);		
}


