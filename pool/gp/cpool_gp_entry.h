#ifndef ____cpool_gp_entry_H__
#define ____cpool_gp_entry_H__
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

#include <stdlib.h>
#include "ospx_compatible.h"
#include "msglog.h"
#include "cpool_core.h"
#include "cpool_gp_struct.h"

char *__cpool_gp_entry_dumpl(cpool_gp_t *gpool, char *buffer, size_t len);

static inline void __cpool_gp_entry_set_attrl(cpool_gp_t *gpool, ctask_entry_t *entry, struct scheduler_attr *attr) 
{
	/**
	 * Save the old configurations 
	 */
	int old_receive_benifits = entry->receive_benifits;
	
	/**
	 * Configure the new configirations 
	 */
	if (attr) {
		entry->limit_paralle_tasks = attr->limit_paralle_tasks;
		entry->receive_benifits = attr->receive_benifits;
	
	} else {
		entry->limit_paralle_tasks = gpool->core->maxthreads;
		entry->receive_benifits = 1;
	}
	
	if (gpool->core->maxthreads <= entry->limit_paralle_tasks)
		entry->limit_tasks = gpool->core->maxthreads;
	
	else if (entry->limit_paralle_tasks <= 0) 
		entry->limit_tasks = 0;
	
	else
		entry->limit_tasks = entry->limit_paralle_tasks;

	/**
	 * Check the new configurations 
	 */
	if (!entry->limit_tasks && !entry->receive_benifits) {
		MSG_log(M_SCHEDULER, LOG_WARN,
				"Invalid scheduler attribute(%d,%d) gname(%s), id(%d).\n",
				attr->limit_paralle_tasks, attr->receive_benifits, entry->name, entry->id);
		entry->receive_benifits = 1;
	}
	
	/**
	 * Update the effective pending tasks number 
	 */
	if (!entry->paused && entry->npendings) {
		/** Suspend */
		if (old_receive_benifits) 
			gpool->core->npendings -= entry->npendings;
		else 
			gpool->core->npendings -= entry->npendings_eff;
		
		/** Resume */
		if (entry->receive_benifits)
			gpool->core->npendings += entry->npendings;
		else {
			entry->npendings_eff = min(entry->limit_tasks, entry->npendings);
			gpool->core->npendings += entry->npendings_eff;
		}
	}
}

static inline void __cpool_gp_entry_get_attrl(cpool_gp_t *gpool, ctask_entry_t *entry, struct scheduler_attr *attr)
{
	attr->limit_paralle_tasks = entry->limit_paralle_tasks;
	attr->receive_benifits = entry->receive_benifits;
}

static inline void __cpool_gp_entry_update_all_envl(cpool_gp_t *gpool)
{
	int idx=0; 
	struct scheduler_attr attr;

	for (; idx<gpool->num; idx++) {
		if (!(gpool->entry[idx].lflags & SLOT_F_FREE)) {
			__cpool_gp_entry_get_attrl(gpool, gpool->entry + idx, &attr);
			__cpool_gp_entry_set_attrl(gpool, gpool->entry + idx, &attr);
		}
	}
}

int __cpool_gp_entry_notifyl(cpool_gp_t *gpool, ctask_entry_t *entry, long lflags);

static inline void __cpool_gp_entry_dump_statl(cpool_gp_t *gpool, ctask_entry_t *entry, struct ctask_group_stat *gstat)
{
	if (!gstat->desc || gstat->desc_length <= 0)
		gstat->desc = entry->name; 
	else {
		strncpy(gstat->desc, entry->name, gstat->desc_length -1);
		gstat->desc[gstat->desc_length - 1] = '\0';
	}
	gstat->desc_length = strlen(gstat->desc);
	gstat->gid = entry->id;
	gstat->created = entry->created;
	__cpool_gp_entry_get_attrl(gpool, entry, &gstat->attr);
	gstat->waiters = entry->tsk_wref + entry->ev_wref;
	gstat->suspended = entry->paused;
	gstat->throttle_on = entry->lflags & SLOT_F_THROTTLE;
	gstat->priq_num = entry->c.priq_num;
	gstat->npendings = entry->npendings;
	gstat->nrunnings = entry->nrunnings;
	gstat->ndispatchings = entry->ndispatchings;
}

static inline void __cpool_gp_entry_swapl(cpool_gp_t *gpool, int idx0, int idx1) 
{
	ctask_entry_t *save = gpool->actentry[idx0];
	
	assert (idx0 >= 0 && idx1 >= 0 && 
			idx0 <= gpool->num &&
			idx1 <= gpool->num);

	/**
	 * Update the group information 
	 */
	gpool->actentry[idx0]->index = idx1;
	gpool->actentry[idx1]->index = idx0;
	
	/**
	 * Swap the brief entry 
	 */
	gpool->actentry[idx0] = gpool->actentry[idx1];
	gpool->actentry[idx1] = save;			
}

static inline void __cpool_gp_entry_activel(cpool_gp_t *gpool, ctask_entry_t *entry) 
{	
	assert (entry && entry->top && !entry->paused && entry->npendings); 
	
	entry->lflags |= SLOT_F_ACTIVE;

	if (1 == ++ gpool->nactives_ok) {
		if (entry->index)
			__cpool_gp_entry_swapl(gpool, 0, entry->index);
		gpool->active_idx = 1;	
	
	} else if (gpool->active_idx <= entry->index) { 
		__cpool_gp_entry_swapl(gpool, gpool->active_idx, entry->index);
		++ gpool->active_idx;
	}
#ifndef NDEBUG
	MSG_log(M_SCHEDULER, LOG_DEBUG,
			"Group{%s}-%d JOIN in the active entries (%d).\n",
			entry->name, entry->id, gpool->nactives_ok);
	
	MSG_log(M_SCHEDULER, LOG_TRACE, 
		"\n%s\n", __cpool_gp_entry_dumpl(gpool, NULL, 0));
#endif
}

static inline void __cpool_gp_entry_shrink(cpool_gp_t *gpool)
{
	int i, j;
	
	if (gpool->active_idx != gpool->nactives_ok) {
		assert (gpool->active_idx > gpool->nactives_ok);

		for (i=0; i<gpool->nactives_ok; i++) {
			if (!(gpool->actentry[i]->lflags & SLOT_F_ACTIVE)) {
				for (j=i+1; j<gpool->active_idx; j++) {
					if (gpool->actentry[j]->lflags & SLOT_F_ACTIVE) {
						__cpool_gp_entry_swapl(gpool, i, j);
						break;
					}
				}
			}
		}
		gpool->active_idx = gpool->nactives_ok;
	}
}

static inline void __cpool_gp_entry_inactivel(cpool_gp_t *gpool, ctask_entry_t *entry) 
{
	assert (gpool->active_idx > entry->index &&
			entry && (entry->paused || !entry->npendings));

	entry->lflags &= ~SLOT_F_ACTIVE;
	if (!-- gpool->nactives_ok) 
		gpool->active_idx = 0;
	
	/**
	 * Shrink the active entry 
	 */
	else if (entry->paused || (gpool->nactives_ok + 2 < gpool->active_idx))
		__cpool_gp_entry_shrink(gpool);

#ifndef NDEBUG
	MSG_log(M_SCHEDULER, LOG_DEBUG,
			"Group{%s}-%d LEAVE the active entries (%d).\n",
			entry->name, entry->id, gpool->nactives_ok);

	MSG_log(M_SCHEDULER, LOG_TRACE, 
		"\n%s\n", __cpool_gp_entry_dumpl(gpool, NULL, 0));
#endif
}

int  __cpool_gp_entry_mark_cbl(cpool_gp_t *gpool, ctask_entry_t *entry, Visit_cb cb, void *arg, struct list_head *rmq);

#endif

