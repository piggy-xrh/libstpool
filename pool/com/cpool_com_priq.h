#ifndef __CPOOL_COM_PRIQ_H__
#define __CPOOL_COM_PRIQ_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "list.h"

typedef struct cpriq cpriq_t;
typedef struct cpriq_container cpriq_container_t;

/** Priority queue */
struct cpriq {
	struct list_head link;
	int    qindex;
	struct list_head task_q;
};

struct cpriq_container {
	int priq_num;
	struct cpriq *priq;
	
	int avg_pri;
	struct list_head *ready_q;
};

static inline void
__cpool_com_priq_insert0(struct list_head *q, ctask_t *ptask)
{
	ctask_t *peek;

	/**
	 * Sort the task according to the priority 
	 */
	if (likely((ptask->f_vmflags & eTASK_VM_F_PUSH) || list_empty(q))) {
		list_add_tail(&ptask->link, q);	
		return;
	}
		
	/**
	 * Compare the task with the last one
	 */
	peek = list_first_entry(q, ctask_t, link);
	if (ptask->pri <= peek->pri) {	
		if ((ptask->pri < peek->pri) || (ep_BACK == ptask->pri_policy)) {
			list_add(&ptask->link, &peek->link);
			return;
		}
			
		assert (ep_TOP == ptask->pri_policy);
		list_for_each_entry_continue_reverse(peek, q, ctask_t, link) {
			if (peek->pri != ptask->pri) {
				list_add(&ptask->link, &peek->link);
				return;
			}
		}
		assert (0);
	} 
		
	/**
	 * Scan the tasks from the the head 
	 */
	list_for_each_entry(peek, q, ctask_t, link) {
		if ((ptask->pri > peek->pri) || (ptask->pri == peek->pri && ep_TOP == ptask->pri_policy)) {
			list_add_tail(&ptask->link, &peek->link);
			return;
		}
	}
	assert (0);
}

static inline void
__cpool_com_priq_active(cpriq_container_t *c, int q)
{
	assert (q < c->priq_num);

	/**
	 * Should our task queue join in the ready queue ? 
	 */
	if (list_empty(c->ready_q)) 
		list_add(&c->priq[q].link, c->ready_q);

	/**
	 * Compare the qindex with the last task
	 */
	else if (q < list_entry(c->ready_q->prev, cpriq_t, link)->qindex) 
		list_add_tail(&c->priq[q].link, c->ready_q);
	
	else {
		cpriq_t *priq;
		
		list_for_each_entry(priq, c->ready_q, cpriq_t, link) {
			if (q > priq->qindex) {
				list_add_tail(&c->priq[q].link, &priq->link);
				break;
			}
		}
	}
}

static inline void
__cpool_com_priq_init(cpriq_container_t *c, cpriq_t *priq, int priq_num, struct list_head *ready_q)
{
	int idx = 0;
	
	assert (priq_num >= 1);
	c->priq = priq;
	c->ready_q = ready_q;
	c->priq_num = max(1, priq_num);
	c->avg_pri = 100 / priq_num;
	
	for (; idx<priq_num; idx++) {
		INIT_LIST_HEAD(&priq[idx].task_q);
		priq[idx].qindex = idx;
	}
	INIT_LIST_HEAD(ready_q);
}

static inline int
__cpool_com_priq_locate(cpriq_container_t *c, int priority)
{
	return (priority < c->avg_pri) ? 0 : (priority / c->avg_pri);
}


static inline int
__cpool_com_priq_empty(cpriq_container_t *c)
{
	return list_empty(c->ready_q);
}

static inline ctask_t *
__cpool_com_priq_pop(cpriq_container_t *c)
{
	cpriq_t *priq  = list_first_entry(c->ready_q, cpriq_t, link);
	ctask_t *ptask = list_first_entry(&priq->task_q, ctask_t, link);
	
	list_del(&ptask->link);
	if (list_empty(&priq->task_q))
		list_del(&priq->link);
#ifndef NDEBUG
	else {
		ctask_t *n = list_entry(priq->task_q.next, ctask_t, link);
		assert (ptask->pri >= n->pri);
	}
#endif
	return ptask;
}

static inline ctask_t *
__cpool_com_priq_top(cpriq_container_t *c)
{
	return list_first_entry(
					&list_first_entry(c->ready_q, cpriq_t, link)->task_q,
					ctask_t, link
			);
}

static inline void
__cpool_com_priq_insert(cpriq_container_t *c, ctask_t *ptask) 
{
	assert (ptask->priq < c->priq_num);

	if (list_empty(&c->priq[ptask->priq].task_q)) 
		__cpool_com_priq_active(c, ptask->priq);
	__cpool_com_priq_insert0(&c->priq[ptask->priq].task_q, ptask);
}

static inline void
__cpool_com_priq_erase(cpriq_container_t *c, ctask_t *ptask)
{
	list_del(&ptask->link);
	if (list_empty(&c->priq[ptask->priq].task_q))
		list_del(&c->priq[ptask->priq].link);
}

static inline void
__cpool_com_task_nice_preprocess(cpriq_container_t *c, ctask_t *ptask)
{
	/**
	 * Select a propriate queue to store the active task according 
	 * to its priority attributes 
	 */
	if (eTASK_VM_F_ADJPRI & ptask->f_vmflags) {
		ptask->f_vmflags &= ~eTASK_VM_F_ADJPRI;
		ptask->priq = __cpool_com_priq_locate(c, ptask->pri);
	}
}

static inline void
__cpool_com_task_nice_adjust(ctask_t *ptask)
{
	/**
	 * We reset its priority if the task doesn't have 
	 * the permanent priority attribute, it means that
	 * the task will be pushed into the tail of the 
	 * pending queue 
	 */
	if (eTASK_VM_F_PRI_ONCE & ptask->f_vmflags) {
		ptask->f_vmflags |= eTASK_VM_F_PUSH;
		ptask->f_vmflags &= ~eTASK_VM_F_PRI_ONCE;
		ptask->priq = 0;
		ptask->pri = 0;
		ptask->pri_policy = ep_BACK;
	}
}

#endif
