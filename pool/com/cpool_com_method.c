/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_com_method.h"
#include "cpool_core.h"
#include "cpool_core_gc.h"
#include "cpool_factory.h"

static inline cpool_core_t *CORE(void * ins)
{
	/**
	 * The first member of the context must be the address of the core object
	 */
	return *(cpool_core_t **)(ins);
}

void  
cpool_com_atexit(void * ins, void (*__atexit)(void *), void *opaque)
{
	cpool_core_atexit(CORE(ins), __atexit, opaque);
}

void
cpool_com_adjust(void * ins, int max, int min)
{
	cpool_core_t *core = CORE(ins);

	OSPX_pthread_mutex_lock(&core->mut);
	max += core->maxthreads;
	min += core->minthreads;
	
	/**
	 * Correct the param 
	 */	
	if (max <= 0)
		max = 1;

	if (min <= 0)
		min = 0;
	min = min(max, min);	
	cpool_core_adjust_abs_l(core, max, min);
	OSPX_pthread_mutex_unlock(&core->mut);
}

void
cpool_com_adjust_abs(void * ins, int max, int min)
{
	cpool_core_t *core = CORE(ins);
	
	if (min < 0 || max < min) 
		return;

	OSPX_pthread_mutex_lock(&core->mut);	
	cpool_core_adjust_abs_l(core, max, min);
	OSPX_pthread_mutex_unlock(&core->mut);
}

int   
cpool_com_flush(void * ins)
{
	int ndecs;
	cpool_core_t *core = CORE(ins);
	
	OSPX_pthread_mutex_lock(&core->mut);
	ndecs = core->nthreads_real_pool - core->minthreads;
	ndecs = cpool_core_dec_threadsl(core, ndecs);
	OSPX_pthread_mutex_unlock(&core->mut);
	
	return ndecs;
}

void  
cpool_com_resume(void * ins)
{	
	cpool_core_resume(CORE(ins));
}

long  
cpool_com_addref(void * ins)
{
	long ref = 0;
	cpool_core_t *core = CORE(ins);
	
	OSPX_pthread_mutex_lock(&core->mut);
	if (CORE_F_created & core->status)
		ref = cpool_core_addrefl(core, 1);
	OSPX_pthread_mutex_unlock(&core->mut);
	
	return ref;

}

long  
cpool_com_release(void * ins)
{
	/**
	 * We do not waste our time on waiting for 
	 * the pool's being destroyed completely 
	 */
	return cpool_core_release_ex(CORE(ins), 0, 0);
}

void  
cpool_com_setattr(void * ins, struct thread_attr *attr)
{
	cpool_core_t *core = CORE(ins);
	
	core->thattr.stack_size = attr->stack_size;
	core->thattr.sche_policy = (enum ep_POLICY)attr->ep_schep;
	core->thattr.sche_priority = attr->sche_priority;
}

void  
cpool_com_getattr(void * ins, struct thread_attr *attr)
{
	cpool_core_t *core = CORE(ins);
	
	attr->stack_size = core->thattr.stack_size;
	attr->ep_schep = (enum ep_TH)core->thattr.sche_policy;
	attr->sche_priority = core->thattr.sche_priority;
}

void  
cpool_com_set_schedulingattr(void * ins, struct scheduling_attr *attr)
{
	cpool_core_t *core = CORE(ins);
	
	core->max_tasks_qscheduling  = attr->max_qscheduling;
	core->max_tasks_qdispatching = attr->max_qdispatching;
}

void  
cpool_com_get_schedulingattr(void * ins, struct scheduling_attr *attr)
{
	cpool_core_t *core = CORE(ins);
	
	attr->max_qscheduling  = core->max_tasks_qscheduling;
	attr->max_qdispatching = core->max_tasks_qdispatching;
}

void  
cpool_com_set_activetimeo(void * ins, long acttimeo, long randtimeo)
{
	cpool_core_t *core = CORE(ins);
	
	core->acttimeo = acttimeo * 1000;
	core->randtimeo = randtimeo * 1000;
}

ctask_t *
cpool_com_cache_get(void * ins)
{
	ctask_t *ptask;
	cpool_core_t *core = CORE(ins);
	
	cpool_core_slow_GC(core);
	ptask = smcache_get(core->cache_task, 0);

	if (!ptask) {
		cpool_core_objs_local_flush_all(core);
		ptask = smcache_get(core->cache_task, 1);
	}
	
	return ptask;
}

void  
cpool_com_cache_put(void * ins, ctask_t *ptask)
{
	cpool_core_t *core = CORE(ins);
	
	assert (ptask && ptask->f_vmflags & eTASK_VM_F_LOCAL_CACHE);
	smcache_add(core->cache_task, ptask);
}

