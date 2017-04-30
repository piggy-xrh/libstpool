#ifndef __STPOOL_CAPS_H__
#define __STPOOL_CAPS_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */


/**
 * The capabilitis of the pool
 *
 * @note
 * To improve the perfermance of the pool, some features may not be avalible for users, 
 * if user really needs these features, user should pass their fetures masks to @ref 
 * stpool_create. 
 * 
 * All of the APIs is avalible for user, but if user calls a API without having passed its
 * needed fetures masks to @ref stpool_create, this API may return @ref POOL_ERR_NSUPPORT if 
 * the underlying pool object does not support it.
 */
enum 
{
	/**
	 * The working threads will be created and destroyed by the pool automatically
	 */
	eCAP_F_DYNAMIC  = 0x01L,

	/**
	 * All of the working threads will be created to wait for tasks and
	 * they will always stay alive event if there are none any pendings
	 * tasks existing in the pool
	 */
	eCAP_F_FIXED    = 0x02L,

	/**
	 * The task's scheduling attributes must be effective
	 */
	eCAP_F_PRIORITY = 0x04L,

	/**
	 * The pool must support @ref \@stpool_throttle_enable and @ref \@stpool_throttle_wait
	 */
	eCAP_F_THROTTLE = 0x08L,

	/**
	 * The pool must support @ref \@stpool_suspend and @ref \@stpool_resume
	 */
	eCAP_F_SUSPEND  = 0x10L,

	/**
	 * All of the tasks must be traceable
	 *
	 * Normally, the library ensures that all of the tasks who is in the pending queue will 
	 * be visitable for users, but If the task is traceable, the library will ensure that all 
	 * of the tasks will be visitable for users even if some of them are being scheduled or 
	 * being dispatched.
	 */
	eCAP_F_TRACE    = 0x20L,
	
	/**
	 * The pool must support @ref \@stpool_wait_any
	 */
	eCAP_F_WAIT_ANY = 0x40L,
	
	/**
	 * The pool must support @ref \@stpool_wait_all
	 */
	eCAP_F_WAIT_ALL = 0x80L,
	
	/**
	 * The pool must support @ref \@stpool_set_overload_attr, @ref \@stpool_get_overload_attr
	 */
	eCAP_F_OVERLOAD = 0x0100L,

	/**
	 * TASK_VMMARK_ENABLE_QUEUE and TASK_VMARK_DISABLE_QUEUE must be avaliable
	 */
	eCAP_F_DISABLEQ = 0x1000L,
	
	/**
	 * TASK_VMARK_REMOVE_BYPOOL must be avaliable
	 *
	 * The pool will be responsible for calling the @ref struct sttask::task_err_hander 
	 * in the background if the task has been removed from the pending queue by user
	 */
	eCAP_F_REMOVE_BYPOOL = 0x2000L,
		
	/**
	 * The pool must support the routine tasks 
	 *
	 * see @ref stpool_add_routine, @ref stpool_group_add_routine
	 */
	eCAP_F_ROUTINE    = 0x080000L,

	/**
	 * The pool must support the tasks who is created by users or is created by @ref
	 * stpool_task_new
	 */
	eCAP_F_CUSTOM_TASK = 0x100000L,
	
	/**
	 * For backwards compatablity
	 */
	#define eCAP_F_TASK_EX eCAP_F_CUSTOM_TASK 
	
	/**
	 * The pool must support @ref stpool_task_wait
	 */
	eCAP_F_TASK_WAIT  = 0x200000L,
	
	/**
	 * The pool must support @ref stpool_task_wait_all
	 */
	eCAP_F_TASK_WAIT_ALL = 0x400000L,
	
	/**
	 * The pool must support @ref stpool_task_wait_any
	 */
	eCAP_F_TASK_WAIT_ANY = 0x800000L,
	
	/**
	 * The pool must support GROUP
	 *
	 * The APIs listed below must be avaliable for user
	 *
	 * @ref stpool_task_set_gid      \n      
	 * @ref stpool_task_gid          \n
	 * @ref stpool_task_gname2       \n
	 * @ref stpool_task_gname        \n
	 *                               \n
	 * @ref stpool_group_create      \n
	 * @ref stpool_group_id          \n
	 * @ref stpool_group_name2       \n
	 * @ref stpool_group_name        \n
	 * @ref stpool_group_getattr     \n
	 * @ref stpool_group_setattr     \n
	 * @ref stpool_group_stat        \n
	 * @ref stpool_group_stat_all    \n
	 * @ref stpool_group_remove_all  \n
	 * @ref stpool_group_mark_all    \n
	 * @ref stpool_group_mark_cb     \n
	 * @ref stpool_group_delete      \n
	 */
	eCAP_F_GROUP = 0x01000000L,
	
	/**
	 * The pool must support @ref stpool_group_throttle_enable and @ref stpool_group_throttle_wait
	 */
	eCAP_F_GROUP_THROTTLE = 0x020000L,

	/**
	 * The pool must support @ref stpool_group_wait_all
	 */
	eCAP_F_GROUP_WAIT_ALL = 0x040000L,
	
	/**
	 * The pool must support @ref stpool_group_wait_any
	 */
	eCAP_F_GROUP_WAIT_ANY = 0x080000L,
	
	/**
	 * The pool must support @ref stpool_group_suspend, @ref stpool_group_resume, @ref stpool_group_suspend_all
	 * and  @ref stpool_group_resume_all
	 */
	eCAP_F_GROUP_SUSPEND  = 0x100000L,
	
	/**
	 * The pool must support @ref \@stpool_group_set_overload_attr, @ref \@stpool_group_get_overload_attr
	 */ 
	eCAP_F_GROUP_OVERLOAD = 0x200000L,
	
	/**
	 * The capabilities sets
	 */
	eCAP_F_ALL = eCAP_F_DYNAMIC|eCAP_F_FIXED|eCAP_F_PRIORITY|eCAP_F_THROTTLE|
				 eCAP_F_SUSPEND|eCAP_F_WAIT_ANY|eCAP_F_WAIT_ALL|eCAP_F_WAIT_ANY|eCAP_F_OVERLOAD|eCAP_F_TRACE|
			     eCAP_F_ROUTINE|eCAP_F_CUSTOM_TASK|eCAP_F_TASK_WAIT|eCAP_F_TASK_WAIT_ALL|
				 eCAP_F_DISABLEQ|eCAP_F_REMOVE_BYPOOL|
				 eCAP_F_GROUP|eCAP_F_GROUP_THROTTLE|eCAP_F_GROUP_WAIT_ANY|eCAP_F_GROUP_WAIT_ALL|
				 eCAP_F_GROUP_SUSPEND|eCAP_F_GROUP_OVERLOAD
};



#endif
