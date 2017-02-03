/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "ospx.h"
#include "msglog.h"
#include "cpool_rt_factory.h"
#include "cpool_com_method.h"
#include "cpool_rt_method.h"
#include "cpool_rt_struct.h"

static cpool_method_t __fixed_me;
static cpool_method_t __fixed_pri_me; 
static cpool_method_t __dynamic_me;
static cpool_method_t __dynamic_pri_me; 

static OSPX_pthread_once_t __octl = OSPX_PTHREAD_ONCE_INIT;

static void 
__cpool_rt_method_init()
{
	static cpool_method_t __def_me = {
		{
			 sizeof(ctask_t),
			 cpool_com_cache_get,
			 cpool_com_cache_put, 
			 cpool_rt_task_init,
			 NULL, 
			 cpool_rt_task_queue,
			 cpool_rt_task_remove,
			 cpool_rt_task_mark,
			 NULL,
			 cpool_rt_task_stat,
			 NULL,
			 NULL,
			 NULL
		},
		{
			 cpool_rt_stat,
			 cpool_rt_scheduler_map_dump,
			 cpool_com_atexit,
			 cpool_com_addref,
			 cpool_com_release,
			 cpool_com_setattr,
			 cpool_com_getattr,
			 cpool_com_set_schedulingattr,
			 cpool_com_get_schedulingattr,
			 cpool_com_set_activetimeo,
			 cpool_com_adjust,
			 cpool_com_adjust_abs,
			 cpool_com_flush,
			 cpool_rt_suspend,
			 cpool_com_resume,
			 cpool_rt_remove_all,
			 cpool_rt_mark_all,
			 cpool_rt_mark_cb,
			 cpool_rt_wait_all,
			 cpool_rt_throttle_ctl,
			 cpool_rt_throttle_wait,
			 NULL,
			 NULL,
		},
		{0}
	};

	memcpy(&__fixed_me,       &__def_me, sizeof(__def_me));
	memcpy(&__fixed_pri_me,   &__def_me, sizeof(__def_me));
	memcpy(&__dynamic_me,     &__def_me, sizeof(__def_me));
	memcpy(&__dynamic_pri_me, &__def_me, sizeof(__def_me));
	
	//__fixed_pri_me.me.task_queue = __dynamic_pri_me.me.task_queue = cpool_rt_pri_task_queue;
}

static void
__fac_rt_common_dtor(cpool_t *fac_ins)
{
	cpool_rt_free_instance(fac_ins->ins);
	free(fac_ins);
}

static cpool_t *
__fac_rt_common_ctor(long efuncs, const cpool_method_t *me,
	const char *desc, int maxthreads, int minthreads, int pri_q_num, int suspend)
{
	int  e;
	cpool_t *pool = calloc(1, sizeof(cpool_t) + strlen(desc) + 1); 

	if (!pool)
		return NULL;
	
	pool->desc = (char *)(pool + 1);
	strcpy(pool->desc, desc);
	
	pool->efuncs = efuncs;
	pool->me  = me;
	pool->free = __fac_rt_common_dtor;
	assert (!(efuncs & eFUNC_F_ADVANCE));
		
	/**
	 * Create the rt pool instance
	 */
	if ((e=cpool_rt_create_instance((cpool_rt_t **)&pool->ins, pool->desc, maxthreads, minthreads, pri_q_num, suspend, efuncs))) {
		MSG_log2(M_RT, LOG_ERR,
			   "Failed to create rt pool. code(%d)",
			   e);
		free(pool);
		return NULL;
	}

	return pool;
}

cpool_t *
fac_rt_fixed_ctor(const char *desc, int maxthreads, int minthreads, int pri_q_num, int suspend)
{
	const cpool_factory_t *const fac = get_rt_fixed_factory();

	return __fac_rt_common_ctor(fac->efuncs, fac->method, desc, maxthreads, minthreads, pri_q_num, suspend);
}

cpool_t *
fac_rt_fixed_pri_ctor(const char *desc, int maxthreads, int minthreads, int pri_q_num, int suspend)
{
	const cpool_factory_t *const fac = get_rt_fixed_pri_factory();
	
	return __fac_rt_common_ctor(fac->efuncs, fac->method, desc, maxthreads, minthreads, pri_q_num, suspend);
}

cpool_t *
fac_rt_dynamic_ctor(const char *desc, int maxthreads, int minthreads, int pri_q_num, int suspend)
{
	const cpool_factory_t *const fac = get_rt_dynamic_factory();
	
	return __fac_rt_common_ctor(fac->efuncs, fac->method, desc, maxthreads, minthreads, pri_q_num, suspend);
}

cpool_t *
fac_rt_dynamic_pri_ctor(const char *desc, int maxthreads, int minthreads, int pri_q_num, int suspend)
{
	const cpool_factory_t *const fac = get_rt_dynamic_pri_factory();
	
	return __fac_rt_common_ctor(fac->efuncs, fac->method, desc, maxthreads, minthreads, pri_q_num, suspend);
}


const cpool_factory_t *const
get_rt_dynamic_factory()
{
	static cpool_factory_t __fac = {
		92, eFUNC_F_DYNAMIC_THREADS|eFUNC_F_DISABLEQ, &__dynamic_me, fac_rt_dynamic_ctor
	};

	OSPX_pthread_once(&__octl, __cpool_rt_method_init);
	
	return &__fac;
}

const cpool_factory_t *const
get_rt_dynamic_pri_factory()
{
	static cpool_factory_t __fac = {
		89, eFUNC_F_DYNAMIC_THREADS|eFUNC_F_PRIORITY|eFUNC_F_DISABLEQ, &__dynamic_pri_me, fac_rt_dynamic_pri_ctor,
	};

	OSPX_pthread_once(&__octl, __cpool_rt_method_init);
	
	return &__fac;
}

const cpool_factory_t *const
get_rt_fixed_factory()
{	
	static cpool_factory_t __fac = {
		100, eFUNC_F_DISABLEQ, &__fixed_me, fac_rt_fixed_ctor, 
	};
	
	OSPX_pthread_once(&__octl, __cpool_rt_method_init);

	return &__fac;
}

const cpool_factory_t *const
get_rt_fixed_pri_factory()
{
	static cpool_factory_t __fac = {
		95, eFUNC_F_PRIORITY|eFUNC_F_DISABLEQ, &__fixed_pri_me, fac_rt_fixed_pri_ctor, 
	};

	OSPX_pthread_once(&__octl, __cpool_rt_method_init);
	
	return &__fac;
}

