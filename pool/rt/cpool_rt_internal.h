#ifndef __CPOOL_RT_INTERNAL_H__
#define __CPOOL_RT_INTERNAL_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */
#include "cpool_core.h"
#include "cpool_rt_struct.h"
#include "cpool_factory.h"

static inline void 
__cpool_rt_task_queue(cpool_rt_t *rtp, ctask_t *ptask, int raise)
{
	ptask->f_stat = eTASK_STAT_F_WAITING;

	OSPX_pthread_mutex_lock(&rtp->core->mut);
	list_add_tail(&ptask->link, &rtp->ready_q);
	++ rtp->core->npendings;
	/**
	 * wake up threads to schedule tasks
	 */
	if (raise && cpool_core_need_ensure_servicesl(rtp->core) && !rtp->core->paused) 
		cpool_core_ensure_servicesl(rtp->core, NULL);
	OSPX_pthread_mutex_unlock(&rtp->core->mut);
}

static inline void
__cpool_rt_pri_task_queue(cpool_rt_t *rtp, ctask_t *ptask, int raise) 
{		
	ptask->f_stat = eTASK_STAT_F_WAITING;
	__cpool_com_task_nice_preprocess(&rtp->c, ptask);
	
	OSPX_pthread_mutex_lock(&rtp->core->mut);
	__cpool_com_priq_insert(&rtp->c, ptask);
	++ rtp->core->npendings;
	/**
	 * Create more threads to provide services
	 */
	if (raise && cpool_core_need_ensure_servicesl(rtp->core) && !rtp->core->paused) 
		cpool_core_ensure_servicesl(rtp->core, NULL);
	OSPX_pthread_mutex_unlock(&rtp->core->mut);
}

static inline void
__cpool_rt_remove_all(cpool_rt_t *rtp, struct list_head *rmq)
{
	__list_splice(&rtp->ready_q, rmq, rmq->next);
	INIT_LIST_HEAD(&rtp->ready_q);
	rtp->core->npendings = 0;
}

static inline void
__cpool_rt_priq_remove_all(cpool_rt_t *rtp, struct list_head *rmq)
{
	cpriq_t *priq;
	
	while (!list_empty(&rtp->ready_q)) {
		priq = list_first_entry(&rtp->ready_q, cpriq_t, link);
		list_del(&priq->link);
		
		assert (!list_empty(&priq->task_q));
		__list_splice(&priq->task_q, rmq, rmq->next);
		INIT_LIST_HEAD(&priq->task_q);
	}

	rtp->core->npendings = 0;
}

static inline void
__cpool_rt_try_wakeup(cpool_rt_t *rtp)
{
	OSPX_pthread_mutex_lock(&rtp->core->mut);
	if (cpool_core_all_donel(rtp->core))
		rtp->core->me->notifyl(rtp->core, eEvent_F_free);
	OSPX_pthread_mutex_unlock(&rtp->core->mut);
}

struct cpool_stat *
__cpool_rt_stat_conv(cpool_rt_t *rtp, struct cpool_core_stat *src, struct cpool_stat *dst);

void
__cpool_rt_task_dispatch(cpool_rt_t *rtp, struct list_head *rmq, int dispatch_by_pool);

#endif
