#ifndef __ST_POOL_H__
#define __ST_POOL_H__

#include <time.h>
#include <stdlib.h>

#ifdef _WIN32
#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#else
#define EXPORT
#endif

/* Author: piggy_xrh@163.com 
 *	  Stpool is portable and efficient tasks pool library, it can works on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  ( Email: piggy_xrh@163.com  QQ: 1169732280 )
 */
typedef void *HPOOL;

/* Error code */
enum {	
	/* System is out of memeory 
	 *  
	 * (@stpool_add_routine may return the error code)
	 */
	STPOOL_ERR_NOMEM = 1,
	
	/* Task pool is being destroyed 
	 *  
	 * (It indicates that @stpool_release has been called and 
	 *  the reference of the pool is zero)
	 */
	STPOOL_ERR_DESTROYING = 2, 
	
	/* Task pool has been destroyed or has not been created */
	STPOOL_ERR_NOCREATED  = 3,

	/* The throttle of the pool is disabled 
     * 
	 * (It indicates that user has called @stpool_throttle_enable(hp, 0)
	 *  to turn the throttle swither on)
	 */
	STPOOL_ERR_THROTTLE = 4,

	/* The task has been removed by user.
	 *
	 * (It indicates that the task has been removed by @stpool_remove_pending_task 
	 *  or @stpool_mark_task(ex))
	 */
	STPOOL_TASK_ERR_REMOVED = 5,				
		
	/* The task is requested to be added into a deferent pool, But the task has 
	 * not been done completely.
	 *
	 *  (@stpool_add_routine may return the error code)
	 */
	STPOOL_TASK_ERR_BUSY = 7,	
};

struct sttask_t {
	/* A const string to describle the task */
	const char *task_name;
	
	/* @task_run will be called when the task is scheduled by the pool. 
     *  user can do their works in this function.
	 */
	int  (*task_run)(struct sttask_t *tsk);	
	
	/*  	If @task_complete is not NULL, it will be called when one of the conditions 
	 *  below matches.
	 *       1. @task_run has been executed by the pool.
	 *       2. The task is removed from the pool by @stpool_remove_pending_task
	 *          or @stpool_mark_task(ex)
	 *
	 *   NOTE:
	 *	  1). If @task_run has been excuted by the pool, the argument @vmflags will
	 * owns the mask STTASK_VMARK_DONE, and the @task_code will be set to the value
	 * returned by @task_run. or the the @task_code will be set properly. 
	      (@see the error codes STPOOL_XXX describled above)
	 */
	void (*task_complete)(struct sttask_t *tsk, long vmflags, int task_code);

	/* The argument reserved for task */
	void *task_arg;
	
	/* After users' succeeding in calling @stpool_add_task or @stpool_add_routine, 
	 * the @hp_last_attached will be set to the handle of the pool automatically.
	 */
	HPOOL hp_last_attached;

	/* Private datas for inner implemention. */
	char impl_stack[0];
};

struct schattr_t {
	/* If @permanent is not zero, the task's priority will not
	 * be changed until the user call @stpool_task_setschattr
	 * to modifiy it manually */
	int permanent;
	
	/* The priority of the task */
	int sche_pri;

	/* The policy to schedule the task (STPOLICY_PRI_SORT_XX) */
	int sche_pri_policy;
};

/* Status of the task */
enum {
	/* Task is waiting for being schuduled */
	STTASK_STAT_WAIT  = (short)0x01,
	
	/* Task is being scheduled by the pool */
	STTASK_STAT_SCHEDULING  = (short)0x02,
	
	/* Task has been swaped from the ready queue since the 
	 * pool that the task belong to has been marked suspened */
	STTASK_STAT_SWAPED = (short)0x04,
	
	/* Task has been marked with removed */
	STTASK_STAT_DISPATCHING = (short)0x08,
		
	/* The task should be added into the pool automatically 
	 * after having done the task */
	STTASK_STAT_WAIT_PENDING = (short)0x10,
};

/* Flags of the task */
enum {
	/* @task_run has been executed */
	STTASK_VMARK_DONE = 0x0001,
		
	/* The task is removed by @stpool_remove_pending_task/@stpool_mark_task 
	 *    The user can mark tasks with STTASK_VMARK_REMOVE_BYPOOL or 
	 * STTASK_VMARK_REMOVE, and as a result, the tasks will be removed from 
	 * the pending queue.
     *    If task is marked with STTASK_VMARK_REMOVE_BYPOOL, @task_complete 
	 * will be called by the pool. or @task_complete will be called by the 
	 * function who marks the task. 
	 */
	STTASK_VMARK_REMOVE_BYPOOL = 0x0004, 	
	STTASK_VMARK_REMOVE = 0x0008,
	
	/* The pool is being destroyed */
	STTASK_VMARK_POOL_DESTROYING = 0x0010,		
};

/* The policy to schedule the tasks */
enum {
	/* Insert our task before the tasks who has the same
	 * priority exisiting in the pool.
	 */
	STPOLICY_PRI_SORT_INSERTBEFORE = 1,

    /* Insert our task after the tasks who has the same
	 * priority exisiting in the pool.
	 */
	STPOLICY_PRI_SORT_INSERTAFTER,
};

/* Status details about the task */
struct stpool_tskstat_t {
	/* Status of the task */
	long  stat;
	
	/* Flags of the task (STTASK_VMARK_XX) */
	long vmflags;

	/* Current priority of the task */
	int  pri;

	/* The object of the task */
	struct sttask_t *task;
};

/* Status of the pool */
struct stpool_stat_t {
	long ref;                    /* The user refereces */
	time_t created;              /* The time when the pool is created */
	int pri_q_num;               /* The number of the priority queue */
	int throttle_enabled;        /* Is throttle swither on ? */
	int suspended;               /* Is pool suspended ? */
	int maxthreads;              /* Max servering threads number */
	int minthreads;              /* Min servering threads number */
	int curthreads;              /* The number of threads exisiting in the pool */
	int curthreads_active;       /* The number of threads who is scheduling tasks */
	int curthreads_dying;        /* The number of threads who has been marked died by @stpool_adjust(_abs)/@stpool_flush */
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

/*----------------APIs about the task --------------*/

/*@stpool_task_size
 *     Acquire the object size of the task  
 *
 * Arguments:
 *      None
 *
 * Return:
 * 		the object size of the task 
 */
EXPORT size_t stpool_task_size();


/*@stpool_task_init
 *     Initialize the task object 
 *
 * Arguments:
 * 		@see the definition of the struct task_t for more details.
 *
 * Return:
 * 		None
 */
EXPORT void   stpool_task_init(struct sttask_t *ptsk, 
					const char *name, int (*run)(struct sttask_t *ptsk),
					void (*complete)(struct sttask_t *ptsk, long vmflags, int code),
					void *arg);
/*@stpool_task_new
 *     Allocate a task object from the heap. (The task allocated by
 * @stpool_task_new should be free manually by calling @stpool_task_delete)
 * 
 * Arguments:
 * 		@see the definition of the struct task_t for more details.
 *
 * Return:
 *		On success, @stpool_task_new returns a task object, 
 *		On error, NULL is returned.
 */
EXPORT struct sttask_t *stpool_task_new(const char *name,
						int (*run)(struct sttask_t *ptsk),
						void (*complete)(struct sttask_t *ptsk, long vmflags, int code),
						void *arg);

/*@stpool_task_clone
 *     Clone a task from a task object. the cloned task should be free manually 
 * by calling @stpool_task_delete.
 *
 * Arguments: 
 * 		@ptsk           [in] The source object.
 * 		@clone_schattr  [in] Tell the system whether clone the task's scheduling attribute or not.
 *
 * Return:
 *		On success, @stpool_task_clone returns a task object, 
 *		On error, NULL is returned.
 */
EXPORT struct sttask_t *stpool_task_clone(struct sttask_t *ptsk, int clone_schattr);

/*@stpool_task_delete
 *     Free the task object that allocated by @stpool_task_new or @stpool_task_clone
 *
 * NOTE:
 * 	   Only tasks who does not belong to any pool can be free safely !
 *	normaly, the task will not be removed from the pool until the task's 
 *	completion routine is executed completely. it means that it is not 
 *	safe to destroy the task manually in the task's completion routine . 
 *	(But If you really want to do this like that, you should call 
 *	 @stpool_detach_task firstly to remove the task from the pool 
 *	 imediately).
 *
 * Arguments: 
 * 		@ptsk [in] The task object that the user wants to destroy.
 *
 * Return:
 * 	    None
 */
EXPORT void  stpool_task_delete(struct sttask_t *ptsk);

/*@stpool_task_setschattr
 *     Set the scheduling attribute for the task
 *
 * Arguments: 
 * 		@ptsk [in] The task object.
 * 		@attr [in] The scheduling attribute that will be applyed on the task
 *
 * Return:
 * 	    None
 */
EXPORT void  stpool_task_setschattr(struct sttask_t *ptsk, struct schattr_t *attr);

/*@stpool_task_getschattr
 *     Get the scheduling attribute for the task
 *
 * Arguments: 
 * 		@ptsk [in]  The task object.
 * 		@attr [out] The scheduling attribute of the task.
 *
 * Return:
 * 	    None
 */
EXPORT void  stpool_task_getschattr(struct sttask_t *ptsk, struct schattr_t *attr);


/*----------------APIs about the pool --------------*/
/*@stpool_version
 *    y/m/d-version-desc
 */
EXPORT const char *stpool_version();

/*@stpool_create
 *     Create a thread pool to running tasks. user should call
 * @stpool_release to free the pool if he does not need it any
 * more.
 * 
 * Arguments:
 *    @maxthreads [in]  the limited number of threads that is created 
 *    				    by pool to provide services.
 *
 *    @minthreads [in]  the min number of threads that should be reserved 
 *    	                to wait for tasks.
 *
 *    @suspend    [in]  if @suspend is 1, the pool will not schedule any 
 *                      tasks that have been added into the pool untill the 
 *                      user calls @stpool_resume to wake up it.
 *
 *    @pri_q_num  [in]  the number of the priority queues that the user wants 
 *    				    to create. a priority task will be inserted into a propriate 
 *    				    priority queue, and the tasks who has the higher priority 
 *    				    will be scheduled prior to the tasks who has a lower priority. 
 * Return:
 *		On success, @stpool_create returns a pool handle.  On error, NULL is returned, 
 */
EXPORT HPOOL stpool_create(int maxthreads, int minthreads, int suspend, int pri_q_num);

/*@stpool_addref
 *     Increase the reference of the pool. and the reference is 1 
 * after calling @stpool_create. When the reference of the pool is 
 * zero, the pool will be destroyed automatically.
 *
 * Arguments:
 *    @hp  [in]  the pool handle
 *
 * Return:
 *	  the current reference of the pool.
 */
EXPORT long stpool_addref(HPOOL hp);

/*@stpool_release
 *     Decrease the reference of the pool. 
 * <see @stpool_addref for more details about the reference>
 *
 * Arguments:
 *    @hp  [in]  the pool handle
 *
 * Return:
 *	  the current reference of the pool.
 *
 * NOTE:
 * 	   If the reference is zero, the pool will be marked destroyed, 
 * and as a result, all tasks existing in the pool will be marked with 
 * STTASK_VMARK_POOL_DESTROYING. and in this case. If the pool is 
 * suspended, all pending tasks will be removed and dispatched by the
 * pool in the background.
 * 	  @stpool_release always return imediately. and the pool will be 
 * destroyed automatically at the background after all tasks have been
 * done when it is marked with STTASK_VMARK_POOL_DESTROYING.
 */
EXPORT long stpool_release(HPOOL hp);

/*@stpool_set_activetimeo
 *     Set the timeout value of the servering threads belong to the pool, 
 * when tasks are added into the pool and the pool has not been marked sus
 * -pended, the pool will create service threads imediately to excute the 
 * tasks. and the threads will quit automatically if they have not gotton 
 * any tasks in (@acttimeo + rand() % 60) seconds. the default value of 
 * @actitimeo is 20.
 *
 * Arguments:
 *    @hp       [in]  the pool handle
 *
 *    @acttimeo [in]  seconds for waitting for tasks 
 * Return:
 *	  None
 */
EXPORT void stpool_set_activetimeo(HPOOL hp, long acttimeo);

/*@stpool_adjust_abs
 *     Adjust the threads number of the pool. this function does not
 * block, the pool will record the param and adjust the environment
 * as soon as possible in the background.
 *
 * Arguments:
 *    @hp          [in]  the pool handle
 *
 *	  @maxthreads  [in]  the limited number of threads who is created
 *	                     by the pool to excute tasks.
 *	 
 *	  @minthreads  [in]  the min number of threads that should be reserved 
 *						 to wait for tasks. and it can not be greater than
 *						 the @maxthreads.
 * Return:
 *	  None
 */
EXPORT void stpool_adjust_abs(HPOOL hp, int maxthreads, int minthreads);

/*@stpool_adjust
 *   @stpool_adjust is similar to @stpool_adjust_abs, the only 
 * difference between them is that the paraments received by 
 * @stpool_adjust are relative.
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
EXPORT void stpool_adjust(HPOOL hp, int maxthreads, int minthreads);

/*@stpool_flush
 *     Mark all unused threads died. it's not recommented to do this since
 * the pool knows when to shutdown the threads.
 * 	
 * 	Arguments:
 *    @hp   [in]  the pool handle
 *
 * Return:
 * 	   The number of threads who is marked died by @stpool_flush
 *
 * NOTE:
 * 	   This function does not block, user can call @stpool_adjust_wait to
 * wait for all died threads' exits.
 */
EXPORT int  stpool_flush(HPOOL hp);

/*@stpool_adjust_wait
 *    @stpool_adjust and @stpool_adjust_abs do not block, If users 
 * want to make sure that the pool's status is consistent with the 
 * param they have set, some threads may have to be marked died. 
 * and the threads who has been marked died will exit imediately 
 * after having done their current tasks. 
 *    @stpool_adjust will not return until there are none servering
 * threads marked died in the pool.
 *
 * Arguments:
 *    @hp   [in]  the pool handle
 *
 * Return:
 * 	   Non
 */
EXPORT void stpool_adjust_wait(HPOOL hp);

/*@stpool_getstat
 *     Get the status of the pool
 *
 * Arguments:
 *    @hp       [in]  the pool handle
 *
 *    @stat     [out] the status structure.
 *
 * Return:
 *	  The argument @stat filled up with the pool's status will
 * be returned.
 */
EXPORT struct stpool_stat_t *stpool_getstat(HPOOL hp, struct stpool_stat_t *stat);

/*@stpool_status_dump
 *     Print the status of the pool into the buffer.
 *
 * Arguments:
 *    @hp        [in]  the pool handle
 *
 *    @buffer    [out] the buffer into where the status data will 
 *    				   be flushed, If it is NULL, the inner static 
 *    				   buffer will be used.
 *
 *    @bufferlen [in]  the length of the buffer.
 *
 * Return:
 *	  If the argument @buffer is not NULL, @buffer filled up with 
 * the pool's status will be returned. or the inner static buffer 
 * will be passed to user.
 */
EXPORT const char *stpool_status_print(HPOOL hp, char *buffer, size_t bufferlen);

/*@stpool_gettskstat
 *     Get the status of the task.
 *
 * Arguments:
 *    @hp       [in]  the pool handle 
 *
 *    @stat     [in/out]  the status that the user wants to have a look.
 *                        <NOTE: stat->task should be set to the address of the
 *                               task that the user interests>
 * Return:
 *     If @stat->task does not exist in the pool, the function will return 0, or 
 * @stat->stat will be returned. 
 */
EXPORT long stpool_gettskstat(HPOOL hp, struct stpool_tskstat_t *stat);

/* @stpool_suspend
 *	    If we suspend the pool, the pool will go to sleep, and all pending
 *	tasks will not be excuted any way until the user calls @stpool_resume 
 *	to wake up the pool.
 *
 * Arguments:
 *    @hp     [in]  the pool handle 
 
 *    @wait   [in]  If @wait is 1, it indicated that the user want to wait
 *                  for all the scheduling tasks' completions. 
 *                  (Some tasks maybe is being scheduled while user calls
 *                   @stpool_suspend, the pool give users a choices to determine 
 *                   whether the @stpool_suspend should wait for the scheduling 
 *                   tasks' completions)
 *
 * Return:
 *	  None
 */
EXPORT void stpool_suspend(HPOOL hp, int wait);

/* @stpool_resume
 *	     Wake up the pool to schedule the tasks again.
 *
 * Arguments:
 *    @hp     [in]  the pool handle 
 *
 * Return:
 *	  None
 */
EXPORT void stpool_resume(HPOOL hp);

/* @stpool_add_task
 *	     Add a task into the pool
 *
 * Arguments:
 *    @hp       [in]  the pool handle 
 
 *	  @ptsk     [in]  task that will be added into the pool
 *
 * NOTE:
 * 		@stpool_add_task can be called one more times, If the task
 * is already in the pool's pending queue, it does nothing, but if 
 * the task is being scheduled or has been marked with removed,
 * @stpool_add_task will mark the task with @DO_AGAIN. and as a result, 
 * the task who has been marked with @DO_AGAIN will be added into the
 * pool again automatically after the pool's having executed the task
 * completely.
 *
 *      But if the task has been marked with @DO_AGAIN, what will it
 * happen if the user mark the task with @STTASK_VMARK_REMOVE again 
 * by calling @stpool_mark_task(ex) or @stpool_remove_pending_task ?
 * 	    In this situations, the pool just remove the @DO_AGAIN flag for 
 * the task.
 *
 * Return:
 *		On success, it returns 0. 
 *		On error, error codes below may be returned, 
 *          STPOOL_ERR_DESTROYING
 *          STPOOL_ERR_NOCREATED
			STPOOL_ERR_THROTTLE 
 *          STPOOL_TASK_ERR_BUSY
 *
 */
EXPORT int  stpool_add_task(HPOOL hp, struct sttask_t *ptsk);

/* @stpool_add_routine
 *	     Add a routine into the pool. (The pool will create a task
 * object for the routine with the parameters, and after having executed
 * the routine, the pool will detroy the task object automatically).
 *
 * Arguments:
 *    @hp       [in]  the pool handle 
 *	  @name     [in]  a const string to describle the task
 *	  @run      [in]  the working routine of the task
 *	  @complete [in]  the completion routine of the task
 *	  @arg      [in]  the argument reserved for the task
 *	  @attr     [in]  the scheduling policy of the task
 *
 * Return:
 *		On success, it returns 0. 
 *		On error, error codes below may be returned, 
 *          STPOOL_ERR_DESTROYING
 *          STPOOL_ERR_NOCREATED
 *			STPOOL_ERR_THROTTLE 
 *          STPOOL_ERR_NOMEM 
 */
EXPORT int  stpool_add_routine(HPOOL hp, 
			const char *name, int (*run)(struct sttask_t *), 
			void (*complete)(struct sttask_t *, long, int),
			void *arg, struct schattr_t *attr);

/* @stpool_remove_pending_task
 *	   Remove tasks that existing in the pending queue. 
 *
 * Arguments:
 *    @hp                 [in]  the pool handle
 *
 *    @tsk                [in]  If tsk NULL, all the tasks existing in the pending queue
 *                              will be removed.
 *
 *    @dispatched_by_pool [in] If dispatched_by_pool is 1, the pool will be responsible for
 *                             calling the tasks' @task_complete, and the vmflags passed to 
 *                             @task_complete will be marked with STTASK_VMARK_REMOVE_BYPOOL. 
 *                             or @stpool_remove_pending_task itself will call the task'
 *                             @task_complete, and the vmflags passed to @task_complete will
 *                             be marked with STTASK_MARK_REMOVE.
 * Return:
 *      The number of tasks that have been removed.
 */
EXPORT int  stpool_remove_pending_task(HPOOL hp, struct sttask_t *tsk, int dispatched_by_pool);

/* @stpool_mark_task(ex)
 *	    Walk the status of the tasks existing in the pool. user can mark the task with
 * masks listed below.
 *
 *      .STTASK_VMARK_REMOVE
 *            If the task is in the pending queue, the task will be removed and the task's
 *         vmflags will be set, But if the task is being scheduled or is being dispatched, 
 *         it has no effect. as you see, its functions are just the same as the @stpool_rem\
 *         ove_pending_task(hp, tsk, 0)'s.
 *	    
 *	    .STMASK_VMARK_REMOVE_BYPOOL
 *			  The diference between STTASK_VMARK_REMOVE is that the completion routines of the 
 *		  tasks marked with STTASK_VMARK_REMOVE will be called by @stpool_mark_task itself, but
 *		  if the tasks are marked with STMASK_VMARK_REMOVE_BYPOOL, the tasks will be removed
 *		  from the pending queue and the pool is responsible for calling their completions.
 *		  its functions are just the same as the @stpool_remove_pending_task(hp, tsk, 1)'s.
 *
 *   NOTE:
 *   	 Only tasks who is in the pending queue can be marked with STTASK_VMARK_REMOVE or
 *   STMASK_VMARK_REMOVE_BYPOOL. it means that if the condition #1 is true, the pool will 
 *   prevent the task from being marked with STTASK_VMARK_REMOVE or STTASK_VMARK_REMOVE_BYPOOL.
 *    
 *  #1:
 *   	((STTASK_STAT_SCHEDULING|STTASK_STAT_DISPATCHING) & @task_stat)
 *
 * Arguments:
 *    @hp            [in]  the pool handle 
 
 *    @ptsk      	 [in]  if ptsk is not NULL, @stpool_mark_task will try to mark all tasks
 *                    	   existing in the pool
 *	  @lflags        [in]  the flags that the user want to use to mark the task
 *    
 *    @tskstat_walk  [in]  a callback that will be called by @stpool_mark_task_ex to visit
 *                         the status of tasks exsiting in the pool.
 *
 *                         NOTE:
 *                         	  @tskstat_walk returns the flags that will be used to mark the
 *                         current task that passed to @tskstat_walk. but if @tskstat_walk 
 *                         returns -1, @stpool_mark_task_ex will do nothing and then it'll 
 *                         return and stop visiting the tasks. 
 *
 *    @arg           [in]  the argument reserved for @tskstat_walk
 * Return:
 *    If ptsk is not NULL, @stpool_mark_task return the flags of the task. Or it returns
 *    the number of tasks that processed by @stpool_mark_task.
 *
 *	  @stpool_mark_task_ex return the number of tasks processed by @tskstat_walk.
 */
EXPORT long stpool_mark_task(HPOOL hp, struct sttask_t *ptsk, long lflags); 
EXPORT int  stpool_mark_task_ex(HPOOL hp, 
					 long (*tskstat_walk)(struct stpool_tskstat_t *stat, void *arg),
					 void *arg);

/* @stpool_detach_task
 *	    Deatch a task from the pool. 
 * NOTE:
 * 	 	@stpool_detach_task will be only allowed to be called in
 * 	the in the task's working routine or in the task's completion 
 * 	routine. Usally a task will be connected with a pool when it 
 * 	having been added into the pool. and before the task's being 
 * 	removed from the pool completely, users can call @stpool_detach_task 
 * 	to remove the task from the pool.
 *
 * 	 NOTE:
 * 	 	The pool will give a notification to @stpool_task_wait(1/2/3/ex) 
 * 	once that the task has been removed from the pool. 
 *
 * Arguments:
 *    @hp     [in]  the pool handle 
 *
 *    @ptsk   [in]  the task object 
 *
 * Return:
 *	  None
 */
EXPORT void stpool_detach_task(HPOOL hp, struct sttask_t *ptsk);

/* @stpool_throttle_enable
 *	    The pool has a throttle, if the throttle's switcher is turned on,
 *	tasks can not be added into the pool until the user calls @stpool_th
 *	-rottle_enable(hp, 0) to turn it off. 
 *
 * Arguments:
 *    @hp     [in]  the pool handle 
 *
 *    @enable [in]  if enable is 1, the throttle's switcher will be turned 
 *    				on, or the throttle's switcher will be turned off.
 * Return:
 *	  None
*/
EXPORT void stpool_throttle_enable(HPOOL hp, int enable);

/* @stpool_wkid
 *	   User can save the wkid before his calling the wait functions such as 
 *@stpool_throttle_wait, @stpool_wait(1/2/3/ex) and @stpool_pending_leq_wait.
 *and then call stpool_wakeup(wkid) to wake up these wait functions. Actually, 
 *the wkid returned by @stpool_wkid is the thread id.
 *	
 *	  model:
 *	        thread1:                                  thread2:
 *	           wkid = stpool_wkid(); 
 *	           stpool_task_wait(...)   wake up
 *	           	                    <-----------   stpool_wakeup(wkid)
 *
 * Arguments:
 *     None
 *
 * Return:
 *	  The id used to wake up the wait functions
 */
EXPORT long stpool_wkid();

/* @stpool_throttle_wait
 *		If the throttle's switcher is on, the call will not return
 * util some users call @stpool_throttle_enable(hp, 0) to turn it 
 * off.
 *    
 * Arguments:
 *    @hp     [in]  the pool handle 
 *
 *    @ms     [in]  milliseconds that the user wants to wait on the
 *                  throttle's switcher.<@ms = -1 = INFINITE>
 *                    
 * Return:
 *	 On success, it returns 0.  On timeout, 1 is returned.
 *	 
 *	 NOTE: If @stpool_release is called by user and the reference of 
 *	       the pool is zero, as a result, the pool will be destroyed, 
 *	       If the throttle's switcher is on in this case, 2 will be 
 *	       returned.
 */
EXPORT int stpool_throttle_wait(HPOOL hp, long ms);

/* @stpool_wait(1/2/3/ex)
 *	  Wait for the tasks' being done in @ms milliseconds. the functions
 * of @stpool_waitex are just the same as @stpool_wait's. and as you see,
 * @stpool_waitex use the callback @sttask_match to find the task. if there 
 * are tasks who matches the condition, @stpool_waitex will not return.
 *
 * Arguments:
 *    @hp           [in]  the pool handle 
 *
 *    @tsk          [in]  If tsk is not NULL, @stpool_wait will not return
 *                        until all tasks whose address is equal to the tsk
 *                        have been done. or @stpool_wait will wait for all
 *                        tasks's quiting.
 *
 *    @sttask_match [in]  a callback to match the task, if it return non zero
 *                        value, @stpool_waitex will wait on the task that 
 *                        passed to it.
 *
 *    @ms           [in]  -1 == INFINITE                   
 *                    
 * Return:
 *		On success, it returns 0.  
 *		On timeout, 1 is returned, 
 *		(If it is woke up by @stpool_wakeup , it returns -1)
 */
EXPORT int  stpool_task_wait(HPOOL hp, struct sttask_t *tsk, long ms);
EXPORT int  stpool_task_wait2(HPOOL hp, struct sttask_t *entry, int n, long ms);
EXPORT int  stpool_task_wait3(HPOOL hp, struct sttask_t *entry, int *n, long ms);
EXPORT int  stpool_task_waitex(HPOOL hp, int (*sttask_match)(struct stpool_tskstat_t *stat, void *arg), void *arg, long ms); 
EXPORT int  stpool_pending_leq_wait(HPOOL hp, int n_max_pendings, long ms);

/* @stpool_wakeup
 *		 Wake up the wait functions such as @stpool_throttle_wait,
 * @stpool_task_wait, @stpool_task_wait2, @stpool_task_wait3 and 
 * so on.
 * 
 * Arguments:
 *    @hp          [in]  the pool handle 
 *
 *    @wkid        [in]  the wkid returned by stpool_wkid(), stpool_wakeup(-1)
 *                       will wake up all of the wait functions
 * Return:
 *	  None
 */
EXPORT void stpool_wakeup(HPOOL hp, long wkid);

#endif
