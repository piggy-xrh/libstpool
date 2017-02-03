#ifndef __CPOOL_CORE_GC_H__
#define __CPOOL_CORE_GC_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "timer.h"
#include "cpool_core.h"

int  cpool_core_GC_init(cpool_core_t *core);
void cpool_core_GC_deinit(cpool_core_t *core);

static inline void cpool_core_GC_leavel(cpool_core_t *core, thread_t *self)
{
	assert (self->flags & THREAD_STAT_GC);

#ifndef NDEBUG
	MSG_log(M_SCHEDULER, LOG_TRACE,
			"Thread(%d) stop doing GC ... ncached(%d)\n",
			self, smcache_nl(core->cache_task));
#endif
	self->flags &= ~THREAD_STAT_GC;
	core->GC = NULL;
}

/**
 * We do not need to hold the global lock
 */
#define cpool_core_GC_leave(core, self) cpool_core_GC_leavel(core, self)

static inline long cpool_core_GC_gettimeol(cpool_core_t *core, thread_t *self)
{
	long us;

	assert (core->us_gc_left_timeo >= 0);
	
	if (!core->us_gc_left_timeo || !(CORE_F_created & cpool_core_statusl(core)))
		core->us_gc_left_timeo = core->cattr.rest_timeo * 1000;
	
	us = us_endr(core->us_last_gcclock);
	if (core->us_gc_left_timeo <= us) 
		core->us_gc_left_timeo = 0;
	else
		core->us_gc_left_timeo -= us;
	core->us_last_gcclock = us_startr();
	
	self->last_to = max(1, core->us_gc_left_timeo / 1000);
	
	return self->last_to;
}

/**
 * Get a GC task
 */
int cpool_core_GC_gettaskl(cpool_core_t *core, thread_t *self);

/**
 * Notify the core to slow down the GC
 */
static inline void cpool_core_slow_GC(cpool_core_t *core)
{
	/**
	 * We set the @b_GC_delay to tell the pool
	 * that we should slow down GC 
	 */
	if (core->GC)
	 	core->b_GC_delay = 1;
}

/**
 * we try to wake up a threads to do the GC to avoid holding so many memories 
 */
void cpool_core_try_GC(cpool_core_t *core);
#endif
