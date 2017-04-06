#ifndef __CPOOL_CORE_THRAD_STATUS_H__
#define __CPOOL_CORE_THRAD_STATUS_H__
/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  cpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "msglog.h"
#include "cpool_core.h"
#include "cpool_core_gc.h"

#define M_THREAD  "thread"

/*  The status of the threads:
 *
 *           |--------------<---------------|
 *           |                              |
 *           |                              |
 *  . JOIN-----> |-> RUN --> COMPLETE ------|--># (RM)
 *           |   |                          
 *           |   |        |----> QUIT ---># (RM)
 *           |   |--------|                           
 *           |            |----> WAIT ----|--> WAIT_TIMEOUT---># (RM)
 *           |                            |            |
 *           |                            |            |
 *           |                            |            |
 *           |                            |--> FREE -->|
 *           |                                         | 
 *           ----<--------------------------<----------|
 *  
 *    It'll expand our library size if we mark @cpool_core_thread_status_changel 
 * inline, but a good new for us is that it'll improve our perfermance in the real
 * test.
 */

static inline void 
cpool_core_thread_status_changel(cpool_core_t *core, thread_t *self, long status) 
{	
	VERIFY(core, self);
	assert (status != self->status);

#if !defined(NDEBUG) && defined(CONFIG_TRACE_THREAD_STATUS)
	MSG_log(M_THREAD, LOG_TRACE,
			"self(%d/%p) b_waked(%d) %p ==> %p\n",
			self, self->flags, self->b_waked, self->status, status);
#endif

	switch (status) {
	case THREAD_STAT_JOIN:	
		break;
	case THREAD_STAT_WAIT: 
		if (!(THREAD_STAT_RM & self->flags)) {
			-- core->nthreads_real_free;
			++ core->nthreads_real_sleeping;
		}
		/**
		 * We flush the cache
		 */
		cpool_core_objs_local_flushl(self);

		/**
		 * Add the threads into the wait queue 
		 */
		self->b_waked = 0;
		list_add(&self->run_link, &core->ths_waitq);
		++ core->n_qths_wait;
		
		cpool_core_event_free_try_notifyl(core);
		break;
	case THREAD_STAT_TIMEDOUT: {			
		/**
		 * We try to remove the thread from the servering sets 
		 * if the threads should be stopped providing service.
		 *
		 * Warning: The situation below may happen.
		 * 	The thread has been waked up, but @OSPX_cond_timed_wait
		 * returns ETIMEDOUT. so we should process it carefully.
		 */	
		if (!self->b_waked) {
			list_del(&self->run_link);
			-- core->n_qths_wait;
		} 
		if (!((THREAD_STAT_RM|THREAD_STAT_GC) & self->flags)) {
			if ((core->nthreads_real_pool > core->minthreads) && !core->n_qdispatchs &&
				((!core->npendings) || core->paused)) { 
				unsigned n = (unsigned)time(NULL);
                /**
				 * We do not plan to exit if we have created servering threads recently 
				 */
				if (core->crttime > n || core->crttime + 4 <= n) {
					self->flags |= THREAD_STAT_RM;
					
					++ core->nthreads_dying;
					-- core->nthreads_real_pool;
					break;
				}
			}
		}
		break;
	}
	case THREAD_STAT_FORCE_QUIT: {
		if (THREAD_STAT_RM & self->flags) 
			assert (!self->run);
		else {
			self->flags |= THREAD_STAT_RM;
			++ core->nthreads_dying;
			-- core->nthreads_real_pool;
			-- core->nthreads_real_free;
			self->run = 0;		
		} 
		break;
	}
	case THREAD_STAT_RUN: 
		++ core->nthreads_running;
		
		if (likely(0 == self->flags) || !(THREAD_STAT_RM & self->flags))
			-- core->nthreads_real_free;
		/**
		 * JOIN|RM ->gettask(RUN) 
		 */
		else 
			++ core->nthreads_dying_run;

		/**
		 * Try to create more threads to provide services 
		 * before our's executing any task. 
		 *    .Are there any FREE threads ? 
		 *    .Has the threads number arrived at the limit ? 
		 *    .Are there any pending tasks ?  (Optimize, Skip)
		 */
		if (!core->nthreads_real_free && !core->n_qths_waked && 
			/* core->maxthreads >= core->nthreads_real_pool && */core->npendings) 
			cpool_core_ensure_servicesl(core, self); 		
	
#ifndef NDEBUG
		MSG_log(M_THREAD, LOG_TRACE,
				"{\"%s\"/%p} thread(%d) RUN(\"%s\"/%p). <ndones:%u npendings:%d/%d>\n", 
				core->desc, core, self, (self->task_type == TASK_TYPE_DISPATCHED) ? "DISPATCH" : __curtask->task_desc, 
				__curtask, self->ntasks_processed, core->npendings, core->n_qdispatchs);
#endif	
		break;
	case THREAD_STAT_COMPLETE:
		-- core->nthreads_running;
	
#if 0
		if (unlikely(THREAD_STAT_RM & self->flags)) 
			-- core->nthreads_dying_run;
		else
			++ core->nthreads_real_free; 
#else
		if (likely(0 == self->flags) || THREAD_STAT_FLUSH == self->flags) 
			++ core->nthreads_real_free;
		else {
			if (THREAD_STAT_RM & self->flags) 
				-- core->nthreads_dying_run;
		 	else 
				assert (self->flags & THREAD_STAT_GC);
		}
#endif
#ifndef NDEBUG
		++ self->ntasks_processed;
		/**
		 * We do not need to check @__curtask at present. 
         * so it is not neccessary to reset it to waste our
		 * CPU. 
		 */
		__curtask = NULL;
#endif		
		break;
	case THREAD_STAT_FREE: 
		break;
	case THREAD_STAT_LEAVE: 
		/**
		 * Remove current thread from the THREAD queue 
		 */
		list_del(&self->link);
		-- core->n_qths;
		
		if (THREAD_STAT_RM & self->flags) {
			assert (core->nthreads_dying > 0 && !self->run);
			
			if (!-- core->nthreads_dying)
				OSPX_pthread_cond_broadcast(&core->cond_ths);
		} else 
			/**
			 * Send a notification to @cpool_release_ex 
			 */
			if (list_empty(&core->ths))
				OSPX_pthread_cond_broadcast(&core->cond_ths);
		
		if (THREAD_STAT_GC & self->flags) 
			cpool_core_GC_leavel(core, self);

		/**
		 * We flush the cache
		 */
		cpool_core_objs_local_flushl(self);

		/**
		 * NOTE:
		 * 	 We also update the @crttime if the thread is going to exit. 
		 * it is a nice measure method to prevent the waiting threads
		 * from quiting at the same time.
		 */
		core->crttime = (unsigned)time(NULL);
#ifndef NDEBUG
		MSG_log(M_THREAD, LOG_DEBUG,
			"{\"%s\"/%p} thread(%d) exits. <ndones:%d status:0x%lx=>0x%lx> (@nthreads:%d(%d) @npendings:%d/%d)\n", 
			core->desc, core, self, self->ntasks_processed, (long)self->status, status, core->n_qths, core->nthreads_real_pool,
			core->npendings, core->n_qdispatchs);
#endif		
		cpool_core_event_free_try_notifyl(core);
		break;
	}
	
	/**
	 * Check the thread's previous status 
	 */
	if (THREAD_STAT_WAIT == self->status) {	
		if (self->b_waked) {
			assert (THREAD_STAT_FREE == status || 
			        THREAD_STAT_TIMEDOUT == status);

			assert (core->n_qths_waked > 0 && self->b_waked);
			-- core->n_qths_waked;
		}
		
		/**
		 * If the thread is waked up to exit since it is marked died by
		 * @cpool_core_adjust(_abs) or @cpool_core_flush, and meanwhile 
		 * there are a few tasks coming, we should check the pool status 
		 * again to ensure that there are servering threads in the pool. 
		 */
		if (THREAD_STAT_RM & self->flags && !self->run) {
			if (!core->nthreads_real_free && !core->n_qths_waked && 
				/* core->maxthreads >= core->nthreads_real_pool && */ core->npendings) 
				cpool_core_ensure_servicesl(core, self); 		
		
		} else {
			assert (core->nthreads_real_sleeping > 0 &&
			       (!(THREAD_STAT_RM & self->flags) ||
				     THREAD_STAT_TIMEDOUT == status));
			-- core->nthreads_real_sleeping;
			
			if (THREAD_STAT_RM & self->flags) 
				self->run = 0;
			else
				++ core->nthreads_real_free;
		} 
		assert (!(THREAD_STAT_RM & self->flags) || !self->run);
	}	
	VERIFY(core, self);
		
	self->status = status; 	
}

/**
 * The wrapper is aimed to try to shrink the exe file size 
 */
void cpool_core_thread_status_changel_wrapper(cpool_core_t *core, thread_t *self, long status);
void cpool_core_thread_status_change(cpool_core_t *core, thread_t *self, long status);

#endif
