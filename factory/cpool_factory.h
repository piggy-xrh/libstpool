#ifndef __CPOOL_FACTORY_H__
#define __CPOOL_FACTORY_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "ospx_type.h"
#include "list.h"
#include "cpool_method.h"

enum {
	/**
	 * The task is removed by user
	 */
	eErr_removed_byuser = 0x1,
	
	/**
	 * The task is removed by the library since the pool has been marked 
	 * suspended and it is being destroyed
	 */
	eErr_pool_destroying = 0x2,
	
	/**
	 * The task is removed by the library since The group is being destroyed
	 */
	eErr_group_destroying = 0x4,
};

/**
 * Status of the task
 */
enum {
	/**
	 * Task is in the pending queue
	 */
	eTASK_STAT_F_WAITING  = 0x01,
	
	/**
	 * Task is being scheduled by the pool 
	 */
	eTASK_STAT_F_SCHEDULING  = 0x02,
	
	/**
	 * Task has been marked with removed 
	 */
	eTASK_STAT_F_DISPATCHING = 0x08,
		
	/**
	 * The task will be added into the pool automatically 
	 * after its done 
	 */
	eTASK_STAT_F_WPENDING = 0x10,
	
	/**
	 * the removable stats sets 
	 */
	eTASK_STAT_F_REMOVABLE = eTASK_STAT_F_WAITING|eTASK_STAT_F_WPENDING,
};

/** Flags of the task object */
enum {
	/**
	 * \@task_run has been executed 
	 *
	 *  This flag will be passed to the task's completion routine if
	 *  the task's working routine has been executed by the pool
	 */
	eTASK_VM_F_DONE = 0x0001,
		
	/**
	 * Remove the task and the pool is responsible for calling its
	 * completion routine in the background if it is not NULL.
	 */
	eTASK_VM_F_REMOVE_BYPOOL = 0x0004, 	
	
	/**
	 * Remove the task and the APIs itself is responsible for calling
	 * it completion routine direcly if it is not NULL.
	 */
	eTASK_VM_F_REMOVE = 0x0008,
	
	/**
	 * The REMOVE mask sets
	 */
	eTASK_VM_F_REMOVE_FLAGS = eTASK_VM_F_REMOVE_BYPOOL|eTASK_VM_F_REMOVE,
	
	/**
	 * The pool is being destroyed by @ref Method::release
	 */
	eTASK_VM_F_POOL_DESTROYING = 0x0010,		
		
	/**
	 * The task can be delived into the pool
	 *
	 * @note It is the default value
	 */
	eTASK_VM_F_ENABLE_QUEUE  = 0x0080,		

	/**
	 * The task is disallowed to be added into the pool
	 */
	eTASK_VM_F_DISABLE_QUEUE = 0x0040,		
	
	/**
	 * The mask sets that can be used by users to mark the tasks
	 */
	eTASK_VM_F_USER_FLAGS = eTASK_VM_F_REMOVE_FLAGS|eTASK_VM_F_ENABLE_QUEUE|
	                        eTASK_VM_F_DISABLE_QUEUE,
	
	/**
	 * The group is being destroyed by @ref Method::group_delete
	 */
	eTASK_VM_F_GROUP_DESTROYING = 0x0100,
	
	/**
	 * The task is created by Method::cache_get
	 */
	eTASK_VM_F_LOCAL_CACHE = 0x0200,

	/**
	 * It indicates that the priority of the task is not zero 
	 */
	eTASK_VM_F_PRI = 0x0400,
	
	/**
	 * It indicates that the task should be pushed into
	 * the tail queue directly 
	 */
	eTASK_VM_F_PUSH   = 0x0800,
	
	/**
	 * It indicates that the task's scheduling attribute has been changed 
	 */
	eTASK_VM_F_ADJPRI = 0x1000,
	
	/**
	 * The task's priority should be reset after its delivering into the pool's priority queue succefully
	 */
	eTASK_VM_F_PRI_ONCE = 0x2000,

	/**
	 * The task has been marked detached by Method::task_detach
	 */
	eTASK_VM_F_DETACHED = 0x4000,
};

/** The policy to schedule the taskA */
enum {
	/**
	 * If there are tasks who has the same priority with taskA, 
	 * the taskA will be scheduled prior to all of them 
	 */
	ep_TOP = 1,

    /**
	 * If there are tasks who has the same priority with taskA, 
	 * they will be scheduled prior to taskA
	 */
	ep_BACK,
};

/** The definition of the private data of the task object */
struct ctask {
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
	int8_t ref;

	/**
	 * Not used
	 */
	uint8_t  f_reserved;
};
#define TASK_CAST_FAC(ptask) ((ctask_t *)(ptask))

/** Error code sets */
enum {	
	/**
	 * System is out of memeory 
	 */
	eERR_NOMEM = 1,
	
	/**
	 * Task pool is being destroyed 
	 *  
	 * It indicates that @ref sterelease has been called by user and 
	 *  the reference of the pool is zero
	 */
	eERR_DESTROYING = 2, 
	
	/**
	 * The throttle of the pool is turned on
     * 
	 * It indicates that user has called @ref stethrottle_enable (pool, 0)
	 *  to turn the throttle swither on
	 */
	eERR_THROTTLE = 4,
		
	/**
	 * The task is requed to be added into a deferent pool, But the task 
	 * is not free now.
	 */
	eTASK_ERR_BUSY = 7,	
	
	/**
	 * The task has been marked with TASK_VMARK_DISABLE_QUEUE 
	 *
	 * @see <pre>
	 *      @ref task_mark
	 *      @ref mark_all
	 *      @ref mark_cb
	 *      @ref group_mark_all  (If the library supports GROUP)
	 *      @ref group_mark_cb   (If the library supports GROUP)
	       </pre>
	 */
	eTASK_ERR_DISABLE_QUEUE = 8,
	
	/** 
	 * The task has not set \@pool or \@pool has been changed while
	 * the task is in progress
	 */
	eTASK_ERR_DESTINATION = 9,
	
	/*----------------------------------------------------------------*/
	/*--------group error code (If the instance supports GROUP) ------*/
	/*----------------------------------------------------------------*/
	/**
	 * The group's throttle has been turned on 
	 */ 
	eERR_GROUP_THROTTLE = 10,
	
	/**
	 * The group does not exist
	 */
	eERR_GROUP_NOT_FOUND = 11,
	
	/**
	 * The group is being destroyed
	 */
	eERR_GROUP_DESTROYING = 12,
	/*----------------------------------------------------------------*/

	/**
	 * The operation fails for its timeout 
	 */
	eERR_TIMEDOUT = 13,
	
	/**
	 * The wait function is waked up by user for some reasons
	 *
	 * @see @ref wakeid \n
	 *      @ref wakeup
	 */
	eERR_INTERRUPTED = 14,
	
	/**
	 * All other unkown errors
	 *
	 * It should not be changed in the future 
	 */
	eERR_OTHER = 15,

	/**
	 * The operation is not supported
	 */
	eERR_NSUPPORT = 16,

	/**
	 * A error occured by the system library 
	 *
	 * (The errno has been set properly)
	 */
	eERR_errno  = 17,
};

enum ep_TH
{
	/**
	 * Default 
	 *
	 * Do not change anything, use the OS default policy
	 */
	ep_TH_SCHED_NONE,

	/**
	 * Posix SCHED_RR 
	 */
	ep_TH_SCHED_RR,
	
	/**
	 * Posix SCHED_FIFO 
	 */
	ep_TH_SCHED_FIFO,

	/**
	 * Posix SCHED_OTHER 
	 */
	ep_TH_SCHED_OTHER
};

struct thread_attr {
	/**
	 * Stack size (0:default) 
	 */
	int stack_size;

	/**
	 * Schedule policy 
	 */
	enum ep_TH ep_schep;

	/**
	 * schedule priority ([0-99) 0:default) 
	 */
	int sche_priority;
};

struct scheduling_attr {
	/**
	 * The max scheduling tasks of the thread's local queue
	 */
	int max_qscheduling;
	
	/**
	 * The max dispatching tasks of the thread's local queue
	 */
	int max_qdispatching;
};

/**
 * The scheduling attribute of the group.
 */
struct scheduler_attr {
	/**
	 * The limit parralle tasks. 
	 *
	 *    Configure the group's paralle limited tasks number. it is the max limited
	 * threads number of the pool by default. the pool will save the param and configure
	 * the group properly according to the pool's limited working threads number, the
	 * group will be re-configured according to the param saved by the pool if user calls
	 * @Method::adjust or @Method::adjust_abs to change the working threads number of the
	 * pool.
	 *
	 *
	 * @note
	 * 	    If the pool satifies all groups' configurations and there are more free 
	 * threads, the pool will peek tasks from all pending queues of the active groups 
	 * to execute according to their priorities and added time, so the tasks belong to 
	 * a group may also be peeked to schedule even if current paralle tasks number of 
	 * that group is not less than \@limit_paralle_tasks.
	 *
	 *    FAQ: What is kind of group a ACTIVE group ? \n
	 *        if the group has pending tasks and it is not suspended, the pool will 
	 * mark the group ACTIVE.
	 */
	int limit_paralle_tasks;
	
	/**
	 * benifits switcher
	 *
	 *   If it is zero, it tells the library that the paralle tasks number of this 
	 * group can not be greater than \@limit_paralle_tasks even if there are one more 
	 * free threads existing in the group.
	 */
	int receive_benifits;
};

/**
 * The status of the group.
 */
struct ctask_group_stat {
	/** 
	 * The group id returned by @ref stpool_group_create
	 */
	int gid;

	/** 
	 * A string to describle the group
	 */
	char *desc;
	
	/**
	 * the length of the \@desc
	 */
	size_t  desc_length;
	
	/** 
	 * The scheduling attribute 
	 */
	struct scheduler_attr attr;

	/** 
	 * The created time 
	 */
	time_t created;
	
	/**
	 * The number of waiters who is waiting on our group
	 */
	int waiters;

	/** 
	 * A var that is used to indicate whether the group is suspended 
	 * or not. if it is set to 1, the tasks existing in the group's 
	 * pending queue will not be scheduled by the pool until user 
	 * calls @ref stpool_group_resume to set it to 0.
	 */
	int suspended;

	/** 
	 * A var who is used to indicate whether the group's throttle is
	 * turned on or not. if it is set to 1,the group will enject all 
	 * tasks requests from outside.
	 */
	int throttle_on;

	/** 
	 * The priority queue number 
	 */
	int priq_num;
	
	/** 
	 * The number of pending tasks existing in the group's pending 
	 * queue currently.
	 */
	int npendings;

	/** 
	 * The scheduling number of tasks who is being removed or is 
	 * being scheduled by the pool currently.
	 */
	int nrunnings;

	/** 
	 * The number of tasks who has been marked removed. 
	 *
	 * @note All of these tasks are being removed or waiting for being removed.
	 */
	int ndispatchings;
};

/** Status of the pool */
struct cpool_stat {
	/**
	 * A const string to describle the pool 
	 */
	const char *desc;            
	
	/**
	 * The time when the pool is created 
	 */
	time_t created;              

	/**
	 * The current user refereces 
	 *
	 * @see @ref Method::addref, @ref Method::release
	 */
	long ref;           
	
	/**
	 * The number of waiters who is waiting on our group
	 */
	int waiters;

	/**
	 * The number of the priority queue 
	 */
	int priq_num;               

	/**
	 * A var indicates whether the pool's throttle switcher is on or not
	 *
	 * If it is none zero, it indicates that the throttle has been turned
	 * on, the pool will enject all requests delived by these Method::task_queue:
	 *
	 * user can set the throttle status by @ref MethodEx::throttle_enable
	 */
	int throttle_on;        
	
	/**
	 * A var indicates whether the pool is active now or not
	 *
	 * If it is none zero, it indicates that the pool is inactive now,
	 * the pool will stop scheduling the pending tasks.
	 *
	 * @see @ref Method::suspend, @ref Method::resume
	 */
	int suspended;               

	/**
	 * The max limited servering threads number of the pool
     *
	 * @see @ref Method::adjust, @Method::adjust_abs
	 */
	int maxthreads;              

	/**
	 * The reserved servering threads number of the pool
	 *
	 * If it is postive, the pool will make sure that there
	 * are \@minthreads created threads staying in the pool
	 * all the time.
	 *
	 * @see @ref Method::adjust, @ref Method::adjust_abs
	 */
	int minthreads;              
	
	/**
	 * The number of threads existing in the pool currently
	 */
	int curthreads;              
	
	/**
	 * The number of threads who is scheduling the tasks currently
	 */
	int curthreads_active;       
	
	/**
	 * The number of threads who is marked died currently
	 *
	 * @note the threads will be marked died if there are none any
	 * tasks existing in the pool to execute for a long time. the
	 * APIs @ref Method::adjust, @ref Method::adjust_abs, and @ref
	 * Method::flush may also makr a few threads died.
	 */
	int curthreads_dying;        

	/**
	 * The base sleeping time for the free threads
	 *
	 * @note the threads will quit themself if they have not gotten
	 * any tasks for \@acttimeo + random() % @randtimeo milliseconds
	 */
	long acttimeo;               

	/**
	 * The random sleeping time for the free threads
	 * 
	 * @see @ref acttimeo for more details
	 */
	long randtimeo;              
	
	/**
	 * The peek of the tasks number since the pool is created
	 */
	int tasks_peak;     

	/**
	 * The peek of the threads number since the pool is created.
	 */
	int threads_peak;   

	/**
	 * The number of tasks that has been added into the pool since
	 * the pool is created
	 */
	int tasks_added;          

	/**
	 * The number of tasks that has been processed by the pool since
	 * the pool is created
	 */
	int tasks_processed;      
	
	/**
	 * The number of tasks that has been removed by the pool since
	 * the pool is created
	 */
	int tasks_removed;           

	/**
	 * The number of tasks existing in the pool's pending queue
	 */
	int curtasks_pending;    

	/**
	 * The number of tasks who is being scheduled or is being removed by the pool 
	 */
	int curtasks_scheduling; 

    /** 
	 * The number of tasks who has been marked removed 
	 */
	int curtasks_removing;   
};

/** The definition of pool method sets */
typedef struct cpool_method {
	/**
	 * Task method sets
	 */
	cpool_task_method_t   tskm;

	/**
	 * Basic pool method sets 
	 */
	cpool_basic_method_t   pm;
	
	/**
	 * Advanced pool method sets
	 */
	cpool_advance_method_t pmex;

} cpool_method_t;

/** The function masks */
enum {
	/**
	 * The pool support the ADVANCE methods
	 */
	eFUNC_F_ADVANCE = 0x02,

	/**
	 * The pool support creating and destroying the threads automatically
	 */
	eFUNC_F_DYNAMIC_THREADS = 0x04,

	/**
	 * The pool support the priority tasks
	 */
	eFUNC_F_PRIORITY = 0x08,
	
	/**
	 * The pool support the WAITABLE tasks
	 */
	eFUNC_F_TASK_WAITABLE = 0x10,
	
	/**
	 * The tasks in the pool can be traced
	 */
	eFUNC_F_TRACEABLE = 0x20,
	
	/**
	 * Support non-routine tasks
	 */
	eFUNC_F_TASK_EX = 0x40,
	
	/**
	 * Support eTASK_VM_F_DISABLE
	 */
	eFUNC_F_DISABLEQ = 0x80,
};

/** The definition of the pool object */
struct cpool {
	/**
	 * The supported method masks of the instance object 
	 */
	long  efuncs;
	
	/**
	 * Method sets
	 */
	const cpool_method_t *me;
	
	/**
	 * The description of the instance 
	 */
	char  *desc;

	/**
	 * The instance object created by the factory
	 */
	void *ins;	
	
	/**
	 * The interface to destroy the instance object
	 *
	 * \@destroy should be called by user to free the pool object if it is useless. 
	 */
	void (*free)(cpool_t *fac_ins);
}; 

/** The definition of the pool factory */
typedef struct cpool_factory {
	/**
	 * The perfermance scores
	 */
	const int scores;

	/**
	 * The method sets masks
	 */
	const long efuncs;
	
	/**
	 * Get the method sets
	 */
	const cpool_method_t *const method;
	
	/**
	 * Create the pool instance 
	 */
	cpool_t *(*create)(const char *desc, int maxthreads, int minthreads, int priq_num, int suspend);
	
} cpool_factory_t;

/** 
 * Add a factory into the global dictionary 
 */
int add_factory(const char *fac_desc, const cpool_factory_t *const fac);

/**
 * Find the specified factory in the global dictionary according to its description 
 */
const cpool_factory_t *get_factory(const char *fac_desc);

/**
 * Get the first factory stored in the global dictionary
 */
const cpool_factory_t *first_factory(const char **p_fac_desc);

/**
 * Get the next factory stored in the global dictionary
 */
const cpool_factory_t *next_factory(const char **p_fac_desc);
#endif
