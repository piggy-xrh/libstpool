/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *	  Stpool is portable and efficient tasks pool library, it can works on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 * 	  blog: http://www.oschina.net/code/snippet_1987090_44422
 */

#ifndef __TPOOL_STRUCT_H__
#define __TPOOL_STRUCT_H__

#include <time.h>
#include "ospx.h"
#include "list.h"

struct tpool_t;

/* Status of the task */
enum {
	/* Task is waiting for being schuduled */
	TASK_STAT_WAIT  = (short)0x01,
	
	/* Task is being scheduled */
	TASK_STAT_SCHEDULING  = (short)0x02,
	
	/* Task has been swaped from the ready queue since the 
	 * pool that the task belong to has been marked suspened */
	TASK_STAT_SWAPED = (short)0x04,
	
	/* Task has been done completely, and the pool is calling
	 * @task_complete to give user a notification. */
	TASK_STAT_DISPATCHING = (short)0x08,
	
	/* The task should be added into the pool automatically 
	 * after having done the task */
	TASK_STAT_WAIT_PENDING = (short)0x10,
};

/* f_mask of the task */
enum {
	/* It indicates that the priority of the task is 
	 * not zero */
	TASK_F_PRI    = 0x01,
	
	/* It indicates that the task should be pushed into
	 * the tail queue directly */
	TASK_F_PUSH   = 0x02,

	/* It indicates that the task should be recycled
	 * by the pool automatically */
	TASK_F_ONCE   = 0x04,
	
	/* It indicates that the task object is allocated 
	 * by the memory pool */
	TASK_F_MPOOL  = 0x08,

	/* It indicates that the task's scheduling attribute
	 * has been changed */
	TASK_F_ADJPRI = 0x10,
	
	TASK_F_PRI_ONCE = 0x20,
};

/* f_vmflags of the task */
enum {
	/* @task_run has been executed */
	TASK_VMARK_DONE = 0x0001,
	
	/* The task is removed by @tpool_remove_pending_task/@tpool_mark_task(ex)
	 *    The user can mark tasks with TASK_VMARK_REMOVE_BYPOOL or 
	 * TASK_VMARK_REMOVE_DIRECTLY, and as a result, the tasks will be removed 
	 * from the pending queue.
     *    If task is marked with TASK_VMARK_REMOVE_BYPOOL, @task_complete 
	 * will be called by the pool. or @task_complete will be called by the 
	 * functions who marks the task. 
	 */
	TASK_VMARK_REMOVE_BYPOOL = 0x0004,
	TASK_VMARK_REMOVE_DIRECTLY = 0x0008,
	
	TASK_VMARK_REMOVE = TASK_VMARK_REMOVE_BYPOOL|TASK_VMARK_REMOVE_DIRECTLY,

	/* The pool is being destroyed */
	TASK_VMARK_POOL_DESTROYING = 0x0010,	
	
	/* The task should be done again */
	TASK_VMARK_DO_AGAIN = 0x0020,
	
	/* Task can(not) be delived into the pool */
	TASK_VMARK_DISABLE_QUEUE = 0x0040,
	TASK_VMARK_ENABLE_QUEUE = 0x0080,
};


/* The policy to schedule the tasks */
enum {
	/* Insert our task before the tasks who has the same
	 * priority exisiting in the pool.
	 */
	P_SCHE_TOP = 1,

    /* Insert our task after the tasks who has the same
	 * priority exisiting in the pool.
	 */
	P_SCHE_BACK,
};

/* Priority attribute of the task */
struct xschattr_t {
	int permanent;

	/* Priority of the task [0~99] */
	int pri;

	/* Priority policy of the task (P_SCHE_XX) */
	int pri_policy;
};

/* The definition of the task object */
struct task_t {
	/* A const string to describle the task */
	const char *task_name;
	
	/* @task_run will be called when the task is scheduled by the pool. 
     *  user can do their works in this function.
	 */
	int  (*task_run)(struct task_t *ptsk);	

	/*  	If @task_complete is not NULL, it will be called when one of the 
	 *  conditions below matches.
	 *       1. @task_run has been executed by the pool.
	 *       2. The task is removed from the pool by @tpool_remove_pending_task
	 *          or @tpool_mark_task(ex)
	 *
	 *   NOTE:
	 *	   	If @task_run has been excuted by the pool, the argument @vmflags will
	 * owns the mask TASK_VMARK_DONE, and the @task_code will be set to the value
	 * returned by @task_run. or the the @task_code will be set properly. 
	      (@see the error codes describled in the tpool.h)
	 */
	void (*task_complete)(struct task_t *ptsk, long vmflags, int task_code);

	/* The argument reserved for task */
	void *task_arg;
	
	/* The recent pool into which the task is added */
	struct tpool_t *hp_last_attached;
	
	/* The servering thread */
	struct tpool_thread_t *th;

	/* The reference of the task */
	uint8_t ref;
	uint8_t do_again:1;
	uint8_t resv:7;

	/* The priority attribute of the task */
	uint16_t pri:7;
	uint16_t pri_q:7;
	uint16_t pri_policy:2;

	/* Whether the task has been detached to the pool */
	uint8_t *pdetached;
	
	/* Flags of the task */
	union {
		uint32_t f_flags;
		struct {
			uint32_t f_stat:8;
			uint32_t f_vmflags:10;
			uint32_t f_mask:6;
			uint32_t resv:8;
		};
	} uflags0;
#define f_flags    uflags0.f_flags
#define f_vmflags  uflags0.f_vmflags
#define f_stat     uflags0.f_stat
#define f_mask     uflags0.f_mask
	struct list_head wait_link;
	struct list_head trace_link;
};


/* Status of the working threads */
enum {
	THREAD_STAT_INIT,        /*Initializing*/ 
	THREAD_STAT_JOIN,        /*Joing*/
	THREAD_STAT_WAIT,        /*Waiting task*/ 
	THREAD_STAT_RUN,         /*Doing task*/	
	THREAD_STAT_COMPLETE,    /*Task completed */ 
	THREAD_STAT_TIMEDOUT,    /*Timedout*/
	THREAD_STAT_FREE,        /*Free*/         
	THREAD_STAT_FORCE_QUIT,  /*Pool is being destroyed*/
	THREAD_STAT_LEAVE,       /*Leaving*/
	THREAD_STAT_RM     = (uint16_t)0x4000, /* Thread is in the RM queue */
	THREAD_STAT_GC     = (uint16_t)0x8000,
	THREAD_STAT_INNER  = THREAD_STAT_RM|THREAD_STAT_GC
};

/* Task type */
enum {
	/* A removed task */
	TASK_TYPE_DISPATCHED = 0x1,
	
	/* A garbage collection task */
	TASK_TYPE_GC = 0x2,
};

/* The definition of the thread object */
struct tpool_thread_t {
	uint8_t run;

	/* Should the structure be released ? */
	uint8_t structure_release;
	
	/* Status of this threads */
	uint16_t status;

	/* The number of tasks that the thread has been done. */
#ifndef NDEBUG	
	uint32_t ntasks_done; 
#endif	
	/* The last timeo value to wait for tasks */
	long last_to;
	
	/* The GC counter */
	uint32_t ncont_GC_counters;

	/* The rest counter */
	uint16_t ncont_rest_counters;
	
	/* The current task that the thread is servering for. */
	uint8_t  task_type;  
	uint8_t  detached;
	void (*task_complete)(struct task_t *, long, int);
	struct   task_t *current_task;
	
	/* GC env */
	struct list_head clq;

	/* Optimize */
	struct list_head link_free;
	OSPX_pthread_t thread_id;
	struct list_head  thq;

	/* The task pool that the thread belongs to */
	struct tpool_t *pool;
	struct list_head link;
	
	/* If the thread has gotten a task from the pool,
	 * the thread will be pushed into the running queue
	 */
	struct list_head run_link;	
};

/* The status of the pool */
enum {
	POOL_F_CREATING    = (long)0x01, 
	POOL_F_CREATED     = (long)0x02,
	POOL_F_DESTROYING  = (long)0x04,
	POOL_F_DESTROYED   = (long)0x08,
	POOL_F_WAIT        = (long)0x10,
};

struct tpool_stat_t {
	long ref;                    /* The user refereces */
	time_t created;              /* The time when the pool is created */
	int pri_q_num;               /* The number of the priority queue */
	int throttle_enabled;        /* Is throttle swither on ? */
	int suspended;               /* Is pool suspended ? */
	int maxthreads;              /* Max servering threads number */
	int minthreads;              /* Min servering threads number */
	int curthreads;              /* The number of threads exisiting in the pool */
	int curthreads_active;       /* The number of threads who is scheduling tasks */
	int curthreads_dying;        /* The number of threads who has been marked died by @stpool_adjust(_abs) */
	long acttimeo;               /* Max rest time of the threads (ms) */
	size_t tasks_peak;           /* The peak of the tasks number */
	size_t threads_peak;         /* The peak of the threads number */
	size_t tasks_added;          /* The number of tasks that has been added into the pool since the pool is created */
	size_t tasks_done;           /* The number of tasks that the pool has done since the pool is created */
	size_t tasks_dispatched;     /* The number of completion routines that the pool has called for removed tasks */	
	size_t cur_tasks;            /* The number of tasks existing in the pool */
	size_t cur_tasks_pending;    /* The number of tasks who is waiting for being scheduled */
	size_t cur_tasks_scheduling; /* The number of tasks who is being scheduled */
	size_t cur_tasks_removing;   /* The number of tasks who is marked removed */
};

struct tpool_tskstat_t {
	/* Status of the task */
	long  stat;
	
	/* The flags of the task */
	long  vmflags;
	
	/* Current priority of the task */
	int   pri;

	/* The object of the task */
	struct task_t *task;
};

/* Priority queue */
struct tpool_priq_t {
	struct list_head link;
	int    index;
	struct list_head task_q;
};

/* Cache attribute */
struct cache_attr_t {
	size_t nGC_cache;
	size_t nGC_wakeup;
	
	size_t nGC_one_time;
	long   nGC_rest_to;
	long   nGC_delay_to;
};

/* The definition of the pool object */
struct tpool_t {
	long status;
	int  paused;
	int  release_cleaning;
	
	/* Waiters' env */
	int  wokeup, waiters, suspend_waiters;
	struct list_head wq;

	/* Object memory pool */
	struct mpool_t *mp;

	/* GC env */
	long nGC, nGC_queued;
	char b_GC_delay, b_GC_queued;
	struct cache_attr_t cattr;
	struct list_head clq;
	struct tpool_thread_t *GC;
	struct task_t sys_GC_task;
	struct task_t sys_GC_notify_task;
	
	/* @ref is the references of the pool, and the
	 * @user_ref is the references of users who is 
	 * using our pool.
	 */
	long ref, user_ref;
	size_t ntasks_added, ntasks_done, ntasks_dispatched;
	time_t tpool_created;
	void (*atexit)(struct tpool_t *, void *);
	void *atexit_arg;
	
	/* global env */
	long threads_wait_throttle, acttimeo, randtimeo;
	int  limit_threads_free, limit_threads_create_per_time;
	
	/* task env */
	int  ndispatchings;
	int64_t npendings, n_qtrace, n_qdispatch;
	struct list_head ready_q, trace_q, dispatch_q;
	OSPX_pthread_cond_t  cond_comp;
	
	/* throttle env */
	int throttle_enabled;
	OSPX_pthread_cond_t cond_ev;
	
	/* variable for pending event */
	int npendings_ev;

	/* service threads env */
	char *buffer;
	int  maxthreads, minthreads;
	struct list_head ths, freelst, ths_waitq;
	int  n_qths, n_qths_wait;
	OSPX_pthread_t launcher;
	int  nthreads_running, nthreads_dying, nthreads_dying_run;
	int  nthreads_going_rescheduling, nthreads_waiters;
	int  ncont_completions, limit_cont_completions;
	int  nthreads_real_sleeping, nthreads_real_pool;
	OSPX_pthread_cond_t cond_ths;
	OSPX_pthread_attr_t thattr;

	/* Statics report */
	int64_t nthreads_peak, ntasks_peak;

	/* priority queue */
	uint16_t pri_q_num, avg_pri;
	struct tpool_priq_t *pri_q;

	/* condition var is more effecient than the
	 * semaphore in our pool implemention.
	 */
	OSPX_pthread_cond_t  cond;
	OSPX_pthread_mutex_t mut;
};

#endif
