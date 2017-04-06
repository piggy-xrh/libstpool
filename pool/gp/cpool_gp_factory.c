/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <stdlib.h>

#include "msglog.h"
#include "cpool_com_method.h"
#include "cpool_gp_method.h"
#include "cpool_gp_factory.h"

static cpool_method_t __gpool_me = {
		{
			sizeof(ctask_trace_t),
			cpool_com_cache_get,
			cpool_com_cache_put, 
			NULL,
			NULL, 
			cpool_gp_task_queue,
			cpool_gp_task_remove,
			cpool_gp_task_mark,
			cpool_gp_task_detach, 
			cpool_gp_task_stat,
			cpool_gp_task_wait,
			cpool_gp_task_wait_any,
			cpool_gp_task_wsync
		},
		{
			cpool_gp_stat,
			cpool_gp_scheduler_map_dump,
			cpool_com_atexit,
			cpool_com_addref,
			cpool_com_release,
			cpool_com_setattr,
			cpool_com_getattr,
			cpool_com_set_schedulingattr,
			cpool_com_get_schedulingattr,
			NULL,
			NULL,
			cpool_com_set_activetimeo,
			cpool_com_adjust,
			cpool_com_adjust_abs,
			cpool_com_flush,
			cpool_gp_suspend,
			cpool_com_resume,
			cpool_gp_remove_all,
			cpool_gp_mark_all,
			cpool_gp_mark_cb,
			cpool_gp_wait_all,
			cpool_gp_throttle_ctl,
			cpool_gp_throttle_wait,
			cpool_gp_wait_any,
			cpool_gp_wait_cb,
		}, 
		{
			cpool_gp_entry_create,
			cpool_gp_entry_delete,
			cpool_gp_entry_id,
			cpool_gp_entry_desc,
			cpool_gp_entry_stat,
			cpool_gp_entry_stat_all,
			cpool_gp_entry_suspend,
			cpool_gp_entry_suspend_all,
			cpool_gp_entry_resume,
			cpool_gp_entry_resume_all,
			cpool_gp_entry_setattr,
			cpool_gp_entry_getattr,
			cpool_gp_entry_set_oaattr,
			cpool_gp_entry_get_oaattr,
			cpool_gp_entry_throttle_ctl,
			cpool_gp_entry_throttle_wait,
			cpool_gp_entry_remove_all,
			cpool_gp_entry_mark_all,
			cpool_gp_entry_mark_cb,
			cpool_gp_entry_wait_all,
			cpool_gp_entry_wait_cb,
			cpool_gp_entry_wait_any
		}
};

void 
__fac_gp_common_dtor(cpool_t *fac_ins)
{
	cpool_gp_free_instance(fac_ins->ins);
	free(fac_ins);
}

static cpool_t *
__fac_gp_common_ctor(long efuncs, const cpool_method_t *me,
	const char *desc, int maxthreads, int minthreads, int priq_num, int suspend)
{
	int  e;
	cpool_t *pool = calloc(1, sizeof(cpool_t) + strlen(desc) + 1);

	if (!pool)
		return NULL;
	
	pool->desc = (char *)(pool + 1);
	strcpy(pool->desc, desc);

	pool->efuncs = efuncs;
	pool->me  = me;
	pool->free = __fac_gp_common_dtor;
		
	/**
	 * Create the group pool instance
	 */
	if ((e=cpool_gp_create_instance((cpool_gp_t **)&pool->ins, pool->desc, maxthreads, minthreads, priq_num, suspend, efuncs))) {
		MSG_log(M_GROUP, LOG_ERR,
			   "Failed to create gp pool. code(%d)\n",
			   e);
		free(pool);
		return NULL;
	}

	return pool;
}

cpool_t *
fac_gp_dynamic_create(const char *desc, int max, int min, int priq_num, int suspend)
{
	const cpool_factory_t *const fac = get_gp_dynamic_factory();

	return __fac_gp_common_ctor(fac->efuncs, fac->method, desc, max, min, priq_num, suspend);
}


const cpool_factory_t *const
get_gp_dynamic_factory()
{
	static cpool_factory_t __dummy_fac = {
		85, 
		eFUNC_F_TASK_EX|eFUNC_F_ADVANCE|eFUNC_F_DYNAMIC_THREADS|eFUNC_F_DISABLEQ|
		eFUNC_F_TASK_WAITABLE|eFUNC_F_PRIORITY|eFUNC_F_TRACEABLE, 
		&__gpool_me, fac_gp_dynamic_create,
	};

	return &__dummy_fac;
}


