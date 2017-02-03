#ifndef __CPOOL_CORE_STRUCT_H__
#define __CPOOL_CORE_STRUCT_H__
/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  cpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <time.h>
#include "ospx_compatible.h"
#include "list.h"
#include "objpool.h"

typedef struct cpool_core cpool_core_t;
typedef struct cpool_thread thread_t;

/** error Reasons for task */
enum {
	eReason_ok = 0x01,
	eReason_removed = 0x02,
	eReason_core_destroying = 0x04,
};

/** The definition of the private data of the task object */
typedef struct basic_task {
	/**
	 * A const string to describle the task 
	 */
	const char *task_desc;
	
	/**
	 * The working routine of the task 
	 */
	void (*task_run)(struct basic_task *ptask);

	/**
	 * The error handler of the task
	 */
	void (*task_err_handler)(struct basic_task *ptask, long eReasons);
	
	/**
	 * The task argument reserved for user
	 */
	void *task_arg;

	/**
	 * The task code reserved for user
	 */
	int task_code;
	
	/**
	 * Link node 
	 */
	struct list_head link;

} basic_task_t;
#define TASK_CAST_CORE(ptask) ((basic_task_t *)(ptask))

typedef enum eEvent {
	eEvent_F_thread = 0x01,
	eEvent_F_free = 0x02,
	eEvent_F_core_suspend = 0x04,
	eEvent_F_core_resume = 0x08,
	eEvent_F_destroying = 0x10,
	eEvent_F_shutdown = 0x20,
} eEvent_t;

typedef struct cpool_core_method {
	const char * desc;
	const size_t task_size;
	
	int    (*ctor)(void *priv);
	void   (*notifyl)(void *priv, eEvent_t events);
	int    (*gettask)(void *priv, thread_t *self);
	long   (*err_reasons)(basic_task_t *ptask);
	void   (*finished)(void *priv, thread_t *self, basic_task_t *ptask, long eReasons);
	void   (*dtor)(void *priv);
} cpool_core_method_t;

/** Status of the working threads */
enum {
	THREAD_STAT_INIT = 0,    /** Initializing */ 
	THREAD_STAT_JOIN,        /** Joing */
	THREAD_STAT_WAIT = 2,    /** Waiting task */ 
	THREAD_STAT_RUN,         /** Doing task */	
	THREAD_STAT_COMPLETE = 4,/** Task completed */ 
	THREAD_STAT_TIMEDOUT,    /** Timedout */
	THREAD_STAT_FREE = 6,    /** Free */         
	THREAD_STAT_FORCE_QUIT,  /** Pool is being destroyed */
	THREAD_STAT_LEAVE = 8,   /** Leaving */
	THREAD_STAT_RM     = (uint16_t)0x1000, /** Thread is in the RM queue */
	THREAD_STAT_GC     = (uint16_t)0x2000,
	THREAD_STAT_FLUSH  = (uint16_t)0x4000,
	THREAD_STAT_INNER  = THREAD_STAT_RM|THREAD_STAT_GC|THREAD_STAT_FLUSH
};

/** Task type */
enum {
	/** 
	 * A task retreived by \@cpool_core_method::gettask
	 */
	TASK_TYPE_NORMAL, 
	
	/**
	 * A task that removed by user or the Core
	 */
	TASK_TYPE_DISPATCHED,
	
	/**
	 * A garbage collection task 
	 */
	TASK_TYPE_GC,
};

/** Condition variable */
typedef struct cond_attr {
	int initialized;
	OSPX_pthread_cond_t cond;
} cond_attr_t;

/** The definition of the thread object */
struct cpool_thread {
	OSPX_pthread_t thread_id;
	struct list_head thq;
	struct list_head link_free;

	int run;
	/**
	 * Should the structure be released ? 
	 */
	int structure_release;
	
	/**
	 * Status of this threads 
	 */
	long status;
	long flags;

	/**
	 * The number of tasks that the thread has been done. 
	 */
#ifndef NDEBUG	
	long ntasks_processed; 
#endif	
	/**
	 * The last timeo value to wait for tasks 
	 */
	long last_to;
	
	/**
	 * The GC counter 
	 */
	int ncont_GC_counters;

	/**
	 * The GC cache number 
	 */
	int ncache_limit;
	
	/**
	 * The current task that the thread is servering for. 
	 */
	int  trace_args;
	long task_type;  
	basic_task_t *current_task;
		
	/**
	 * The task pool that the thread belongs to 
	 */
	cpool_core_t *core;
	struct list_head link;
	
	/**
	 * If the thread has gotten a task from the pool,
	 * the thread will be pushed into the running queue
	 */
	struct list_head run_link;	
	
	/**
	 * A dispatching queue 
	 */
	struct list_head dispatch_q;
	
	/**
	 * A temple object cache
	 */
	int local_cache_limited;
	smlink_q_t qcache;

	/**
	 * Optimize 
	 */
	int b_waked;
#ifndef NDEBUG	
	int n_reused;
#endif
	struct cond_attr *cattr;
};
#define __curtask  self->current_task

/* The status of the pool */
enum {
	CORE_F_creating   = (long)0x01, 
	CORE_F_created    = (long)0x02,
	CORE_F_destroying = (long)0x04,
	CORE_F_destroyed  = (long)0x08,
	CORE_F_dynamic    = (long)0x0100
};

/* Cache attribute */
typedef struct cache_attr {
	int nGC_cache;
	int nGC_wakeup;
	
	int  nGC_one_time;
	long rest_timeo;
	long delay_timeo;
} cache_attr_t;

/** The definition of the pool object */
struct cpool_core {
	const char *desc;
	time_t start;

	long status;
	long lflags;
	int  paused;
	int  release_cleaning;
	
	/** core methods */
	const cpool_core_method_t *me;
	
	/** Reserved for user */
	void *priv;
	objpool_t objp_task;
	smcache_t *cache_task;
	struct cache_attr cattr;
	int thread_local_cache_limited;

	/**
	 * free notification
	 */
	int event_free_notified;
	
	/** 
	 * env for GC sub system 
	 */
	int b_GC_delay;
	basic_task_t sys_GC_task;
	long us_gc_left_timeo;
	uint64_t  us_last_gcclock;
	thread_t *GC;
	
	/**
	 * @ref is the references of the pool, and the
	 * @user_ref is the references of users who is 
	 * using our pool.
	 */
	long ref, user_ref;
	time_t tpool_created;
	void (*atexit)(void *);
	void *atexit_arg;
	
	/** timeo */
	unsigned crttime;

	/** global env */
	long acttimeo, randtimeo;
	int  limit_threads_create_per_time;
	
	/** Scheduler env */
	int npendings;
	int max_tasks_qscheduling;
	int max_tasks_qdispatching;

	/** service threads env */
	char *buffer;
	int  maxthreads, minthreads;
	struct list_head ths, ths_waitq;
	int  n_qths, n_qths_waked, n_qths_wait;
	int  nthreads_running, nthreads_dying, nthreads_dying_run;
	int  nthreads_real_sleeping, nthreads_real_pool, nthreads_real_free;
	OSPX_pthread_cond_t cond_ths;
	OSPX_pthread_attr_t thattr;
	smcache_t cache_thread;
	
	/** Statics reports */
	int nthreads_peak;

	/** task env */
	long eReasons0, eReasons1;
	int n_qdispatchs;
	struct list_head dispatch_q;
	OSPX_pthread_mutex_t mut;
};

/**
 * The status of the Core
 */
struct cpool_core_stat {
	const char *desc;
	time_t start;
	long status;
	int  user_ref;
	int  max, min;
	long acttimeo;
	long randtimeo;
	int  paused;
	int  ncaches;
	int  n_qpendings;
	int  n_qdispatchs;
	int  n_qths;
	int  n_qths_wait;
	int  n_qths_waked;
	int  nths_dying;
	int  nths_running;
	int  nths_dying_run;
	int  nths_free_effective;
	int  nths_effective;
	int  nths_peak;
};

#endif
