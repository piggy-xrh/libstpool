#ifndef __CPOOL_COM_INTERNAL_H__
#define __CPOOL_COM_INTERNAL_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "sm_cache.h"
#include "cpool_core_thread_status.h"
#include "cpool_factory.h"

static inline int
__cpool_com_get_dispatch_taskl(cpool_core_t *core, thread_t *self, int n)
{
	struct list_head *entry;

	assert (core->n_qdispatchs > 0 && n >= 1);

	if (core->n_qdispatchs <= n) {
		__list_splice(&core->dispatch_q, &self->dispatch_q, self->dispatch_q.next);
		core->n_qdispatchs = 0;
	
	} else {
		core->n_qdispatchs -= n;
		for (entry=core->dispatch_q.next; -- n > 0;) 
			entry = entry->next;
		__list_cut_position(&self->dispatch_q, &core->dispatch_q, entry);
	}

#ifndef NDEBUG
	MSG_log(M_SCHEDULER, LOG_TRACE,
			"Thread(%d) is selected to schedue the dispatch queue ...\n",
			self);
#endif
	
	return 1;
}

static inline int
__cpool_com_get_dispatch_taskl2(cpool_core_t *core, thread_t *self, int n)
{
	struct list_head *entry;

	assert (core->n_qdispatchs > 0 && n>=1);

	if (core->n_qdispatchs <= n) {
		__list_splice(&core->dispatch_q, &self->dispatch_q, self->dispatch_q.next);
		core->n_qdispatchs = 0;
	
	} else {
		core->n_qdispatchs -= n;
		for (entry=core->dispatch_q.next; --n > 0;) 
			entry = entry->next;
		__list_cut_position(&self->dispatch_q, &core->dispatch_q, entry);
	}
#ifndef NDEBUG
	self->task_type = TASK_TYPE_DISPATCHED;
	MSG_log(M_SCHEDULER, LOG_TRACE,
			"Thread(%d) is selected to schedue the dispatch queue ...\n",
			self);
#endif
	cpool_core_thread_status_changel(core, self, THREAD_STAT_RUN);
	OSPX_pthread_mutex_unlock(&core->mut);
	
	self->task_type = TASK_TYPE_DISPATCHED;
	
	return 1;
}

static inline int
__cpool_com_get_dispatch_taskl3(cpool_core_t *core, thread_t *self, int n)
{
	struct list_head *entry;

	assert (core->n_qdispatchs > 0 && n>=1);

	if (core->n_qdispatchs <= n) {
		__list_splice(&core->dispatch_q, &self->dispatch_q, self->dispatch_q.next);
		n = core->n_qdispatchs;
		core->n_qdispatchs = 0;

	} else {
		int m = n;

		core->n_qdispatchs -= n;
		for (entry=core->dispatch_q.next; --m > 0;) 
			entry = entry->next;
		__list_cut_position(&self->dispatch_q, &core->dispatch_q, entry);
	}

	return n;
}

static inline int
__cpool_com_task_mark(ctask_t *ptask, long lflags)
{
	if (eTASK_VM_F_ENABLE_QUEUE & lflags) {
		if (eTASK_VM_F_DISABLE_QUEUE & ptask->f_vmflags) {
			ptask->f_vmflags &= ~eTASK_VM_F_DISABLE_QUEUE;
			ptask->f_vmflags |= eTASK_VM_F_ENABLE_QUEUE;
			return 1;
		}
		
	} else if (eTASK_VM_F_DISABLE_QUEUE & lflags &&
		eTASK_VM_F_ENABLE_QUEUE & ptask->f_vmflags) {
		ptask->f_vmflags &= ~eTASK_VM_F_ENABLE_QUEUE;
		ptask->f_vmflags |= eTASK_VM_F_DISABLE_QUEUE;
		return 1;
	}

	return 0;
}

#define __cpool_com_task_mark_append(q, lflags) \
	do {\
		ctask_t *__ptask; \
		\
		list_for_each_entry(__ptask, q, ctask_t, link) {\
			__ptask->f_vmflags |= (lflags); \
		} \
	} while (0) 

static inline int
__cpool_com_get_err_handler_q(struct list_head *rmq, struct list_head *null_q)
{
	int n = 0, m = 0;
	ctask_t *ptask, *next;
	
	list_for_each_entry_safe(ptask, next, rmq, ctask_t, link) {
		++ n;
		
		/**
		 * Skip the tasks who has the error handler
		 */
		if (ptask->task_err_handler) 
			continue;

		list_move_tail(&ptask->link, null_q);
		++ m;
	}

	return n-m;
}

static inline void
__cpool_com_list_to_smq(struct list_head *q, struct smlink_q *smq)
{
	struct list_head *c, *n;
	
	list_for_each_safe(c, n, q) {
		smlink_q_push(smq, c);
	}
}

#endif
