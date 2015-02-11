#include "stpool.h"
#include "tpool.h"

/* Change logs:
 * 15-2-21:
 *       .Add @stpool_flush
 *       .Fix DEBUG bugs: mpool.c/@mpool_assert should lock the mpool while
 *                        checking the object ptr.
 */


/* stpool is just a simple Wrapper of the tpool */

const char *
stpool_version() {
	return "2015/02/09-2.3-libstpool-filter";
}

static void
tpool_hook_atexit(struct tpool_t *pool, void *arg) {
	free(pool);
}

HPOOL 
stpool_create(int maxthreads, int minthreads, int suspend, int pri_q_num) {
	struct tpool_t *pool;
	
	/* It does not need to load the ospx library since 
     * we do not call any APIs who must use the TLS datas.
	 */
	pool = (struct tpool_t *)malloc(sizeof(struct tpool_t));
	if (pool) {
		if (tpool_create(pool, pri_q_num, maxthreads, minthreads, suspend)) {
			free(pool);
			pool = NULL;
		} else {
			tpool_atexit(pool, tpool_hook_atexit, NULL);	

#ifdef _USE_MPOOL	
			tpool_use_mpool(pool);
#endif	
		}
	}

	return (HPOOL)pool;
}

long 
stpool_addref(HPOOL hp) {	
	return tpool_addref((struct tpool_t *)hp);
}

long 
stpool_release(HPOOL hp) {		
	return tpool_release((struct tpool_t *)hp, 0);	
}

/* We do not export the interface at present. */
void 
stpool_load_env(HPOOL hp) {
	tpool_load_env((struct tpool_t *)hp);	
}


void 
stpool_set_activetimeo(HPOOL hp, long acttimeo) {
	tpool_set_activetimeo((struct tpool_t *)hp, acttimeo); 
}

void 
stpool_adjust_abs(HPOOL hp, int maxthreads, int minthreads) {
	tpool_adjust_abs((struct tpool_t *)hp, 
					maxthreads, 
					minthreads);
}

void 
stpool_adjust(HPOOL hp, int maxthreads, int minthreads) {
	tpool_adjust((struct tpool_t *)hp, 
				 maxthreads, 
				 minthreads);
}

int
stpool_flush(HPOOL hp) {
	return tpool_flush((struct tpool_t *)hp);
}

void 
stpool_adjust_wait(HPOOL hp) {
	tpool_adjust_wait((struct tpool_t *)hp);
}

struct stpool_stat_t *
stpool_getstat(HPOOL hp, struct stpool_stat_t *stat) {
	struct tpool_stat_t *st;
	
	st = tpool_getstat((struct tpool_t *)hp, 
					  (struct tpool_stat_t *)stat);
	
	return (struct stpool_stat_t *)st;
}

const char *
stpool_status_print(HPOOL hp, char *buffer, size_t bufferlen) {
	return tpool_status_print((struct tpool_t *)hp, 
							buffer, 
							bufferlen);
}

long
stpool_gettskstat(HPOOL hp, struct stpool_tskstat_t *stat) {
	return tpool_gettskstat((struct tpool_t *)hp, 
	                        (struct tpool_tskstat_t *)stat);
}

int  
stpool_mark_task(HPOOL hp, struct sttask_t *tsk,
				int (*tskstat_walk)(struct stpool_tskstat_t *, void *),
				void *arg) {
	return tpool_mark_task((struct tpool_t *)hp, 
							(struct task_t *)tsk,
							(int (*)(struct tpool_tskstat_t *, void *))tskstat_walk,
							arg);
}

void 
stpool_throttle_enable(HPOOL hp, int enable) {
	tpool_throttle_enable((struct tpool_t *)hp, enable);
}

int  
stpool_throttle_disabled_wait(HPOOL hp, long ms) {
	return tpool_throttle_disabled_wait((struct tpool_t *)hp, ms);
}

int 
stpool_disable_rescheduling(HPOOL hp, struct sttask_t *tsk) {
	return tpool_disable_rescheduling((struct tpool_t *)hp,
	                      			  (struct task_t *)tsk);
}

void 
stpool_suspend(HPOOL hp, int wait) {
	tpool_suspend((struct tpool_t *)hp, wait);
}

void 
stpool_resume(HPOOL hp) {
	tpool_resume((struct tpool_t *)hp);
}

int  
stpool_add_task(HPOOL hp, struct sttask_t *tsk) {
	return tpool_add_task((struct tpool_t *)hp,
	                      (struct task_t *)tsk);
}

int  
stpool_add_routine(HPOOL hp, 
				int (*task_run)(void *), 
				int (*task_complete)(long, int , void *, struct stpriority_t *),
				void *arg) {
	return tpool_add_routine((struct tpool_t *)hp,
							task_run,
							(int (*)(long, int, void *, struct priority_t*))task_complete,
							arg);
}

int  
stpool_add_pri_task(HPOOL hp, struct sttask_t *tsk, int pri, int pri_policy) {
	return tpool_add_pri_task((struct tpool_t *)hp, 
							  (struct task_t *)tsk, 
							  pri, pri_policy); 
}

int  
stpool_add_pri_routine(HPOOL hp, 
					int (*task_run)(void *arg), 
					int (*task_complete)(long, int, void *, struct stpriority_t *),
					void *arg,
					int pri, int pri_policy) {
	return tpool_add_pri_routine((struct tpool_t *)hp,
								task_run,
								(int (*)(long, int, void *, struct priority_t*))task_complete,
								arg,
								pri, pri_policy);
}

void 
stpool_extract(struct sttask_t *task, void **task_run, void **task_complete, void **arg) {
	tpool_extract((struct task_t *)task, task_run, task_complete, arg);
}

int  
stpool_remove_pending_task(HPOOL hp, struct sttask_t *tsk, int dispatched_by_pool) {
	int ele;

	if (dispatched_by_pool)
		ele = tpool_remove_pending_task2((struct tpool_t *)hp,
	                      			 	(struct task_t *)tsk);
	else 
		ele = tpool_remove_pending_task((struct tpool_t *)hp,
	                      			 	(struct task_t *)tsk);

	return ele;
}

int  
stpool_wait(HPOOL hp, struct sttask_t *tsk, long ms) {
	return tpool_wait((struct tpool_t *)hp,
	                  (struct task_t *)tsk,
					  ms);
}

int  
stpool_waitex(HPOOL hp, int (*sttask_match)(struct stpool_tskstat_t *, void *), void *arg, long ms) {
	return tpool_waitex((struct tpool_t *)hp,
	                  (int (*)(struct tpool_tskstat_t *, void *))sttask_match,
					  arg,
					  ms);
}

struct stevent_t *
stpool_event_get(HPOOL hp, struct stevent_t *ev) {
	return (struct stevent_t *)tpool_event_get((struct tpool_t *)hp, 
											   (struct event_t *)ev);
}

void 
stpool_event_set(HPOOL hp, struct stevent_t *ev) {
	tpool_event_set((struct tpool_t *)hp, 
					(struct event_t *)ev);
}

int stpool_event_wait(HPOOL hp, long ms) {
	return tpool_event_wait((struct tpool_t *)hp, ms);
}

void stpool_event_pulse(HPOOL hp) {
	tpool_event_pulse((struct tpool_t *)hp);
}

