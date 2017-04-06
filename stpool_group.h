/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 *
 */

#ifndef  __STPOOL_GROUP_H__
#define  __STPOOL_GROUP_H__

#include "stpool.h"

/**
 * The scheduling attribute of the group.
 */
struct gscheduler_attr 
{
	/**
	 * The limit parralle tasks. 
	 *
	 *    Configure the group's paralle limited tasks number. it is the max limited
	 * threads number of the pool by default. the pool will save the param and configure
	 * the group properly according to the pool's limited working threads number, and the
	 * group will be re-configured according to the param saved by the pool if user calls
	 * @stpool_adjust or @stpool_adjust_abs to change the working threads number of the
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
	 * free threads existing in the pool.
	 */
	int receive_benifits;
};

/** Model
 * <pre>
 * 	     thread1    thread2     thread3     thread4    thread5      thread6            ......
 *
 * 	    ---------------------------threadManager----------------------------
 *                                     |  |
 *                                     |  |
 *                                     |  |
 *      --------------------------taskScheduler-----------------------------
 *
 *
 *          group0               group1          group2             group3            .......
 *
 *      priority queue       priority queue    priority queue     priority queue      .......
 *    -------------------   ---------------   ---------------     --------------
 *
 *
 *    1. Every group has its own priority queue. 
 *       (see @ref stpool_create for details about the priority queue)
 *
 *    2. Every task can only be added into one group of the pool at the same time.
 *
 *    3. The task should not change its group id casually if it has not been done completely.
 *
 *    4. the pool's taskScheduler will peek some tasks from the groups and delive 
 *    	 them into the threadManager to execute.
 *
 *         .Only tasks from ACTIVE groups will be peeked by the taskScheduler
 *
 *         .If there are multiple ACTIVE groups at the same time, taskScheduler will
 *          choose the best task according to the groups' configurations, the tasks'
 *          scheduling attribute (priority, policy, added_time), etc, and then pass 
 *          it to the threadManager.
 *
 *         .If the paralle tasks number of the group has arrived at the limits, the 
 *          scheduler will stop scheduling this group even if the priority of top task
 *          existing in this group's queue is hightest in the pool, in this situation, 
 *          the Scheduler will scan the other groups whose paralle tasks number has not
 *          arrived at the limits and choose a best task to execute, and if none tasks
 *          have been peeked by the scheduler, the scheduler will scan the groups again
 *          to choose the best task from the groups whose \@receive_benifits is none zero
 *         
 *         .the only duty of the threadManager is to provide the best services for
 *           the ready tasks.
 *
 *    5. the pool has a global dispatching queue, all of the removed tasks from the 
 *      groups will be pushed into it, and the pool will determine when to dispatch
 *      their completion callback. (Actually, the removed tasks are always be peeked
 *      prior to the normal tasks by the scheduler)
 *
 *    (In fact, the threadManager does not exist, I present it to users is to make
 *     the APIs of the library easier to be used by users)
 * </pre>
 */


/**
 * The status of the group.
 */
struct sttask_group_stat 
{
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
	struct gscheduler_attr attr;

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
	int throttle_enabled;

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

/*----------------APIs about the task --------------*/
/**
 * Set the task's group id
 *
 * When user calls @ref stpool_task_queue to deliver the task
 * into the pool, the pool will try to push it into the pending
 * queue of the specified group according to the task's group id.
 *
 * @note the default group id of the task is 0. the group whose
 * id is 0 is created by @ref stpool_create, it is the default
 * group of the pool, and it can not deleted by user, if you want
 * more groups, you should call @ref stpool_group_create to create
 * them.
 */
EXPORT void stpool_task_set_gid(struct sttask *ptsk, int gid);

/**
 * Get the task's group id 
 */
EXPORT int  stpool_task_gid(struct sttask *ptsk);

/**
 * Get the name of the group that the task belongs to 
 *
 * @note It may simply be implented like that:
 *	
 *	   stpool_t *pool = stpool_task_p(ptsk);
 *
 *	   return pool ? stpool_group_name2(p, stpool_task_gid(ptsk),
 *	          name_buffer, len) ? NULL;
 *
 * (see @ref stpool_group_name2 for more details)
 */
EXPORT const char *stpool_task_gname2(struct sttask *ptsk, char *name_buffer, int len);

/**
 * Get the name of the group that the task belongs to 
 */
#define stpool_task_gname(ptsk) stpool_task_gname2(ptsk, NULL, 0)

/**
 * Wait for the group's throtle in \@ms milliseconds
 *
 * @param [in] ptsk the task object
 * @param [in] ms   the timeout. (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise.
 *                                                  \n
 *        @ref POOL_TASK_ERR_DESTINATION            \n
 *        @ref POOL_ERR_GROUP_NOT_FOUND             \n
 *        @ref POOL_ERR_TIMEDOUT                    \n
 *		  @ref POOL_ERR_DESTROYING                  \n
 *		  @ref POOL_ERR_GROUP_DESTROYING            \n
 */
EXPORT int  stpool_task_gthrottle_wait(struct sttask *ptsk, long ms);

/**
 * Wait for both the pool's throtle and the group's throttle in \@ms milliseconds
 *
 * @param [in] ptsk the task object
 * @param [in] ms   the timeout. (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise.
 *                                                       \n
 *       @ref  POOL_TASK_ERR_DESTINATION                 \n
 *       @ref  POOL_ERR_GROUP_NOT_FOUND                  \n
 *       @ref  POOL_ERR_TIMEDOUT                         \n
 *		 @ref  POOL_ERR_DESTROYING                       \n
 *		 @ref  POOL_ERR_GROUP_DESTROYING                 \n
 *
 * @note  this API will not return in \@ms milliseconds until both of throttles
 *        are turned off
 */
EXPORT int  stpool_task_pgthrottle_wait(struct sttask *ptsk, long ms);

/*----------------APIs about the pool --------------*/
/**
 * Create a group on the pool
 *
 * @param [in] pool     the pool object
 * @param [in] name     a const string to descible the group
 * @param [in] attr     the scheduling attribute of the group. (Can be NULL)
 * @param [in] priq_num the priority queue number
 * @param [in] suspend  mark the group suspended or runable
 *
 * @return the group id on success, -1 otherwise
 *
 * @note If \@name[0] is equal to '?', the library will allocate a buffer and copy 
 *       the name by strcpy(allocted_buffer, name + 1), or the library just record 
 *       the address directly.
 *       
 *       0 is the id of the default group created by @ref stpool_create
 */
EXPORT int  stpool_group_create(stpool_t *pool, const char *name, struct gscheduler_attr *attr, int priq_num, int suspend);

/**
 * Get the group id according to the group name
 *
 * @param [in] pool  the pool object
 * @param [in] name  the group name
 *
 * @return the group id on finding it succefully, -1 otherwise
 */
EXPORT int  stpool_group_id(stpool_t *pool, const char *name);

/**
 * Get the description of the group according to the group id
 *
 * @param [in]     pool        the pool object
 * @param [in]     gid         the group id
 * @param [in,out] name_buffer the buffer to store the group name, if it is NULL,
 * 							   the address of group's name recorded by the pool will
 * 							   be returned directly, and the parameter \@len will be
 * 							   ignored.
 * @param [in]     len         the length of the name_buffer
 *
 * @return the group name on find it succefully, NULL otherwise
 */
EXPORT const char *stpool_group_name2(stpool_t *pool, int gid, char *name_buffer, int len);

/**
 * Get the description of the group according to the group id
 */
#define stpool_group_name(pool, id) stpool_group_name2(pool, id, NULL, 0)

/**
 * Set the scheduling attribute for the group
 *
 * @param [in] pool  the pool object
 * @param [in] gid   the group id
 * @param [in] attr  the scheduling attribute that will apply on the group
 *
 * @return none
 */
EXPORT void stpool_group_setattr(stpool_t *pool, int gid, struct gscheduler_attr *attr);

/**
 * Get the current scheduling attribute of the group
 *
 * @param [in]  pool  the pool object
 * @param [in]  gid   the group id
 * @param [out] attr  a buffer into what the group's attribute will be flushed 
 *
 * @return \@attr on success, NULL on not finding the group
 */
EXPORT struct gscheduler_attr *stpool_group_getattr(stpool_t *pool, int gid, struct gscheduler_attr *attr);

/**
 * Suspend the group
 *
 * This API will mark the group suspended, as a result, the pool will
 * stop peeking tasks from the group's pending queue. user can call
 * @ref stpool_group_resume to mark the group runable later.
 *
 * @param [in] pool  the pool object
 * @param [in] gid   the group id
 * @param [in] ms    if it is a none zero value, it tells the API that it
 *               	 should wait for the completions of all tasks who is 
 *               	 in progress (is being removed or is being scheduled) 
 *               	 and belongs to this group.
 *
 * @return on success, error code listed below otherwise
 *                                              \n
 *          @ref POOL_ERR_GROUP_NOT_FOUND       \n
 *          @ref POOL_ERR_TIMEDOUT              \n
 *          @ref POOL_ERR_NSUPPORT              \n
 */
EXPORT int stpool_group_suspend(stpool_t *pool, int gid, long ms);

/**
 * Suspend all group
 *
 * Call @ref stpool_group_suspend (pool, gid) on every group existing in the pool
 *
 * @return on success, error code listed below otherwise
 *                                              \n
 *          @ref POOL_ERR_TIMEDOUT              \n
 *          @ref POOL_ERR_NSUPPORT              \n
 *
 * @see @ref stpool_group_resume_all
 */
EXPORT int stpool_group_suspend_all(stpool_t *pool, long ms);

/**
 * Resume the group
 *
 * If the group is marked suspended, this API will reset it and mark the 
 * group runable again, and if the pool is also not suspened now, the pool
 * will try to peek tasks from the pending queue of this group to execute.
 *
 * @param [in] pool the pool object
 * @param [in] gid  the group id
 *
 * @return none
 *
 * @see @ref stpool_group_suspend
 */
EXPORT void stpool_group_resume(stpool_t *pool, int gid);

/**
 * Resume the group
 *
 * Call @ref stpool_group_resume (pool, gid) on every group existing in the pool
 */
EXPORT void stpool_group_resume_all(stpool_t *pool);

/**
 * Set the overload policy for the group
 * 
 * @param [in]  pool the pool object
 * @param [in]  gid  the group id 
 * @param [out] attr the overload policy that the user wants to apply
 *
 * @return 
 */
EXPORT int stpool_group_set_overload_attr(stpool_t *pool, int gid, struct oaattr *attr);

/**
 * Get the overload policy of the group
 * 
 * @param [in]  pool the pool object
 * @param [in]  gid  the group id 
 * @param [out] attr the current overload policy
 *
 * @return \@attr
 */
EXPORT struct oaattr *stpool_group_get_overload_attr(stpool_t *pool, int gid, struct oaattr *attr);

/**
 * Get the status of the group
 *
 * @param [in]  pool the pool object
 * @param [in]  gid  the group id
 * @param [out] stat the buffer into what the status of the group will be flushed
 *
 * @return \@stat on success, NULL on not finding the group according to the gid
 *
 * @note  If the arguments \@stat->desc is NULL or \@stat->desc_length is zero, the 
 * 		  libraray will just pass the address of group's name recorded by the pool 
 * 		  to user directly.
 */
EXPORT struct sttask_group_stat *stpool_group_stat(stpool_t *pool, int gid, struct sttask_group_stat *stat);

/**
 * Get status of all groups 
 *
 * @param [in]      pool the pool object
 * @param [in,out]  stat the address used to received the status block allocated by malloc,
 *                       user should call free to release it.
 *
 * @return the number of groups
 */
EXPORT int  stpool_group_stat_all(stpool_t *pool, struct sttask_group_stat **stat);

/**
 * Add a routine into the group's pending queue
 *
 * This API will create a dummy task object firstly, and then initialize it
 * with the parameters passed by user. when the routine is done completely,
 * the dummy task object will be destroyed by the library automatically.
 *
 * @param [in] pool             the pool object
 * @param [in] gid              the group id
 * @param [in] name             a const string to describle the routine
 * @param [in] task_run         the work routine         (Can not be NULL)
 * @param [in] task_err_handler the error routine        (Can be NULL)
 * @param [in] task_arg         the arguments            (Can be NULL)
 * @param [in] attr             the scheduling attribute (Can be NULL)
 *
 * @return 0 on success, error code listed below otherwise
 *                                                  \n
 *         @ref POOL_ERR_NOMEM                      \n
 *         @ref POOL_ERR_GROUP_NOT_FOUND            \n
 *         @ref POOL_ERR_GROUP_THROTTLE             \n
 *         @ref POOL_ERR_THROTTLE                   \n
 *         @ref POOL_ERR_DESTROYING                 \n
 *         @ref POOL_ERR_NSUPPORT                   \n
 */
EXPORT int  stpool_group_add_routine(stpool_t *pool, int gid, const char *name, 
									void (*task_run)(struct sttask *ptsk),
									void (*task_err_handler)(struct sttask *ptsk, long reasons),
									void *task_arg, struct schattr *attr);

/**
 * Remove all pending tasks existing the group
 *	
 * This API will mark all tasks who is in the group's pending queue removed, 
 * and remove the \@DO_AGAIN flag for all tasks who is being scheduling or is
 * being removed.
 *
 * @param [in] pool                the pool object
 * @param [in] gid                 the group id
 * @param [in] dispatched_by_pool  if it is none zero, the completion routines of tasks
 *                                 who is in the pending queue will be called by the pool
 *                                 in the background,or the API itself will call them direclty.
 *
 * @return the effected tasks number
 */
EXPORT int  stpool_group_remove_all(stpool_t *pool, int gid, int dispatched_by_pool);

/**
 * Mark all tasks existing in the group currently with the specified flags
 *
 * @param [in] pool    the pool object
 * @param [in] gid     the group id
 * @param [in] lflags  the flags (see @ref stpool_mark_all)
 *
 * @return the effected tasks number
 */
EXPORT int  stpool_group_mark_all(stpool_t *pool, int gid, long lflags);

/**
 * Mark all tasks existing in the group currently with specified flags.
 *
 * @ref stpool_group_mark_all and @ref stpool_group_mark_cb do essentially the same thing,
 * the only difference is that @ref stpool_group_mark_cb uses the flags returned by the
 * walk callback to mark the tasks.
 *
 * @param [in] pool      the pool object
 * @param [in] gid       the group id
 * @param [in] wcb       the user callback to walk the tasks.
 * @param [in] wcb_args  the argument reserved for wcb
 * 
 * @return the effected tasks number
 *
 * @note If wcb returns -1, @ref stpool_group_mark_cb will stop walking the tasks exisiting
 * in the group and return.
 */
EXPORT int  stpool_group_mark_cb(stpool_t *pool, int gid, Walk_cb wcb, void *wcb_args);

/**
 * Wait for all completions of the tasks existing in the group in \@ms milliseconds
 *
 * @param [in] pool the pool object
 * @param [in] gid  the group id
 * @param [in] ms   the timeout (-1 = INFINITE)
 *
 * @return 0 on success, error code listed below otherwise
 *                                            \n
 *        @ref POOL_ERR_GROUP_NOT_FOUND       \n
 *        @ref POOL_ERR_TIMEDOUT              \n
 *        @ref POOL_ERR_NSUPPORT              \n
 */
EXPORT int  stpool_group_wait_all(stpool_t *pool, int gid, long ms);

/**
 * Wait for all completions of the tasks existing in the group in \@ms milliseconds
 *
 * @param [in] pool     the pool object
 * @param [in] gid      the group id
 * @param [in] ms       the timeout (-1 == INFINITE)
 *
 * @return 0 on success, error code listed below otherwise
 *                                         \n
 *        @ref POOL_ERR_GROUP_NOT_FOUND    \n
 *        @ref POOL_ERR_TIMEDOUT           \n
 *        @ref POOL_ERR_NSUPPORT           \n
 */
EXPORT int  stpool_group_wait_any(stpool_t *pool, int gid, long ms);

/**
 * Wait for all completions of the tasks existing in the group in \@ms milliseconds
 *
 * This API will visit all of tasks existing in the group and pass their 
 * status to the user's walk callback, and if user's walk callback returns
 * a none zero value on them, this API will wait for their completions.
 *
 * @param [in] pool     the pool object
 * @param [in] gid      the group id
 * @param [in] wcb      the callback to walk the tasks
 * @param [in] wcb_args the arguments reserved for wcb
 * @param [in] ms       the timeout (-1 == INFINITE)
 *
 * @return 0 on success, error code listed below otherwise
 *                                            \n
 * 	      @ref POOL_ERR_GROUP_NOT_FOUND       \n
 *        @ref POOL_ERR_TIMEDOUT              \n
 *        @ref POOL_ERR_NSUPPORT              \n
 */
EXPORT int  stpool_group_wait_cb(stpool_t *pool, int gid, Walk_cb wcb, void *wcb_args, long ms);

/**
 * Wait for the throttle's being turned off in \@ms milliseconds
 *
 * @param [in] pool     the pool object
 * @param [in] gid      the group id
 * @param [in] ms       the timeout (-1 == INFINITE)
 *
 * @return 0 on success, error code listed below otherwise
 *                                           \n
 *        @ref POOL_ERR_GROUP_NOT_FOUND      \n
 *        @ref POOL_ERR_TIMEDOUT             \n
 *        @ref POOL_ERR_GROUP_DESTROYING     \n
 *        @ref POOL_ERR_DESTROYING           \n
 *
 * @note  User can call @ref stpool_group_throttle_enable to set
 *        the throttle's status of the group.
 */
EXPORT int  stpool_group_throttle_wait(stpool_t *pool, int gid, long ms);

/**
 * Set the throttle's status of the group
 *
 * @param [in] pool    the pool object
 * @param [in] gid     the group id
 * @param [in] enable  if it is none zero, the group's throttle will be turned 
 *                     on, or the group's throttle will be turned off
 *
 * @return none
 *
 * @note  If the group's throttle is on, the group will prevent users delivering 
 * 		  any tasks into its pending queue.
 */
EXPORT void stpool_group_throttle_enable(stpool_t *pool, int gid, int enable);

/**
 * Delete the group
 *
 * @param [in] pool the pool object
 * @param [in] gid  the group id
 *
 * @return none
 * @note Its functions are equal to the codes presented below:
 *
 *       ___set_status(pool, gid, DESTROYING);
 *       ___notify_all_tasks(pool, gid, TASK_VM_F_GROUP_DESTROYING);
 *       stpool_group_mark_all(pool, gid, TASK_VMARK_REMOVE);
 *       stpool_group_wait_all(pool, gid, -1);
 *       ___wakeup_all_waiters(pool, gid);
 *       ___free(pool, gid)
 */
EXPORT void stpool_group_delete(stpool_t *pool, int gid);

#endif
