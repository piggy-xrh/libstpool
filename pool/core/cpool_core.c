/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */
#include "ospx.h"
#include "ospx_errno.h"
#include "msglog.h"
#include "cache_creater.h"
#include "cpool_core.h"
#include "cpool_core_gc.h"
#include "cpool_core_thread_status.h"

static void cpool_core_setstatus(cpool_core_t *core, long status, int synchronized);
static void cpool_core_schedule(cpool_core_t *core, thread_t *self);

int
cpool_core_ctor(cpool_core_t *core, const char *desc, const cpool_core_method_t *const me, 
	int maxthreads, int minthreads, int suspend, long lflags) 
{
	static cache_attr_t attr = {
		40   , /** cache 40 objects */
		90   , /** wakeup throttle: 90 */
		30   , /** GC 30 objects one at a time */
		4000 , /** interleaved time: 4000 ms */
		5000 , /** delay time: 5000 ms */
	};
	
	void *priv = core->priv;
	int e, index;
	/**
	 * Connrect the param 
	 */
	maxthreads = max(1, maxthreads);
	minthreads = max(0, minthreads);
	minthreads = min(minthreads, maxthreads);

	/** 
	 * We reset the memory 
	 **/
	bzero(core, sizeof(*core));
	core->desc = desc;
	core->me = me;
	core->priv = priv;
	core->start = time(NULL);
	core->lflags = lflags;
	core->thread_local_cache_limited = 5;
	/**
	 * If the os supports recursive mutex, it will be more convenient 
	 * for users to use our APIs. 
	 */
	if ((errno = OSPX_pthread_mutex_init(&core->mut, 1))) {
		MSG_log2(M_CORE, LOG_ERR,
			"get RECURSIVE MUTEX:%s.", OSPX_sys_strerror(errno));
		
		if ((errno = OSPX_pthread_mutex_init(&core->mut, 0))) {
			MSG_log2(M_CORE, LOG_ERR,
				"mutex_init:%s.", OSPX_sys_strerror(errno));
			return -1;
		}
	}
	cpool_core_setstatus(core, CORE_F_creating, 0);
	
	/**
	 * Initialize the task cache if it is neccessary
	 */
	if (me->task_size) {
		if (objpool_ctor2(&core->objp_task, "FObjp-local-cache", me->task_size, 0,
				attr.nGC_cache, &core->mut)) {
			MSG_log2(M_CORE, LOG_ERR,
				"Fail to execute objpool_ctor.");
			goto errout;
		}
		core->cache_task = objpool_get_cache(&core->objp_task);
	}
	cpool_core_adjust_cachel(core, &attr, NULL);
		
	/**
	 * Preallocate memory for threads. 
	 */
	index = CORE_F_dynamic & lflags ? 15 : maxthreads;
	smcache_init2(&core->cache_thread, "thread-cache", index, NULL, 0, core,
		thread_obj_create, thread_obj_destroy, thread_obj_need_destroy);
	core->buffer = calloc(1, index * (sizeof(thread_t) + 
		sizeof(struct cond_attr)));
	if (core->buffer) {
		thread_t *ths;
		char *addr = core->buffer;

		for (--index; index>=0; --index) {
			ths = (thread_t *)addr;
			INIT_thread_structure(core, ths);
			ths->cattr = (struct cond_attr *)(ths + 1);
			smcache_add(&core->cache_thread, ths);
			addr += sizeof(thread_t) + sizeof(struct cond_attr);
		}
	}

	/**
	 * Initialize the queue 
	 */
	INIT_LIST_HEAD(&core->ths);
	INIT_LIST_HEAD(&core->ths_waitq);
	INIT_LIST_HEAD(&core->dispatch_q);
	
	if ((errno = OSPX_pthread_cond_init(&core->cond_ths)))
		goto free_cache;

	/**
	 * Initialize the default env 
	 */
	core->ref = core->user_ref = 1;
	core->paused = suspend;
	core->maxthreads = maxthreads;
	core->minthreads = minthreads;
	core->acttimeo  = 1000 * 20;
	core->randtimeo = 1000 * 30;
	core->eReasons0 = eReason_ok;
	core->eReasons1 = eReason_removed;
	core->max_tasks_qscheduling  = 1;
	core->max_tasks_qdispatching = 5;

	/**
	 * Call method::ctor to construct the underlying context 
	 */
	if (core->me->ctor && (e = core->me->ctor(core->priv))) {
		MSG_log2(M_CORE, LOG_ERR,
			"Method::ctor error(%d).",
			e);
		goto free_cond_ths;
	}

	/**
	 * Initialize the GC sub system
	 */
	cpool_core_GC_init(core);

	cpool_core_setstatus(core, CORE_F_created, 0);
	{
		time_t n = time(NULL);

		MSG_log(M_CORE, LOG_INFO,
				"Core (\"%s\"/%p) starts up at %s",
				core->desc, core, ctime(&n));
	}

	/**
	 * Load the variables from the environment 
	 */
	if (CORE_F_dynamic & lflags) {
		cpool_core_load_envl(core);

		MSG_log(M_CORE, LOG_INFO,
				">@limit_threads_create_per_time=%d\n",
				core->limit_threads_create_per_time);
	}
	/**
	 * Start up the reserved threads 
	 */
	core->thattr.stack_size = 1024 * 1024 * 2;
	core->thattr.sche_policy = ep_NONE;
	if (core->minthreads > 0) {
		OSPX_pthread_mutex_lock(&core->mut);
		cpool_core_add_threadsl(core, NULL, core->minthreads);
		OSPX_pthread_mutex_unlock(&core->mut);
	}
	
	return 0;

free_cache:
	if (core->me->task_size && core->cache_task)
		objpool_dtor(&core->objp_task);
free_cond_ths:
	OSPX_pthread_cond_destroy(&core->cond_ths);
errout:
	cpool_core_setstatus(core, CORE_F_destroyed, 0);
	OSPX_pthread_mutex_destroy(&core->mut);	
	
	MSG_log2(M_CORE, LOG_ERR,
		"Err:%s", OSPX_sys_strerror(errno));
	return -1;
}

void 
cpool_core_setstatus(cpool_core_t *core, long status, int synchronized)
{
#ifndef NDEBUG
	time_t n = time(NULL);

	MSG_log(M_CORE, LOG_TRACE, 
		"Pool(\"%s\"/%p) [0x%ld ==> 0x%ld] at %s",
		core->desc, core, core->status, status, ctime(&n));
#endif
	if (synchronized)
		core->status = status;
	else {
		OSPX_pthread_mutex_lock(&core->mut);
		core->status = status;
		OSPX_pthread_mutex_unlock(&core->mut);
	}	
}

/**
 * A Register for the exiting callback 
 */
void
cpool_core_atexit(cpool_core_t *core, void (*atexit_func)(void *), void *arg) 
{
	assert (CORE_F_created & core->status);
	core->atexit = atexit_func;
	core->atexit_arg = arg;
}

void
cpool_core_adjust_cachel(cpool_core_t *core, cache_attr_t *attr, cache_attr_t *oattr) 
{
	if (oattr)
		*oattr = core->cattr;
	
	if (attr)
		core->cattr = *attr;
}

void 
cpool_core_load_envl(cpool_core_t *core) 
{
	const char *env;
		
	/**
	 * Load the @limit_threads_create_per_time 
	 */
	env = getenv("LIMIT_THREADS_CREATE_PER_TIME");
	if (!env || atoi(env) <= 0)	
		core->limit_threads_create_per_time = 1;
	else
		core->limit_threads_create_per_time = atoi(env);
}

void 
cpool_core_dtor(cpool_core_t *core) 
{	
	assert (list_empty(&core->ths) &&
		   !core->nthreads_running &&
		   !core->nthreads_real_sleeping &&
		   !core->npendings && !core->n_qdispatchs); 	
	
	VERIFY(core, NULL);
	/**
	 * Call Method::dtor to free the underlying context 
	 */
	if (core->me->dtor)
		core->me->dtor(core->priv);
	
	cpool_core_GC_deinit(core);

	/**
	 * Destroy the task cache if it is neccessary
	 */
	if (core->me->task_size && core->cache_task)
		objpool_dtor(&core->objp_task);
	
	/**
	 * Destroy the thread cache 
	 */
	{
		thread_t *thread;

		smcache_reset(&core->cache_thread, NULL);
		do {
			thread = (thread_t *)smcache_get(&core->cache_thread, 0);
			if (thread) {
				assert (!thread->structure_release);
				if (thread->cattr->initialized)
					OSPX_pthread_cond_destroy(&thread->cattr->cond);
			}
		} while (thread);
		smcache_deinit(&core->cache_thread);
		
		/**
		 * Free the preallocated memory 
		 */
		if (core->buffer)
			free(core->buffer);
	}
	
	OSPX_pthread_mutex_destroy(&core->mut);
	OSPX_pthread_cond_destroy(&core->cond_ths);
	
	/**
	 * Call the atexit
	 */
	core->atexit(core->atexit_arg);
}

long 
cpool_core_release_ex(cpool_core_t *core, int is_wrthread, int clean)
{
	long ref;	
	static cache_attr_t attr = {
		0    /** cache 0 objects */,  
		0    /** wakeup throttle: 0 */,
		3000 /** GC 300 objects one at a time */, 
		100  /** interleaved time: 100 ms */, 
		0    /** delay time: 0 ms */
	};
	
	OSPX_pthread_mutex_lock(&core->mut);
	ref = cpool_core_releasel(core, !is_wrthread);
	assert (core->ref >= 0 && ref >= 0);
	if (!is_wrthread && !ref) {
	#ifndef NDEBUG
		time_t now = time(NULL);

		MSG_log(M_CORE, LOG_INFO,
			"Core(\"%s\"/%p/%p) is being destroyed ... %s\n",
			core->desc, core, core->priv, ctime(&now));
	#endif	
		/**
		 * Note: Tasks should not be added into the pending queue
		 * any more if the pool is being destroyed.
		 */
		core->eReasons0 |= eReason_core_destroying;
		core->eReasons1 |= eReason_core_destroying;
		cpool_core_setstatus(core, CORE_F_destroying, 1);		
			
		/**
		 * Notify all tasks that the core is going to be destroyed 
		 */
		if (core->me->notifyl)
			core->me->notifyl(core->priv, eEvent_F_destroying);
		
		/**
		 * Adjust the cache attribute 
		 */
		smcache_remove_unflushable_objectsl(core->cache_task);
		cpool_core_adjust_cachel(core, &attr, NULL);
		/**
		 * Wake up all working threads 
		 */
		cpool_core_wakeup_n_sleeping_threadsl(core, -1);
		if (list_empty(&core->ths) || clean) {
			/**
			 * Tell the pool that we are responsible for releasing the resources.
			 */
			clean = 1;
			core->release_cleaning = 1;
			
			/**
			 * Wait for all working threads' exitting 
			 */
			for (;!list_empty(&core->ths) || core->ref;) 
				OSPX_pthread_cond_wait(&core->cond_ths, &core->mut);	
		}
	} 	
	
	if (!core->ref) {
		/**
		 * If the working threads is not responsible for destroying the
	 	 * pool, we should give the person responsible a notification 
		 */
		if (is_wrthread) {
			if (core->release_cleaning) 
				OSPX_pthread_cond_broadcast(&core->cond_ths);
			else
				clean = 1;
		}
	} 
	OSPX_pthread_mutex_unlock(&core->mut);
	
	if (clean) {
		/**
		 * Before ours staring clearing the resources, we give the last 
		 * notification to the underlying context 
		 */
		if (core->me->notifyl) {
			OSPX_pthread_mutex_lock(&core->mut);
			core->me->notifyl(core->priv, eEvent_F_shutdown);
			OSPX_pthread_mutex_unlock(&core->mut);
		}

		/**
		 * Now we can free the pool env safely 
		 */
		assert (0 == core->user_ref && core->ref == 0);
		assert (list_empty(&core->ths)); 	
		
		cpool_core_setstatus(core, CORE_F_destroyed, 0);		
		{
			time_t n = time(NULL);
	
			MSG_log(M_CORE, LOG_INFO,
				"Core (\"%s\"/%p/%p) is destroyed at %s",
				core->desc, core, core->priv, ctime(&n));
		}
		
		/**
		 * If there are huge number of task objects existing in
		 * the cache, we have a little break after our recycling
		 * part of them to ensure that the CPU runs at a low speed 
		 */ 
		if (is_wrthread && smcache_nl(core->cache_task) >= 15000) {
			int n;
			
			for (;;) {
				n = smcache_nl(core->cache_task) - 3000;

				// FIX Warning: gcc -O2/-O3 
				//if (n <= 0 || !smcache_flushablel(core->cache_task, n))
				if (n <= 0 || !smcache_need_destroy2(core->cache_task))
					break;
				smcache_flush(core->cache_task, n);
				msleep(1);
			}
		} 
		/**
		 * Clear the task cache 
		 */
		smcache_flush(core->cache_task, 0);
			
		cpool_core_dtor(core);
	}

	return ref;
}

void 
cpool_core_adjust_abs_l(cpool_core_t *core, int maxthreads, int minthreads) 
{
	int nthreads;
	
	MSG_log(M_CORE, LOG_INFO,
		"threads_configure(%d,%d) ... (\"%s\"/%p) \n",
		maxthreads, minthreads, core->desc, core);

	/**
	 * Verify the params 
	 */	
	assert (maxthreads >= 1 && minthreads >= 0 && 
			maxthreads >= minthreads);

	if (!((CORE_F_created|CORE_F_destroying) & core->status) ||
		((CORE_F_destroying & core->status) && (maxthreads > 0 || 
		minthreads > 0))) {
		MSG_log(M_CORE, LOG_WARN,
			"Action is not supported in current status\n");		
		return;
	}
	core->maxthreads = maxthreads;
	core->minthreads = minthreads;
	
	/**
	 * Notify the underlying context 
	 */
	if (core->me->notifyl)
		core->me->notifyl(core->priv, eEvent_F_thread);
	
	if (core->minthreads > core->nthreads_real_pool) {
		nthreads = core->minthreads - core->nthreads_real_pool;
		cpool_core_add_threadsl(core, NULL, nthreads);
	
	} else if (core->nthreads_real_pool > core->maxthreads) {
		nthreads = core->nthreads_real_pool - core->maxthreads; 
		cpool_core_dec_threadsl(core, nthreads);
	
	} else	
		/**
		 * We wakeup all of the sleeping threads 
		 */
		cpool_core_wakeup_n_sleeping_threadsl(core, -1);	
	
	assert (core->nthreads_real_pool == core->nthreads_real_sleeping +
			core->nthreads_real_free + (core->nthreads_running - 
			core->nthreads_dying_run));	
	
	/** 
	 * Reset the statics report 
	 */
	core->nthreads_peak = core->nthreads_real_pool;
	
	assert (core->n_qths_wait >= 0 && core->n_qths_wait <= core->n_qths &&
	        core->n_qths_waked >= 0 && core->n_qths_waked <= core->n_qths);	
}


/**
 * It seems that random() can not work well on windows, so we use @cpool_core_random to instead.
 */
#define cpool_core_random(core, self) \
	(time(NULL) ^ (unsigned long)self * 1927 * (unsigned long)random())

long
cpool_core_get_restto(cpool_core_t *core, thread_t *self) 
{
	/**
	 * If the thread has been markd with @THREAD_STAT_RM o the pool is 
	 * being destroying, we'll plan to destroy the thread imediately 
	 */
	if (THREAD_STAT_RM & self->flags && CORE_F_created & core->status) {
		self->last_to = 0;
		goto out;
	}
		
	if (!(CORE_F_created & core->status) && (core->release_cleaning || 
		!(self->flags & THREAD_STAT_GC))) {
		self->last_to = 0;
		goto out;
	}
		
	/**
	 * If the thread is doing the GC, we should slow down its speed 
	 * to reuse the cached objects if we have detected that there are
	 * tasks coming 
	 */
	if (THREAD_STAT_GC & self->flags) {
		cpool_core_GC_gettimeol(core, self);
		goto out;
	} 
	
	/**
	 * If there are none any tasks existing in the pending
	 * queue and the number of waiting threads who should
	 * not be destroyed has not arrived at the limited number,
	 * we plan to take the thread into deep sleep 
	 */
	if (core->nthreads_real_sleeping - core->minthreads <= -1) 
		self->last_to = -1; 
	 
	 else {
		long t_interleaved;
		unsigned long nt = (unsigned long)time(NULL);

		/**
		 * Initialize the random sed 
		 */
		srandom(nt);

		self->last_to = (long)(core->acttimeo + (unsigned long)cpool_core_random(core, self) % core->randtimeo); 
		if (self->last_to < 0) 
			(self->last_to) &= 0x0000ffff;
		
		/**
		 * If we have created threads latest, maybe the thread 
		 * should wait for tasks for enough time to avoid destroying 
		 * and creating threads frequently, so we try to increase the 
		 * thread's sleeping time if it is too short. 
		 */
		if (nt <= core->crttime) 
			core->crttime = nt;
		t_interleaved = 4600 - 1000 * (nt - core->crttime);

		while (self->last_to < t_interleaved)
			self->last_to += t_interleaved / 4;
	}

out:
#if !defined(NDEBUG) && defined(CONFIG_TRACE_THREAD_TIMEO)
	if (self->flags & THREAD_STAT_GC)
		MSG_log(M_SCHEDULER, LOG_TRACE,
			   "{\"%s\"/%p} *thread(%d) gets WAIT timeo : %ld ms. ncached(%d)\n",
			   core->desc, core, self, self->last_to, smcache_nl(core->cache_task));
	else
		MSG_log(M_SCHEDULER, LOG_TRACE,
			   "{\"%s\"/%p} thread(%d) gets WAIT timeo : %ld ms. \n",
			   core->desc, core, self, self->last_to);
#endif

	return self->last_to;
}

int
cpool_core_thread_entry(void *arg) 
{
	thread_t *self = (thread_t *)arg;
	cpool_core_t *core = self->core;
	
	cpool_core_thread_status_change(core, self, THREAD_STAT_JOIN);	
	do {
	#ifndef NDEBUG
		/**
		 * We use @n_reused to trace the thread's reused status 
		 */
		++ self->n_reused;
	#endif
		cpool_core_schedule(core, self);
		
		/**
		 * The exiting threads may be reused by our pool.
		 *  <see @cpool_core_add_threads for more details> 
		 */
		OSPX_pthread_mutex_lock(&core->mut);
		if (!self->run) 
			cpool_core_thread_status_changel_wrapper(core, self, THREAD_STAT_LEAVE);
		OSPX_pthread_mutex_unlock(&core->mut);
		
	} while (THREAD_STAT_LEAVE != (self->status & ~THREAD_STAT_INNER));
		
	INIT_thread_structure(core, self);
	smcache_add_limit(&core->cache_thread, self, -1);
	
	/** 
	 * We should decrease the references since we 
	 * have increased it in the @cpool_core_add_thread. 
	 */
	cpool_core_release_ex(core, 1, 0);
	
	return 0;
}

void
do_create_threads(thread_t *self) 
{
	int died, ok = 1, locked = 0;
	LIST_HEAD(exq);
	cpool_core_t *core = self->core;
	
	while (!list_empty(&self->thq)) {
		thread_t *thread = list_entry(self->thq.next, 
					thread_t, link_free);
		
		list_del(&thread->link_free);	
		
		died = THREAD_STAT_RM & thread->flags;
		/**
		 * Check whether the thread has been marked died, or
		 * we should kill it, and  We try to give a notification 
		 * to @cpool_core_adjust_wait 
		 */
	exerr:
		if (died || !ok) {
			if (!locked) {
				OSPX_pthread_mutex_lock(&core->mut);
				locked = 1;
			}
			
			/**
			 * We should check the thread status again if we
			 * detect that it has been marked died before 
			 */
			if (!(died && (THREAD_STAT_RM & thread->flags))) {
				-- core->n_qths;
				list_del(&thread->link);
			
				if (died) {
					if (!-- core->nthreads_dying)
						OSPX_pthread_cond_broadcast(&core->cond_ths);
				
				} else {
					-- core->nthreads_real_pool;
					-- core->nthreads_real_free;
				}

				/**
				 * We decrease the pool's reference, and then put the 
				 * thread object into the cache again 
				 */
				cpool_core_releasel(core, 0);
				list_add_tail(&thread->link_free, &exq);
				continue;
			}
			OSPX_pthread_mutex_unlock(&core->mut);
			
			/**
			 * If it goes here, it indicates that the thread has been
			 * marked died, and at the same time @cpool_core_add_threads is 
			 * called by some modules to create more threads to provide
			 * services, and in this case, @stpol_add_threads will decide
			 * to reuse it 
			 */
			locked = 0;
		}
	
		assert (ok);
		if (!thread->cattr->initialized) {
			if ((errno = OSPX_pthread_cond_init(&thread->cattr->cond))) {
				ok = 0;
				goto exerr;
			}
			thread->cattr->initialized = 1;
		}
	
		if ((errno = OSPX_pthread_create(&thread->thread_id, &core->thattr, 
				cpool_core_thread_entry, thread))) {
			ok = 0;
			goto exerr;
		}
	#ifndef NDEBUG	
		MSG_log(M_THREAD, LOG_DEBUG,
			"{\"%s\"%p} create thread(%d) @nthreads:%d\n",
			core->desc, core, thread, core->nthreads_real_pool);
	#endif		
	}
	if (locked)
		OSPX_pthread_mutex_unlock(&core->mut);
	
	/**
	 * If the exception queue is not empty, we deal with it 
	 */ 
	if (!list_empty(&exq)) { 
		do {
			thread_t *thread = list_entry(self->thq.next, 
					thread_t, link_free);
		
			list_del(&thread->link_free);	
			smcache_add(&core->cache_thread, thread);	
		} while (!list_empty(&exq));
		
		if (!ok)
			MSG_log2(M_CORE, LOG_ERR,
				"launch thread error:%s", OSPX_sys_strerror(errno));
	}
}

static inline int
cpool_core_gettask(cpool_core_t *core, thread_t *self)
{
	if (core->me->gettask(core->priv, self)) {
		/**
		 * Remove the GC flag
		 *
		 *  Optimize: jne unlikely(THREAD_STAT_GC .....
		 */
		if (likely(self->flags == 0)) 
			return 1;

		if (unlikely(THREAD_STAT_GC & self->flags) && self->task_type != TASK_TYPE_GC) 
			cpool_core_GC_leave(core, self);

		if (THREAD_STAT_FLUSH & self->flags)
			cpool_core_objs_local_flush(self);

		return 1;
	}
	
	/**
	 * We will leave some threads to do the GC even if the user has 
	 * requested to destroy the pool, and the last GC thread will be 
	 * responsible for destroying the pool 
	 */
	return cpool_core_GC_gettaskl(core, self);
}

void 
cpool_core_schedule(cpool_core_t *core, thread_t *self) 
{
	do {
		/**
		 * We try to select a task from the pending queue to execute, 
		 * and if we can not get one, we'll go to sleep 
		 */
		if (!cpool_core_gettask(core, self)) {
			/**
			 * We reset the thread's status before our going to sleep 
			 */
			if (!cpool_core_get_restto(core, self))
				cpool_core_thread_status_changel_wrapper(core, self, THREAD_STAT_FORCE_QUIT);
			
			else if (self->last_to != 1) {
				cpool_core_thread_status_changel_wrapper(core, self, THREAD_STAT_WAIT);		
				
				assert (core->paused || !core->npendings);
				/**
				 * Queue ourself if we have not gotten a task, and @cpool_core_wakeup_n_sleeping_threads 
				 * will be responsible for waking us up if it is neccessary.
				 */
				if (self->last_to > 0) 	
					errno = OSPX_pthread_cond_timedwait(&self->cattr->cond, 
								&core->mut, self->last_to);
				else
					errno = OSPX_pthread_cond_wait(&self->cattr->cond, &core->mut);
				/**
				 * Adjust the thread status if we have been woken up 
				 *    THREAD_STAT_WAIT -> |THREAD_STAT_TIMEDOUT
				 *                        |THREAD_STAT_FREE
				 */
				cpool_core_thread_status_changel(core, self, (ETIMEDOUT == errno) ? 
										THREAD_STAT_TIMEDOUT : THREAD_STAT_FREE);
			}
			OSPX_pthread_mutex_unlock(&core->mut);	
			continue;
		}

		/**
		 * Check whether there are system tasks for us, our threads are 
	 	 * responsible for creating the working threads in the background.
	     * see @cpool_core_add_threads for more details.
	     */
	    if (!list_empty(&self->thq)) 
		    do_create_threads(self);	
		
		/**
		 * Run the task if the task is not marked with DISPATCHED, and then 
		 * we call Method::finished to tell the user that the task has been 
		 * done completely 
		 */
		if (likely(self->task_type == TASK_TYPE_NORMAL)) {
			__curtask->task_run(__curtask);
			
			/**
			 * Notify the underlying pool that we have done the task
			 */
			core->me->finished(core->priv, self, __curtask, core->eReasons0);

		} else if (self->task_type == TASK_TYPE_DISPATCHED) {
			while (!list_empty(&self->dispatch_q)) {
				/**
				 * Pop up a task from the dispatching queue
				 */
				__curtask = list_first_entry(&self->dispatch_q, basic_task_t, link);
				list_del(&__curtask->link);
				assert (__curtask->task_err_handler);
				/**
				 * Schedule the task error handler
				 */
				__curtask->task_err_handler(__curtask, core->me->err_reasons(__curtask));

				/**
				 * Notify the underlying pool that we have done the task
				 */
				core->me->finished(core->priv, self, __curtask, core->eReasons1);
			}
		
		} else {
			assert (self->task_type == TASK_TYPE_GC);

			__curtask->task_run(__curtask);
			continue;
		}
	} while (self->run);
}

void 
cpool_core_ensure_servicesl(cpool_core_t *core, thread_t *self) 
{
	int64_t ntasks_pending = core->paused ? 0 : core->npendings;
	/**
	 * We do nothing if there are a few kind of threads listed below:
	 *  1. Threads that has been woken up by @tpool_wakeup_n_sleeping_threads 
	 *  2. Threads who is free now 
	 */
	if (core->n_qths_waked || core->nthreads_real_free ||
		(!core->n_qdispatchs && !ntasks_pending))
		return;
	
	/**
	 * We try to wake up the sleeping threads firstly rather than
	 * creating some new threads to provide services 
	 */
	if (!list_empty(&core->ths_waitq)) {
		assert (core->n_qths_wait > 0);
		
		/**
		 * Wake up a few threads to provide service 
		 */
		cpool_core_wakeup_n_sleeping_threadsl(core, min(2, core->limit_threads_create_per_time));
		return;
	}
		
	/**
	 * Verify the @maxthreads 
	 */
	if (core->maxthreads > core->nthreads_real_pool) {
		int nthreads = core->maxthreads - core->nthreads_real_pool;
		
		/**
		 * Acquire the number of pending tasks 
		 */
		if (core->n_qdispatchs) 
			ntasks_pending += 1;

		/**
		 * Compute the number of threads who should be created according 
		 * to @limit_threads_create_per_time and @ntasks_pending 
		 */
		if (nthreads > core->limit_threads_create_per_time)
			nthreads = core->limit_threads_create_per_time;

		if (nthreads > ntasks_pending) 
			nthreads = (int)ntasks_pending;
	
		/**
		 * Call @tpool_add_threads to create @nthreads threads to provide service 
		 */
		cpool_core_add_threadsl(core, self, nthreads);		
	}
}

int
cpool_core_add_threadsl(cpool_core_t *core, thread_t *self, int nthreads) 
{
	int n;
	thread_t *thread = NULL;
	
	/**
	 * To improve the perfermance , we scan the thread queue to try to reuse the dying threads 
	 */
	if (core->nthreads_dying && nthreads > 0) {
	 	list_for_each_entry(thread, &core->ths, thread_t, link) {	
			VERIFY(core, thread);

			if (!(THREAD_STAT_RM & thread->flags)) 
				continue;

			thread->run = 1;
			thread->flags &= ~THREAD_STAT_RM;
			++ core->nthreads_real_pool;
			-- core->nthreads_dying;

			switch (thread->status) {
			case THREAD_STAT_WAIT:
				++ core->nthreads_real_sleeping;
				
				/** 
				 * We wake up it 
				 */
				if (!thread->b_waked) {
					thread->b_waked = 1;
					++ core->n_qths_waked;
					-- core->n_qths_wait;
					list_del(&thread->run_link);
					OSPX_pthread_cond_signal(&thread->cattr->cond);
				}
				break;
			case THREAD_STAT_RUN:
				-- core->nthreads_dying_run;
				break;
			case THREAD_STAT_FORCE_QUIT:
				/**
				 * We reset the thread's status if it is in @QUIT status,
				 * or it may triggle the assertion in the DEBUG mode.
				 * 	(@cpool_core_thread_status_changel:
				 * 		assert (status != self->status)
				 *  ) 
				 */
				thread->status = THREAD_STAT_FREE;

			default:
				++ core->nthreads_real_free;
				break;
			}
			VERIFY(core, thread);

			if (!-- nthreads || !core->nthreads_dying)
				break;
		}	
	}
	VERIFY(core, self);
		
	/**
	 * Actually, In order to reduce the time to hold the global lock,
	 * we'll try to just add the threads structure into the threads sets, 
	 * and then we call @pthread_create in the background. 
	 */
	for (n=0; n<nthreads; n++) {
		if (!(thread = smcache_get(&core->cache_thread, 1))) {
			MSG_log2(M_CORE, LOG_ERR,
				"smcache_get:None.");
			goto over;
		}
		assert (THREAD_STAT_INIT == thread->status && thread->run);
		
		/**
		 * If @cpool_core_add_thread is not called by threads, we just create 
		 * the wokring threads here directly, or we just assign the task 
		 * to the threads who triggles this function 
		 */
		if (!self) {
			if (!thread->cattr->initialized) {
				if ((errno = OSPX_pthread_cond_init(&thread->cattr->cond))) {
					MSG_log2(M_CORE, LOG_ERR,
						"cond_init:%s.", OSPX_sys_strerror(errno));
					goto over;
				}
				thread->cattr->initialized = 1;
			}

			if ((errno = OSPX_pthread_create(&thread->thread_id, &core->thattr, 
								cpool_core_thread_entry, thread))) {
				MSG_log2(M_CORE, LOG_ERR,
					"launch thread:%s.", OSPX_sys_strerror(errno));
				goto over;
			}
			#ifndef NDEBUG	
				MSG_log(M_THREAD, LOG_DEBUG,
					"{\"%s\"/%p} create thread(%d) @nthreads:%d\n", 
					core->desc, core, thread, core->nthreads_real_pool + n + 1);
			#endif		
		} else  
			list_add_tail(&thread->link_free, &self->thq);	
		
		list_add_tail(&thread->link, &core->ths);
		
		/**
		 * We increase the reference to make sure that 
		 * the task pool object will always exist before 
		 * the service thread's exitting. 
		 */
		cpool_core_addrefl(core, 0);
		VERIFY(core, thread);
	}
over:
	/**
	 * If we fail to create the working threads for some reasons,
	 * we should clean the resources 
	 */
	if (nthreads != n && thread) 
		smcache_add(&core->cache_thread, thread);
	
	core->n_qths += n;
	core->nthreads_real_free += n;
	core->nthreads_real_pool += n;
	
	/**
	 * Update the statics report 
	 */
	if (core->nthreads_peak < core->nthreads_real_pool) 
		core->nthreads_peak = core->nthreads_real_pool;
	
	/**
	 * Update the timer 
	 */
	core->crttime = (unsigned)time(NULL);
	
	VERIFY(core, self);

	return n;
}

int 
cpool_core_dec_threadsl(cpool_core_t *core, int nthreads)
{
	thread_t *thread;
	int ndied = 0, nrunthreads_dec = 0;
	int nthreads_real_running, nthreads_real_others;
		
	if (nthreads <= 0 || !core->nthreads_real_pool)
		return 0;

	nthreads = min(core->nthreads_real_pool, nthreads);
	
	nthreads_real_running = core->nthreads_running - core->nthreads_dying_run;
	nthreads_real_others = core->nthreads_real_pool - nthreads_real_running;
	assert (nthreads_real_others >= 0);
	
	/**
	 * Record the counter of running threads who is should 
	 * be stopped providing services 
	 */
	if (nthreads_real_others < nthreads)
		nrunthreads_dec = nthreads - nthreads_real_others;
	
	/**
	 * Mark some threads died to decrease the service threads,
	 * after doing their jobs the threads will quit imediately
	 * if they find that they have been marked died.
	 */
	list_for_each_entry(thread, &core->ths, thread_t, link) {	
		VERIFY(core, thread);
		
		/**
		 * Skip the threads who has been marked died 
		 */
		if (THREAD_STAT_RM & thread->flags) 
			continue;	
		
		if (THREAD_STAT_RUN != thread->status) { 
			-- nthreads;	
			thread->run = 0;
			
			/**
			 * If the thread is sleeping, we wake it up 
			 */
			if (THREAD_STAT_WAIT == thread->status) {
				-- core->nthreads_real_sleeping;

				if (!thread->b_waked) {
					thread->b_waked = 1;
					++ core->n_qths_waked;
					-- core->n_qths_wait;
					list_del(&thread->run_link);
					OSPX_pthread_cond_signal(&thread->cattr->cond);
				}
			
			} else
				-- core->nthreads_real_free;
		} else if (nrunthreads_dec) {
			-- nrunthreads_dec;;
			
			thread->run = 0;
			++ core->nthreads_dying_run;
		}

		if (!thread->run) {
			thread->flags |= THREAD_STAT_RM;
			-- core->nthreads_real_pool;
			++ core->nthreads_dying;
			++ ndied;
		}
		VERIFY(core, thread);

		if (!nthreads && !nrunthreads_dec)
			break;
	}
	VERIFY(core, NULL);

	return ndied;
}

void 
cpool_core_suspendl(cpool_core_t *core)
{
	if (!core->paused) {
		core->paused = 1;	
		
		MSG_log(M_CORE, LOG_INFO,
			"{\"%s\"/%p} Suspend. <npendings:%d nrunnings:%d nremovings:%d> @nthreads:%d>\n", 
			core->desc, core, core->npendings, core->nthreads_running, core->n_qdispatchs, core->n_qths);

		if (core->me->notifyl)
			core->me->notifyl(core->priv, eEvent_F_core_suspend);
	}
}

void 
cpool_core_resume(cpool_core_t *core) 
{	
	OSPX_pthread_mutex_lock(&core->mut);	
	if (core->paused && (CORE_F_created & core->status)) {
		core->paused = 0;
		
		MSG_log(M_CORE, LOG_INFO,
			"{\"%s\"/%p} Resume. <npendings:%d nrunnings:%d nremovings:%d> @nthreads:%d>\n", 
			core->desc, core, core->npendings, core->nthreads_running, core->n_qdispatchs, core->n_qths);
	
		if (core->me->notifyl)
			core->me->notifyl(core->priv, eEvent_F_core_resume);

		/**
		 * Notify the server threads that we are ready now. 
		 */
		if (core->npendings) 
			cpool_core_ensure_servicesl(core, NULL);
	}
	OSPX_pthread_mutex_unlock(&core->mut);
}

void
cpool_core_try_GC(cpool_core_t *core) 
{
	int flushable;

	OSPX_pthread_mutex_lock(&core->mut); 
	if (unlikely(!core->nthreads_real_free && core->nthreads_running == core->nthreads_dying_run)) {
		if (smcache_auto_lock(core->cache_task))
			smcache_lock(core->cache_task);

		flushable = smcache_flushablel(core->cache_task, core->cattr.nGC_wakeup);
		
		if (smcache_auto_lock(core->cache_task))
			smcache_unlock(core->cache_task);
		
		if (flushable) {
			if (core->nthreads_real_sleeping) 
				cpool_core_wakeup_n_sleeping_threadsl(core, 1); 
			else 
				cpool_core_add_threadsl(core, NULL, 1);
		}
	} 
	OSPX_pthread_mutex_unlock(&core->mut); 
}

void 
cpool_core_thread_status_changel_wrapper(cpool_core_t *core, thread_t *self, long status)
{
	cpool_core_thread_status_changel(core, self, status);
}

void 
cpool_core_thread_status_change(cpool_core_t *core, thread_t *self, long status)
{
	OSPX_pthread_mutex_lock(&core->mut);
	cpool_core_thread_status_changel(core, self, status);
	OSPX_pthread_mutex_unlock(&core->mut);
}

#ifndef NDEBUG
void 
VERIFY(cpool_core_t *core, thread_t *self) 
{
	/** @nthreads_dying (WAKED|RM, ...) */
	assert (core->n_qths <= core->n_qths_wait + 
		core->n_qths_waked + core->nthreads_running + 
		(core->nthreads_dying - core->nthreads_dying_run) + 
		core->nthreads_real_free); 
		
	assert (core->nthreads_real_pool == core->nthreads_real_sleeping + 
		core->nthreads_real_free + (core->nthreads_running - 
		core->nthreads_dying_run)); 
	
	assert (core->n_qths_waked >= 0 && core->n_qths_wait <= core->n_qths &&
	        core->n_qths_waked <= core->n_qths);

	if (self)
		assert ((!self->run && (self->flags & THREAD_STAT_RM)) ||
				(self->run && !(self->flags & THREAD_STAT_RM)));
}
#endif
