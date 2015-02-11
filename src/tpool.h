#ifndef __TASK_POOL_H__
#define __TASK_POOL_H__
/* piggy_xrh@163.com */

#include "tpool_struct.h"
enum {	
	/* System is out of memeory 
	 *  
	 * (@tpool_add_task(2)/@tpool_add_pri_task(2) may return
	 *  the error code)
	 */
	POOL_ERR_NOMEM = 1,
	
	/* Task pool is beging destroyed 
	 *  
	 * (It indicates that @tpool_release has been called and 
	 *  the reference of the pool is zero)
	 */
	POOL_ERR_DESTROYING = 2, 
	
	/* Task pool has been destroyed or has not been created */
	POOL_ERR_NOCREATED  = 3,

	/* The throttle of the pool is disabled 
     * 
	 * (It indicates that user has called @tpool_throttle_enable(hp, 0)
	 *  to turn the throttle swither on)
	 */
	POOL_ERR_THROTTLE = 4,

	/* The task has been removed by user.
	 *
	 * (It indicates that the task has been removed by @tpool_remove_pending_task(2) 
	 *  or @tpool_mark_task(2))
	 */
	POOL_TASK_ERR_REMOVED = 5,
					
	/* The task can not be rescheduled.
	 * 
	 * (It indicates that the task has been marked with TASK_VMARK_DISABLE_RESCHEDULE,
	 *  But the code returned by @task.task_complete is not zero).
	 */
	POOL_TASK_ERR_DISABLE_RESCHEDULE = 6,
	
	POOL_TASK_ERR_FILTER_DISCARD = 7,

	POOL_TASK_ERR_FILTER_WAIT = 8,
	
	/* The errno has been set */
	POOL_ERR_ERRNO = 0x1000,
};

/* Flags of the task */
enum {
	/* @task_run has been executed */
	TASK_VMARK_DONE = 0x0001,

	/* The task is not allowed to be rescheduled */
	TASK_VMARK_DISABLE_RESCHEDULE = 0x0002,
	
	/* The task is removed by @tpool_remove_pending_task/@tpool_mark_task 
	 *    The user can mark tasks with TASK_VMARK_REMOVE_BYPOOL or 
	 * TASK_VMARK_REMOVE_DIRECTLY, and as a result, the tasks will be removed from 
	 * the pending queue.
     *    If task is marked with TASK_VMARK_REMOVE_BYPOOL, @task_complete 
	 * will be called by the pool. or @task_complete will be called by the 
	 * functions who marks the task. 
	 */

	TASK_VMARK_REMOVE_BYPOOL = 0x0004,
	TASK_VMARK_REMOVE_DIRECTLY = 0x0008,
	
	TASK_VMARK_REMOVE = TASK_VMARK_REMOVE_BYPOOL|TASK_VMARK_REMOVE_DIRECTLY,

	/* The pool is being destroyed */
	TASK_VMARK_POOL_DESTROYING = 0x0010,	
};

/* Priority attribute of the task */
struct priority_t {
	/* Priority of the task [0~99] */
	int pri;

	/* Priority policy of the task (STPOOL_POLICY_PRI_XX) */
	int pri_policy;
};

struct task_t {
	/* A const string to describle the task */
	const char *task_name;
	
	/* @task_run will be called when the task is scheduled by the pool. 
     *  user can do their works in this function.
	 */
	int  (*task_run)(struct task_t *tsk);	

	/*  If @task_complete is not NULL, it will be called when one of the conditions below matches.
	 *       1. @task_run has been executed by the pool.
	 *       2. The task is removed from the pool by @tpool_remove_pending_task
	 *          or @tpool_mark_task
	 *
	 *   NOTE:
	 *	  1). If @task_run has been excuted by the pool, the argument @vmflags will
	 * owns the mask TASK_VMARK_DONE, and the @task_code will be set to the value
	 * returned by @task_run. or the the @task_code will be set properly. 
	      (@see the error codes POOL_XXX describled above)
	 *
	 *    2). If @task_complete returns non-zero value and the task has not been marked with
	 * TASK_VMARK_DISABLE_RESCHEDULE, The task will be added into the pool again automatically.
	 *
	 *    3). If the task is added by @tpool_add_pri_task(2), The task's priority @pri 
	 * will be passed to @task_complete, user can change the task's priority attribute 
	 * to determine how to reschedule the task, and in this case, he should return 2 to 
	 * tell the pool that the the task's priority has been changed by user. 
	 */
	int  (*task_complete)(struct task_t *tsk, long vmflags, int task_code, struct priority_t *pri);

	/* The argument reserved for task */
	void *task_arg;
};

/*@tpool_create
 *     Create a thread pool to running tasks. user should call
 * @tpool_release to free the pool if he does not need it any
 * more.
 * 
 * Arguments:
 *    @pool       [in]  the pool object
 *    @q_ri       [in]  the number of the priority queues that the user wants to create.
 *                      a priority task added by @tpool_add_pri_task(2) will be inserted
 *                      into a propriate priority queue, and the tasks who has the higher 
 *                      priority will be scheduled prior to the tasks who has a lower 
 *                      priority. 
 *    @maxthreads [in]  the limited number of threads that is created 
 *    				    by pool to provide services.
 *
 *    @minthreads [in]  the min number of threads that should be reserved 
 *    	                to wait for tasks.
 *
 *    @suspend    [in]  if @suspend is 1, the pool will not schedule any 
 *                      tasks that have been added into the pool untill the 
 *                      user calls @tpool_resume to wake up it.
 *
 * Return:
 *		On success, @tpool_create returns 0.  On error, error code will be returned, 
 */
int  tpool_create(struct tpool_t *pool, int q_pri, int maxthreads, int minthreads, int suspend);

/* @tpool_use_mpool.
 *      mpool can improve our perfermance. it must be called before @tpool_add_(pri_)task(2)
 *
 *  Arguments:
 *      @pool     [in]  the pool object
 *
 *  Return:
 *       None
 */
void tpool_use_mpool(struct tpool_t *pool);

/* @tpool_loadenv_
 * 	 Load the config from the env
 */
void tpool_load_env(struct tpool_t *pool);

/* @tpool_atexit
 *    User can set a exit function for the pool, when the pool is destroyed completely
 * it will be called. 
 *   
 *   Arguments:
 *      @pool        [in]  the pool object
 *      @atexit_func [in]  the exit function that the user wants to set
 *      @arg         [in]  argument of the @atexit_func
 *
 *    Return:
 *      None
 */
void tpool_atexit(struct tpool_t *pool, void (*atexit_func)(struct tpool_t *pool, void *arg), void *arg);

/* @tpool_addref
 *      Increase the reference of the pool. and the reference is 1 
 * after calling @tpool_create. When the reference of the pool is 
 * zero, the pool will be destroyed automatically.
 *
 * Arguments:
 *    @pool     [in]  the pool object
 *
 * Return:
 *	  the current reference of the pool.
 */
long tpool_addref(struct tpool_t *pool);

/* @tpool_release
 *     Decrease the reference of the pool. 
 * <see @tpool_addref for more details about the reference>
 *
 * Arguments:
 *    @pool       [in]  the pool object
 *    @clean_wait [in]  whether the user wants to wait for the pool's being destroyed 
 *                      completely when it's user_ref is zero.
 * Return:
 *	  the current reference of the pool.
*/
long tpool_release(struct tpool_t *pool, int clean_wait);

/* @tpool_set_activetimeo
 *     Set the timeout value of the servering threads belong to the pool, 
 * when tasks are added into the pool and the pool has not been marked sus
 * -pended, the pool will create service threads imediately to excute the 
 * tasks. and the threads will quit automatically if they have not gotton 
 * any tasks in (@acttimeo + rand() % 60) seconds. the default value of 
 * @actitimeo is 20.
 * *
 * Arguments:
 *    @pool     [in]  the pool object
 *    @acttimeo [in]  seconds for waitting for tasks
 *
 * Return:
 *	  None
*/
void tpool_set_activetimeo(struct tpool_t *pool, long acttimeo);

/* @tpool_adjust_abs
 *     Adjust the threads number of the pool. this function does not
 * block, the pool will record the param and adjust the pool as soon
 * as possible in the background.
 *
 * Arguments:
 *    @pool        [in]  the pool object
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
void tpool_adjust_abs(struct tpool_t *pool, int maxthreads, int minthreads);

/* @tpool_adjust
 *   @tpool_adjust is similar to @tpool_adjust_abs, the only difference between
 * them is that the paraments received by @tpool_adjust are relative.
 *
 *    tpool_adjust(pool, 1, 2)  <==> tpool_adjust_abs(pool, pool->maxthreads + 1, pool->minthreads + 2)
 *    tpool_adjust(pool, 2, -1) <==> tpool_adjust_abs(pool, pool->maxthreads + 2, pool->minthreads - 1)
 */
void tpool_adjust(struct tpool_t *pool, int maxthreads, int minthreads);

/*@tpool_flush
 *     Mark all unused threads died. it's not recommented to do this since
 * the pool know when to shutdown the threads.
 * 	
 * 	Arguments:
 *    @pool  [in]  the pool object 
 *
 * Return:
 * 	   The number of threads who is marked died by @tpool_flush
 *
 * NOTE:
 * 	   This function does not block, user can call @tpool_adjust_wait to
 * wait for all died threads' exits.
 */
int  tpool_flush(struct tpool_t *pool);

/* @tpool_adjust
 *    @tpool_adjust and @tpool_adjust_abs do not block, If users want 
 * to make sure that the pool's status is consistent with the param they
 * have set, some threads may have been markeddied. and the threads who 
 * has been marked died will exit imediately after having done their 
 * current tasks. 
 *    @tpool_adjust will not return until there are none servering
 * threads marked died in the pool.
 *
 * Arguments:
 *    @pool     [in]  the pool object
 *
 * Return:
 * 	   Non
*/
void tpool_adjust_wait(struct tpool_t *pool);


/* @tpool_getstat
 *     Get the status of the pool
 *
 * Arguments:
 *    @pool     [in]     the pool object
 *
 *    @stat     [in/out] the status structure.
 *
 * Return:
 *	  The argument @stat filled up with the pool's status will be returned.
 */
struct tpool_stat_t *tpool_getstat(struct tpool_t *pool, struct tpool_stat_t *stat);

/* @tpool_status_print
 *     Print the status of the pool to the buffer.
 *
 * Arguments:
 *    @pool      [in]  the pool object
 *
 *    @buffer    [out] the buffer into where the status data will be flushed,
 *                     If it is NULL, the inner static buffer will be used.
 *
 *    @bufferlen [in]  the length of the buffer.
 *
 * Return:
 *	  If the argument @buffer is not NULL, @buffer filled up with the pool's status
 * will be returned. or the inner static buffer will be passed to user.
*/
const char *tpool_status_print(struct tpool_t *pool, char *buffer, size_t bufferlen);

/*@tpool_gettskstat
 *     Get the status of the task.
 *
 * Arguments:
 *    @pool     [in]      the pool object
 *
 *    @st       [in/out]  the status that the user wants to have a look.
 *                        <NOTE: st->task should be set to the address of the
 *                               task that the user interests>
 * Return:
 * 	  If @st->task does not exist in the pool, the function will return 0, or 
 * @st->stat will be returned. 
*/
long tpool_gettskstat(struct tpool_t *pool, struct tpool_tskstat_t *st);

/* @tpool_mark_task 
 *	    Walk the status of the tasks existing in the pool. user can mark the task with
 * masks listed below.
 *
 *      .TASK_VMARK_REMOVE
 *            If the task is in the pending queue, the task will be removed and the task's
 *         vmflags will be set, But if the task is being scheduled or is being dispatched, 
 *         it has no effect and the task's vmflags will not be set. as you see, its functions 
 *         are just the same as the @tpool_remove_pending_task(pool, tsk, 0)'s.
 *	    
 *	    .MASK_VMARK_REMOVE_BYPOOL
 *			  The diference between TASK_VMARK_REMOVE is that the completion routines of the 
 *		  tasks marked with TASK_VMARK_REMOVE will be called by @tpool_mark_task itself, but
 *		  if the tasks are marked with MASK_VMARK_REMOVE_BYPOOL, the tasks will be removed
 *		  from the pending queue and the pool is responsible for calling their completions.
 *
 *      .TASK_VMARK_DISABLE_RESCHEDULE
 *            If the task's vmflags owns this mask, the task will not be rescheduled even
 *         if the code returned by @task_complete is not zero. But user can call functions
 *         such as @tpool_add_task, @tpool_add_routine, @tpool_add_pri_task, @tpool_add_pri_routine
 *         explicitly to reschedule it.
 *
 *   NOTE:
 *   	 Only tasks who is in the pending queue can be marked with TASK_VMARK_REMOVE or
 *   MASK_VMARK_REMOVE_BYPOOL. it means that if the condition #1 is true, the pool will 
 *   prevent the task freom being marked with TASK_VMARK_REMOVE or TASK_VMARK_REMOVE_BYPOOL,
 *
 *  #1:
 *   	((TASK_F_SCHEDULING|TASK_F_DISPATCHING) & @task_stat)
 *
 * Arguments:
 *    @pool     	 [in]  the pool object 
 
 *    @tsk      	 [in]  if tsk is not NULL, @tpool_mark_task will only pass the tasks
 *                    	   whose address is equal to tsk to the tskstat_walk.
 
 *    @tskstat_walk  [in]  the callback that will be called by @tpool_mark_task to visit
 *                         the status of tasks exsiting in the pool.
 * Return:
 *	  The number of tasks processed by tskstat_walk.
*/
int  tpool_mark_task(struct tpool_t *pool, struct task_t *tsk,
					 int (*tskstat_walk)(struct tpool_tskstat_t *stat, void *arg),
					 void *arg);

/* @tpool_disable_rescheduling
 *	    Mark the task with TASK_VMARK_DISABLE_RESCHEDULE. see @tpool_mark_task for more
 * details about TASK_VMARK_DISABLE_RESCHEDULE.
 *
 * Arguments:
 *    @pool     [in]  the pool object 
 *
 *    @tsk      [in]  if tsk is not NULL, all tasks existing in the pool will be marked
 *                    with TASK_VMARK_DISABLE_RESCHEDULE.
 *
 * Return:
 *	  The number of tasks who has been marked.
 */
int tpool_enable_rescheduling_task(struct tpool_t *pool, struct task_t *tsk, int enable); 


/* @tpool_throttle_enable
 *	    The pool has a throttle, if the throttle's switcher is turned on,
 *	tasks can not be added into the pool until the user calls @tpool_throttle_enable(pool, 0)
 *	to turn it off. 
 *
 * Arguments:
 *    @pool     [in]  the pool object 
 
 *    @enable   [in]  if enable is 1, the throttle's switcher will be turned on,
 *                    or the throttle's switcher will be turned off.
 * Return:
 *	  None
*/
void tpool_throttle_enable(struct tpool_t *pool, int enable);

/* @tpool_throttle_enabled_wait
 *		If the throttle's switcher is on, the call will not return
 * util some users call @tpool_throttle_enable(pool, 0) to turn it off.
 *    
 * Arguments:
 *    @pool   [in]  the pool object 
 
 *    @ms     [in]  milliseconds that the user wants to wait on the
 *                    throttle's switcher.
 *                    
 * Return:
 *	 On success, it returns 0.  On timeout, 1 is returned, 
*/
int  tpool_throttle_disabled_wait(struct tpool_t *pool, long ms);

/* @tpool_suspend
 *	    If we suspend the pool, the pool will go to sleep, and all the 
 *	tasks that is not being scheduled will not be excuted any way until 
 *	the user calls @tpool_resume to wake up the pool.
 *
 * Arguments:
 *    @pool     [in]  the pool object 
 
 *    @wait     [in]  If @wait is 1, it indicated that the user want to wait
 *                    for all the scheduling tasks' completions. 
 *                    (Some tasks maybe is being scheduled while user calls
 *                    @stpool_suspend, the pool give users a choices to determine 
 *                    whether the @stpool_suspend wait for the scheduling tasks' 
 *                    completions)
 * Return:
 *	  None
*/
void tpool_suspend(struct tpool_t *pool, int wait);

/* @tpool_resume
 *	     Wake up the pool to schedule the tasks again.
 *
 * Arguments:
 *    @pool     [in]  the pool object 
 *
 * Return:
 *	  None
*/
void tpool_resume(struct tpool_t *poo);

/* @tpool_add_task
 *	     Add a task into the pool
 *
 * Arguments:
 *    @pool     [in]  the pool object 
 *
 *	  @tsk      [in]  task that will be added into the pool
 *
 * Return:
 *		On success, it returns 0.  On error, error code is returned, 
*/
int  tpool_add_task(struct tpool_t *pool, struct task_t *tsk);

/* @tpool_add_routine
 *	     Add a routine into the pool
 *
 * Arguments:
 *    @pool          [in]  the pool object 
 *
 *	  @task_run      [in]  the routine that the user wants to execute
 *
 *	  @task_complete [in]  the task complete routine that the pool will 
 *	  					   call after having finished excuting @task_run, 
 *	  					   it can be NULL.
 *	  @arg 
 * Return:
 *		On success, it returns 0.  On error, error code is returned.
*/
int  tpool_add_routine(struct tpool_t *pool, 
					int (*task_run)(void *arg), 
					int (*task_complete)(long vmflags, int task_code, void *arg, struct priority_t *pri),
					void *arg);

/* @tpool_add_pri_task(2)  
 *     User can use @tpool_add_task to add the task with priority into the pool, 
 * the pool sort the tasks according the priority, the higher priority tasks will 
 * be scheduled prior to tasks who has the lower priority.  
 *
 *  Arguments:
 *    @pri         [in] The range of the priority is from 0 to 99.
 *    @pri_policy  [in] The policy to sort the tasks. (see TPOOL_POLICY_XX)
 *
 * Return:
 *		On success, it returns 0.  On error, error code is returned.
 */
int  tpool_add_pri_task(struct tpool_t *pool, struct task_t *tsk, int pri, int pri_policy);
int  tpool_add_pri_routine(struct tpool_t *pool, 
					int (*task_run)(void *arg), 
					int (*task_complete)(long vmflags, int task_code, void *arg, struct priority_t *pri),
					void *arg,
					int pri, int pri_policy);

void tpool_extract(struct task_t *task, void **task_run, void **task_complete, void **arg); 
/* @tpool_remove_pending_task
 *	   Remove tasks that existing in the pending queue. 
 *
 * Arguments:
 *    @pool               [in]  the pool object 
 *
 *    @tsk                [in]  If tsk is not NULL, @tpool_remove_pending_task will only 
 *    							remove tasks whose address is equal to tsk. or all the 
 *    							tasks existing in the pending queue will be removed.
 *
 *    @dispatched_by_pool [in] If dispatched_by_pool is 1, the pool will be responsible for
 *                             calling the tasks' @task_complete, and the vmflags passed to 
 *                             @task_complete will be marked with TASK_VMARK_REMOVE_BYPOOL. 
 *                             or @tpool_remove_pending_task itself will call the task'
 *                             @task_complete, and the vmflags passed to @task_complete will
 *                             be marked with TASK_MARK_REMOVE.
 * Return:
 *      The number of tasks that have been removed.
*/
int  tpool_remove_pending_task(struct tpool_t *pool, struct task_t *tsk);
int  tpool_remove_pending_task2(struct tpool_t *pool, struct task_t *tsk);

/* @tpool_wait(ex)
 *	  Wait for the tasks' being done in @ms milliseconds. the functions
 * of @tpool_waitex are just the same as @tpool_wait's . and as you see,
 * @tpool_waitex use the callback @ttask_match to find the task. if there 
 * are tasks who matches the condition, @tpool_waitex will not return.
 * *
 * Arguments:
 *    @pool     [in]  the pool object 
 
 *    @tsk      [in]  If tsk is not NULL, @tpool_wait will not return
 *                    until all tasks whose address is equal to the tsk
 *                    have been done. or @tpool_wait will wait for all
 *                    tasks's quiting.
 *    @ms       [in]  -1 == INFINITE                   
 *                    
 * Return:
 *		On success, it returns 0.  On timeout, 1 is returned, 
*/
int  tpool_wait(struct tpool_t *pool, struct task_t *tsk, long ms);
int  tpool_waitex(struct tpool_t *pool, int (*task_match)(struct tpool_tskstat_t *stat, void *arg), void *arg, long ms); 


/* extensions */
struct event_t *tpool_event_get(struct tpool_t *pool, struct event_t *ev);
void tpool_event_set(struct tpool_t *pool, struct event_t *ev);
int  tpool_event_wait(struct tpool_t *pool, long ms);
void tpool_event_pulse(struct tpool_t *pool);
#endif
