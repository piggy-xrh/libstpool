#ifndef __TASK_POOL_H__
#define __TASK_POOL_H__
/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

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
	
	/* The task has been marked with TASK_VMARK_DISABLE_QUEUE */
	POOL_TASK_ERR_DISABLE_QUEUE = 8,

	/* The errno has been set */
	POOL_ERR_ERRNO = 0x1000,
};

typedef long (*TWalk_cb)(struct tpool_tskstat_t *stat, void *arg);

/* For dev-c++ env */
#ifdef __cplusplus
extern "C" {
#endif

/* --------------APIs about the task ------------------- */
#define tpool_task_init(ptsk, name, run, complete, arg) \
	do {\
		(ptsk)->task_name = name;\
		(ptsk)->task_run  = run;\
		(ptsk)->task_complete = complete;\
		(ptsk)->task_arg = arg;\
		(ptsk)->f_vmflags = TASK_VMARK_ENABLE_QUEUE;\
		(ptsk)->f_mask |= TASK_F_PUSH; \
		(ptsk)->ref = 0; \
		(ptsk)->pri = 0; \
		(ptsk)->f_stat = 0; \
		(ptsk)->pri_policy = P_SCHE_BACK; \
	} while (0)

#define tpool_task_init2(ptsk, name, run, complete, arg) \
	do { \
		memset(ptsk, 0, sizeof(struct task_t)); \
		tpool_task_init(ptsk, name, run, complete, arg); \
	} while (0) 

void tpool_task_setschattr(struct task_t *ptsk, struct xschattr_t *attr);
void tpool_task_getschattr(struct task_t *ptsk, struct xschattr_t *attr);
#define tpool_task_set_userflags(ptsk, uflags) ((ptsk)->user_flags = uflags & 0x7f, (ptsk)->user_flags)
#define tpool_task_get_userflags(ptsk) (ptsk)->user_flags
#define tpool_task_is_free(ptsk) (0 == (ptsk)->f_stat)

/* --------------APIs about the pool ------------------- */
int  tpool_create(struct tpool_t *pool, const char *desc, int q_pri, int maxthreads, int minthreads, int suspend);
#define tpool_thread_getscheattr(pool, att) do {*(att) = (pool)->thattr;} while (0)
#define tpool_thread_setscheattr(pool, att) do {(pool)->thattr = *(att);} while (0)

void tpool_load_env(struct tpool_t *pool);
void tpool_adjust_cache(struct tpool_t *pool, struct cache_attr_t *attr, struct cache_attr_t *oattr); 
void tpool_atexit(struct tpool_t *pool, void (*atexit_func)(struct tpool_t *pool, void *arg), void *arg);
long tpool_addref(struct tpool_t *pool);
long tpool_release(struct tpool_t *pool, int clean_wait);
void tpool_set_activetimeo(struct tpool_t *pool, long acttimeo, long randtimeo);
void tpool_adjust_abs(struct tpool_t *pool, int maxthreads, int minthreads);
void tpool_adjust(struct tpool_t *pool, int maxthreads, int minthreads);
int  tpool_flush(struct tpool_t *pool);
void tpool_adjust_wait(struct tpool_t *pool);
struct tpool_stat_t *tpool_getstat(struct tpool_t *pool, struct tpool_stat_t *stat);
const char *tpool_status_print(struct tpool_t *pool, char *buffer, size_t bufferlen);
long tpool_gettskstat(struct tpool_t *pool, struct tpool_tskstat_t *st);
long tpool_mark_task(struct tpool_t *pool, struct task_t *ptsk, long lflags);
int  tpool_mark_task_cb(struct tpool_t *pool, TWalk_cb twcb, void *twcb_arg);
void tpool_throttle_enable(struct tpool_t *pool, int enable);
int  tpool_throttle_wait(struct tpool_t *pool, long ms);
void tpool_suspend(struct tpool_t *pool, int wait);
void tpool_resume(struct tpool_t *pool);
int  tpool_add_task(struct tpool_t *pool, struct task_t *ptsk);
int  tpool_add_routine(struct tpool_t *pool, 
					const char *name, int (*task_run)(struct task_t *ptsk), 
					void (*task_complete)(struct task_t *ptsk, long vmflags, int task_code),
					void *arg, struct xschattr_t *attr);
int  tpool_remove_pending_task(struct tpool_t *pool, int dispatched_by_pool);
void tpool_detach_task(struct tpool_t *pool, struct task_t *ptsk); 
int  tpool_task_wait_cb(struct tpool_t *pool, TWalk_cb twcb, void *twcb_arg, long ms); 
int  tpool_task_wait_entry(struct tpool_t *pool, struct task_t *entry[], int n, int wait_all, long ms);
int  tpool_status_wait(struct tpool_t *pool,  int n_max_pendings, long ms); 
void tpool_wakeup(struct tpool_t *pool, long wakeup_id);

#ifdef __cplusplus
}
#endif

#endif
