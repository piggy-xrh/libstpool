/*
 *  COPYRIGHT (C) 2014 - 2020, pigy_xrh
 * 
 *	  stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: pigy_xrh@163.com  QQ: 1169732280)
 */
#include "timer.h"
#include "ospx_compatible.h"
#include "cpool_gp_struct.h"
#include "cpool_gp_method.h"
#include "cpool_gp_entry.h"
#include "cpool_gp_wait.h"
#include "cpool_gp_wait_internal.h"
#include "cpool_gp_internal.h"

int   
cpool_gp_entry_create(void * ins, const char *desc, int priq_num, int suspend)
{
	int idx, q_num = 0, id = -1, inc = 3;
	time_t now = time(NULL);
	ctask_entry_t *entry = NULL, **actentry = NULL;
	cpool_gp_t *gpool = ins;
	
	/**
	 * for name buffer 
	 */
	int name_fixed = 1;
	char *name_desc = (char *)desc, buffer[80];
	
	MSG_log(M_GROUP, LOG_INFO,
			"{\"%s\"/%p} Creating %s group(\"%s\") ... %s",
			gpool->core->desc, gpool, gpool->entry ? "" : "the default", 
			desc, ctime(&now));
	
	if (!desc) {
		srandom((unsigned int)now);
		sprintf(buffer, "?dummy_%u_%p", (unsigned int)now, (void *)random());
		desc = buffer;
	}
	
	/**
	 * Parse the name buffer and allocate a string to store 
	 * the group name if it is neccessary 
	 */
	if (*desc == '?') {
		name_fixed = 0;
		name_desc  = malloc(strlen(desc));
		if (!name_desc)
			return eERR_NOMEM;

		strcpy(name_desc, desc + 1);
	} 
	
	/**
	 * If the parameters is ilegal, we try to correct them
	 */
	priq_num = max(priq_num, 1);
	priq_num = min(priq_num, 99);

	/**
	 * FIX me:
	 *     It is not a good idea to call @malloc with holding
	 * the global lock.
	 */
	OSPX_pthread_mutex_lock(&gpool->core->mut);
	if (gpool->nfrees) {
		/**
		 * Choose the best priority queue for the request 
		 */
		for (idx=0; idx< gpool->num && q_num != priq_num; idx++) {
			entry = gpool->entry + idx;

			if (!(entry->lflags & SLOT_F_FREE))
				continue;

			if (-1 == id) {
				id = idx;
				q_num = entry->priq_max_num;
				continue;

			} else if (entry->priq_max_num >= priq_num) {
				if (q_num < priq_num) {
					id = idx;
					q_num = entry->priq_max_num;
					continue;
				
				} 
			} else if (q_num < priq_num && q_num > entry->priq_max_num) {
				id = idx;
				q_num = entry->priq_max_num;
			}
		}
		assert (id >= 0);
	} else {
		if (!(actentry = realloc(gpool->actentry, (gpool->num + inc) * sizeof(*actentry))))
			goto out;
		bzero(actentry + gpool->num, inc * sizeof(*actentry));
		gpool->actentry = actentry;
		
		/**
		 * Creat the entry blocks 
		 */
		if (!(entry = realloc(gpool->entry, (gpool->num + inc) * sizeof(*entry)))) {
			id = -1;
			goto out;
		}
		bzero(entry + gpool->num, inc * sizeof(*entry));
		gpool->entry = entry;
		
		/**
		 * WARNING: We should fix the old mapped address since the 
		 * memory address has been changed 
		 */
		for (idx=0; idx<gpool->num; idx++) {
			assert (entry[idx].index < gpool->num);
			actentry[entry[idx].index] = entry + idx;	
		}

		/** 
		 * Intiailize the new blocks 
		 */
		for (idx=gpool->num; idx<gpool->num + inc; idx++) {
			actentry[idx] = entry + idx;
			entry[idx].id = -1;
			entry[idx].index = idx;
			entry[idx].ev_need_notify = &gpool->ev_need_notify;
			entry[idx].cond_ev = &gpool->cond_ev;
			entry[idx].cond_sync = &gpool->cond_sync;
			entry[idx].cond_task = &gpool->cond_task_entry;
			entry[idx].lflags = SLOT_F_FREE;
			entry[idx].pool = gpool;
		}
		
		id = gpool->num;
		gpool->num += inc;
		gpool->nfrees += inc;
	}
	entry = gpool->entry + id;
	
	/**
	 * Create the priority queue for the group 
	 */
	if (!entry->c.priq || entry->priq_max_num < priq_num) {
		void *mem = realloc(entry->c.priq, sizeof(cpriq_t) * priq_num + 2 * sizeof(struct list_head));
		
		if (!mem) {
			id = -1;
			goto out;
		}
		entry->c.priq = mem;
		entry->priq_max_num = priq_num;
	}
	__cpool_com_priq_init(&entry->c, entry->c.priq, priq_num, (struct list_head *)(entry->c.priq + priq_num));
	entry->trace_q = entry->c.ready_q + 1;

	INIT_LIST_HEAD(entry->trace_q);
	entry->name = name_desc;
	entry->name_fixed = name_fixed;
	entry->id = id;
	entry->created = now;
	entry->tsk_wref = entry->ev_wref = 0;
	entry->paused = suspend;
	entry->lflags &= ~SLOT_F_FREE;
	entry->ntasks_processed = 0;
	entry->tsk_need_notify = entry->tsk_any_wait = 0;
	entry->task_threshold = -1;
	entry->eoa = eIFOA_none;
	-- gpool->nfrees;

	assert (!entry->nrunnings && !entry->ndispatchings &&
			!entry->npendings && !entry->n_qtraces);
	/**
	 * Configure the entry's attributes 
	 */
	__cpool_gp_entry_set_attrl(gpool, entry, NULL);
out:	
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
	
	if (-1 == id)
		free(name_desc);

	return id;

	return 0;
}

void  
cpool_gp_entry_delete(void * ins, int id)
{
	int warn = 1, e;
	char *name_desc = NULL;
	LIST_HEAD(rmq);
	ctask_entry_t *entry;
	cpool_gp_t *gpool = ins;
	
	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (IS_VALID_ENTRY(&gpool->entry[id])) {
			if (id != 0 || (!(CORE_F_created & cpool_core_statusl(gpool->core)))) {
				warn  = 0;
				entry = gpool->entry + id;
				entry->lflags |= SLOT_F_DESTROYING;
				name_desc  = entry->name;
				
				/**
				 * Wake up the throttle
				 */
				__cpool_gp_w_wakeup_entry_throttlel(entry);

				/**
				 * Notify all of the tasks that our entry is being destroyed
				 */
				if (__cpool_gp_entry_notifyl(gpool, entry, eTASK_VM_F_GROUP_DESTROYING) > 0)
					__cpool_gp_entry_mark_cbl(gpool, entry, NULL, (void *)eTASK_VM_F_REMOVE, &rmq);
			}
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
		
		if (!warn) {
			time_t now = time(NULL);

			MSG_log(M_GROUP, LOG_INFO,
					"{\"%s\"/%p} Deleting group(\"%s\"-%d) ... %s",
					gpool->core->desc, gpool, name_desc, id, ctime(&now));

			/**
			 * If there are tasks who has the completion routine, then we call 
			 * @__cpool_gp_task_dispatcher to dispatch them 
			 */
			if (!list_empty(&rmq)) 
				__cpool_gp_task_dispatcher(gpool, &rmq);
			
			MSG_log(M_GROUP, LOG_INFO,
					"{\"%s\"/%p} group(\"%s\"-%d) waiting ...\n",
					gpool->core->desc, gpool, name_desc, id);

			OSPX_pthread_mutex_lock(&gpool->core->mut);
			entry = gpool->entry + id;
			assert (id == entry->id && SLOT_F_DESTROYING & entry->lflags);

			/**
			 * Wait for all group tasks' being done 
			 */
			do {
				e = __cpool_gp_w_wait_cbl(gpool, id, WAIT_CLASS_ENTRY|WAIT_TYPE_TASK_ALL, NULL, NULL, -1);
			} while (e == eERR_INTERRUPTED);
			
			/**
			 * Now we can free the group safely 
			 */
			assert (!entry->npendings && !entry->npendings_eff &&
			        !entry->nrunnings && !entry->ndispatchings);
			
			/**
			 * Wake up all WAIT functions 
			 */
			cpool_gp_w_wakeup(gpool, WAIT_CLASS_ENTRY|WAIT_TYPE_ENTRY_ALL, id); 
			
			/**
			 * Synchronze the WAIT requests by the references 
			 */
			for (;entry->tsk_wref || entry->ev_wref;) {
				MSG_log(M_GROUP, LOG_INFO,
						"{\"%s\"/%p} tsk_wref(%d) ev_wref(%d): {%s} id(%d) ...\n",
						gpool->core->desc, gpool, entry->tsk_wref, entry->ev_wref, entry->name, id);
				
				OSPX_pthread_cond_wait(entry->cond_sync, &gpool->core->mut);
				entry = gpool->entry + id;
			}
			
			name_desc = entry->name_fixed ? NULL : entry->name;
			
			/* Pay back the entry to the pool */
			entry->lflags = SLOT_F_FREE;
			gpool->ntasks_processed0 += entry->ntasks_processed;
			++ gpool->nfrees;
			OSPX_pthread_mutex_unlock(&gpool->core->mut);
			
			if (name_desc)
				free(name_desc);
		}
	}
	
	if (warn) {
		if (id)
			MSG_log(M_GROUP, LOG_WARN,
				"@%s:Invalid group id(%d):(%s-%d)\n",
				__FUNCTION__, id, gpool->core->desc, gpool->num);
		else
			MSG_log(M_GROUP, LOG_WARN,
				"@%s:Can not delete the default group:(%s-%d)\n",
				__FUNCTION__, gpool->core->desc, gpool->num);
	}
}

int   
cpool_gp_entry_id(void * ins, const char *desc)
{
	int idx, id = -1;
	cpool_gp_t *gpool = ins;

	OSPX_pthread_mutex_lock(&gpool->core->mut);
	for (idx=0; idx<gpool->num; idx++) {
		if (SLOT_F_FREE & gpool->entry[idx].lflags)
			continue;

		if (!strcmp(desc, gpool->entry[idx].name)) {
			id = gpool->entry[idx].id;
			break;
		}
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
	
	return id;
}

char *
cpool_gp_entry_desc(void * ins, int id, char *desc_buff, size_t len)
{
	int get = 0;
	cpool_gp_t *gpool = ins;
	
	/**
	 * Check the parameters
	 */
	if (desc_buff) {
		if (len <= 0) {
			MSG_log(M_GROUP, LOG_ERR,
					"Invalid arguments. id(%d) desc_buff(%p) len(%d)\n",
					id, desc_buff, len);
			return NULL;
		}
		desc_buff[len - 1] = '\0';
	}

	/**
	 * We always assume the the pool is alive
	 */
	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (id < gpool->num && IS_VALID_ENTRY(&gpool->entry[id])) {
			if (desc_buff)
				strncpy(desc_buff, gpool->entry[id].name, len -1);
			else
				desc_buff = gpool->entry[id].name;
			get = 1;
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}
	
	return get ? desc_buff : NULL;
}

int
cpool_gp_entry_suspend(void * ins, int id, long ms)
{
	int e = 0;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = ins;

	if (id < 0 || id > gpool->num) 
		return eERR_GROUP_NOT_FOUND;

	OSPX_pthread_mutex_lock(&gpool->core->mut);
	entry = gpool->entry + id;
	if (!IS_VALID_ENTRY(entry))
		e = eERR_GROUP_NOT_FOUND;

	else if (!(CORE_F_created & cpool_core_statusl(gpool->core)))
		e = eERR_DESTROYING;

	else {
		if (!entry->paused) {
			/**
			 * Mark the entry paused and inactive it if it is neccessary
			 */
			entry->paused = 1;

#ifndef NDEBUG	
			MSG_log(M_GROUP, LOG_INFO,
				"{\"%s\"/%p} Suspend(%d/%s-%d). <npendings:%d nrunnings:%d nremovings:%d> @nthreads:%d>\n", 
				gpool->core->desc, gpool, id, entry->name, entry->npendings, gpool->npendings,
				gpool->core->nthreads_running, gpool->ndispatchings, gpool->core->n_qths);
#endif
			if (entry->npendings) {
				gpool->npendings -= entry->npendings;
				__cpool_gp_entry_inactivel(gpool, entry);

				/**
				 * Update the effective pending tasks number 
				 */
				if (entry->receive_benifits)
					gpool->core->npendings -= entry->npendings;
				else {
					gpool->core->npendings -= entry->npendings_eff;
					entry->npendings_eff = 0;
				}
			} else {
				assert (!entry->npendings_eff);
				/**
				 * We try to shrink the active entry 
				 */
				__cpool_gp_entry_shrink(gpool);
			}
		}
		
		/**
		 * Should we wait for both the dispatching tasks and the
		 * scheduling tasks ? 
		 */
		if (entry->ndispatchings || entry->nrunnings) {
			if (!ms)
				e = eERR_TIMEDOUT;
			else
				e = __cpool_gp_w_wait_cbl(gpool, id, WAIT_CLASS_ENTRY|WAIT_TYPE_TASK, __cpool_gp_wcb_paused, NULL, ms);
		}
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
	
	return e;
}

int
cpool_gp_entry_suspend_all(void * ins, long ms)
{
	int idx, e = eERR_TIMEDOUT;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = ins;
	int ids[100], *ids_idx = &ids[0];
	
	OSPX_pthread_mutex_lock(&gpool->core->mut);
	if (!(CORE_F_created & cpool_core_statusl(gpool->core))) {
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
		return eERR_DESTROYING;
	}

	for (idx=0; idx<gpool->num; idx++) {
		entry = gpool->entry + idx;
		
		if (IS_VALID_ENTRY(entry)) {
			if (!entry->paused) {
				/**
				 * Mark the entry paused 
				 */
				entry->paused = 1;
#ifndef NDEBUG	
				MSG_log(M_GROUP, LOG_INFO,
					"{\"%s\"/%p} Suspend(%d/%s-%d). <npendings:%d nrunnings:%d nremovings:%d> @nthreads:%d>\n", 
					gpool->core->desc, gpool, entry->id, entry->name, entry->npendings, gpool->npendings,
					gpool->core->nthreads_running, gpool->ndispatchings, gpool->core->n_qths);
#endif
				/**
				 * Inactive this entry if it is neccessary
				 */
				if (entry->npendings) {
					gpool->npendings -= entry->npendings;
					__cpool_gp_entry_inactivel(gpool, entry);

					/**
					 * Update the effective pending tasks number 
					 */
					if (entry->receive_benifits)
						gpool->core->npendings -= entry->npendings;
					else {
						gpool->core->npendings -= entry->npendings_eff;
						entry->npendings_eff = 0;
					}

					assert (gpool->core->npendings >= 0 && gpool->npendings >= gpool->core->npendings);
				} else {
					assert (!entry->npendings_eff);
					/**
					 * We try to shrink the active entry 
					 */
					__cpool_gp_entry_shrink(gpool);
				}
			}
			ids[++ *ids_idx] = entry->id;
		}
	}
	assert (!gpool->npendings);
			
	/**
	 * Should we wait for both the dispatching tasks and the scheduling tasks ? 
	 */
	if (gpool->ndispatchings || gpool->core->nthreads_running) {
		if (!ms)
			e = eERR_TIMEDOUT;
		else
			e = __cpool_gp_w_wait_cbl(gpool, -1, WAIT_CLASS_POOL|WAIT_TYPE_TASK, __cpool_gp_wcb_paused, (void *)ids, ms);
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);

	return e;
}

void  
cpool_gp_entry_resume(void * ins, int id)
{
	int ok = 0;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = ins;
	
	if (id < 0 || id > gpool->num) 
		return;

	OSPX_pthread_mutex_lock(&gpool->core->mut);
	entry = gpool->entry + id;
	ok = IS_VALID_ENTRY(entry) && CORE_F_created & cpool_core_statusl(gpool->core);
	
	if (ok && entry->paused) {
		/**
		 * Mark the entry ready 
		 */
		entry->paused = 0;
#ifndef NDEBUG	
		MSG_log(M_GROUP, LOG_INFO,
			"{\"%s\"/%p} Resume(%d/%s-%d). <npendings:%d nrunnings:%d nremovings:%d> @nthreads:%d>\n", 
			gpool->core->desc, gpool, entry->id, entry->name, entry->npendings, gpool->npendings,
			gpool->core->nthreads_running, gpool->ndispatchings, gpool->core->n_qths);
#endif	
		if (entry->npendings) {
			gpool->npendings += entry->npendings;
			
			/**
			 * Update the effective pending tasks number 
			 */
			if (entry->receive_benifits)
				gpool->core->npendings += entry->npendings;
			else {
				entry->npendings_eff = min(entry->npendings, entry->limit_tasks);
				gpool->core->npendings += entry->npendings_eff;
			}
			__cpool_gp_entry_activel(gpool, entry);
					
			/** 
			 * Notify the server threads that we are ready now. 
			 */
			assert (gpool->npendings >= gpool->core->n_qdispatchs);
			if (cpool_core_need_ensure_servicesl(gpool->core))
				cpool_core_ensure_servicesl(gpool->core, NULL);
			
			/**
			 * Update the statics report 
			 */
			if (gpool->ntasks_peak < gpool->npendings) 
				gpool->ntasks_peak = gpool->npendings;	
		}
		assert (gpool->active_idx >= gpool->nactives_ok);
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
}

void  
cpool_gp_entry_resume_all(void * ins)
{
	int idx;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = ins;

	OSPX_pthread_mutex_lock(&gpool->core->mut);
	if (!(CORE_F_created & cpool_core_statusl(gpool->core))) {
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
		return;
	}

	for (idx=0; idx<gpool->num; idx++) {
		entry = gpool->entry + idx;
		
		if (IS_VALID_ENTRY(entry) && entry->paused) {
			/**
			 * Mark the entry ready 
			 */
			entry->paused = 0;
#ifndef NDEBUG	
			MSG_log(M_GROUP, LOG_INFO,
				"{\"%s\"/%p} Resume(%d/%s-%d). <npendings:%d nrunnings:%d nremovings:%d> @nthreads:%d>\n", 
				gpool->core->desc, gpool, entry->id, entry->name, entry->npendings, gpool->npendings,
				gpool->core->nthreads_running, gpool->ndispatchings, gpool->core->n_qths);
#endif	
			if (entry->npendings) {
				gpool->npendings += entry->npendings;
				
				/**
				 * Update the effective pending tasks number 
				 */
				if (entry->receive_benifits)
					gpool->core->npendings += entry->npendings;
				else {
					entry->npendings_eff = min(entry->npendings, entry->limit_tasks);
					gpool->core->npendings += entry->npendings_eff;
				}
				__cpool_gp_entry_activel(gpool, entry);
						
				assert (gpool->npendings >= gpool->core->n_qdispatchs);
			}
			assert (gpool->active_idx >= gpool->nactives_ok);
		}
	}
	
	/**
	 * Update the statics report 
	 */
	if (gpool->ntasks_peak < gpool->npendings) 
		gpool->ntasks_peak = gpool->npendings;	
	
	/**
	 * Notify the server threads that we are ready now. 
	 */
	if (cpool_core_need_ensure_servicesl(gpool->core))
		cpool_core_ensure_servicesl(gpool->core, NULL);
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
}

int   
cpool_gp_entry_setattr(void * ins, int id, struct scheduler_attr *attr)
{
	int ok = 0;
	cpool_gp_t *gpool = ins;
	
	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (id < gpool->num && IS_VALID_ENTRY(&gpool->entry[id])) {
			ok  = 1;
			__cpool_gp_entry_set_attrl(gpool, gpool->entry + id, attr);
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}

	return ok ? 0 : eERR_GROUP_NOT_FOUND;
}

int   
cpool_gp_entry_getattr(void * ins, int id, struct scheduler_attr *attr)
{
	int ok = 0;
	cpool_gp_t *gpool = ins;

	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (id < gpool->num && IS_VALID_ENTRY(&gpool->entry[id])) {
			ok = 1;

			if (attr)
				__cpool_gp_entry_get_attrl(gpool, gpool->entry + id, attr);
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}

	return ok ? eERR_GROUP_NOT_FOUND : 0;
}

int
cpool_gp_entry_set_oaattr(void *ins, int id, struct cpool_oaattr *attr)
{
	int ok = 0;
	cpool_gp_t *gpool = ins;

	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (id < gpool->num && IS_VALID_ENTRY(&gpool->entry[id])) {
			ok = 1;

			if (attr) {
				gpool->entry[id].task_threshold = attr->task_threshold;
				gpool->entry[id].eoa = attr->eifoa;
			}
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}

	return ok ? eERR_GROUP_NOT_FOUND : 0;
}

int
cpool_gp_entry_get_oaattr(void *ins, int id, struct cpool_oaattr *attr)
{
	int ok = 0;
	cpool_gp_t *gpool = ins;

	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (id < gpool->num && IS_VALID_ENTRY(&gpool->entry[id])) {
			ok = 1;

			if (attr) {
				attr->task_threshold = gpool->entry[id].task_threshold;
				attr->eifoa = gpool->entry[id].eoa;
			}
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}

	return ok ? eERR_GROUP_NOT_FOUND : 0;
}

void  
cpool_gp_entry_throttle_ctl(void * ins, int id, int enable)
{
	cpool_gp_t *gpool = ins;

	if (id < 0 || id >= gpool->num) 
		return;

	OSPX_pthread_mutex_lock(&gpool->core->mut);
	if (IS_VALID_ENTRY(&gpool->entry[id])) {
		if (!enable) {
			if (SLOT_F_THROTTLE & gpool->entry[id].lflags) {
				gpool->entry[id].lflags &= ~SLOT_F_THROTTLE;
				
				/**
				 * Wake up the \@cpool_gp_entry_throttle_wait
				 */
				__cpool_gp_w_wakeup_pool_throttlel(gpool);
			}

		} else
			gpool->entry[id].lflags |= SLOT_F_THROTTLE;
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
}

int   
cpool_gp_entry_throttle_wait(void * ins, int id, long ms)
{
	int e = eERR_GROUP_NOT_FOUND;
	cpool_gp_t *gpool = ins;

	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (IS_VALID_ENTRY(&gpool->entry[id])) 
			__cpool_gp_w_waitl_utils(
				gpool, WAIT_CLASS_ENTRY|WAIT_TYPE_THROTTLE, id, NULL, ms,
				e, us_startr(), 
				
				/**
		 		 * If the pool is being destroyed, we return @eERR_DESTROYING
		 		*/
				if (CORE_F_destroying & cpool_core_statusl(gpool->core)) {
					e = eERR_DESTROYING;
					break;
				}

				/**
				 * Check the throttle's status
				 */
				if (!(gpool->entry[id].lflags & SLOT_F_THROTTLE)) {
					e = 0;
					break;
				}
			);
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}

	return e;
}

int   
cpool_gp_entry_remove_all(void * ins, int id, int dispatched_by_pool)
{
	cpool_gp_t *gpool = ins;
	long lflags = dispatched_by_pool ? eTASK_VM_F_REMOVE_BYPOOL : eTASK_VM_F_REMOVE;
		
	return cpool_gp_entry_mark_all(gpool, id, lflags);
}

int   
cpool_gp_entry_mark_all(void * ins, int id, long lflags)
{
	cpool_gp_t *gpool = ins;
	
	return cpool_gp_entry_mark_cb(gpool, id, NULL, (void *)lflags);
}

int   
cpool_gp_entry_mark_cb(void * ins, int id, Visit_cb wcb, void *wcb_arg)
{
	int neffs = 0;
	LIST_HEAD(rmq);
	cpool_gp_t *gpool = ins;
	
	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (!(SLOT_F_FREE & gpool->entry[id].lflags))
			neffs = __cpool_gp_entry_mark_cbl(gpool, gpool->entry + id, wcb, wcb_arg, &rmq);
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}

	if (!list_empty(&rmq))
		__cpool_gp_task_dispatcher(gpool, &rmq);
	
	return neffs;
}

int   
cpool_gp_entry_wait_all(void * ins, int id, long ms)
{
	cpool_gp_t *gpool = ins;
	
	return cpool_gp_entry_wait_cb(gpool, id, NULL, NULL, ms);
}

int   
cpool_gp_entry_wait_cb(void * ins, int id, Visit_cb cb, void *cb_arg, long ms)
{
	int e = eERR_GROUP_NOT_FOUND;
	int type = WAIT_CLASS_ENTRY;
	cpool_gp_t *gpool = ins;
	
	if (cb)
		type |= WAIT_TYPE_TASK;
	else
		type |= WAIT_TYPE_TASK_ALL;

	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (!(SLOT_F_FREE & gpool->entry[id].lflags))
			e = __cpool_gp_w_wait_cbl(gpool, id, type, cb, cb_arg, ms);
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}
	
	return e;
}

int   
cpool_gp_entry_wait_any(void * ins, int id, long ms)
{
	int e = eERR_GROUP_NOT_FOUND;
	cpool_gp_t *gpool = ins;

	if (id >= 0 && id < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (!(SLOT_F_FREE & gpool->entry[id].lflags))
			e = __cpool_gp_w_wait_cbl(gpool, id, WAIT_CLASS_ENTRY|WAIT_TYPE_TASK_ANY, NULL, NULL, ms);
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}
	
	return e;
}

int 
cpool_gp_entry_stat(void * ins, int gid, struct ctask_group_stat *gstat)
{
	int e = eERR_GROUP_NOT_FOUND;
	ctask_entry_t *entry;
	cpool_gp_t *gpool = ins;

	/**
	 * We always assume the the pool is active 
	 */
	if (gid >= 0 && gid < gpool->num) {
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		if (IS_VALID_ENTRY(&gpool->entry[gid])) {
			e = 0;
			entry = gpool->entry + gid;
			
			/**
			 * Dump the status of the entry if it is neccessary 
			 */
			if (gstat) 
				__cpool_gp_entry_dump_statl(gpool, entry, gstat);
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
	}
	
	return e;
}

int 
cpool_gp_entry_stat_all(void * ins, struct ctask_group_stat **gstat)
{
	int n, m = 0, idx;
	ctask_entry_t *entry;
	struct ctask_group_stat *p;
	cpool_gp_t *gpool = ins;
	
	/**
	 * for parsing the name buffer 
	 */
	size_t desc_length = 0;
	unsigned char *name_end, *pos;

	assert (gstat);
	*gstat = NULL;

	/**
	 * We always assume that the the pool is alive 
	 */
	OSPX_pthread_mutex_lock(&gpool->core->mut);
	n = gpool->num - gpool->nfrees;
	for (idx=0; idx < gpool->num && m < n; idx++) {
		entry = gpool->entry + idx;

		if (SLOT_F_FREE & entry->lflags)
			continue;
		if (!entry->name_fixed)
			desc_length += strlen(entry->name) + 1;
	}
	OSPX_pthread_mutex_unlock(&gpool->core->mut);
	
	/**
	 * We allocate extra 250 bytes for the essential needs 
	 */
	#define EXTRA_NAME_BUFFER_SIZE 250
	
	if (n > 0) {
		desc_length += EXTRA_NAME_BUFFER_SIZE;
		
		/**
		 * Allocate a block to store the status records 
		 */
		if (!(p = malloc(sizeof(*p) * n + desc_length))) {
			MSG_log(M_GROUP, LOG_WARN,
					"@%s: system is out of memory.\n",
					__FUNCTION__);
			return 0;
		}
		*gstat = p;
		pos = (unsigned char *)(p + n);
		name_end = pos + desc_length;

		/**
		 * Dump all entries' status 
		 */
		OSPX_pthread_mutex_lock(&gpool->core->mut);
		for (idx=0; idx < gpool->num && m < n; idx++) {
			entry = gpool->entry + idx;

			if (SLOT_F_FREE & entry->lflags)
				continue;

			if (entry->name_fixed)
				p->desc = NULL;
			else {
				/**
				 * If we have no enough space to store the group
				 * name, we just skip this group 
				 */
				if ((size_t)(name_end - pos) < strlen(entry->name) + 1)
					continue;

				p->desc = (char *)pos;
				p->desc_length = strlen(entry->name) + 1;
				pos += p->desc_length;
			}
			__cpool_gp_entry_dump_statl(gpool, entry, p ++);
			++ m;
		}
		OSPX_pthread_mutex_unlock(&gpool->core->mut);
		
		/**
		 * Check the result 
		 */
		if (!m) {
			free(p);
			*gstat = NULL;
		}
	}
		
	return m;
}
