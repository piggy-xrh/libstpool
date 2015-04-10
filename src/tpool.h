#ifndef __TASK_POOL_H__
#define __TASK_POOL_H__
/* piggy_xrh@163.com */

#include "tpool_struct.h"

/* Error codes */
enum {	
	/* System is out of memeory */
	POOL_ERR_NOMEM = 1,
	
	/* Task pool is beging destroyed */
	POOL_ERR_DESTROYING = 2, 
	
	/* Task pool has been destroyed or has not been created */
	POOL_ERR_NOCREATED  = 3,

	/* The throttle of the pool is disabled */
	POOL_ERR_THROTTLE = 4,

	/* The task has been removed by user */
	POOL_TASK_ERR_REMOVED = 5,
						
	/* The task has been attached to a pool */
	POOL_TASK_ERR_BUSY = 7,	
	
	/* The errno has been set */
	POOL_ERR_ERRNO = 0x1000,
};

/* --------------APIs about the task ------------------- */
#define tpool_task_init(ptsk, name, run, complete, arg) \
	do {\
		(ptsk)->task_name = name;\
		(ptsk)->task_run  = run;\
		(ptsk)->task_complete = complete;\
		(ptsk)->task_arg = arg;\
	} while (0)

struct task_t *tpool_new_task(struct tpool_t *pool);
void tpool_delete_task(struct tpool_t *pool, struct task_t *ptsk);
void tpool_task_setschattr(struct task_t *ptsk, struct xschattr_t *attr);
void tpool_task_getschattr(struct task_t *ptsk, struct xschattr_t *attr);

/* --------------APIs about the pool ------------------- */
int  tpool_create(struct tpool_t *pool, int q_pri, int maxthreads, int minthreads, int suspend);
void tpool_use_mpool(struct tpool_t *pool);
void tpool_load_env(struct tpool_t *pool);
void tpool_atexit(struct tpool_t *pool, void (*atexit_func)(struct tpool_t *pool, void *arg), void *arg);
long tpool_addref(struct tpool_t *pool);
long tpool_release(struct tpool_t *pool, int clean_wait);
void tpool_set_activetimeo(struct tpool_t *pool, long acttimeo);
void tpool_adjust_abs(struct tpool_t *pool, int maxthreads, int minthreads);
void tpool_adjust(struct tpool_t *pool, int maxthreads, int minthreads);
int  tpool_flush(struct tpool_t *pool);
void tpool_adjust_wait(struct tpool_t *pool);
struct tpool_stat_t *tpool_getstat(struct tpool_t *pool, struct tpool_stat_t *stat);
const char *tpool_status_print(struct tpool_t *pool, char *buffer, size_t bufferlen);
long tpool_gettskstat(struct tpool_t *pool, struct tpool_tskstat_t *st);
long tpool_mark_task(struct tpool_t *pool, struct task_t *ptsk, long lflags);
int  tpool_mark_task_ex(struct tpool_t *pool, 
					 long (*tskstat_walk)(struct tpool_tskstat_t *stat, void *arg),
					 void *arg);
int  tpool_enable_rescheduling_task(struct tpool_t *pool, struct task_t *ptsk, int enable); 
void tpool_throttle_enable(struct tpool_t *pool, int enable);
int  tpool_throttle_wait(struct tpool_t *pool, long ms);
void tpool_suspend(struct tpool_t *pool, int wait);
void tpool_resume(struct tpool_t *poo);
int  tpool_add_task(struct tpool_t *pool, struct task_t *ptsk);
int  tpool_add_routine(struct tpool_t *pool, 
					const char *name, int (*task_run)(struct task_t *ptsk), 
					void (*task_complete)(struct task_t *ptsk, long vmflags, int task_code),
					void *arg, struct xschattr_t *attr);
int  tpool_remove_pending_task(struct tpool_t *pool, int dispatched_by_pool);
void tpool_detach_task(struct tpool_t *pool, struct task_t *ptsk); 
int  tpool_task_wait(struct tpool_t *pool,  struct task_t *ptsk, long ms);
int  tpool_task_wait2(struct tpool_t *pool, struct task_t *entry, int n, long ms);
int  tpool_task_wait3(struct tpool_t *pool, struct task_t *entry, int *n, long ms);
int  tpool_task_waitex(struct tpool_t *pool, int (*task_match)(struct tpool_tskstat_t *stat, void *arg), void *arg, long ms); 
int  tpool_pending_leq_wait(struct tpool_t *pool,  int n_max_pendings, long ms); 

#define WK_T_DISABLE_WAIT 0x01  
#define WK_T_WAIT   0x02        
#define WK_T_WAIT2  0x04        
#define WK_T_WAIT3  0x08        
#define WK_T_WAITEX 0x10        
#define WK_T_PENDING_WAIT 0x20  
#define WK_T_WAIT_ALL  (long)-1 
void tpool_wakeup(struct tpool_t *pool, long wakeup_type);

#endif
