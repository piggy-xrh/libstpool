/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_factory.h"
#include "cpool_core.h"
#include "cpool_rt_struct.h"
#include "cpool_rt_internal.h"

static inline const char *
__cpool_rt_stat_desc(struct cpool_stat *stat)
{
	if (stat->throttle_on) {
		if (stat->suspended)
			return "PA+THR";
		else
			return "PA_ACT";
	} else if (stat->suspended)
		return "PAUSED";
	else
		return "ACTIVE";
}

void   
cpool_rt_set_oaattr(void * ins, struct cpool_oaattr *attr)
{
	cpool_rt_t *rtp = ins;
	
	rtp->task_threshold = attr->task_threshold;
	rtp->eoa = attr->eifoa;
}

void   
cpool_rt_get_oaattr(void * ins, struct cpool_oaattr *attr)
{
	cpool_rt_t *rtp = ins;
	
	attr->task_threshold = rtp->task_threshold;
	attr->eifoa = rtp->eoa;
}
	
char *
cpool_rt_scheduler_map_dump(cpool_core_t *core, char *buff, size_t bufflen)
{
	int n, len = bufflen;
	long tsk_wref, ev_wref;
	char *pos = buff;
	ctask_t task = {0};
	struct cpool_stat stat;
	struct cpool_core_stat core_stat;
	
	cpool_rt_t *rtp = core->priv;
	/**
	 * Make the static buffer big enough 
	 */
	static char __buffer[4000] = {0};

	/** 
	 * If @buffer is NULL, we use the static buffer instead 
	 */
	if (!buff) {
		pos = __buffer;
		len = sizeof(__buffer);
		bufflen = len;
	} 
	*pos = '\0';
	
	/**
	 * Get the status of the pool
	 */
	OSPX_pthread_mutex_lock(&core->mut);
	cpool_core_statl(core, &core_stat);
	stat.curtasks_removing = rtp->tsks_held_by_dispatcher;
	stat.throttle_on = rtp->throttle_on;
	if (core_stat.n_qpendings) {
		if (rtp->lflags & eFUNC_F_PRIORITY)
			task = *__cpool_com_priq_top(&rtp->c);
		else
			task = *list_first_entry(&rtp->ready_q, ctask_t, link);
	}
	tsk_wref = rtp->tsk_wref;
	ev_wref = rtp->ev_wref;
	OSPX_pthread_mutex_unlock(&core->mut);
	
	__cpool_rt_stat_conv(rtp, &core_stat, &stat);
	
	/**
	 * We should not assume that the buffer is big enough to
	 * store the status information 
	 */
	n = snprintf(pos, len,
	            "<%s/%8p> max:%d min:%d run:%d cur:%d wtsk:%ld wev:%ld tsks:[%d~%d] qtsks:%d qtsks_eff:%d [%s]\n"
				"---------------------------------------------------------------------------------------------------\n",
				stat.desc, rtp, stat.maxthreads, stat.minthreads, 
				stat.curthreads_active, stat.curthreads, tsk_wref, ev_wref, 
				stat.curtasks_pending + stat.curtasks_removing, 
				stat.curtasks_pending + stat.curtasks_removing + stat.curthreads_active,
				stat.curtasks_pending, stat.curtasks_pending, __cpool_rt_stat_desc(&stat));
	
	if (n > 0 && stat.curtasks_pending) 
		n = snprintf(pos + n, len - n,
				"  TASK: \"%s\", pri(%d), policy(%s)\n%s\n",
				task.task_desc, task.pri, 
				(ep_TOP == task.pri_policy) ? "top" : "back",
				"---------------------------------------------------------------------------------------------------");
	if (n < 0)
		buff[bufflen-1] = '\0';
	
	return buff ? buff : __buffer;
}


