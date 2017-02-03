#ifndef __CPOOL_GORUP_STRUCT_H__
#define __CPOOL_GORUP_STRUCT_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_factory.h"
#include "cpool_core_struct.h"
#include "cpool_com_priq.h"

#define M_GROUP "PGroup"

typedef struct ctask_trace ctask_trace_t;
typedef struct ctask_entry ctask_entry_t;
typedef struct cpool_gp cpool_gp_t;

struct ctask_trace
{
	/**
	 * A const string to describle the task 
	 *
	 * User should set it before his delivering the task into the pool
	 */
	const char *task_desc;
	
	/**
	 * The working routine of the task 
	 *
	 * User should set it before his delivering the task into the pool
	 */
	void (*task_run)(struct ctask *ptask);

	/**
	 * The error handler of the task
	 *
	 * User should set it before his delivering the task into the pool
	 */
	void (*task_err_handler)(struct ctask *ptask, long eReasons);
	
	/**
	 * The task argument reserved for user
	 *
	 * User should set it before his delivering the task into the pool
	 */
	void *task_arg;

	/**
	 * The task code reserved for user
	 */
	int task_code;
	
	/**
	 * Link node 
	 *
	 * (It is used by the library, do not touch it)
	 */
	struct list_head link;

	/** 
	 * The task pool 
	 *
	 * User should set it before his delivering the task into the pool
	 */
	cpool_t *pool;
	
	/**
	 * The flags reserved for the library (do not touch it)
	 */
	uint8_t f_sys_flags;
	
	/**
	 * The priority value [0 ~99)
	 *
	 * User should set it before his delivering the task into the pool
	 */
	uint8_t  pri;

	/**
	 * The scheduling policy
	 *
	 * User should set it before his delivering the task into the pool
	 */
	uint8_t  pri_policy;
	
	/**
	 * The priority queue 
	 *
	 * (It is set by the library, do not touch it)
	 */
	uint8_t  priq;
	
	/** 
	 * Flags reserved for user 
	 */
	uint16_t user_flags;
	
	/**
	 * Group id (Set by user)
	 */
	uint8_t  gid;

	/**
	 * Task status (Read-only)
	 *
	 * If it is a none zero value, it indicates that the task is in progress.
	 * and user should dot change the \@gid and \@pool until it is changed to
	 * zero.
	 */
	uint8_t  f_stat;
	
	/**
	 * Task mark  (Read only)
	 */
	uint16_t  f_vmflags;
		
	/**
	 * The current reference of the task (Read only)
	 */
	uint8_t ref;

	/**
	 * A inner flag used by \@cpool_gp_wait_any
	 */
	uint8_t f_global_wait;
	
	/**
	 * Task seq
	 */
	uint32_t  seq;
	
	/**
	 * detatch status
	 */
	uint16_t *pdetached;
	
	/**
	 * reserved param
	 */
	uint16_t  reserved;

	/**
	 * The serving thread
	 */
	thread_t *thread;

	/**
	 * Trace link node
	 */
	struct list_head trace_link;
};
#define TASK_CAST_TRACE(ptask)  ((ctask_trace_t *)(ptask))

#define SLOT_F_FREE       0x01
#define SLOT_F_DESTROYING 0x02
#define SLOT_F_THROTTLE   0x04
#define SLOT_F_ACTIVE     0x08

struct ctask_entry
{
	/**
	 * Created time
	 */
	time_t created;

	/**
	 * entry id 
	 */
	int  id;
	
	/**
	 * reference 
	 */
	long tsk_wref, ev_wref;

	/**
	 * name buffer 
	 */
	char *name;
	int   name_fixed;
	
	/**
	 * Flags 
	 */
	long lflags;

	/**
	 * pool 
	 */
	cpool_gp_t *pool;
	int index;

	/**
	 * Priority queue
	 */
	int priq_max_num;
	cpriq_container_t c;

	/**
	 * Trace queue
	 */
	int n_qtraces;
	struct list_head *trace_q;
	
	/**
	 * Scheduler env
	 */
	int paused;
	int limit_tasks;
	int limit_paralle_tasks;
	int receive_benifits;
	
	int npendings_eff;
	ctask_trace_t *top;

	/**
	 * Status
	 */
	int npendings;
	int ndispatchings, nrunnings;
	int ntasks_processed;
	
	/**
	 * WAIT env 
	 */
	int *ev_need_notify;
	int tsk_need_notify;
	int tsk_any_wait;
	OSPX_pthread_cond_t *cond_task;
	OSPX_pthread_cond_t *cond_ev;
	OSPX_pthread_cond_t *cond_sync;
};

struct cpool_gp {
	/**
	 * The Core
	 */
	cpool_core_t *core;
	
	/**
	 * The function masks (CPOOL_F_RT_XX)
	 */
	long lflags;
		
	/**
	 * Arguments passed by user
	 */
	int priq_num;
	
	/**
	 * Task seq generator
	 */
	int seq;

	/**
	 * Throttle
	 */
	int  throttle_on;
	OSPX_pthread_cond_t *cond_event;

	/**
	 * Event WAIT env
	 */
	long ev_wref;
	int  ev_need_notify;
	OSPX_pthread_cond_t cond_ev;
	
	/**
	 * Task WAIT env
	 */
	long tsk_wref;
	int  tsk_need_notify;
	int  tsk_any_wait;
#define MAX_WAIT_ENTRY 64
	int entry_idx, entry_idx_max;
	ctask_trace_t *glbentry[MAX_WAIT_ENTRY];
	struct list_head wq;
	OSPX_pthread_cond_t cond_task;

	/**
	 * A common condition variable for events
	 */
	OSPX_pthread_cond_t cond_com;
	
	/*
	 * A common condition for synchronizion
	 */
	OSPX_pthread_cond_t cond_sync;
	/**
	 * Task entries
	 */
#define IS_VALID_ENTRY(entry) (!((SLOT_F_FREE|SLOT_F_DESTROYING) & (entry)->lflags))
	int num, nfrees;
	int active_idx, nactives_ok;
	ctask_entry_t *entry, **actentry;
	
	/**
	 * Dispatch env
	 */
	int n;

	/**
	 * Status
	 */
	int npendings;
	int ndispatchings;
	int n_qtraces;
	int ntasks_processed0;
	
	/**
	 * Statics report
	 */
	int ntasks_peak;
	
	/**
	 * A condition var shared by all entires
	 */
	OSPX_pthread_cond_t cond_task_entry;	
};

#endif
