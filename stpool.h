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

/* Error code */
enum {	
	/* System is out of memeory 
	 *  
	 * (@stpool_add_task/@stpool_add_pri_task/@stpool_add_routine/
	 *  @stpool_add_pri_routine may return the error code)
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
	 * (It indicates that the task has been removed by @stpool_remove_pending_task(2) 
	 *  or @stpool_mark_task(2))
	 */
	STPOOL_TASK_ERR_REMOVED = 5,				
	
	/* The task can not be rescheduled.
	 * 
	 * (It indicates that the task has been marked with TASK_VMARK_DISABLE_RESCHEDULE,
	 *  But the code returned by @task.task_complete is not zero).
	 */
	STPOOL_TASK_ERR_DISABLE_RESCHEDULE = 6,
	
	/* The task has been discarded by the filter 
	 *
	 * (see @stpool_event_set for more details) 
	 */
	STPOOL_TASK_ERR_FILTER_DISCARD = 7,
	
	/* The task has been rejected by the filter since the pool is busy now, user can 
	 * call @stpool_event_wait(hp, ms) to wait on the pool.
	 *
	 * (see @stpool_event_set for more details) 
	 */
	STPOOL_TASK_ERR_FILTER_WAIT = 8,
};

/* Priority attribute of the task */
struct stpriority_t {
	/* Priority of the task [0~99] */
	int pri;

	/* Priority policy of the task (STPOOL_POLICY_PRI_XX) */
	int pri_policy;
};

struct sttask_t {
	/* A const string to describle the task */
	const char *task_name;
	
	/* @task_run will be called when the task is scheduled by the pool. 
     *  user can do their works in this function.
	 */
	int  (*task_run)(struct sttask_t *tsk);	
	
	 /*  If @task_complete is not NULL, it will be called when one of the conditions below matches.
	 *       1. @task_run has been executed by the pool.
	 *       2. The task is removed from the pool by @stpool_remove_pending_task
	 *          or @stpool_mark_task
	 *
	 *   NOTE:
	 *	  1). If @task_run has been excuted by the pool, the argument @vmflags will
	 * owns the mask STTASK_VMARK_DONE, and the @task_code will be set to the value
	 * returned by @task_run. or the the @task_code will be set properly. 
	      (@see the error codes STPOOL_XXX describled above)
	 *
	 *    2). If @task_complete returns non-zero value and the task has not been marked with
	 * STTASK_VMARK_DISABLE_RESCHEDULE, The task will be added into the pool again automatically.
	 *
	 *    3). If the task is added by @stpool_add_pri_task/@stpool_add_pri_routine, The task's 
	 * priority @pri  will be passed to @task_complete, user can change the task's priority 
	 * attribute to determine how to reschedule the task, and in this case, he should return 2
	 * to tell the pool that the the task's priority has been changed by user. 
	 */
	int  (*task_complete)(struct sttask_t *tsk, long vmflags, int task_code, struct stpriority_t *pri);

	/* The argument reserved for task */
	void *task_arg;
};

/* Status of the task */
enum {
	/* Task is waiting for being scheduled */
	STTASK_F_WAIT  = (short)0x01,
	
	/* Task is being scheduled */
	STTASK_F_SCHEDULING  = (short)0x02,
	
	/* Task has been swaped from the pending queue since
	 * the pool has been marked suspended 
	 */
	STTASK_F_SWAPED = (short)0x04,
	
	/* The pool is going to call @task_complete to give user a 
	 * notification or @task_complete is being called.
	 */
	STTASK_F_DISPATCHING = (short)0x08,
};

/* Flags of the task */
enum {
	/* @task_run has been executed */
	STTASK_VMARK_DONE = 0x0001,
	
	/* The task is not allowed to be rescheduled */
	STTASK_VMARK_DISABLE_RESCHEDULE = 0x0002,
	
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

typedef void *HPOOL;

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
 *    				    to create. a priority task added by @stpool_add_pri_task 
 *    				    or @stpool_add_pri_routine will be inserted into a propriate 
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
 * STTASK_VMARK_POOL_DESTROYING, see the details below about the pool
 * how to process the tasks in this case.
 *	
 *     If the pool is not suspended, @stpool_release will return imediatly
 * and all tasks will be done in the background, but users can not call any
 * APIs to operate the pool any more and the tasks can not be rescheduled
 * after having done their work even if they have not been marked with
 * STMASK_VMARK_DISABLE_RESCHEDULE.
 *     Or @stpool_release will return imediatly and all tasks will be removed 
 * and dispatched by the pool in the background.
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
 *                   whether the @stpool_suspend wait for the scheduling tasks' 
 *                   completions)
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
 
 *	  @tsk      [in]  task that will be added into the pool
 *
 * Return:
 *		On success, it returns 0. On error, error code is returned, 
*/
EXPORT int  stpool_add_task(HPOOL hp, struct sttask_t *tsk);

/* @stpool_add_routine
 *	     Add a routine into the pool
 *
 * Arguments:
 *    @hp            [in]  the pool handle 
 
 *	  @task_run      [in]  the routine that the user wants to execute
 
 *	  @task_complete [in]  the task complete routine that the pool will 
 *	  					   call after having finished excuting @task_run, 
 *	  					   (it can be NULL)
 *	  @arg 
 * Return:
 *		On success, it returns 0.  On error, error code is returned, 
*/
EXPORT int  stpool_add_routine(HPOOL hp, 
					int (*task_run)(void *arg), 
					int (*task_complete)(long vmflags, int task_code, void *arg, struct stpriority_t *pri),
					void *arg);

/* @stpool_add_pri_task  
 *     User can use @stpool_add_pri_task to add the task with priority into the pool, 
 * the pool will sort the task according to its priority, and the higher priority tasks 
 * will be scheduled prior to the tasks who has a lower priority.  
 *
 *  Arguments:
 *    @hp          [in] the pool handle
 *    @tsk         [in] see @stpool_add_task
 *    @pri         [in] The range of the priority is from 0 to 99.
 *    @pri_policy  [in] The policy to sort the tasks. (see STPOOL_POLICY_XX)
 *
 * Return:
 *		On success, it returns 0.  On error, error code is returned.
 */
EXPORT int  stpool_add_pri_task(HPOOL hp, struct sttask_t *tsk, int pri, int pri_policy);

/* @stpool_add_pri_routine
 *     User can call @stpool_add_pri_routine to add the routine with priority into the pool.
 * the pool sorts the tasks and routines according to their priority, and the higher priority 
 * tasks(routines) will be scheduled prior to the tasks(routines) who has a lower priority.  
 *
 *  Arguments:
 *    @hp            [in] the pool handle
 *    @task_run      [in] See @stpool_add_routine
 *    @task_complete [in] See @stpool_add_routine
 *    @arg           [in] See @stpool_add_routine
 *    @pri           [in] See @stpool_add_pri_task
 *    @pri_policy    [in] See @stpool_add_pri_task
 *
 * Return:
 *		On success, it returns 0.  On error, error code is returned.
 */
EXPORT int  stpool_add_pri_routine(HPOOL hp, 
					int (*task_run)(void *arg), 
					int (*task_complete)(long vmflags, int task_code, void *arg, struct stpriority_t *pri),
					void *arg, int pri, int pri_policy);


/* @stpool_extract
 *    Extract routine's information from the task, If user calls @stpool_add_routine or
 * @stpool_add_pri_routine to add a routine into the pool, the pool will convert the 
 * routine into a anonymous task firstly, and then deilver the task into the pool.
 *
 *    User can call @stpool_extract to extract the routine's information from the task
 * passed to @tskstat_walk(see @stpool_mark_task) or @sttask_match(see @stpool_waitex).
 *
 *   Arguments:
 *   	@task          [in]   the task object passed by @tskstat_walk or @sttask_match
 *
 *   	@task_run      [out]  if param @task_run is not NULL, it will be filled up with the
 *   	                      working routine. (see @stpool_add_routine/@stpool_add_pri_routine)
 *
 *   	@task_complete [out]  if param @task_complete is not NULL, it will be filled up with 
 *   	                      the completion routine. (see @stpool_add_routine/@stpool_add_pri_routine)
 *      
 *      @arg           [out]  if param @arg is not NULL, it will be filled up with the 
 *                            routine's argument (see @stpool_add_routine/@stpool_add_pri_routine)
 *
 *   Return:
 *   		None
 */
EXPORT void stpool_extract(struct sttask_t *task, void **task_run, void **task_complete, void **arg);

/* @stpool_remove_pending_task
 *	   Remove tasks that existing in the pending queue. 
 *
 * Arguments:
 *    @hp                 [in]  the pool handle
 *
 *    @tsk                [in]  If tsk is not NULL, @stpool_remove_pending_task
 *                              will only remove tasks whose address is equal to
 *                              tsk. or all the tasks existing in the pending queue
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

/* @stpool_disable_rescheduling
 *	    Mark the task with STTASK_VMARK_DISABLE_RESCHEDULE. see @stpool_mark_task for more
 * details about STTASK_VMARK_DISABLE_RESCHEDULE.
 *
 * Arguments:
 *    @hp       [in]  the pool handle 
 *
 *    @tsk      [in]  if tsk is not NULL, all tasks existing in the pool will be marked
 *                    with STTASK_VMARK_DISABLE_RESCHEDULE.
 *
 * Return:
 *	  The number of tasks who has been marked.
 */

EXPORT int stpool_disable_rescheduling(HPOOL hp, struct sttask_t *tsk); 

/* @stpool_mark_task 
 *	    Walk the status of the tasks existing in the pool. user can mark the task with
 * masks listed below.
 *
 *      .STTASK_VMARK_REMOVE
 *            If the task is in the pending queue, the task will be removed and the task's
 *         vmflags will be set, But if the task is being scheduled or is being dispatched, 
 *         it has no effect and the task's vmflags will not be set. as you see, its functions 
 *         are just the same as the @stpool_remove_pending_task(hp, tsk, 0)'s.
 *	    
 *	    .STMASK_VMARK_REMOVE_BYPOOL
 *			  The diference between STTASK_VMARK_REMOVE is that the completion routines of the 
 *		  tasks marked with STTASK_VMARK_REMOVE will be called by @stpool_mark_task itself, but
 *		  if the tasks are marked with STMASK_VMARK_REMOVE_BYPOOL, the tasks will be removed
 *		  from the pending queue and the pool is responsible for calling their completions.
 *
 *      .STTASK_VMARK_DISABLE_RESCHEDULE
 *            If the task's vmflags owns this mask, the task will not be rescheduled even
 *         if the code returned by @task_complete is not zero. But user can call functions
 *         such as @stpool_add_task, @stpool_add_pri_task, @stpool_add_pri_routine, and 
 *         @stpool_add_pri_routine explicitly to reschedule it.
 *
 *   NOTE:
 *   	 Only tasks who is in the pending queue can be marked with STTASK_VMARK_REMOVE or
 *   STMASK_VMARK_REMOVE_BYPOOL. it means that if the condition #1 is true, the pool will 
 *   prevent the task from being marked with STTASK_VMARK_REMOVE or STTASK_VMARK_REMOVE_BYPOOL.
 *    
 *  #1:
 *   	((STTASK_F_SCHEDULING|STTASK_F_DISPATCHING) & @task_stat)
 *
 * Arguments:
 *    @hp            [in]  the pool handle 
 
 *    @tsk      	 [in]  if tsk is not NULL, @stpool_mark_task will only pass the tasks
 *                    	   whose address is equal to tsk to the @tskstat_walk.
 *
 *    @tskstat_walk  [in]  a callback that will be called by @stpool_mark_task to visit
 *                         the status of tasks exsiting in the pool.
 * Return:
 *	  The number of tasks processed by @tskstat_walk.
*/
EXPORT int  stpool_mark_task(HPOOL hp, struct sttask_t *tsk,
					 int (*tskstat_walk)(struct stpool_tskstat_t *stat, void *arg),
					 void *arg);

/* @stpool_throttle_enable
 *	    The pool has a throttle, if the throttle's switcher is turned on,
 *	tasks can not be added into the pool until the user calls @stpool_th
 *	-rottle_enable(hp, 0) to turn it off. 
 *
 * Arguments:
 *    @hp     [in]  the pool handle 
 
 *    @enable [in]  if enable is 1, the throttle's switcher will be turned 
 *    				on, or the throttle's switcher will be turned off.
 * Return:
 *	  None
*/
EXPORT void stpool_throttle_enable(HPOOL hp, int enable);

/* @stpool_throttle_disabled_wait
 *		If the throttle's switcher is on, the call will not return
 * util some users call @stpool_throttle_enable(hp, 0) to turn it 
 * off.
 *    
 * Arguments:
 *    @hp     [in]  the pool handle 
 
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
 *	 
*/
EXPORT int stpool_throttle_disabled_wait(HPOOL hp, long ms);


/* @stpool_wait(ex)
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
 *		On success, it returns 0.  On timeout, 1 is returned, 
*/
EXPORT int  stpool_wait(HPOOL hp, struct sttask_t *tsk, long ms);
EXPORT int  stpool_waitex(HPOOL hp, int (*sttask_match)(struct stpool_tskstat_t *stat, void *arg), void *arg, long ms); 

/* extensions :
 *     The user can install a filter on the pool, tasks except rescheduling tasks will 
 * be processed by the filter before their being delivered into the pool.
 * 	   (see @stpool_event_xx for more details)
 */
enum {
	EV_TRIGGLE_THREADS = 1,
	EV_TRIGGLE_TASKS, 
	EV_TRIGGLE_THREADS_OR_TASKS,
	EV_TRIGGLE_THREADS_AND_TASKS,
};

enum {
	/* The task should be discarded.
	 *
	 *     STPOOL_TASK_ERR_FILTER_DISCARD will be returned by @stpool_add_xx
	 */
	EV_FILTER_DISCARD = 1,

	/* The pool is busy now, the pool itself will call @stpool_wait with @ev_param 
	 * milliseconds. the task will be added into the pool automatically after
	 * the @stpool_wait's returning.
	 */
	EV_FILTER_WAIT,

	/* The pool is busy now, the user can call @stpool_event_wait
	 * to wait on the pool.
	 *
	 *     STPOOL_TASK_ERR_FILTER_WAIT will be returned by @stpool_add_xx
	 */
	EV_FILTER_WAIT2,

	/* The task is allowed to be delivered into the pool */
	EV_FILTER_PASS,
};

#ifdef _WIN32
typedef unsigned __int32 uint32_t;
#else
#include <stdint.h>
#endif

struct stbrf_stat_t {
	/* The number of current pending task of the pool */
	int ntasks_pendings;
	
	/* The number of running threads of the pool */
	int nthreads_running;
	
	/* Is the pool busy now ? */
	int evflt_busy;
};

struct stevflt_res_t {
	/* The policy to process the task. (EV_FILTER_XX) */
	uint32_t ev_flttyp:6;
	
	/* extra result reseved for @ev_filter */
	uint32_t ev_param:26;
};

struct stevent_t {
	/* The triggle number of running threads.
	 *
	 *     if the number of current running threads of the pool is less
	 * than @ev_threads_num, @stpool_event_wait will be woke up.	
	 */ 
	int ev_threads_num;
	
	/* The triggle pending number of tasks 
	 *
	 *     if the number of current pending tasks of the pool is less
	 * than @ev_tasks_num, the @stpool_event_wait will be woke up.
	 */
	int ev_tasks_num;
	
	/* The filter type 
	 *	  EV_TRIGGLE_THREADS              @ev_threads_num will effect
	 *    EV_TRIGGLE_TASKS                @ev_tasks_num will effect
	 *    EV_TRIGGLE_THREADS_OR_TASKS     both @ev_threads_num and @ev_tasks_num 
	 *                                    will effect. it means that one of the 
	 *                                    situations below happen, @stpool_wait will
	 *                                    be woke up.
	 *                                    	1) @pool->nthreads_running < @ev_threads_num
	 *                                    	2) @pool->ntasks_pending < @ev_tasks_num
	 *
	 *    EV_TRIGGLE_THREADS_AND_TASKS    both @ev_threads_num and @ev_tasks_num will 
	 *                                    effect. it means that @stpool_wait will not 
	 *                                    be woke up until all of the situations above
	 *                                    happend.
	 */
	long ev_triggle_type;
	
	
	/* The filter callback. 
	 * Arguments that are passed to the callback.
	 *    @hp    the pool handle.
	 *    @ev    the current event param.
	 *    @brfst the brief status of the pool
	 *    @task  the delivering task.
	 *
	 * Return:
	 *    The policy to process the task.
	 */
	struct stevflt_res_t (*ev_filter)(HPOOL hp, struct stevent_t *ev, struct stbrf_stat_t *brfst, struct sttask_t *task);
	
	/* The filter argument */
	void *ev_arg;
};

/* @stpool_event_get
 *		Get the current setting of the filter.
 *    
 * Arguments:
 *    @hp     [in]  the pool handle 
 *
 *    @ev     [in]  the filter param
 *                    
 * Return:
 * 	  The effecitive setting
*/

EXPORT struct stevent_t *stpool_event_get(HPOOL hp, struct stevent_t *ev);

/* @stpool_event_set
 *		Install the filter. tasks will be processed by the filter before
 * their delivering into the pool.
 *    
 * Arguments:
 *    @hp     [in]  the pool handle 
 *
 *    @ev     [in]  the filter param
 *                    
 * Return:
 * 	  None
*/
EXPORT void stpool_event_set(HPOOL hp, struct stevent_t *ev);

/* @stpool_event_wait
 *		Wait on the filter in @ms milliseconds. If the pool is busy now, the
 * filter will prevent the task from being added into the pool imediately.
 * @stpool_event_wait will return if the pool is ready or it is woke up by
 * @stpool_event_pulse.
 *
 * Arguments:
 *    @hp     [in]  the pool handle 
 *
 *    @ms     [in]  -1 == INFINITE                   
 *                    
 * Return:
 *		On success, it returns 0.  On timeout, 1 is returned, 
 */

EXPORT int  stpool_event_wait(HPOOL hp, long ms);

/* @stpool_event_pulse
 *		Wakeup the @stpool_event_wait	
 * 
 * Arguments:
 *    @hp     [in]  the pool handle 
 *                    
 * Return:
 *	  None
 */
EXPORT void stpool_event_pulse(HPOOL hp);
#endif
