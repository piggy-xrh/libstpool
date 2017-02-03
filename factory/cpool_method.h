#ifndef __CPOOL_METHOD_H__
#define __CPOOL_METHOD_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <stdlib.h>

/** task */
typedef struct ctask ctask_t;
typedef struct cpool cpool_t;

struct scheduler_attr;
struct thread_attr;
struct scheduling_attr;
struct ctask_group_stat;

/** walk callback */
typedef long (*Visit_cb)(ctask_t *ptask, void *opaque);

typedef struct cpool_task_method {
	const size_t task_size;

	ctask_t *(*cache_get)       (void * ins);
	void     (*cache_put)       (void * ins, ctask_t *ptask);
	int      (*task_init)       (void * ins, ctask_t *ptask);
	void     (*task_deinit)     (void * ins, ctask_t *ptask);
	int      (*task_queue)      (void * ins, ctask_t *ptask);
	int      (*task_remove)     (void * ins, ctask_t *ptask, int dispatched_by_pool);
	void     (*task_mark)       (void * ins, ctask_t *ptask, long lflags);
	void     (*task_detach)     (void * ins, ctask_t *ptask);
	long     (*task_stat)       (void * ins, ctask_t *ptask, long *vm);
	int      (*task_wait)       (void * ins, ctask_t *ptask, long ms);
	int      (*task_wait_any)   (void * ins, ctask_t *entry[], int n, long ms);
	int      (*task_wsync)      (void * ins, ctask_t *ptask);
} cpool_task_method_t;

/** Basic pool method sets */
typedef struct cpool_basic_method {
	struct cpool_stat *(*stat)  (void * ins, struct cpool_stat *stat);
	char *(*scheduler_map_dump) (void * ins, char *buff, size_t bufflen);
	void  (*atexit)             (void * ins, void (*__atexit)(void *), void *opaque);
	long  (*addref)             (void * ins);
	long  (*release)            (void * ins);
	void  (*setattr)            (void * ins, struct thread_attr *attr);
	void  (*getattr)            (void * ins, struct thread_attr *attr);
	void  (*set_schedulingattr)  (void * ins, struct scheduling_attr *attr); 
	void  (*get_schedulingattr)  (void * ins, struct scheduling_attr *attr); 
	void  (*set_activetimeo)    (void * ins, long acttimeo, long randtimeo);
	void  (*adjust)             (void * ins, int max, int min);
	void  (*adjust_abs)         (void * ins, int max, int min);
	int   (*flush)              (void * ins);
	int   (*suspend)            (void * ins, long ms);
	void  (*resume)             (void * ins);
	int   (*remove_all)         (void * ins, int dispatched_by_pool);
	int   (*mark_all)           (void * ins, long lflags);
	int   (*mark_cb)            (void * ins, Visit_cb wcb, void *wcb_arg);
	int   (*wait_all)           (void * ins, long ms);
	void  (*throttle_enable)    (void * ins, int enable);
	int   (*throttle_wait)      (void * ins, long ms);	
	int   (*wait_any)           (void * ins, long ms);
	int   (*wait_cb)            (void * ins, Visit_cb wcb, void *wcb_arg, long ms);
} cpool_basic_method_t;

/** Advance pool method sets */
typedef struct cpool_advance_method {
	int   (*group_create)          (void * ins, const char *desc, int pri_q_num, int suspend);
	void  (*group_delete)          (void * ins, int gid);
	int   (*group_id)              (void * ins, const char *desc);
	char *(*group_desc)            (void * ins, int gid, char *desc_buff, size_t len);
	int   (*group_stat)            (void * ins, int gid, struct ctask_group_stat *gstat);
	int   (*group_stat_all)        (void * ins, struct ctask_group_stat **gstat);
	int   (*group_suspend)         (void * ins, int gid, long ms);
	int   (*group_suspend_all)     (void * ins, long ms);
	void  (*group_resume)          (void * ins, int gid);
	void  (*group_resume_all)      (void * ins);
	int   (*group_setattr)         (void * ins, int gid, struct scheduler_attr *attr);
	int   (*group_getattr)         (void * ins, int gid, struct scheduler_attr *attr);
	void  (*group_throttle_enable) (void * ins, int gid, int enable);
	int   (*group_throttle_wait)   (void * ins, int gid, long ms);
	int   (*group_remove_all)      (void * ins, int gid, int dispatched_by_pool);
	int   (*group_mark_all)        (void * ins, int gid, long lflags);
	int   (*group_mark_cb)         (void * ins, int gid, Visit_cb wcb, void *wcb_arg);
	int   (*group_wait_all)        (void * ins, int gid, long ms);
	int   (*group_wait_cb)         (void * ins, int gid, Visit_cb wcb, void *wcb_arg, long ms);
	int   (*group_wait_any)        (void * ins, int gid, long ms);
} cpool_advance_method_t;

#endif
