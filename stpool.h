#ifndef __ST_POOL_H__
#define __ST_POOL_H__

/** @mainpage COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the pool, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */
#include <time.h>

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64)
#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#else
#define EXPORT
#endif

#include "stpool_caps.h"

typedef struct cpool stpool_t;

/** Error code sets */
enum 
{	
	/**
	 * System is out of memeory 
	 */
	POOL_ERR_NOMEM = 1,
	
	/**
	 * Task pool is being destroyed 
	 *  
	 * It indicates that @ref stpool_release has been called by user and 
	 *  the reference of the pool is zero
	 */
	POOL_ERR_DESTROYING = 2, 
	
	/**
	 * The throttle of the pool is turned on
     * 
	 * It indicates that user has called @ref stpool_throttle_enable (pool, 0)
	 *  to turn the throttle swither on
	 */
	POOL_ERR_THROTTLE = 4,
		
	/**
	 * The operation can not be done since the task is in progress now 
	 */
	POOL_TASK_ERR_BUSY = 7,	
	
	/**
	 * The task has been marked with TASK_VMARK_DISABLE_QUEUE 
	 *
	 * @see <pre>
	 *      @ref stpool_task_mark
	 *      @ref stpool_mark_all
	 *      @ref stpool_mark_cb
	 *      @ref stpool_group_mark_all  
	 *      @ref stpool_group_mark_cb   
	       </pre>
	 */
	POOL_TASK_ERR_DISABLE_QUEUE = 8,
	
	/** 
	 * The task has not called @ref stpool_task_set_p to set its 
	 * destination pool 
	 */
	POOL_TASK_ERR_DESTINATION = 9,
	
	/*----------------------------------------------------------------*/
	/*------------------------- group error code  --------------------*/
	/*----------------------------------------------------------------*/
	/**
	 * The group's throttle has been turned on by @ref stpool_group_throttle_enable
	 */ 
	POOL_ERR_GROUP_THROTTLE = 10,
	
	/**
	 * The group does not exist
	 */
	POOL_ERR_GROUP_NOT_FOUND = 11,
	
	/**
	 * The group is being destroyed
	 */
	POOL_ERR_GROUP_DESTROYING = 12,
	
	/**
	 * The group is overloaded
	 */
	POOL_ERR_GROUP_OVERLOADED = 19,
	/*----------------------------------------------------------------*/

	/**
	 * The operation fails for its timeout 
	 */
	POOL_ERR_TIMEDOUT = 13,
	
	/**
	 * The wait function is waked up by user for some reasons
	 *
	 * @see @ref stpool_wakeid \n
	 *      @ref stpool_wakeup
	 */
	POOL_ERR_INTERRUPTED = 14,
	
	/**
	 * All other unkown errors
	 *
	 * It should not be changed in the future 
	 */
	POOL_ERR_OTHER = 15,

	/**
	 * The operation is not supported
	 */
	POOL_ERR_NSUPPORT = 16,
	
	/**
	 * An error occured by the system library 
	 *
	 * (The errno has been set properly)
	 */
	POOL_ERR_errno  = 17,
	
	/**
	 * The task can not be delived into the pool since the pool
	 * is overloaded now
	 */
	POOL_ERR_OVERLOADED = 18,
};

/** Task error reasons */
enum 
{
	/**
	 * The task is removed from the pool for some reasons
	 */
	eReason_removed = 0x01,
	
	/**
	 * The task can not been executed since the pool has been marked 
	 * suspended and the pool itself is being destroyed
	 */
	eReason_pool_destroying = 0x02,

	/**
	 * The task can not been executed since the group is being destroyed
	 */
	eReason_group_destroying = 0x04,
	
	/**
	 * The task can not been executed because of the pool's overload
	 */
	eReason_pool_overloaded = 0x08,
	
	/**
	 * The task can not been executed because of the group's overload
	 */
	eReason_group_overloaded = 0x10
};

/** The definition of the task object */
struct sttask 
{
	/**
	 * A const string to describle the task 
	 */
	const char *task_name;
	
	/** 
	 * The working routine of the task
	 *
	 * \@task_run will be called when the task is scheduled by the pool. 
     *  user can do their customed works in this function.
	 */
	void (*task_run)(struct sttask *ptask);	
	
	/**
	 * The exception handler of the task
	 *
	 * If \@task_err_handler is not NULL, it will be called when one of the conditions 
	 *  below matches.
	 *
	 *       . The task is removed from the pool for some reasons.
	 */
	void (*task_err_handler)(struct sttask *ptask, long reasons);

	/**
	 * The argument reserved for task 
	 *
	 * (The library will not touch it)
	 */
	void *task_arg;
	
	/**
	 * The task code reserved for task
	 *
	 * (The library will not touch it)
	 */
	int task_code;
};

/** The policy to schedule the taskA */
enum 
{
	/**
	 * If there are tasks who has the same priority with taskA, 
	 * the taskA will be scheduled prior to all of them 
	 */
	ep_SCHE_TOP = 1,

    /**
	 * If there are tasks who has the same priority with taskA, 
	 * they will be scheduled prior to taskA
	 */
	ep_SCHE_BACK,
};

/** The scheduling attributes of the task object */
struct schattr 
{
	/**
	 * If \@permanent is not zero, the task's priority will not
	 * be changed until the user call @ref stpool_task_setschattr
	 * to modifiy it manually, or it will be set to zero after 
	 * the next time to add it into the pool 
	 */
	int permanent;
	
	/**
	 * The priority of the task [0-99] 
	 */
	int sche_pri;

	/**
	 * The policy to schedule the task (ep_SCHE_TOP(BACK)) 
	 */
	int sche_pri_policy;
};

/** The status of the task object */
enum 
{
	/**
	 * Task is in the pending queue
	 */
	TASK_STAT_WAIT  = (short)0x01,
	
	/**
	 * Task is being scheduled by the pool 
	 */
	TASK_STAT_SCHEDULING  = (short)0x02,
	
	/**
	 * Task has been marked with removed 
	 */
	TASK_STAT_DISPATCHING = (short)0x08,
		
	/**
	 * The task will be added into the pool automatically 
	 * after its done 
	 */
	TASK_STAT_WAIT_PENDING = (short)0x10,	
};

/** VM Flags of the task object 
 *
 * @note If a task has been really marked by the VM flags, they will
 * be stored in the task object. and user can get the tasks' VM flags
 * by @ref stpool_task_vm
 */
enum 
{
	/**
	 * Remove the task and the pool is responsible for calling its
	 * error handler in the background if it is not NULL.
	 */
	TASK_VMARK_REMOVE_BYPOOL = 0x0004, 	
	
	/**
	 * Remove the task and the API itself is responsible for calling
	 * its error handler direcly if it is not NULL.
	 */
	TASK_VMARK_REMOVE = 0x0008,
	
	/**
	 * The task can be added into the pool
	 *
	 * @note It is the default value
	 */
	TASK_VMARK_ENABLE_QUEUE  = 0x0080,		

	/**
	 * The task is disallowed to be added into the pool
	 */
	TASK_VMARK_DISABLE_QUEUE = 0x0040,		
	
	/**
	 * The REMOVE mask sets: 
	 * 	(TASK_VMARK_REMOVE_BYPOOL|TASK_VMARK_REMOVE)
	 *
	 * @note the library will ensure that the REMOVE flags will not be cleared until 
	 * the user calls the APIs such as @ref stpool_task_queue, @ref stpool_add_routine,
	 * @ref stpool_clone_add_queue, and @ref stpool_group_add_routine to try to deliver
	 * the task into the pool's pending queue again.
	 *
	 * 
	 * The mask sets that can be used by users to mark the tasks: 
	 * 	 (TASK_VMARK_REMOVE_FLAGS|TASK_VMARK_ENABLE_QUEUE|TASK_VMARK_DISABLE_QUEUE)
	 *
	 * @note User can get the recent VM flags of the task by @ref stpool_task_vm
	 */
};

/** Schedule policy for the servering threads */
enum ep_SCHE 
{
	/**
	 * Default 
	 *
	 * Do not change anything, use the OS default policy
	 */
	ep_SCHE_NONE,

	/**
	 * Posix SCHED_RR 
	 */
	ep_SCHE_RR,
	
	/**
	 * Posix SCHED_FIFO 
	 */
	ep_SCHE_FIFO,

	/**
	 * Posix SCHED_OTHER 
	 */
	ep_SCHE_OTHER
};

/** Attribute of servering thread */
struct stpool_thattr 
{
	/**
	 * Stack size (0:default) 
	 */
	int stack_size;

	/**
	 * Schedule policy 
	 */
	enum ep_SCHE ep_schep;

	/**
	 * schedule priority ([0-99) 0:default) 
	 */
	int sche_priority;
};

/** Scheduling attributes of the thread */
struct stpool_taskattr 
{
	/**
	 * The max scheduling tasks of the thread's local queue
	 */
	int max_qscheduling;
	
	/**
	 * The max dispatching tasks of the thread's local queue
	 */
	int max_qdispatching;
};

/** The overload actions */
enum
{
  /**
   * The newer task will be delived into the pending queue (default policy)
   */
  eOA_none,

  /**
   * The newer task can not be delived into the pool
   */
  eOA_discard,

  /**
   * The newer task will be delived into the pending queue, and menwhile the
   * the oldest tasks existing in the pending queue will be removed
   */
  eOA_drain
};

/** The overload policies */
struct oaattr
{
	/**
	 * The threshold of the task existing in the pending queue
	 */
	int task_threshold;
	
	/** 
	 * The action that the library should execute when the task number arrives
	 * at the threshold
	 */
	int eoa;
};

/** Status of the pool */
struct pool_stat 
{
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
	 * @see @ref stpool_addref, @ref stpool_release
	 */
	long ref;           

	/**
	 * The number of waiters who is waiting on our pool
	 */
	int waiters;
	
	/**
	 * The number of the priority queue passed to @ref stpool_create
	 */
	int priq_num;               

	/**
	 * A var indicates whether the pool's throttle switcher is on or not
	 *
	 * If it is none zero, it indicates that the throttle has been turned
	 * on, the pool will enject all requests delived by these APIs below:
	 *
	 *    @ref stpool_task_queue 
	 *    @ref stpool_add_routine 
	 *    @ref stpool_group_add_routine.
	 *
	 * user can set the throttle status by @ref stpool_throttle_enable
	 */
	int throttle_enabled;        
	
	/**
	 * A var indicates whether the pool is active now or not
	 *
	 * If it is none zero, it indicates that the pool is inactive now,
	 * the pool will stop scheduling the pending tasks.
	 *
	 * @see @ref stpool_suspend, @ref stpool_resume
	 */
	int suspended;               

	/**
	 * The max limited servering threads number of the pool
     *
	 * @see @ref stpool_adjust, @stpool_adjust_abs
	 */
	int maxthreads;              

	/**
	 * The reserved servering threads number of the pool
	 *
	 * If it is postive, the pool will make sure that there
	 * are \@minthreads created threads staying in the pool
	 * all the time.
	 *
	 * @see @ref stpool_adjust, @ref stpool_adjust_abs
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
	 * APIs @ref stpool_adjust, @ref stpool_adjust_abs, and @ref
	 * stpool_flush may also makr a few threads died.
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
	 * The peek of the tasks number since the pool was created (-1 = UNKOWN)
	 */
	unsigned int tasks_peak;     

	/**
	 * The peek of the threads number since the pool was created.
	 */
	unsigned int threads_peak;   

	/**
	 * The number of tasks that has been added into the pool since
	 * the pool is created (-1 = UNKOWN)
	 */
	unsigned int tasks_added;          

	/**
	 * The number of tasks that has been processed by the pool since
	 * the pool is created (-1 = UNKOWN)
	 */
	unsigned int tasks_processed;      
	
	/**
	 * The number of tasks that has been removed by the pool since
	 * the pool is created (-1 = UNKOWN)
	 */
	unsigned int tasks_removed;        

	/**
	 * The number of tasks existing in the pool's pending queue
	 */
	unsigned int curtasks_pending;    

	/**
	 * The number of tasks who is being scheduled or is being removed by the pool 
	 */
	unsigned int curtasks_scheduling; 

    /** 
	 * The number of tasks who has been marked removed 
	 */
	unsigned int curtasks_removing;   
};

/** The walk callback that is used to visit the tasks */
typedef long (*Walk_cb)(struct sttask *ptask, void *opaque);

#ifdef __cplusplus
extern "C" {
#endif

/*----------------APIs about the task --------------*/

/**
 * Acquire the real size of the task object
 */
EXPORT size_t stpool_task_size();

/**
 * Initialize the task object with the specified parameters
 *
 * @param [in] ptask            the task object needed to be initialized
 * @param [in] pool             the destination pool (Can be NULL)
 * @param [in] task_name        a const string to describle the task (NOTE: The library just copys its address) 
 * @param [in] task_run         the task's working routine     (Can not be NULL)
 * @param [in] task_err_handler the task's completion routine  (Can be NULL)
 * @param [in] task_arg         the argument reserved for task (Can be NULL)
 * 
 * @return None
 *
 * @code example:
 *      struct sttask *ptask;
 *
 *      ptask = malloc(stpool_task_size());
 *      stpool_task_init(ptask, pool, "task", run, complete, arg);
 *      ...
 * @endcode
 */
EXPORT int stpool_task_init(struct sttask *ptask, stpool_t *pool,
					const char *task_name, void (*task_run)(struct sttask *ptask),
					void (*task_err_handler)(struct sttask *ptask, long reasons),
					void *task_arg);
/**
 * Allocate a task object from the global cache. 
 *
 * @param  @see @ref stpool_task_init 
 * 
 * @return On success, a task object, NULL otherwise
 *
 * @note The task returned by @ref stpool_task_new should be free manually 
 *       by calling @ref stpool_task_delete
 */
EXPORT struct sttask *stpool_task_new(stpool_t *pool, const char *task_name,
						void (*task_run)(struct sttask *ptask),
						void (*task_err_handler)(struct sttask *ptask, long reasons),
						void *task_arg);

/**
 *  Clone a task from the source task object. 
 *
 * 	@param [in] ptask          The source object.
 * 	@param [in] clone_schattr  Tell the system whether it should clone the task's scheduling attribute or not.
 *
 *  @return a task object On success, NULL otherwise
 *
 * @note the cloned task should be free manually by calling @ref stpool_task_delete.
 */
EXPORT struct sttask *stpool_task_clone(struct sttask *ptask, int clone_schattr);

/**
 * Destroy the task object that allocated by @ref stpool_task_new or @ref stpool_task_clone
 * 
 * @param [in] ptask The task object that the user wants to destroy.
 *
 * @return none
 *
 * @note
 * 	   Only free tasks can be destroyed safely !
 *
 *	   But how can we detect whether the task is free now or not ? if one of
 *	the conditions listed below matches, it indicates that the task is free
 *	now.
 *		               <pre>
 *		. @ref stpool_task_is_free returns a non-zero value
 *      . @ref stpool_task_wait  (ptask, ms) returns 0
 *      . @ref stpool_task_stat  (ptask) == 0
 *      . @ref stpool_task_stat2 (ptask, &vm) == 0
                      </pre>
 *	   Normaly, the task will not be removed from the pool until the task's 
 *	completion routine is executed completely. it means that it is not safe 
 *	to destroy the task manually in the task's completion routine But If you 
 *	really want to do this like that, you should call @ref stpool_task_detach
 *	firstly to remove the task from the pool completely.
 */
EXPORT void  stpool_task_delete(struct sttask *ptask);

/*
 * Set the destination pool for the task object
 * 
 * @param [in] ptask the task object
 * @param [in] pool the destination pool
 *
 * @return if the created pool does not support the customed tasks (see eCAP_F_CUSTOM_TASK), 
 *         it returns POOL_ERR_NSUPPORT, or it returns 0 
 * 
 * @note the task should set its destination pool firstly before its being 
 *       delived into the pool's pending queue by @ref stpool_task_queue,
 *       and if \@pool is not equal to the previous destination pool of 
 *       the task, the flag TASK_VMAKR_DISABLE_QUEUE will be cleared by the 
 *       library.
 */
EXPORT int stpool_task_set_p(struct sttask *ptask, stpool_t *pool);

/**
 * Get the destination pool of the task 
 *
 * @see @ref stpool_task_set_p
 */
EXPORT stpool_t *stpool_task_p(struct sttask *ptask);

/**
 * Get the name of the pool that the task belongs to 
 */
EXPORT const char *stpool_task_pname(struct sttask *ptask);

/**
 * Set the task's user flags. (its range is from 0 to 0xffff)
 *
 * @param [in] ptask   The task object.
 * @param [in] uflags The private user flags that the user want to set
 *
 * @return None
 * 
 * @note The user flags is reserved for users, the library will not touch it
 */
EXPORT void stpool_task_set_userflags(struct sttask *ptask, unsigned short uflags);

/**
 * Get the task's user flags
 */
EXPORT unsigned short stpool_task_get_userflags(struct sttask *ptask);

/**
 * Set the scheduling attribute for the task
 *
 * @param [in] ptask The task object.
 * @param [in] attr The scheduling attribute that will be applyed on the task
 *
 * @return None
 */
EXPORT void  stpool_task_setschattr(struct sttask *ptask, struct schattr *attr);

/**
 * Get the scheduling attribute of the task
 *
 * @param [in]  ptask The task object.
 * @param [out] attr The buffer that used to received the task's scheduling attribute 
 *
 * @return None
 */
EXPORT void  stpool_task_getschattr(struct sttask *ptask, struct schattr *attr);

/**
 * Get the status of the task
 *
 * @param [in] ptask the task object
 *
 * @return the status of the task
 */
EXPORT long  stpool_task_stat(struct sttask *ptask);

/**
 * Get the mark(VM) flags of the task
 *
 * @param [in] ptask the task object
 *
 * @return the recent VM flags of the task
 */
EXPORT long  stpool_task_vm(struct sttask *ptask);

/**
 * Get the mark(VM) flags and status of the task
 *
 * @param [in]       ptask the task object
 * @param [in, out]  the buffer to receive the mark flags
 *
 * @return the status of the task
 */
EXPORT long  stpool_task_stat2(struct sttask *ptask, long *vm);

/**
 * Deliver the task into the pending queue of the destination pool
 *
 * @param   [in] the task object
 *
 * @return  0 on success, error code listed below on error
 *                                               \n
 *          @ref POOL_ERR_DESTROYING             \n
 *          @ref POOL_ERR_THROTTLE               \n
 *          @ref POOL_TASK_ERR_DESTINATION       \n
 *          @ref POOL_TASK_ERR_DISABLE_QUEUE     \n
 *          @ref POOL_ERR_GROUP_THROTTLE         \n
 *          @ref POOL_ERR_GROUP_DESTROYING       \n
 *          @ref POOL_ERR_NSUPPORT               \n
 *
 * @note 
 *    @ref stpool_task_queue can be called one more times, If the task
 * is already in the pool's pending queue, it does nothing, but if 
 * the task is being scheduled or is being removed, it will mark the 
 * task with @DO_AGAIN. and as a result, the task will be added into the
 * pool again automatically after the pool's having executed the task's 
 * callback \n
 *                    \n
 *      FAQ: If the task has been marked with @DO_AGAIN, what will it
 * happen if the user mark the task with @ref TASK_VMARK_REMOVE_FLAGS again 
 * by calling APIs such as @ref stpool_mark_all, @ref stpool_task_mark, etc ? \n
 * 	                                                          \n
 * 	    In this situations, the pool just remove the \@DO_AGAIN flag 
 * for the task. and the task will not be marked removed.
 *
 * @see @ref stpool_add_routine
 */
EXPORT int   stpool_task_queue(struct sttask *ptask);

/**
 * Remove the task
 *
 * @param [in] ptask                the task object needed to be removed
 * @param [in] dispatched_by_pool  if it is none zero, this API will call @ref stpool_task_mark 
 *                                 (ptask, TASK_VMARK_REMOVE_BYPOOL), or it will call @ref stpool_task_mark 
 *                                 (ptask, TASK_VMARK_REMOVE)
 *
 * @return it returns 0 If the task is free now, or it returs 1.
 */
EXPORT int stpool_task_remove(struct sttask *ptask, int dispatched_by_pool);

/**
 * Mark the task with the specified flags
 *
 * @param [in] ptask   the task object that will be marked with \@lflags
 * @param [in] lflags  see @ref stpool_mark_all for more details about the mark flags
 *
 * @return None 
 */
EXPORT void stpool_task_mark(struct sttask *ptask, long lflags);

/** 
 *	 Deatch a traceable task from the pool. the purpose to export this 
 * API is to allow users to destroy its task in the task's callbacks.
 *
 * @note
 * 	@ref stpool_task_detach will be only allowed to be called in
 * 	the task's working routine or in the task's error handler. 
 * 	Usally a traceable task will be connected with a pool after its 
 * 	having been added into the pool succefully. the task will be removed
 * 	from the pool if its callback has been done completly, it means 
 * 	that it is not safe to destroy the task object in the task's callback, 
 * 	@stpool_task_detach is designed to resolve this problem, it will force 
 * 	the pool remove the task from its destination pool. and then the user 
 * 	can free the task object safely.
 *
 * @code example:
 * 	          #define MYTASK_AUTO_FREE 0x1
 *
 *             void task_run(struct sttask *ptask) {
 *             	  // TO DO
 *             	
 *             	  // Try to free the customed task in its working routine,
 *                // here we just call @task_err_handler to free it directly
 *                task_err_handler(ptask, 0);
 *             }
 *
 * 	           void task_err_handler(struct sttask *ptask, long reasons) {
 * 	           		// Try to free the customed task in its error handler
 * 	           		if (MYTASK_AUTO_FREE & stpol_get_userflags(ptask)) {
 * 	           			stpool_task_detach(ptask);
 * 	           			stpool_delete_task(ptask);
 * 	           			return;
 * 	           		}
 * 	           }
 *
 * 	           task = stpool_task_new(pool, "task", task_run, task_err_handler, task_arg);
 * 	           stpool_task_set_userflags(ptask, MYTASK_AUTO_FREE);
 * 	           stpool_task_queue(task);
 * 	           
 * 	           (Here we do not concern about when to free the customed task object, we just
 * 	            free it in its callback if we do not use it any more)
 * @endcode
 *
 * @return  None
 */
EXPORT void stpool_task_detach(struct sttask *ptask);

 /**
 * Check whether the task is free or not 
 *
 * @return 0 if the task is not free 
 *
 * @note its function is equals to (0 == @ref stpool_task_stat (ptask)), 
 *       but it is much more faster.
 * 	     
 * 	     (Only free tasks can be destroyed safely)
 */
EXPORT int  stpool_task_is_free(struct sttask *ptask);

/**
 * Wait for the pool's throtle in \@ms milliseconds
 *
 * @param [in] ptask the task object
 * @param [in] ms   the timeout. (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise.
 *               \n
 *         @ref POOL_ERR_TIMEDOUT \n
 *		   @ref POOL_TASK_ERR_DESTINATION 
 *		   @ref POOL_ERR_DESTROYING
 * 
 * @note  If the destination pool or group doest not support the throttle, 
 * their throttles are always off.
 */
EXPORT int  stpool_task_pthrottle_wait(struct sttask *ptask, long ms);

/**
 * Wait for the task's completion in \@ms milliseconds
 *
 * @param [in] ptask the task object
 * @param [in] ms   the timeout. (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise.
 *                                              \n
 *         @ref POOL_ERR_TIMEDOUT               \n
 *		   @ref POOL_TASK_ERR_DESTINATION       \n
 *		   @ref POOL_ERR_NSUPPORT               \n
 */
EXPORT int  stpool_task_wait(struct sttask *ptask, long ms);

/**
 * Wait for the all tasks' completions in \@ms milliseconds
 *
 * @param [in] ptask  the task object
 * @param [in] entry the task objects array
 * @param [in] n     the length of the array
 * @param [in] ms    the timeout. (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise.
 *                                              \n
 *		   @ref POOL_TASK_ERR_DESTINATION       \n
 *         @ref POOL_ERR_GROUP_NOT_FOUND        \n
 *         @ref POOL_ERR_TIMEDOUT               \n
 *         @ref POOL_ERR_NSUPPORT               \n
 *
 * @note the NULL items will be skiped, and if the entry does not contain any items, 
 * 		 This API returns 0
 *
 * @see @ref stpool_wait_all, @stpool_wait_cb
 */
EXPORT int   stpool_task_wait_all(struct sttask *entry[], int n, long ms); 

/**
 * Wait for any tasks' completions in \@ms milliseconds
 *
 * @param [in] ptask  the task object
 * @param [in] entry the task objects array
 * @param [in] n     the length of the array
 * @param [in] ms    the timeout. (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise.
 *                                            \n
 *		   @ref POOL_TASK_ERR_DESTINATION     \n
 *         @ref POOL_ERR_GROUP_NOT_FOUND      \n
 *         @ref POOL_ERR_TIMEDOUT             \n
 *         @ref POOL_ERR_NSUPPORT             \n
 *
 * @note the NULL items will be skiped, and if the entry does not contain any items, 
 * 		 This API returns 0
 */
EXPORT int   stpool_task_wait_any(struct sttask *entry[], int n, long ms); 


/*----------------APIs about the pool --------------*/
/**
 * A const string to describle the version of the pool
 *
 *    y/m/d-version-desc
 */
EXPORT const char *stpool_version();

/**
 * Get the description of the error code
 *
 * @param [in] error    the error code returned by the library
 *
 * @return the const string to describle the error code
 */
EXPORT const char *stpool_strerror(int error);

/**
 * Create a task pool
 *
 * @param [in] desc       a const string to describle the pool. (the library will copy its contents)
 * @param [in] eCAPs      the neccessary capabilities that the pool must support
 *                        (see stpool_caps.h for more details)
 *
 * @param [in] maxthreads the limited number of working threads who will be 
 *                        created by pool to provide services.
 *
 * @param [in] minthreads the min number of working threads that should be 
 *                        reserved to wait for tasks. (the reserved threads 
 *                        will not quit even if there are none any tasks 
 *                        existing in the pool's pending queue)
 *
 * @param [in] suspend    if \@suspend is 1, the pool will not schedule any 
 *                        tasks that have been added into the pool untill 
 *                        the user calls @ref stpool_resume to mark it active
 *
 * @param [in] pri_q_num  the number of the priority queues that the user wants 
 *    				      to create. a priority task will be inserted into a 
 *    				      propriate priority queue by order according to its 
 *    				      priority, and the tasks who has the higher priority
 *    				      will be scheduled prior to the tasks who has a lower
 *    				      priority.         
 *                                              <pre>
 *                                       taskPendingQueue (priority queue)
 *                                               |        
 *                                               |
 *                      -------------------------------------------------------
 *                      |task_0_top |          .....              |task_k_top |
 *                      |task_0_top |          .....              |task_k_top |
 *                      |task_0_back|          .....              |task_k_back|
 *                      |task_0_back|          .....              |task_k_back|
 *                      |  .....    |          .....              |   ....    |
 *                      -------------                             -------------
 *                         queue[0]                                  queue[n-1] 
 *                                      (task_\@pri_\@sche_policy)
 *                                    (n = \@pri_q_num, 0 <= k <= 99)
 *                          \n
 *                          Each priority queue has m priorities (m = 100 / \@pri_q_num). 
 *                      and the tasks existing in the queue[i] will be scheduled prior
 *                      to the tasks existing in the queue[i-1]. (i>=1) 
                                               </pre>
 *
 * @return the pool object on success, NULL otherwise
 *
 * @note user should call @ref stpool_release to free it if he does not need it any more.
 *       the param \@suspend and \@priq_q_num may be ignored by the library if the \@eCAPs 
 *       does not contain eCAP_F_SUSPEND and eCAP_F_PRIORITY
 */
EXPORT stpool_t * stpool_create(const char *desc, long eCAPs, int maxthreads, int minthreads, int suspend, int pri_q_num);

/**
 * Get the real capbilities of the created pool.
 *    
 * @param [in] pool the pool object
 *
 * @return the capbilities masks, see stpool_caps.h for more details
 */
EXPORT long stpool_caps(stpool_t *pool);

/**
 * Get the description of the pool.
 *    
 * @param [in] pool the pool object
 *
 * @return the first param passed to @ref stpool_create 
 */
EXPORT const char *stpool_desc(stpool_t *pool);

/**  
 * Set the scheduling attribute for the working threads. 
 *
 * @param [in] pool  the pool object
 * @param [in] attr  the attribute that the user wants to apply on the working threads
 *
 * @return None
 *
 * @note The attribute does not have any effect on the servering threads who has been
 * 		 created by the pool.
 */
EXPORT void  stpool_thread_setscheattr(stpool_t *pool, struct stpool_thattr *attr);

/**
 * Get the scheduling attribute of the working threads
 *
 * @param [in]  pool the pool object
 * @param [out] attr the buffer used to receive the attribute
 *
 * @return \@attr
 */
EXPORT struct stpool_thattr *stpool_thread_getscheattr(stpool_t *pool, struct stpool_thattr *attr);

/**  
 * Set the scheduling attribute for the working threads. 
 *
 * @param [in] pool  the pool object
 * @param [in] attr  the attribute that the user wants to apply on the working threads
 *
 * @return None
 *
 */
EXPORT void  stpool_thread_settaskattr(stpool_t *pool, struct stpool_taskattr *attr);

/**
 * Get the scheduling attribute of the working threads
 *
 * @param [in]  pool the pool object
 * @param [out] attr the buffer used to receive the attribute
 *
 * @return \@attr
 */
EXPORT struct stpool_taskattr *stpool_thread_gettaskattr(stpool_t *pool, struct stpool_taskattr *attr);

/**
 * Increase the reference of the pool
 *
 * The reference of the pool is 1 after user's succeding in returning from
 * @ref stpool_create. and when the reference of the pool is changed to zero, 
 * the pool will be destroyed automatically.
 *
 * @param [in] the pool object
 *
 * @return the current reference of the pool
 *
 * @see @ref stpool_release
 */
EXPORT long stpool_addref(stpool_t *pool);

/**
 * Decrease the reference of the pool. 
 *
 * @param  [in] pool the pool handle
 * @return [in] the current reference of the pool.
 *
 * @note
 * 	   If the reference is changed to zero, the pool object will not 
 * be avaliable for users any more, and the pool will be marked destroyed, 
 * as a result, the pool will take some actions listed below to destroy 
 * the pool properly.
 *
 *      1. @ref stpool_task_queue and @ref stpool_add_routine will 
 *      	return @ref POOL_ERR_DESTROYING          
 *      
 *      1. if the pool is in suspended status, all pending tasks will be removed 
 *         and their error handlers will be called by the pool in the background. 
 *
 * @see @ref stpool_addref
 */
EXPORT long stpool_release(stpool_t *pool);

/**
 * Set the timeout value for the working threads 
 * 
 * when tasks are added into the pool and the pool has not been marked 
 * suspended, the pool will create service threads imediately to excute 
 * the tasks. and the threads will quit automatically if they have not 
 * gotton any tasks in (\@acttimeo + random() % \@randtimeo) seconds. the 
 * default value of \@actitimeo is 20. and the \@randtimeo is 30 
 *
 * @param [in] pool      the pool object
 * @param [in] acttimeo  the base timeout 
 * @param [in] randtimeo the random timeout
 *
 * @return None
 *
 * @note It only does work on the dynamic pool (see @ref eCAP_F_DYNAMIC)
 */
EXPORT void stpool_set_activetimeo(stpool_t *pool, long acttimeo, long randtimeo);

/**
 * Adjust the working threads number
 *
 * This function does not block, the pool will record the param and adjust
 * the environment as soon as possible in the background.
 *
 * @param [in] pool       the pool object
 * @param [in] maxthreads the limited number of working threads who is created by 
 *	                      the pool to excute tasks. it must be a positive value.
 *
 * @param [in] minthreads the min number of working threads that should be reserved 
 *						  to wait for tasks. and it can not be greater than the \@maxthreads.
 *						  (\@minthreads will be ignored if the pool is a pool who has the fixed
 *						  number of working threads).
 * @return None
 *
 * @see @ref stpool_adjust, @stpool_flush
 */
EXPORT void stpool_adjust_abs(stpool_t *pool, int maxthreads, int minthreads);

/**
 * Ajust the the working threads number
 *
 * @ref stpool_adjust is similar to @ref stpool_adjust_abs, the only difference 
 * between them is that the parameters received by @ref stpool_adjust are relative.
 *
 *    stpool_adjust(pool, 1, 2)  <==> stpool_adjust_abs(
 *    									    pool, 
 *    									    pool->maxthreads + 1, 
 *    									    pool->minthreads + 2
 *    									)
 *
 *    stpool_adjust(pool, 2, -1) <==> stpool_adjust_abs(
 *    					                    pool, 
 *    					                    pool->maxthreads + 2, 
 *    					                    pool->minthreads - 1
 *    					                )
 */
EXPORT void stpool_adjust(stpool_t *pool, int maxthreads, int minthreads);

/**
 * Flush the working threads (for dynamic pool)
 *
 * This API is to make sure that the reserved threads number will be equal to the
 * param that we have configured by @ref stpool_adjust, or @ref stpool_adjust_bas
 * absolutly. 
 * 
 * @note @ref stpool_adjust and @stpool_adjust_abs will not kill any threads if its 
 *       settings match the condition listed below. 
 *      
 *      \@maxthread <= pool->maxthreads && pool->minthreads > \@minthreads
 * 
 * (in this situation, @ref stpool_adjust and @ref stpool_adjust_abs just wakes the 
 * threads up, and the waked threads will decide to quit by themself in a propriate
 * time if they can not get any task to execute)
 *
 * @param [in] the pool object
 *
 * @return The number of threads who is marked died by it
 */
EXPORT int  stpool_flush(stpool_t *pool);

/**
 * Set the overload policy for the pool
 * 
 * @param [in]  pool the pool object
 * @param [out] attr the overload policy that the user wants to apply
 *
 * @return None
 */

EXPORT void stpool_set_overload_attr(stpool_t *pool, struct oaattr *attr);

/**
 * Get the overload policy of the pool
 * 
 * @param [in]  pool the pool object
 * @param [out] attr the current overload policy
 *
 * @return \@attr
 */
EXPORT struct oaattr *stpool_get_overload_attr(stpool_t *pool, struct oaattr *attr);

/**
 * Get the status of the pool
 * 
 * @param [in]  pool the pool object
 * @param [out] stat the buffer to receive the status 
 *
 * @return \@stat
 */
EXPORT struct pool_stat *stpool_stat(stpool_t *pool, struct pool_stat *stat);

/**
 * Print the status of the pool into the buffer.
 *
 * @param  [in]  stat      the status object 
 * @param  [out] buffer    the buffer into where the status data will be flushed, if it
 *                         is NULL, the inner static buffer will be used.
 * @param  [in] bufferlen  the length of the buffer.
 *
 * @note \@ref stpool_stat_print use the inner static buffer to receive the status datas.
 *
 * @return the address of the buffer who has been filled up with the status datas
 */
EXPORT const char *stpool_stat_print2(struct pool_stat *stat, char *buffer, size_t bufferlen);

/**
 * Print the status of the pool into the inner static buffer.
 */
EXPORT const char *stpool_stat_print(stpool_t *pool);

/**
 * Print the scheduler's informations into the inner static buffer 
 */
#define stpool_scheduler_map_dump(pool) stpool_scheduler_map_dump2(pool, NULL, 0)

/**
 * Print the scheduler's informations into the buffer 
 *
 * @param [in]     pool    the pool object
 * @param [in,out] buffer  the buffer used to receive the informations of the scheduler,
 *                         if it is NULL, \@len will be ignored, and the inner static buffer 
 *                         will be used to receive the informations 
 *
 * @param [in]     len     the length of the buffer
 *
 * @return the address of the buffer who has been filled up with the scheduler's informations 
 */
EXPORT char *stpool_scheduler_map_dump2(stpool_t *pool, char *buffer, int len);

/**
 * Suspend the pool
 *
 * If we suspend the pool, the pool will go to sleep, and all pending tasks 
 * will not be excuted any way until the user calls @ref stpool_resume to wake
 * it up.
 *
 * @param [in] pool  the pool handle 
 *
 * @param [in] ms    If \@ms is none zero, it indicated that the user want to wait
 *                   for completions of the tasks who is being scheduled or is being
 *                   removed in \@ms milliseconds. (-1 == INFINITE)
 *                  
 * @return on success, error code listed below otherwise
 *                                              \n
 *          @ref POOL_ERR_TIMEDOUT              \n
 *          @ref POOL_ERR_NSUPPORT              \n
 */
EXPORT int stpool_suspend(stpool_t *pool, long ms);

/**
 * Wake up the pool to schedule the pending tasks again.
 *    
 * @param  [in] pool the pool object 
 *
 * @return None
 *
 * @see @ref stpool_suspend
 */
EXPORT void stpool_resume(stpool_t *pool);

/**
 * Add a routine into the pool's pending queue
 *
 * This API will create a dummy task object firstly, and then initialize it
 * with the parameters passed by user. when the routine is done completely,
 * the dummy task object will be destroyed by the pool automatically.
 *
 * @param [in] pool              the pool object
 * @param [in] name              a const string to describle the routine
 * @param [in] task_run          the work routine         (Can not be NULL)
 * @param [in] task_err_handler  the completion routine   (Can be NULL)
 * @param [in] task_arg          the arguments            (Can be NULL)
 * @param [in] attr              the scheduling attribute (Can be NULL)
 *
 * @return 0 on success, error code listed below otherwise
 *                                                                        \n
 *          @ref POOL_ERR_NOMEM                                           \n
 *          @ref POOL_ERR_THROTTLE                                        \n
 *          @ref POOL_ERR_DESTROYING                                      \n
 *          @ref POOL_ERR_NSUPPORT                                        \n
 */
EXPORT int  stpool_add_routine(stpool_t *pool, 
			const char *name, void (*task_run)(struct sttask *), 
			void (*task_err_handler)(struct sttask *, long reasons),
			void *task_arg, struct schattr *attr);

/**
 * Remove all pending tasks existing in the pool
 *	
 * This API will mark all tasks who is in the pool's pending queue removed, 
 * and remove the \@DO_AGAIN flag for all tasks who is being scheduling or is
 * being removed.
 *
 * @param [in] pool                the pool object
 * @param [in] dispatched_by_pool  if it is none zero, the error handlers of tasks
 *                                 who is in the pending queue will be called by the pool
 *                                 in the background, or the API itself will call them direclty.
 *
 * @return the effected tasks number
 */
EXPORT int  stpool_remove_all(stpool_t *pool, int dispatched_by_pool);

/**
 * Mark the visitable tasks existing in the pool with specified flags
 *
 * These flags listed below can be used to mark the task by user
 *
 *      .TASK_VMARK_REMOVE
 *            If the task is really in the pending queue, the task will be marked removed, it 
 *         means that if the task is being scheduled or is being dispatched, this flag will has
 *         no effect. if a task is marked with this flag, the function who marks the task will be
 *         responsible for calling its error handler directly.
 *       
 *	    .TASK_VMARK_REMOVE_BYPOOL
 *			  The diference between TASK_VMARK_REMOVE is that the error handlers of the tasks will 
 *		  be called by the mark function itself, but if the tasks are marked with TASK_VMARK_REMOVE_BYPOOL, 
 *		  the tasks will be removed from the pending queue and the pool is responsible for calling their 
 *		  error handlers in the background.
 *
 *	    .TASK_VMARK_DISABLE_QUEUE		
 *	         If the task who has been marked with this flag, POOL_TASK_ERR_DISABLE_QUEUE will 
 *	     be returned if user tries to delive it into the pool by @ref stpool_task_queue, user 
 *	     can mark the task with TASK_VMARK_ENABLE_QUEUE later to replace it.
 *
 *	    .TASK_VMARK_ENABLE_QUEUE		
 *         See TASK_VMARK_DISABLE_QUEUE
 *
 * @param [in] pool    the pool object 
 * @param [in] lflags  the flags that the user want to use to mark the tasks
 *    
 * @return the number of tasks effected by the flags 
 *
 * @note if the visitable task is being scheduled or is being dispatched, and it has been marked with
 * \@DO_AGAIN by @ref stpool_task_queue, the mark \@DO_AGAIN will be cleared if the param \@lflags
 * owns TASK_VMARK_REMOVE or TASK_VMARK_REMOVE_BYPOOL. but the task object will not store the
 * REMOVE flags.
 */
EXPORT long stpool_mark_all(stpool_t *pool, long lflags); 

/**
 * Mark all tasks existing in the pool currently with specified flags.
 *
 * @ref stpool_mark_all and @ref stpool_mark_cb do essentially the same thing, the only 
 * difference is that @ref stpool_mark_cb uses the flags returned by the walk callback 
 * to mark the tasks.
 *
 * @param [in] pool      the pool object
 * @param [in] wcb       the user callback to walk the tasks.
 * @param [in] wcb_args  the argument reserved for wcb
 * 
 * @return the effected tasks number
 *
 * @note If wcb returns -1, @ref stpool_mark_cb will stop walking the tasks and return
 */
EXPORT int  stpool_mark_cb(stpool_t *pool, Walk_cb wcb, void *wcb_arg);

/**
 * Set the throttle's status of the pool
 *
 * @param [in] pool    the pool object
 * @param [in] enable  if it is none zero, the pool's throttle will be turned 
 *                     on, or the pool's throttle will be turned off
 *
 * @return If the created pool does not has eCAP_F_THROTTLE feature, it returns
 *         POOL_ERR_NSUPPORT, or it returns 0
 *
 * @note  If the pool's throttle is on, the pool will prevent users delivering 
 * 		  any tasks into its pending queue.
 */
EXPORT int stpool_throttle_enable(stpool_t *pool, int enable);

/** 
 * Get the WAKE_id
 *
 *	   User can save the wkid before his calling the wait functions such as 
 * @ref stpool_throttle_wait, @ref stpool_task_wait, etc, then he can call 
 * @ref stpool_wakeup(wakeid) to wake up these wait functions.  
 *                            <pre>
 *	  model:
 *	        thread1:                                     thread2:
 *	           wakeid = stpool_wakeid(); 
 *	           @ref stpool_task_wait (...)   wake up
 *	           	                          <-----------  @ref stpool_wakeup (wakeid)
 *                            </pre>
 *
 * @pass   None
 *
 * @return The id used to wake up the wait functions
 */
EXPORT long stpool_wakeid();

/**
 * Wait for the pool's throtle in \@ms milliseconds
 *
 * @param [in] ptask the task object
 * @param [in] ms   the timeout. (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise.
 *                                    \n
 *         @ref POOL_ERR_TIMEDOUT     \n
 *         @ref POOL_ERR_NSUPPORT     \n
 */
EXPORT int stpool_throttle_wait(stpool_t *pool, long ms);

/**
 * Wait for all completions of the tasks existing in the pool in \@ms milliseconds
 *
 * @param [in] pool the pool object
 * @param [in] ms   the timeout (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise
 *                                     \n
 *         @ref POOL_ERR_TIMEDOUT      \n
 *         @ref POOL_ERR_NSUPPORT      \n
 */
EXPORT int  stpool_wait_all(stpool_t *pool, long ms);

/**
 * Wait for all completions of the visiable tasks existing in the pool in \@ms milliseconds
 *
 * This API will visit all of visitable tasks existing in the pool and pass their 
 * status to the user's walk callback, and if user's walk callback returns
 * a none zero value on them, this API will wait for their completions.
 *
 * @param [in] pool     the pool object
 * @param [in] wcb      the callback to walk the tasks
 * @param [in] wcb_args the arguments reserved for wcb
 * @param [in] ms       the timeout (-1 == INFINITE)
 *
 * @return 0 on success, error code listed below otherwise
 *                                    \n
 *        @ref POOL_ERR_TIMEDOUT      \n
 *        @ref POOL_ERR_NSUPPORT      \n
 */
EXPORT int  stpool_wait_cb(stpool_t *pool, Walk_cb wcb, void *wcb_arg, long ms); 

/**
 * Wait for any completions of the tasks existing in the pool in \@ms milliseconds
 *
 * @param [in] pool     the pool object
 * @param [in] ms       the timeout (-1 == INFINITE)
 *
 * @return 0 on success, error code listed below otherwise
 *                                   \n
 *        @ref POOL_ERR_TIMEDOUT     \n
 *        @ref POOL_ERR_NSUPPORT     \n
 */
EXPORT int  stpool_wait_any(stpool_t *pool, long ms);

/**
 *	Wake up the wait function by \@wakeup_id
 *
 *	All of the WAIT functions listed below can be waked up by @stpool_wakeup
 *                                    \n
 *	@ref stpool_task_wait             \n
 *	@ref stpool_task_wait_all         \n
 *	@ref stpool_task_wait_any         \n
 *	@ref stpool_task_pthrottle_wait   \n
 *	@ref stpool_wait_all              \n
 *	@ref stpool_wait_any              \n
 *	@ref stpool_throttle_wait         \n
 *	@ref stpool_status_wait           \n
 *                                    \n
 *	@ref stpool_task_gthrottle_wait   \n
 *	@ref stpool_task_pgthrottle_wait  \n
 *	@ref stpool_group_wait_all        \n
 *	@ref stpool_group_wait_any        \n
 *	@ref stpool_group_throttle_wait   \n
 *	@ref stpool_group_status_wait     \n
 * 
 *  @param [in] wakeup_id  the id returned by @ref stpool_wakeid
 
 *  @return None
 */
EXPORT void stpool_wakeup(long wakeup_id);

#ifdef __cplusplus
}
#endif

#endif
