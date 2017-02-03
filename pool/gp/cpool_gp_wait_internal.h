#ifndef __CPOOL_GP_WAIT_INTERNAL_H__
#define __CPOOL_GP_WAIT_INTERNAL_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_gp_wait.h"

/**
 * A common WAIT interfaces
 */
int __cpool_gp_w_wait_cbl(cpool_gp_t *gpool, int entry_id, long type, Visit_cb cb, void *arg, long ms);

/**
 * Push our request into the wait queue and wait for the notification from the pool 
 */
int __cpool_gp_w_waitl(cpool_gp_t *gpool, long type, int entry_id, void *arg, long ms);

/**
 * A usefull utils for the WAIT operations
 */
#define __cpool_gp_w_waitl_utils(gpool, type, id, arg, ms, e, us_start_clock, ready_judge) \
	do {\
		uint64_t __us_clock = (us_start_clock); \
		\
		for ((e)=0;;) { \
			ready_judge \
			\
			if ((ms) > 0) { \
				(ms) -= us_endr(__us_clock) / 1000; \
				if ((ms)<= 0) {\
					(ms) = 0; \
					(e) = eERR_TIMEDOUT; \
				} \
			} \
			if (e) \
				break; \
			/**
			 * Start doing the real wait operations
			 */ \
			e = __cpool_gp_w_waitl(gpool, type, id, arg, ms); \
		} \
	} while (0)

#define __try_invoke(p_need_notify, pcond, expr) \
		do { \
			if (*(p_need_notify)) {\
				*(p_need_notify) = 0; \
				OSPX_pthread_cond_broadcast(pcond); \
				expr \
			}\
		} while (0)

/**
 * Wake up the waiters who is waiting on the pool's throttle
 */
#define __cpool_gp_w_wakeup_pool_throttlel(pool) \
			__try_invoke(&(pool)->ev_need_notify, &(pool)->cond_ev, ;)

/**
 * Wake up the waiters who is waiting on the entry's throttle
 */
#define __cpool_gp_w_wakeup_entry_throttlel(entry) \
		__try_invoke((entry)->ev_need_notify, (entry)->cond_ev, ;)

/**
 * Wake up the waiters who is waiting on the pool's tasks
 *
 * (This interface is only for \@cpool_gp_wait_any)
 */
#define __cpool_gp_w_wakeup_pool_taskl(p) \
		do {(p)->tsk_need_notify = 0; \
			(p)->tsk_any_wait = 0; \
			while ((p)->entry_idx) { \
				-- (p)->entry_idx; \
				if ((p)->glbentry[(p)->entry_idx]->pool->ins == (void *)(p)) \
					(p)->glbentry[(p)->entry_idx]->f_global_wait = 0; \
			} \
			OSPX_pthread_cond_broadcast(&(p)->cond_task);\
		} while (0)

/**
 * Wake up the waiters who is waiting on the entry's tasks
 *
 * (All entries share the same condtion variable currently)
 */
#define __cpool_gp_w_wakeup_entry_taskl(ent) \
	do { int ___idx0 = 0; \
		for (;___idx0<(ent)->pool->num; ___idx0++) { \
			if (!(SLOT_F_FREE & (ent)->pool->entry[___idx0].lflags)) {\
				(ent)->pool->entry[___idx0].tsk_need_notify = 0; \
				(ent)->pool->entry[___idx0].tsk_any_wait = 0; \
			} \
		} \
		OSPX_pthread_cond_broadcast((ent)->cond_task); \
	} while (0)
/**
 * WARN:
 * 	 \@cpool_gp_w_wakeup_taskl should be called on every finished tasks 
 *
 *   WAIT task:    ++ pool->tsk_notifer_waiters, entry->task_need_notify, ++ ptask->ref
 *   WAIT entry:   ++ pool->tsk_notifer_waiters, entry->tsk_need_notify
 *   WAIT all:     ++ pool->tsk_notifer_waiters, pool->tsk_need_notify
 */
#define __cpool_gp_w_wakeup_taskl(pool, entry, ptask) \
	do {\
		/**
		 * Are there any waiters now ? 
		 */ \
		if ((pool)->tsk_wref) {\
			/**
			 * Wake up \@cpool_gp_entry_wait_all, \@stpool_gp_entry_wait_any 
			 */ \
			if ((entry)->tsk_need_notify && ((ptask)->ref || (entry)->tsk_any_wait || !(entry)->n_qtraces)) {\
				if ((ptask)->f_global_wait) {\
					/**
					 * We reset the @tsk_any_wait to force the library fireing the event 
					 */ \
					(pool)->tsk_need_notify = 1;\
					(pool)->tsk_any_wait = 1; \
				} \
				__cpool_gp_w_wakeup_entry_taskl(entry); \
			} \
			/**
			 * Wake up \@cpool_gp_wait_all, \@cpool_gp_wait_any 
			 */ \
			if ((pool)->tsk_need_notify && ((pool)->tsk_any_wait || !(pool)->n_qtraces)) \
				__cpool_gp_w_wakeup_pool_taskl(pool); \
		}\
	} while (0)

#endif
