/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_com_internal.h"
#include "cpool_gp_entry.h"
#include "cpool_gp_internal.h"

static inline const char *
__cpool_gp_stat_desc(cpool_gp_t *gpool)
{
	if (gpool->throttle_on) {
		if (gpool->core->paused)
			return "PA+THR";
		else
			return "PA_ACT";
	} else if (gpool->core->paused)
		return "PAUSED";
	else
		return "ACTIVE";
}

static inline const char *
__cpool_gp_entry_stat_desc(cpool_gp_t *gpool, ctask_entry_t *entry)
{
	static struct dummy0 {
		long lflags;
		int  paused;
		const char *desc;
	} sttab[] = {
		{0, 0, "   Ok   "},
		{0, 1, " Paused "},
		{SLOT_F_FREE  , 0, "   *    "},
		{SLOT_F_FREE  , 1, "   *    "},
		{SLOT_F_ACTIVE, 0, " Active "},
		{SLOT_F_DESTROYING|SLOT_F_THROTTLE, 1,  "Fr+Th+Pa"},
		{SLOT_F_DESTROYING|SLOT_F_THROTTLE, 0,  " Fr+Th  "},
		{SLOT_F_DESTROYING, 1,  " Fr+Pa  "},
		{SLOT_F_DESTROYING, 0,  " Frozen "},
		{SLOT_F_THROTTLE,   1,  " Th+Pa  "},
		{SLOT_F_THROTTLE,   0,  "Throttle"}
	};
	int idx = 0;

	for (; idx<sizeof(sttab)/sizeof(*sttab); idx++) 
		if (entry->lflags == sttab[idx].lflags && 
			entry->paused == sttab[idx].paused)
			return sttab[idx].desc;

	return "   ?    ";
}

char *
__cpool_gp_entry_dumpl(cpool_gp_t *gpool, char *buffer, size_t len)
{
	int idx, n, length = len;
	char *pos = buffer;
	ctask_trace_t *ptask;
	ctask_entry_t *entry;
	
	/**
	 * Make the static buffer big enough 
	 */
	static char __buffer[4000] = {0};

	/** 
	 * If @buffer is NULL, we use the static buffer instead 
	 */
	if (!buffer) {
		pos = __buffer;
		len = sizeof(__buffer);
	} 
	*pos = '\0';
	
	/**
	 * We should not assume that the buffer is big enough to
	 * store the status information 
	 */
	n = snprintf(pos, len,
	            "<%s/%8p> max:%d min:%d run:%d cur:%d wtsk:%ld wev:%ld tsks:%d qtsks:%d qtsks_eff:%d [%s]\n"
				"---------------------------------------------------------------------------------------------------\n"
				"   IDX\t    STAT   GROUP(desc,id,WAIT,limits,be,RUN,qtsks,qtsks_eff)   TASKQ(desc,pri,policy,seq)\n%s \n",
				gpool->core->desc, gpool->core, gpool->core->maxthreads, gpool->core->minthreads, 
				gpool->core->nthreads_running, gpool->core->n_qths, gpool->tsk_wref, gpool->ev_wref, 
				gpool->n_qtraces, gpool->npendings, gpool->core->npendings, __cpool_gp_stat_desc(gpool),
				"---------------------------------------------------------------------------------------------------");
	for (idx=0; n > 0 && len > 0 && idx < gpool->num; ++idx) {
		entry = gpool->actentry[idx];

		if (entry->lflags & SLOT_F_FREE)
			continue;
		len -= n;
		pos += n;
		n = snprintf(pos, len, "%s [%2d]\t%c%8s  {%8s},%2d,%4ld, %5d,%2d,%3d,%5d,%9d    ",
					gpool->active_idx == idx + 1 ? "*" : " ",
					idx, entry->limit_tasks ? ' ' : '-', 
					__cpool_gp_entry_stat_desc(gpool, entry),
					entry->name, entry->id, entry->tsk_wref + entry->ev_wref, 
					entry->limit_tasks, entry->receive_benifits,
					entry->nrunnings, entry->npendings, 
					entry->receive_benifits ? entry->npendings : entry->npendings_eff);
		
		if (n < 0)
			break;
		
		len -= n;
		pos += n;
		if (len) {
			if (entry->npendings) {
				ptask = entry->top;
				
				n = snprintf(pos, len, "{%8s}, %2d,%4s,%u\n",
						ptask->task_desc, ptask->pri, 
						(ep_TOP == ptask->pri_policy) ? "  top " : " back ",
						ptask->seq);
			} else
				n = snprintf(pos, len, "%s", 
						"    *\n");
		}
	}

	if (n < 0)
		buffer[length-1] = '\0';
	
	return buffer ? buffer : __buffer;
}

static int
__cpool_gp_entry_mark_cbl0(cpool_gp_t *gpool, ctask_entry_t *entry, Visit_cb cb, void *arg, struct list_head *rmq)
{
	int  neffs = 0, ok = 0;
	long lflags = (long)arg;
	ctask_trace_t *ptask, *n;

	list_for_each_entry_safe(ptask, n, entry->trace_q, ctask_trace_t, trace_link) {
		if (cb) {
			lflags = cb((ctask_t *)ptask, arg);
		
			if (lflags == -1)
				break;
		
			if (!(lflags &= eTASK_VM_F_USER_FLAGS))
				continue;
		}

		if (eTASK_VM_F_REMOVE_FLAGS & lflags && ptask->f_stat & eTASK_STAT_F_REMOVABLE) {
			if (ptask->f_stat & eTASK_STAT_F_WPENDING)
				ptask->f_stat &= ~eTASK_STAT_F_WPENDING;
	
			else {
				__cpool_gp_task_removel(gpool, entry, ptask, 
						eTASK_VM_F_REMOVE & lflags ? rmq: NULL 
					);
				ptask->f_vmflags |= (eTASK_VM_F_REMOVE_FLAGS & lflags);
			}
			ok = 1;
		}

		if (__cpool_com_task_mark((ctask_t *)ptask, lflags) || ok) {
			ok = 0;
			++ neffs;
		}
	}

	return neffs;
}

int __cpool_gp_entry_notifyl(cpool_gp_t *gpool, ctask_entry_t *entry, long lflags)
{
	int idx = 0;
	ctask_trace_t *ptask;

	if (!gpool->n_qtraces || (entry && !entry->n_qtraces))
		return 0;
	
	if (entry) {
		list_for_each_entry(ptask, entry->trace_q, ctask_trace_t, trace_link) {
			ptask->f_vmflags |= lflags;
		}

		return entry->n_qtraces;
	}
	
	for (; idx<gpool->num; idx++) {
		entry = gpool->entry + idx;

		if (entry->lflags & SLOT_F_FREE)
			continue;
		
		list_for_each_entry(ptask, entry->trace_q, ctask_trace_t, trace_link) {
			ptask->f_vmflags |= lflags;
		}
	}

	return gpool->n_qtraces;
}
int  
__cpool_gp_entry_mark_cbl(cpool_gp_t *gpool, ctask_entry_t *entry, Visit_cb cb, void *arg, struct list_head *rmq)
{
	int idx = 0, neffs = 0;

	if (entry) 
		return __cpool_gp_entry_mark_cbl0(gpool, entry, cb, arg, rmq);
	
	for (; idx<gpool->num; idx++) {
		if (SLOT_F_FREE & gpool->entry[idx].lflags)
			continue;
		neffs += __cpool_gp_entry_mark_cbl0(gpool, gpool->entry + idx, cb, arg, rmq);
	}
	
	if (neffs && cpool_core_need_ensure_servicesl(gpool->core))
		cpool_core_ensure_servicesl(gpool->core, NULL);
	
	return neffs;
}

