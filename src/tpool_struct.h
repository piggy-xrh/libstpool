#ifndef __TPOOL_STRUCT_H__
#define __TPOOL_STRUCT_H__

#include <time.h>
#include "ospx.h"
#include "xlist.h"

struct tpool_t;

enum {
	/* Task is waiting for being schuduled */
	TASK_F_WAIT  = (short)0x01,
	
	/* Task is being scheduled */
	TASK_F_SCHEDULING  = (short)0x02,
	
	/* Task has been swaped from the ready queue since the 
	 * pool that the task belong to has been marked suspened */
	TASK_F_SWAPED = (short)0x04,
	
	/* Task has been done completely, and the pool is calling
	 * @task_complete to give user a notification.
	 */
	TASK_F_DISPATCHING = (short)0x08,
};

struct task_ex_t {
	uint16_t pri:7;
	uint16_t pri_q:7;
	uint16_t pri_policy:2;
	union {
		uint16_t f_flags;
		struct {
			uint16_t f_stat:4;
			uint16_t f_vmflags:9;
			uint16_t f_pri:1;
			uint16_t f_push:1;
			uint16_t f_wait:1;
		};
	} uflags0;
#define f_flags    uflags0.f_flags
#define f_stat     uflags0.f_stat
#define f_vmflags  uflags0.f_vmflags
#define f_pri      uflags0.f_pri
#define f_push     uflags0.f_push
#define f_wait     uflags0.f_wait	
#define f_notified uflags0.f_notified
	struct task_t *tsk;
	struct xlink wait_link;
	struct xlink trace_link;
};

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
	THREAD_STAT_WOKEUP = (uint16_t)0x8000,
	THREAD_STAT_INNER  = THREAD_STAT_RM|THREAD_STAT_WOKEUP,
};

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
	/* The rest counter */
	uint16_t ncont_rest_counters;

	/* The current task that the thread is servering. */
	uint8_t is_dispatched_task;
	uint8_t resv;
	struct  task_ex_t *current_task;

#ifdef _OPTIMIZE_PTHREAD_CREATE
	struct xlink link_launcher;
#endif	
	/* Optimize */
	struct xlink link_free;
	long   thread_id;

	/* The task pool that the thread belongs to */
	struct tpool_t *pool;
	struct xlink link;
	
	/* If the thread has gotten a task from the pool,
	 * the thread will be pushed into the running queue
	 */
	struct xlink run_link;
};

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

/* The policy to schedule the tasks */
enum {
	/* Insert our task before the tasks who has the same
	 * priority exisiting in the pool.
	 */
	POLICY_PRI_SORT_INSERTBEFORE = 1,

    /* Insert our task after the tasks who has the same
	 * priority exisiting in the pool.
	 */
	POLICY_PRI_SORT_INSERTAFTER,
};

struct tpool_priq_t {
	struct xlink link;
	int    index;
	XLIST  task_q;
};

/* A physical CPU ---> n logic CPUs
 *
 * HT support:
 * 	 One more cores has the same core ID,
 * and all of the them have the same physical
 * CPU id.
 *		ht_support = cpu.nlogic_cores > cpu.nphysical_cores
 *
 */
struct cpu_t {
	int physical_cpu_id;
	int nlogic_cores;
	int nphysical_cores;
	int ht_support;
	int nthreads_parallel;
};

/* extensions */
enum {
	EV_TRG_THREADS = 1,
	EV_TRG_TASKS,
	EV_TRG_THREADS_OR_TASKS,
	EV_TRG_THREADS_AND_TASKS,
};

enum {
	EV_FLT_DISCARD = 1,
	EV_FLT_WAIT,
	EV_FLT_WAIT2,
	EV_FLT_PASS,
};

struct brf_stat_t {
	/* The number of current pending task of the pool */
	int ntasks_pendings;
	
	/* The number of running threads of the pool */
	int nthreads_running;
	
	/* Is the pool busy now ? */
	int evflt_busy;
};

struct evflt_res_t {
	uint32_t ev_flttyp:6;
	uint32_t ev_param:26;
};

struct event_t {
	int ev_threads_num;
	int ev_tasks_num;
	long ev_triggle_type;
	struct evflt_res_t (*ev_filter)(struct tpool_t *pool, struct event_t *ev, struct brf_stat_t *brfstat, struct task_t *task);
	void *ev_arg;
};

struct tpool_t {
	long status;
	int  paused;
	int  waiters, suspend_waiters;
	int  waiters_all;
	
	/* information of CPUs */
	int ncpus;
	struct cpu_t *cpu;
	int ncpu_threads, ntasks_cpu_suggest;

	/* Object memory pool */
#ifdef _USE_MPOOL	
	struct mpool_t *mp1, *mp2;
#endif
	/* @ref is the references of the pool, and the
	 * @user_ref is the references of users who is 
	 * using our pool.
	 */
	long ref, user_ref;
	size_t ntasks_added, ntasks_done, ntasks_dispatched;
	time_t tpool_created;
	void (*atexit)(struct tpool_t *, void *);
	void *atexit_arg;
	
	/* event filter */
	struct event_t ev;
	int ev_enable, ev_filter_wokeup;
	int ev_thread_waiters;
	int ev_task_waiters;
	OSPX_pthread_cond_t cond_ev;

	/* task throttle */
	int throttle_enabled;
	int64_t npendings;
	
	/* service threads env */
	XLIST ths, freelst, ths_waitq;
	char *buffer;
	long launcher;
	int  release_cleaning;
	int  ndispatchings;
	int  nthreads_running, nthreads_dying, nthreads_dying_run;
	int  nthreads_going_rescheduling, nthreads_waiters;
	int  maxthreads, minthreads;
	int  limit_threads_free, limit_threads_create_per_time;
	int  ncont_completions, limit_cont_completions;
	long threads_wait_throttle, acttimeo, randtimeo;
	int  nthreads_real_sleeping, nthreads_real_pool;
		
	/* Statics report */
	size_t nthreads_peak, ntasks_peak;

	/* priority queue */
	uint16_t pri_q_num, avg_pri;
	uint16_t pri_reschedule;
	struct tpool_priq_t *pri_q;

#ifdef _OPTIMIZE_PTHREAD_CREATE
	XLIST launcherq;
	int launcher_run;
	OSPX_pthread_cond_t cond_launcher;
	#define mut_launcher mut
#endif

	/* Rubbish env */	
#ifdef _CLEAN_RUBBISH_INBACKGROUND
	XLIST clq;
	int rubbish_run;
	OSPX_pthread_cond_t cond_garbage;
	#define mut_garbage mut
#endif
	/* condition var is more effecient than the
	 * semaphore in our pool implemention.
	 */
	OSPX_pthread_cond_t  cond;
	OSPX_pthread_cond_t  cond_ths;
	OSPX_pthread_cond_t  cond_comp;
	XLIST ready_q, trace_q, dispatch_q;
	OSPX_pthread_mutex_t mut;
};
#define POOL_Q_thread(link)    XCOBJ(link, struct tpool_thread_t)
#define POOL_RUNQ_thread(link) XCOBJEX(link, struct tpool_thread_t, run_link)
#define POOL_RMQ_thread(link)  XCOBJEX(link, struct tpool_thread_t, rm_link)

#define POOL_TRACEQ_task(link)  XCOBJEX(link, struct task_ex_t, trace_link)
#define POOL_RUNQ_task(link)    XCOBJEX(link, struct task_ex_t, wait_link)
#define POOL_READYQ_task(link)  XCOBJEX(link, struct task_ex_t, wait_link)
#endif
