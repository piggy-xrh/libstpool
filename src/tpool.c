/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <math.h>

#include "ospx.h"
#include "tpool.h"
#include "msglog.h"
#include "ospx_errno.h"
#include "cache_creater.h"

#ifdef _WIN
	#define PRI64 "%I64"
#else	
	#define PRI64 "%ll"
#ifndef NDEBUG
#include <sys/prctl.h>
#endif
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) < (b) ? (b) : (a))
#endif

#define unlikely(exp) exp
#define likely(exp)   exp
#define M_POOL "pool"
/*------------------------------------APIs about the task-----------*/
void 
tpool_task_setschattr(struct task_t *ptsk, struct xschattr_t *attr) 
{
	/* Correct the priority param */
	if (attr->pri < 0)
		attr->pri = 0;
	if (attr->pri > 99)
		attr->pri = 99;
	
	/* If the task's scheduling attribute is not permanent,
	 * It'll be reset at the next scheduling time */
	if (!attr->permanent) 
		ptsk->f_mask |= TASK_F_PRI_ONCE;	
	else
		ptsk->f_mask &= ~TASK_F_PRI_ONCE;
	
	/* If the task has a zero priority, We just push the task
	 * into the lowest priority queue */
	if (!attr->pri_policy || (!attr->pri && P_SCHE_BACK == attr->pri_policy)) {
		ptsk->f_mask &= ~(TASK_F_PRI|TASK_F_ADJPRI);
		ptsk->f_mask |= TASK_F_PUSH;
		ptsk->pri = 0;
		ptsk->pri_q = 0;
	
	} else {
		/* We set the task with TASK_F_ADJPRI, The pool will 
		 * choose a propriate priority queue to queue the task
		 * when the user calls @tpool_add_task/routine */
		ptsk->f_mask |= (TASK_F_PRI|TASK_F_ADJPRI);
		ptsk->f_mask &= ~TASK_F_PUSH;
		ptsk->pri = attr->pri;	
	}
	ptsk->pri_policy = attr->pri_policy;	
}

void 
tpool_task_getschattr(struct task_t *ptsk, struct xschattr_t *attr) 
{
	attr->pri = ptsk->pri;
	attr->pri_policy = ptsk->pri_policy;
	
	if (ptsk->f_mask & TASK_F_PRI_ONCE)
		attr->permanent = 0;
	else
		attr->permanent = 1;
}

/*------------------------------------APIs about the pool-----------------------------------*/
static long tpool_release_ex(struct tpool_t *pool, int decrease_user, int wait_threads_on_clean);
static void tpool_add_taskl(struct tpool_t *pool, struct task_t *ptsk);
static void tpool_task_complete(struct tpool_t *pool, struct tpool_thread_t *self, struct task_t *ptsk, int task_code); 
static int  tpool_add_threads(struct tpool_t *pool, struct tpool_thread_t *th, int nthreads); 
static int  tpool_dec_threads(struct tpool_t *pool, int nthreads);
static inline void tpool_ensure_services(struct tpool_t *pool, struct tpool_thread_t *self);
static void tpool_schedule(struct tpool_t *pool, struct tpool_thread_t *self);

#ifndef NDEBUG 
static inline void 
VERIFY(struct tpool_t *pool, struct tpool_thread_t *self) {
	/* @nthreads_dying (WAKED|RM, ...) */
	assert (pool->n_qths <= pool->n_qths_wait + 
		pool->n_qths_waked + pool->nthreads_running + 
		(pool->nthreads_dying - pool->nthreads_dying_run) + 
		pool->nthreads_real_free); 
		
	assert (pool->nthreads_real_pool == pool->nthreads_real_sleeping + 
		pool->nthreads_real_free + (pool->nthreads_running - 
		pool->nthreads_dying_run)); 
	
	assert (pool->n_qths_waked >= 0); 

	if (self)
		assert ((!self->run && (self->flags & THREAD_STAT_RM)) ||
				(self->run && !(self->flags & THREAD_STAT_RM)));
}
#else
#define VERIFY(pool, thread) 
#endif

static inline long
tpool_addrefl(struct tpool_t *pool, int increase_user) 
{
	++ pool->ref; 

	if (increase_user) 
		++ pool->user_ref;
	
	return pool->user_ref;
}

static inline long 
tpool_releasel(struct tpool_t *pool, int decrease_user) 
{
	-- pool->ref;
	if (decrease_user)
		-- pool->user_ref;
	
	return pool->user_ref;
}

static void inline
tpool_setstatus(struct tpool_t *pool, long status, int synchronized)
{
#ifndef NDEBUG
	time_t n = time(NULL);

	MSG_log(M_POOL, LOG_TRACE, 
		"Pool(\"%s\"/%p) [0x%ld ==> 0x%ld] at %s",
		pool->desc, pool, pool->status, status, ctime(&n));
#endif
	if (synchronized)
		pool->status = status;
	else {
		OSPX_pthread_mutex_lock(&pool->mut);
		pool->status = status;
		OSPX_pthread_mutex_unlock(&pool->mut);
	}	
}

static int
tpool_GC_run(struct task_t *ptsk) 
{
	struct tpool_thread_t *thread = ptsk->thread;
	
	/* Currently, we do not need the completion routine
	 * for the GC task, so the scheduling codes for the 
	 * GC have been removed */
	assert (!ptsk->task_complete);

#if !defined(NDEBUG) && !defined(_WIN)
	prctl(PR_SET_NAME, ptsk->task_name);
#endif
	if (thread->ncache_limit < 0) {
		smlink_q_t dummy_q;
		
		/* If @ncache_limit is negative, it tell us that
		 * we should reset the GC cache */
		smcache_reset(thread->pool->cache_task, &dummy_q);
		return thread->ncache_limit;
	}

	return smcache_flush(thread->pool->cache_task, thread->ncache_limit);	
}

int  
tpool_create(struct tpool_t  *pool, const char *desc, int q_pri, int maxthreads, int minthreads, int suspend) 
{
	int  error, index = 15;
	struct cache_attr_t attr = {
		40    /* cache 40 objects */,  
		80    /* wakeup throttle: 80 */,
		50    /* GC 50 objects one at a time */, 
		1000  /* interleaved time: 1000 ms */, 
		2500  /* delay time: 2500 ms */
	};

	/* Connrect the param */
	if (maxthreads <=0)
		maxthreads = 1;
	if (minthreads <= 0)
		minthreads = 0;
	minthreads = min(minthreads, maxthreads);

	/* We reset the memory */
	memset(pool, 0, sizeof(*pool));
	pool->desc = desc;
	pool->tpool_created = time(NULL);
	
	/* If the os supports recursive mutex, it will be more convenient 
	 * for users to use our APIs. 
	 */
	if ((errno = OSPX_pthread_mutex_init(&pool->mut, 1))) {
		MSG_log2(M_POOL, LOG_ERR,
			"get RECURSIVE MUTEX:%s.", OSPX_sys_strerror(errno));
		
		if ((errno = OSPX_pthread_mutex_init(&pool->mut, 0))) {
			MSG_log2(M_POOL, LOG_ERR,
				"mutex_init:%s.", OSPX_sys_strerror(errno));
			return POOL_ERR_ERRNO;
		}
	}
	tpool_setstatus(pool, POOL_F_CREATING, 0);
	{
		/* Initialize the task cache for @tpool_add_routine */
		if (objpool_ctor2(&pool->objp_task, "FObjp-C-Local-routine", sizeof(struct task_t), 0,
				attr.nGC_cache, &pool->mut)) {
			MSG_log2(M_POOL, LOG_ERR,
				"Fail to execute objpool_ctor.");
			goto errout;
		}
		pool->cache_task = objpool_get_cache(&pool->objp_task);

		/* Initialize the GC task */
		tpool_task_init(&pool->sys_GC_task, "sys/GC", tpool_GC_run, NULL, NULL);	
	}
	
	{
		/* Preallocate memory for threads. */
		smcache_init2(&pool->cache_thread, "thread-cache", index, NULL, 0, pool,
			thread_obj_create, thread_obj_destroy, thread_obj_need_destroy);
		pool->buffer = calloc(1, index * (sizeof(struct tpool_thread_t) + 
			sizeof(struct cond_attr_t)));
		if (pool->buffer) {
			struct tpool_thread_t *ths;
			char *addr = pool->buffer;

			for (--index; index>=0; --index) {
				ths = (struct tpool_thread_t *)addr;
				INIT_thread_structure(pool, ths);
				ths->cattr = (struct cond_attr_t *)(ths + 1);
				smcache_add(&pool->cache_thread, ths);
				addr += sizeof(struct tpool_thread_t) + sizeof(struct cond_attr_t);
			}
		}
	}

	/* Initialize the queue */
	INIT_LIST_HEAD(&pool->wq);
	INIT_LIST_HEAD(&pool->ths);
	INIT_LIST_HEAD(&pool->ths_waitq);
	INIT_LIST_HEAD(&pool->ready_q);
	INIT_LIST_HEAD(&pool->trace_q);
	INIT_LIST_HEAD(&pool->dispatch_q);
	
	error = POOL_ERR_ERRNO;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_comp)))
		goto errout;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_ths)))
		goto free_cond_comp;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_ev))) 
		goto free_cond_ths;
	
	/* Initialize the default env */
	pool->ref = pool->user_ref = 1;
	pool->paused = suspend;
	pool->maxthreads = maxthreads;
	pool->minthreads = minthreads;
	pool->limit_cont_completions = max(10, pool->maxthreads * 2 / 3);
	pool->throttle_enabled = 0;
	pool->acttimeo  = 1000 * 20;
	pool->randtimeo = 1000 * 30;
	pool->npendings_ev = -1;
	
	/* Initiailize the priority queue */
	if (q_pri <= 0)
		q_pri = 1;
	if (q_pri > 99)
		q_pri = 99;
	pool->pri_q_num = q_pri;
	pool->pri_q = malloc(sizeof(struct tpool_priq_t) * pool->pri_q_num);
	if (!pool->pri_q) {
		errno = ENOMEM;
		goto free_cond_ev;
	}

	for (index=0; index<pool->pri_q_num; index++) {
		INIT_LIST_HEAD(&pool->pri_q[index].task_q);
		pool->pri_q[index].index = index;
	}
	pool->avg_pri = 100 / pool->pri_q_num;
	tpool_setstatus(pool, POOL_F_CREATED, 0);
	
	{
		time_t n = time(NULL);

		MSG_log(M_POOL, LOG_WARN,
				"Pool(\"%s\"/%p) starts up at %s",
				pool->desc, pool, ctime(&n));
	}

	/* Load the variables from the environment */
	tpool_load_env(pool);
	tpool_adjust_cache(pool, &attr, NULL);

	MSG_log(M_POOL, LOG_INFO,
			">@limit_threads_create_per_time=%d\n",
			pool->limit_threads_create_per_time);
	
	/* Start up the reserved threads */
	pool->thattr.stack_size = 1024 * 1024 * 2;
	pool->thattr.sche_policy = ep_NONE;
	
	if (pool->minthreads > 0) {
		OSPX_pthread_mutex_lock(&pool->mut);
		tpool_add_threads(pool, NULL, pool->minthreads);
		OSPX_pthread_mutex_unlock(&pool->mut);
	}
	
	return 0;

free_cond_ev:
	OSPX_pthread_cond_destroy(&pool->cond_ev);
free_cond_ths:
	OSPX_pthread_cond_destroy(&pool->cond_ths);
free_cond_comp:	
	OSPX_pthread_cond_destroy(&pool->cond_comp);
errout:
	tpool_setstatus(pool, POOL_F_DESTROYED, 0);
	OSPX_pthread_mutex_destroy(&pool->mut);	
	
	MSG_log2(M_POOL, LOG_ERR,
		"Err:%s", OSPX_sys_strerror(errno));
	return error;
}

void
tpool_atexit(struct tpool_t *pool, void (*atexit_func)(struct tpool_t *, void *), void *arg) 
{
	assert (POOL_F_CREATED & pool->status);
	pool->atexit = atexit_func;
	pool->atexit_arg = arg;
}

void
tpool_adjust_cache(struct tpool_t *pool, struct cache_attr_t *attr, struct cache_attr_t *oattr) 
{
	OSPX_pthread_mutex_lock(&pool->mut);
	if (oattr)
		*oattr = pool->cattr;
	
	if (attr)
		pool->cattr = *attr;
	OSPX_pthread_mutex_unlock(&pool->mut);
}

void 
tpool_load_env(struct tpool_t *pool) 
{
	const char *env;
		
	/* Load the @limit_threads_create_per_time */
	env = getenv("LIMIT_THREADS_CREATE_PER_TIME");
	if (!env || atoi(env) <= 0)	
		pool->limit_threads_create_per_time = 1;
	else
		pool->limit_threads_create_per_time = atoi(env);
}

static inline void 
tpool_wakeup_n_sleeping_threads(struct tpool_t *pool, int nwake) 
{
	/* Scan the sleeping threads queue to wake up a few threads
	 * to provide services */
	while (!list_empty(&pool->ths_waitq)) {
		struct tpool_thread_t *thread = container_of(pool->ths_waitq.next,
				struct tpool_thread_t, run_link); 
		
		assert (!thread->b_waked);

		thread->b_waked = 1;
		list_del(&thread->run_link);
		OSPX_pthread_cond_signal(&thread->cattr->cond);
		
		-- pool->n_qths_wait;
		++ pool->n_qths_waked;
		if (!-- nwake)
			break;
	}	
	
	VERIFY(pool, NULL);
}

static void 
tpool_free(struct tpool_t *pool) 
{	
	assert (list_empty(&pool->ths) &&
	       list_empty(&pool->trace_q) &&
		   !pool->nthreads_running &&
		   !pool->nthreads_real_sleeping &&
		   !pool->npendings && 
		   !pool->ndispatchings &&
		   !pool->n_qtrace && !pool->n_qdispatch); 	
	
	VERIFY(pool, NULL);
	/* Free the priority queue */
	free(pool->pri_q);
	
	/* Destroy the task cache */
	objpool_dtor(&pool->objp_task);
	
	/* Destroy the thread cache */
	{
		struct tpool_thread_t *thread;

		smcache_reset(&pool->cache_thread, NULL);
		do {
			thread = (struct tpool_thread_t *)smcache_get(&pool->cache_thread, 0);
			if (thread) {
				assert (!thread->structure_release);
				if (thread->cattr->initialized)
					OSPX_pthread_cond_destroy(&thread->cattr->cond);
			}
		} while (thread);
		smcache_deinit(&pool->cache_thread);
		
		/* Free the preallocated memory */
		if (pool->buffer)
			free(pool->buffer);
	}
	
	OSPX_pthread_mutex_destroy(&pool->mut);
	OSPX_pthread_cond_destroy(&pool->cond_comp);
	OSPX_pthread_cond_destroy(&pool->cond_ths);
	OSPX_pthread_cond_destroy(&pool->cond_ev);
}


static long 
tpool_release_ex(struct tpool_t *pool, int decrease_user, int wait_threads_on_clean)
{
	long user_ref, clean = 0;

	OSPX_pthread_mutex_lock(&pool->mut);
	user_ref = tpool_releasel(pool, decrease_user);
	if (decrease_user) {
		if (0 == user_ref) {
			struct task_t *ptsk;
		#ifndef NDEBUG
			{
				time_t now = time(NULL);

				MSG_log(M_POOL, LOG_INFO,
					"Pool(\"%s\"/%p) is being destroyed ... %s\n",
					pool->desc, pool, ctime(&now));
			}
		#endif	
		    /* Note: Tasks can not be added into the pending queue
			 * any more if the pool is being destroyed.
			 */
			tpool_setstatus(pool, POOL_F_DESTROYING, 1);		
			
			/* Notify all tasks that the pool is being destroyed now */
			list_for_each_entry(ptsk, &pool->trace_q, struct task_t, trace_link) {	
				ptsk->f_vmflags |= TASK_VMARK_POOL_DESTROYING;
			}
								
			/* We create service threads to dispatch the task in the 
			 * background if there are one more tasks existing in the 
			 * pool. */
			if (pool->paused && pool->npendings) {
				OSPX_pthread_mutex_unlock(&pool->mut);
				tpool_remove_pending_task(pool, 1);
				OSPX_pthread_mutex_lock(&pool->mut);
			}

			if (list_empty(&pool->ths) || wait_threads_on_clean) {
				/* Tell the pool that we are responsible for 
				 * releasing the resources. */
				clean = 1;
				pool->release_cleaning = 1;
			}

			/* Wake up all working threads */
			tpool_wakeup_n_sleeping_threads(pool, -1);
			if (wait_threads_on_clean) {	
				for (;!list_empty(&pool->ths);) 
					OSPX_pthread_cond_wait(&pool->cond_ths, &pool->mut);	
			}
		}
	} else if (!pool->release_cleaning)
		clean = (0 == pool->ref);	
	OSPX_pthread_mutex_unlock(&pool->mut);
		
	if (clean) {
		/* We delete the pool object if its reference is zero */
		assert ((0 == user_ref) && (decrease_user || pool->ref == 0));
		assert (list_empty(&pool->ths)); 	
		tpool_setstatus(pool, POOL_F_DESTROYED, 0);		
		
		/* DEBUG check */
		assert (list_empty(&pool->trace_q));
					
		/* Now we can free the pool env safely */
		{
			time_t n;
	
#ifndef NDEBUG
			MSG_log(M_POOL, LOG_INFO,
				"\n%s\n", tpool_status_print(pool, NULL, 0));
#endif
			
			n = time(NULL);
			
			MSG_log(M_POOL, LOG_WARN,
				"Pool(\"%s\"/%p) is destroyed at %s",
				pool->desc, pool, ctime(&n));
		}
		/* If there are huge number of task objects existing in
		 * the cache, we have a little break after our's recycling
		 * part of them to ensure that the CPU runs at a low speed */ 
		if (!decrease_user && smcache_nl(pool->cache_task) >= 15000) {
			int n;
			
			for (;;) {
				n = smcache_nl(pool->cache_task) - 3000;
				if (!smcache_flushablel(pool->cache_task, n))
					break;
				smcache_flush(pool->cache_task, n);
				OSPX_msleep(1);
			}
		}
		tpool_free(pool);
		
		/* Call the exit function */
		if (pool->atexit) 
			pool->atexit(pool, pool->atexit_arg);
	}

	return decrease_user ? user_ref : pool->ref;
}

long 
tpool_addref(struct tpool_t *pool) 
{
	long ref = 0;
	
	OSPX_pthread_mutex_lock(&pool->mut);
	if (POOL_F_CREATED & pool->status)
		ref = tpool_addrefl(pool, 1);
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return ref;
}

long 
tpool_release(struct tpool_t *pool, int clean_wait) 
{
	return tpool_release_ex(pool, 1, clean_wait);	
}

void 
tpool_set_activetimeo(struct tpool_t *pool, long acttimeo, long randtimeo) 
{
	pool->acttimeo = acttimeo * 1000;
	pool->randtimeo = randtimeo * 1000;
}

struct tpool_stat_t *
tpool_getstat(struct tpool_t *pool, struct tpool_stat_t *stat) 
{	
	memset(stat, 0, sizeof(*stat));
	
	stat->desc = pool->desc;
	stat->created = pool->tpool_created;
	stat->pri_q_num = pool->pri_q_num;
	stat->acttimeo = pool->acttimeo;
	stat->randtimeo = pool->randtimeo;
	
	OSPX_pthread_mutex_lock(&pool->mut);	
	stat->ref = pool->user_ref;
	stat->throttle_enabled = pool->throttle_enabled;
	stat->suspended = pool->paused;
	stat->maxthreads = pool->maxthreads;
	stat->minthreads = pool->minthreads;
	stat->curthreads = pool->n_qths;
	stat->curthreads_active = pool->nthreads_running;
	stat->curthreads_dying  = pool->nthreads_dying;
	stat->threads_peak = (size_t)pool->nthreads_peak;
	stat->tasks_peak = (size_t)pool->ntasks_peak;
	stat->tasks_added = pool->ntasks_added;
	stat->tasks_done  = pool->ntasks_done;
	stat->tasks_dispatched = pool->ntasks_dispatched;
	stat->cur_tasks = (size_t)pool->n_qtrace;
	stat->cur_tasks_pending = (size_t)(pool->npendings - pool->n_qdispatch);
	stat->cur_tasks_scheduling = pool->nthreads_running;
	stat->cur_tasks_removing = pool->ndispatchings;
	OSPX_pthread_mutex_unlock(&pool->mut);	
	
	return stat;
}

const char *
tpool_status_print(struct tpool_t *pool, char *buffer, size_t bufferlen) 
{
	static char sbuffer[490] = {0};
	struct tm *p_tm;
	struct tpool_stat_t stat;
#ifdef _WIN
	#define snprintf _snprintf
#endif
	if (!buffer) {
		buffer = sbuffer;
		bufferlen = sizeof(sbuffer);
	}
	tpool_getstat(pool, &stat);
	p_tm = localtime(&stat.created);
	snprintf(buffer, bufferlen, 
			"    desc  : \"%s\" (%p)\n"
			"   created: %04d-%02d-%02d %02d:%02d:%02d\n"
			"  user_ref: %ld\n"
			" pri_q_num: %d\n"
			"  throttle: %s\n"
			" suspended: %s\n"
			"maxthreads: %d\n"
			"minthreads: %d\n"
			"threads_current: %d\n"
			" threads_active: %d\n"
			" threads_dying : %d\n"
			" threads_actto : %.2f/%.2f (s)\n"
			" threads_peak  : %u\n"
			"   tasks_peak  : %u\n"
			"tasks_added: %u\n"
			" tasks_done: %u\n"
			"tasks_dispatched: %u\n"
			"  cur_tasks: %u\n"
			"cur_tasks_pending: %u\n"
			"cur_tasks_scheduling: %u\n"
			"cur_tasks_removing: %u\n",
			stat.desc, pool,
			p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
			p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec,
			stat.ref,
			stat.pri_q_num,
			stat.throttle_enabled ? "on" : "off",
			stat.suspended ? "yes" : "no",
			stat.maxthreads,
			stat.minthreads,
			stat.curthreads,
			stat.curthreads_active,
			stat.curthreads_dying,
			(double)stat.acttimeo / 1000,
			(double)stat.randtimeo / 1000,
			stat.threads_peak,
			stat.tasks_peak,
			stat.tasks_added,
			stat.tasks_done,
			stat.tasks_dispatched,
			stat.cur_tasks,
			stat.cur_tasks_pending,
			stat.cur_tasks_scheduling,
			stat.cur_tasks_removing
		);
	return buffer;
}

#define ACQUIRE_TASK_STAT(pool, ptsk, st) \
	do {\
		/* If f_removed has been set, it indicates 
		 * that the task is being dispatching.
		 */\
		if (TASK_STAT_WAIT & (ptsk)->f_stat && (pool)) \
			(st)->stat = (pool)->paused ? TASK_STAT_SWAPED : \
				TASK_STAT_WAIT;\
		else\
			(st)->stat = (ptsk)->f_stat;\
		if ((st)->stat && (TASK_VMARK_DO_AGAIN & (ptsk)->f_vmflags))\
			(st)->stat |= TASK_STAT_WAIT_PENDING;\
		(st)->vmflags = (ptsk)->f_vmflags;\
		(st)->task = (ptsk);\
		(st)->pri  = (ptsk)->pri;\
	} while (0)


long 
tpool_gettskstat(struct tpool_t *pool, struct tpool_tskstat_t *st) 
{	
	assert (!st->task->hp || pool == st->task->hp);
	
	if (!st->task->hp) 
		ACQUIRE_TASK_STAT(st->task->hp, st->task, st);
	else {
		OSPX_pthread_mutex_lock(&pool->mut);
		ACQUIRE_TASK_STAT(pool, st->task, st);
		OSPX_pthread_mutex_unlock(&pool->mut);
	}

	return st->stat;
}

static inline void 
tpool_adjust_abs_l(struct tpool_t *pool, int maxthreads, int minthreads) 
{
	int nthreads;
	
	MSG_log(M_POOL, LOG_INFO,
		"threads_configure(%d,%d) ... (\"%s\"/%p) \n",
		maxthreads, minthreads, pool->desc, pool);

	/* Verify the params */	
	assert (maxthreads >= 1 && minthreads >= 0 && 
			maxthreads >= minthreads);

	if (!((POOL_F_CREATED|POOL_F_DESTROYING) & pool->status) ||
		((POOL_F_DESTROYING & pool->status) && (maxthreads > 0 || 
		minthreads > 0))) {
		MSG_log(M_POOL, LOG_WARN,
			"Action is not supported in current status\n");		
		return;
	}
	pool->maxthreads = maxthreads;
	pool->minthreads = minthreads;
	
	if (pool->minthreads > pool->nthreads_real_pool) {
		nthreads = pool->minthreads - pool->nthreads_real_pool;
		tpool_add_threads(pool, NULL, nthreads);
	
	} else if (pool->nthreads_real_pool > pool->maxthreads) {
		nthreads = pool->nthreads_real_pool - pool->maxthreads; 
		tpool_dec_threads(pool, nthreads);
	
	} else	
		/* We wakeup all of the sleeping threads */
		tpool_wakeup_n_sleeping_threads(pool, -1);	
	
	assert (pool->nthreads_real_pool == pool->nthreads_real_sleeping +
			pool->nthreads_real_free + (pool->nthreads_running - 
			pool->nthreads_dying_run));	

	/* Reset the statics report */
	pool->limit_cont_completions = max(10, pool->maxthreads * 2 / 3);	
	pool->nthreads_peak = pool->nthreads_real_pool;
	pool->ntasks_peak   = (int)pool->npendings;
}

void 
tpool_adjust_abs(struct tpool_t *pool, int maxthreads, int minthreads) 
{	
	if (minthreads >= 0 && maxthreads >= minthreads) {	
		OSPX_pthread_mutex_lock(&pool->mut);	
		tpool_adjust_abs_l(pool, maxthreads, minthreads);
		OSPX_pthread_mutex_unlock(&pool->mut);
	}
}

void 
tpool_adjust(struct tpool_t *pool, int maxthreads, int minthreads) 
{
	OSPX_pthread_mutex_lock(&pool->mut);
	maxthreads += pool->maxthreads;
	minthreads += pool->minthreads;
	
	/* Correct the param */	
	if (maxthreads <= 0)
		maxthreads = 1;

	if (minthreads <= 0)
		minthreads = 0;
	minthreads = min(maxthreads, minthreads);	
	tpool_adjust_abs_l(pool, maxthreads, minthreads);
	OSPX_pthread_mutex_unlock(&pool->mut);
}

int
tpool_flush(struct tpool_t *pool) 
{
	int ndecs;
	
	MSG_log(M_POOL, LOG_INFO,
		"Flushing ... (\"%s\"/%p) \n",
		pool->desc, pool);

	OSPX_pthread_mutex_lock(&pool->mut);
	ndecs = pool->nthreads_real_pool - pool->minthreads;
	ndecs = tpool_dec_threads(pool, ndecs);
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return ndecs;
}

void 
tpool_adjust_wait(struct tpool_t *pool) 
{	
	OSPX_pthread_mutex_lock(&pool->mut);
	for (;pool->nthreads_dying;)
		OSPX_pthread_cond_wait(&pool->cond_ths, &pool->mut);
	OSPX_pthread_mutex_unlock(&pool->mut);
}

/*
 * @tpool_task_wait(hp, ptsk, ms)
 * @tpool_task_wait(hp, NULL, ms)
 * @tpool_suspend(hp, 1)
 *
 *  if (pool->wait_type) { 
 *    	if (WAIT_TYPE_FINISH & pool->wait_type)
 *    		notify = pool->n_qtrace
 *    	
 *    	else if (WAIT_TYPE_TASK & pool->wait_type)
 *    		 notify =  ptsk->ref;
 *    	
 *    	if (!notify && WAIT_TYPE_SUSPEND & pool->wait_type) 
 *    		notify = !pool->nthreads_running && !pool->ndispatchings &&
 *    		         !pool->n_qdispatch;
 *
 *    	if (notify)
 *    		OSPX_pthread_cond_broadcast(&pool->cond_comp);
 *  }
 */
#define TRY_wakeup_waiters(pool, ptsk) \
	do {\
	 	if (pool->waiters && !pool->wokeup && \
			(ptsk->ref || !pool->n_qtrace || \
				(pool->suspend_waiters && \
				!pool->nthreads_running && !pool->ndispatchings && \
				!pool->n_qdispatch)) \
		  ) {\
			pool->wokeup = 1; \
			OSPX_pthread_cond_broadcast(&pool->cond_comp);\
		} \
	} while (0)

static void
tpool_task_complete_nocallback_l(struct tpool_t *pool, struct list_head *rmq)
{
	struct task_t *ptsk;
		
	while (!list_empty(rmq)) {
		ptsk = list_entry(rmq->next, struct task_t, wait_link);
		list_del(rmq->next);

		assert (pool->ndispatchings > 0 &&
			   pool->n_qtrace > 0);
		-- pool->ndispatchings;
		
		assert (!ptsk->thread);
#ifdef CONFIG_STATICS_REPORT
		if (TASK_VMARK_REMOVE_BYPOOL & ptsk->f_vmflags)
			++ pool->ntasks_dispatched;
#endif	
		/* The task is being removed by user, so it should not 
		 * be marked with @TASK_VMARK_DO_AGAIN */ 
		assert (!(ptsk->f_vmflags & TASK_VMARK_DO_AGAIN));
		list_del(&ptsk->trace_link);
		-- pool->n_qtrace;

		ptsk->f_stat = 0;
		TRY_wakeup_waiters(pool, ptsk);
		if (ptsk->f_mask & TASK_F_AUTO_FREE && !ptsk->ref)
			smcache_addl(pool->cache_task, ptsk);
	}
	
	/* If @tpool_task_complete is not called by our working 
	 * threads, we try to wake up a thread to do the GC to 
	 * prevent our pool from holding so many memories */
	if (unlikely(!pool->nthreads_real_free && 
			pool->nthreads_running == pool->nthreads_dying_run && 
			smcache_flushablel(pool->cache_task, pool->cattr.nGC_wakeup))) {
		if (pool->nthreads_real_sleeping)
			tpool_wakeup_n_sleeping_threads(pool, 1);
		else
			tpool_add_threads(pool, NULL, 1);
	}
}

/* NOTE:
 * 		@tpool_task_detach is only allowed being called in the
 * task's working routine or in the task's completion routine.
 */
void
tpool_detach_task(struct tpool_t *pool, struct task_t *ptsk) 
{
	if (ptsk->hp != pool) { 
		MSG_log(M_POOL, LOG_WARN, 
			"Invalid operation:'%s/%p/%p' pool:%p\n",
			ptsk->task_name, ptsk, ptsk->hp, pool);
		return;
	}
	
	assert (ptsk->f_stat & TASK_STAT_SCHEDULING);
	
	/* The @tpool_detach_task should not be called in 
	 * the task's @task_run if it has the completion 
	 * routine. 
	 */
	if (ptsk->thread && ptsk->task_complete &&
		TASK_TYPE_NORMAL == ptsk->thread->task_type) {
		MSG_log(M_POOL, LOG_WARN,
			"Skip executing @%s (%s/%p/%p)\n",
			__FUNCTION__, ptsk->task_name, ptsk, ptsk->thread);
		return;
	}

	/* It is not neccessary to call @tpool_detach_task 
	 * for the routine tasks in the task's completion,
	 * so we skip it.
	 */
	if (ptsk->f_mask & TASK_F_AUTO_FREE) {
		MSG_log(M_POOL, LOG_WARN,
			"Try to deatch a routine task. (%s/%p/%p)\n",
			ptsk->task_name, ptsk, pool);
		return;
	}

	OSPX_pthread_mutex_lock(&pool->mut);	
	/* Pass the detached status to the external module */
	if (ptsk->pdetached)
		*ptsk->pdetached = 1;
	
	/* Remove the task from our pool completely, and 
	 * then update the pool status */
	list_del(&ptsk->trace_link);	
	-- pool->n_qtrace;
	if (TASK_VMARK_REMOVE & ptsk->f_vmflags) {
		assert (pool->ndispatchings > 0);
		-- pool->ndispatchings;	
	}
	
	/* Reset the status of the task and try to triggle a 
	 * event for the wait functions */
	ptsk->f_stat = 0;
	TRY_wakeup_waiters(pool, ptsk);
	OSPX_pthread_mutex_unlock(&pool->mut);
}

static int 
tpool_mark_taskl(struct tpool_t *pool, struct tpool_tskstat_t *stat, 
				long flags, struct list_head *rmq) 
{
	int effected = 0;
	struct task_t *ptsk = stat->task;

	/* Reset the flags properly */
	flags &= TASK_VMARK_USER_FLAGS;
	if (TASK_VMARK_ENABLE_QUEUE & flags)
		flags &= ~TASK_VMARK_DISABLE_QUEUE;

	if (!flags) 
		return 0;
	
	/* Process the DISABLE_QUEUE flag */
	if (TASK_VMARK_DISABLE_QUEUE & flags) {
		if (!(TASK_VMARK_DISABLE_QUEUE & ptsk->f_vmflags)) {
			ptsk->f_vmflags |= TASK_VMARK_DISABLE_QUEUE;
			effected = 1;
		}
	} else if (TASK_VMARK_ENABLE_QUEUE & flags) {
		if (TASK_VMARK_DISABLE_QUEUE & ptsk->f_vmflags) {
			ptsk->f_vmflags &= ~TASK_VMARK_DISABLE_QUEUE;
			effected = 1;
		}
	}
	
	/* Process the REMOVE flag */
	if (TASK_VMARK_REMOVE & flags &&
		TASK_STAT_ALLOW_REMOVE & stat->stat) {
	
		if (TASK_VMARK_DO_AGAIN & ptsk->f_vmflags) 
			ptsk->f_vmflags &= ~TASK_VMARK_DO_AGAIN;

		else {
			ptsk->f_vmflags |= (TASK_VMARK_REMOVE & flags);
			ptsk->f_stat = TASK_STAT_DISPATCHING;
						
			assert (ptsk->pri_q < pool->pri_q_num);
			
			/* Remove the task from the pending queue */
			list_del(&ptsk->wait_link);
			if (list_empty(&pool->pri_q[ptsk->pri_q].task_q))
				list_del(&pool->pri_q[ptsk->pri_q].link);
			if (rmq)
				list_add_tail(&ptsk->wait_link, rmq);	
			effected = 2;
		}
	}

	return effected;
}

long
tpool_mark_task(struct tpool_t *pool, struct task_t *ptsk, long lflags) 
{	
	int do_complete = 0;
	struct tpool_tskstat_t stat;	
		
	assert (ptsk && ptsk->hp == pool);
	
	OSPX_pthread_mutex_lock(&pool->mut);
	ACQUIRE_TASK_STAT(pool, ptsk, &stat);
	
	/* Has the task been removed from the pool's
	 * pending queue ? */
	if (2 != tpool_mark_taskl(pool, &stat, lflags, NULL)) {
		OSPX_pthread_mutex_unlock(&pool->mut);
		return stat.stat;
	}
	
	if (!ptsk->task_complete) {	
		/* We just remove the task directly if it has no
		 * completion routine */
		list_del(&ptsk->trace_link);
		-- pool->n_qtrace;
		ptsk->f_stat = 0;
		-- pool->npendings;

#ifdef CONFIG_STATICS_REPORT
		if (TASK_VMARK_REMOVE_BYPOOL & ptsk->f_vmflags)
			++ pool->ntasks_dispatched;
#endif	
		TRY_wakeup_waiters(pool, ptsk);
		if ((ptsk->f_mask & TASK_F_AUTO_FREE) && !ptsk->ref)
			smcache_addl(pool->cache_task, ptsk);

	} else if (lflags & TASK_VMARK_REMOVE_BYPOOL) {
		++ pool->ndispatchings;
		
		/* Add our task into the pool's dispatching queue */
		list_add_tail(&ptsk->wait_link, &pool->dispatch_q);
		++ pool->n_qdispatch;
		
		/* Wake up threads to schedule the callback */
		if (!pool->nthreads_real_free && !pool->n_qths_waked) 
			tpool_ensure_services(pool, NULL);

	} else {
		-- pool->npendings;
		++ pool->ndispatchings;
		do_complete = 1;
	}
	
	/* We should give a notification to @tpool_status_wait 
	 * if someone interests the changing status */	
	if (pool->npendings_ev >= pool->npendings && !pool->ev_wokeup) {
		OSPX_pthread_cond_broadcast(&pool->cond_ev);
		pool->ev_wokeup = 1;
	}
	lflags = ptsk->f_vmflags;
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* Call the completion routine if it is necessary */
	if (do_complete) 
		tpool_task_complete(pool, NULL, ptsk, POOL_TASK_ERR_REMOVED);

	return lflags;
}

int  
tpool_mark_task_cb(struct tpool_t *pool, TWalk_cb twcb, void *twcb_arg)
{	
	long vmflags = (long)twcb_arg, code;
	int  effected = 0, removed = 0, n_qdispatch = 0;
	struct list_head rmq, no_callback_q, *q;
	struct task_t *ptsk, *n;
	struct tpool_tskstat_t stat;

	INIT_LIST_HEAD(&rmq);
	INIT_LIST_HEAD(&no_callback_q);

	OSPX_pthread_mutex_lock(&pool->mut);
	list_for_each_entry_safe(ptsk, n, &pool->trace_q, struct task_t, trace_link) {
		/* We acquire the task status and pass it to 
		 * the user's walk function */
		ACQUIRE_TASK_STAT(pool, ptsk, &stat);
		if (twcb && !(vmflags = twcb(&stat, twcb_arg)))
			continue;
		
		if (-1 == vmflags)
			break;
		
		/* Choose a propriate queue to store the tasks who 
		 * should be removed from the pool's pending queue */
		if (vmflags & TASK_VMARK_REMOVE) {
			if (!ptsk->task_complete)
				q = &no_callback_q;
			else if (vmflags & TASK_VMARK_REMOVE_BYPOOL) 
				q = &pool->dispatch_q;	
			else 
				q = &rmq;
		} else
			q = NULL;
		
		/* We call @tpool_mark_taskl to set the task's status
		 * propriately */
		code = tpool_mark_taskl(pool, &stat, vmflags, q);
		switch (code) {
		case 2:
			if (q == &pool->dispatch_q)
				++ n_qdispatch;
			++ removed;
		case 1:
			++ effected;
			break;
		}
	}	
	pool->ndispatchings += removed;	
	pool->npendings -= removed;
		
	if (n_qdispatch) {
		pool->npendings += n_qdispatch;
		pool->n_qdispatch += n_qdispatch;
		
		/* Wake up threads to schedule the callbacks */
		if (!pool->nthreads_real_free && !pool->n_qths_waked)
			tpool_ensure_services(pool, NULL);
	} 

	if (!list_empty(&no_callback_q)) 
		tpool_task_complete_nocallback_l(pool, &no_callback_q);	
		
	/* We should give a notification to @tpool_status_wait 
	 * if someone interests the changing status */
	if (pool->npendings_ev >= pool->npendings && !pool->ev_wokeup) {
		OSPX_pthread_cond_broadcast(&pool->cond_ev);	
		pool->ev_wokeup = 1;
	}
	
	assert (pool->npendings >= 0 && 
		pool->ndispatchings <= pool->n_qtrace &&
		pool->npendings <= pool->n_qtrace);

	OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* If there are tasks who has completion routine, we call the
	 * @tpool_task_complete to dispatch them */
	if (!list_empty(&rmq)) {
		list_for_each_entry_safe(ptsk, n, &rmq, struct task_t, wait_link) {
			tpool_task_complete(pool, NULL, ptsk, POOL_TASK_ERR_REMOVED);
		}
	}
	
	return effected;
}

void 
tpool_throttle_enable(struct tpool_t *pool, int enable) 
{
	OSPX_pthread_mutex_lock(&pool->mut);
	if (pool->throttle_enabled && !enable) {
		pool->throttle_enabled = enable;
		OSPX_pthread_cond_broadcast(&pool->cond_ev);
	} else
		pool->throttle_enabled = enable;
	OSPX_pthread_mutex_unlock(&pool->mut);
}

void 
tpool_suspend(struct tpool_t *pool, int wait)
{	
	/* We set the flag with no locking the pool to speed the progress */
	pool->paused = 1;

	OSPX_pthread_mutex_lock(&pool->mut);
#ifndef NDEBUG	
	MSG_log(M_POOL, LOG_INFO,
			"{\"%s\"/%p} Suspend. <ntasks_pending:"PRI64"d ntasks_running:%d ntasks_removing:%d> @nthreads:%d>\n", 
			pool->desc, pool, pool->npendings, pool->nthreads_running, pool->ndispatchings, pool->n_qths);
#endif	
	/* Mark the pool paused */
	pool->paused = 1;	
	for (;wait && (pool->nthreads_running || pool->ndispatchings);) {
		++ pool->suspend_waiters;	
		++ pool->waiters;
		OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);
		-- pool->suspend_waiters;
		-- pool->waiters;
	}
	OSPX_pthread_mutex_unlock(&pool->mut);	
}

void 
tpool_resume(struct tpool_t *pool) 
{
	OSPX_pthread_mutex_lock(&pool->mut);	
#ifndef NDEBUG	
	MSG_log(M_POOL, LOG_INFO,
			"{\"%s\"/%p} Resume. <ntasks_pending:"PRI64"d ntasks_running:%d ntasks_removing:%d> @nthreads:%d>\n", 
			pool->desc, pool, pool->npendings, pool->nthreads_running, pool->ndispatchings, pool->n_qths);
#endif	
	/* Notify the server threads that we are ready now. */
	if (pool->paused && (POOL_F_CREATED & pool->status)) {
		pool->paused = 0;

		assert (pool->npendings >= pool->n_qdispatch);
		/* NOTE: 
		 *      If @n_qdispatch is non-zero and it is equal to 
		 * @npendings, it indicates that all of the tasks are 
		 * being dispatched, we do not need to check the status 
		 */
		if (pool->npendings > pool->n_qdispatch) {
			pool->ncont_completions = 0;
			tpool_ensure_services(pool, NULL);
		}
	}
	OSPX_pthread_mutex_unlock(&pool->mut);
}

int
tpool_add_task(struct tpool_t *pool, struct task_t *ptsk) 
{
	int err = 0;
		
	OSPX_pthread_mutex_lock(&pool->mut);
	if (unlikely(ptsk->f_stat)) { 		
		/* Has the task been connected with another pool ? */
		if (ptsk->hp != pool) {
			OSPX_pthread_mutex_unlock(&pool->mut);
			return POOL_TASK_ERR_BUSY;
		}
		
		/* Has the task been requested being rescheduling again ? */
		if (TASK_VMARK_DO_AGAIN & ptsk->f_vmflags) {
			OSPX_pthread_mutex_unlock(&pool->mut);
			return 0;
		}	
		
		/* Is the task in the pending queue ? */
		if (TASK_STAT_WAIT & ptsk->f_stat) {
			OSPX_pthread_mutex_unlock(&pool->mut);
			return 0;
		}
	} 	
	
	/* Is the task allowed to be delivered into our pool ? */
	if (ptsk->hp == pool) {
		if (TASK_VMARK_DISABLE_QUEUE & ptsk->f_vmflags) {
			OSPX_pthread_mutex_unlock(&pool->mut);
			return POOL_TASK_ERR_DISABLE_QUEUE;
		}
	} else {
		ptsk->hp = pool;
		assert (!(TASK_VMARK_DISABLE_QUEUE & ptsk->f_vmflags));
	}

	/* Check the pool status */
	if (unlikely(!(POOL_F_CREATED & pool->status))) {	
		switch (pool->status) {
		case POOL_F_DESTROYING:
			err = POOL_ERR_DESTROYING;
			ptsk->f_vmflags |= TASK_VMARK_POOL_DESTROYING;
			break;
		case POOL_F_DESTROYED:
		default:
			err = POOL_ERR_NOCREATED;
		}
	} 

	if (!err) {
		/* Check the throttle */
		if (unlikely(pool->throttle_enabled))
			err = POOL_ERR_THROTTLE;
		
		else if (likely(!ptsk->f_stat)) {
			assert (!ptsk->thread);
			
			list_add_tail(&ptsk->trace_link, &pool->trace_q);
			++ pool->n_qtrace;
			tpool_add_taskl(pool, ptsk);
		
		} else 
			ptsk->f_vmflags |= TASK_VMARK_DO_AGAIN;	
	}
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return err;
}

static void
tpool_add_taskl(struct tpool_t *pool, struct task_t *ptsk) 
{		
	int q = 0, empty;

	assert (ptsk->task_run);
	/* Reset the task's flag if it goes here */
	ptsk->f_stat = TASK_STAT_WAIT;
	ptsk->f_vmflags &= (TASK_VMARK_ENABLE_QUEUE|TASK_VMARK_DISABLE_QUEUE);
	
	/* Select a queue to store the active task according to
	 * its priority attributes */
	if (unlikely(!(TASK_F_PUSH & ptsk->f_mask))) {
		if (TASK_F_ADJPRI & ptsk->f_mask) {
			ptsk->f_mask &= ~TASK_F_ADJPRI;
			ptsk->pri_q = (ptsk->pri < pool->avg_pri) ? 0 : 
					((ptsk->pri + pool->avg_pri -1) / pool->avg_pri -1);
		
		/* We reset its priority if the task doesn't have 
		 * the permanent priority attribute, it means that
		 * the task will be pushed into the tail of the 
		 * pending queue */
		} else if (TASK_F_PRI_ONCE & ptsk->f_mask) {
			ptsk->f_mask |= TASK_F_PUSH;
			ptsk->f_mask &= ~TASK_F_PRI_ONCE;
			ptsk->pri_q = 0;
			ptsk->pri = 0;
			ptsk->pri_policy = P_SCHE_BACK;
		}
	}
#ifdef CONFIG_STATICS_REPORT
	++ pool->ntasks_added;
#endif
	++ pool->npendings;
	q = ptsk->pri_q;
	
	empty = list_empty(&pool->pri_q[q].task_q);

	/* Sort the task according to the priority */
	assert (q >= 0 && q < pool->pri_q_num);
	if (likely((ptsk->f_mask & TASK_F_PUSH) || empty))
		list_add_tail(&ptsk->wait_link, &pool->pri_q[q].task_q);	
	else {
		struct task_t *nptsk;
		
		/* Compare the task with the last */
		nptsk = list_entry(pool->pri_q[q].task_q.prev, struct task_t, wait_link);
		if (ptsk->pri <= nptsk->pri) {	
			if ((ptsk->pri < nptsk->pri) || (P_SCHE_BACK == ptsk->pri_policy)) {
				list_add(&ptsk->wait_link, &nptsk->wait_link);
				goto out;
			}
			
			assert (P_SCHE_TOP == ptsk->pri_policy);
			list_for_each_entry_continue_reverse(nptsk, &pool->pri_q[q].task_q,
				struct task_t, wait_link) {
				
				if (nptsk->pri != ptsk->pri) {
					list_add(&ptsk->wait_link, &nptsk->wait_link);
					goto out;
				}
			}
			assert (0);
		} 
		
		/* Scan the tasks from the the head */
		list_for_each_entry(nptsk, &pool->pri_q[q].task_q, struct task_t, wait_link) {
			if ((ptsk->pri > nptsk->pri) || (ptsk->pri == nptsk->pri &&
				 P_SCHE_TOP == ptsk->pri_policy)) {
				list_add_tail(&ptsk->wait_link, &nptsk->wait_link);
				goto out;
			}
		}
		assert (0);
	}

out:
	/* Should our task queue join in the ready group ? */
	if (unlikely(empty)) {	
		if (list_empty(&pool->ready_q)) 
			list_add(&pool->pri_q[q].link, &pool->ready_q);

		/* Compare the index with the last */
		else if (q < list_entry(pool->ready_q.prev, struct tpool_priq_t, link)->index) 
			list_add_tail(&pool->pri_q[q].link, &pool->ready_q);
		
		else {
			struct tpool_priq_t *priq;
			
			list_for_each_entry(priq, &pool->ready_q, struct tpool_priq_t, link) {
				if (q > priq->index) {
					list_add_tail(&pool->pri_q[q].link, &priq->link);
					break;
				}
			}
		}
	}
	
	/* Wake up working threads to schedule tasks.
	 *  .Are there any FREE threads ?
	 *  .Has the threads number arrived at the limit ?
	 *  .Is pool paused ? (Optimize, Skip)
	 */	
	if (!pool->nthreads_real_free && !pool->n_qths_waked &&
		(pool->maxthreads != pool->nthreads_real_pool || pool->n_qths_wait)) 
		tpool_ensure_services(pool, NULL);	

	/* Update the statics report */
	if (pool->ntasks_peak < pool->npendings) 
		pool->ntasks_peak = pool->npendings;	
	
	/* There must be a few active threads if the pool is
	 * not in suspended status */
	assert (pool->paused || pool->n_qths_wait < pool->n_qths);
}

int  
tpool_add_routine(struct tpool_t *pool, 
				const char *name, int (*task_run)(struct task_t *ptsk),
				void (*task_complete)(struct task_t *ptsk, long, int),
				void *task_arg, struct xschattr_t *attr)
{	
	int err;
	struct task_t *ptsk;
	
	/* We set the @b_GC_delay to tell the pool
	 * that we should slow down GC */
	if (pool->GC)
		pool->b_GC_delay = 1;
	
	/* We try to get a task object from the cache, 
	 * and @smcache_add(x) is should be called later 
	 * to recycle it*/
	ptsk = smcache_get(pool->cache_task, 1);
	if (!ptsk)
		return POOL_ERR_NOMEM;
	
	assert (!ptsk->thread && !ptsk->ref);
	tpool_task_init(ptsk, name, task_run, task_complete, task_arg);
	if (attr)
		tpool_task_setschattr(ptsk, attr);
	ptsk->f_mask |= TASK_F_AUTO_FREE;
	err = tpool_add_task(pool, ptsk);
	
	/* Pay the task object back to cache if we fail 
	 * to add it into the pool's pending queue */
	if (err) 
		smcache_add(pool->cache_task, ptsk);

	return err;
}


int  
tpool_remove_pending_task(struct tpool_t *pool, int dispatched_by_pool) 
{
	long mask = dispatched_by_pool ? TASK_VMARK_REMOVE_BYPOOL : 
					TASK_VMARK_REMOVE_DIRECTLY;
	
	/* We call @tpool_mark_task_cb with appropricate 
	 * mask to remove all of the pending task */
	return tpool_mark_task_cb(pool, NULL, (void *)mask);
}

struct waiter_request_t {
	int  b_interrupt;
	long req_id;
	OSPX_pthread_cond_t *wake_cond;
	struct list_head link;
};

/* WPUSH is used to push a request into the wait queue,
 * if the pool status changes, the system will give the
 * waiters a notification */
#define WPUSH(pool, wake_cond) \
	{\
		struct waiter_request_t ___dummy_wr = {\
			0, (long)OSPX_pthread_id(), wake_cond, {0, 0}\
		}; \
		list_add_tail(&___dummy_wr.link, &(pool)->wq); \
		(pool)->wokeup = 0;\

/* WPOP is used to remove our request added by WPUSH from 
 * the wait queue */
#define WPOP(pool, wokeup) \
		list_del(&___dummy_wr.link);\
		wokeup = ___dummy_wr.b_interrupt; \
	} 

void 
tpool_wakeup(struct tpool_t *pool, long wakeup_id) 
{
	struct waiter_request_t *wr;

	OSPX_pthread_mutex_lock(&pool->mut);
	
	/* We scan the requester queue to find the
	 * waiter who is requested to return from 
	 * the wait functions imediately */
	list_for_each_entry(wr, &pool->wq, struct waiter_request_t, link) {
		if (wakeup_id == -1 || wr->req_id == wakeup_id) {
			OSPX_pthread_cond_broadcast(wr->wake_cond);
            
			++ wr->b_interrupt;
			if (wakeup_id != -1)
				break;
		}
	}
	OSPX_pthread_mutex_unlock(&pool->mut);
}

int  
tpool_throttle_wait(struct tpool_t *pool, long ms) 
{
	int wokeup = 0, error = ms ? 0 : ETIMEDOUT;

	OSPX_pthread_mutex_lock(&pool->mut);
	for (;;) {
		/* If the throttle is disabled or the pool is 
		 * being destroyed, we return OK ? */
		if (!pool->throttle_enabled || !(POOL_F_CREATED & pool->status)) {
			error = pool->throttle_enabled ? 2 : 0;
			break;
		}

		/* If it is overtime, we break */
		if (ETIMEDOUT == error)
			break;
		
		/* If @wokeup is a non-zero value, it indicates that 
		 * we have been requested to return from this function 
		 * imediately. */
		if (wokeup) {
			error = -1;
			break;
		}

		/* Push our request into the wait queue and wait for
		 * the notification from the pool */
		WPUSH(pool, &pool->cond_ev)
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_ev, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_ev, &pool->mut);
		WPOP(pool, wokeup)
	}
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error ? 1 : 0;
}

static inline int 
tpool_scan_task_entry(struct tpool_t *pool, struct task_t *entry[], int n, int wait_all) 
{
	int i, ok = 0, n_null = 0;
		
	assert (entry && n > 0);
	
	/* Scan the tasks' entry */
	for (i=0; i<n; i++) {
		if (!entry[i]) {
			++ n_null;
			continue;
		}
		
		/* It's not safe here to wait the task who is added into 
		 * the pool by @tpool_add_routine */
		assert (!(entry[i]->f_mask & TASK_F_AUTO_FREE));
		
		if (entry[i]->hp != pool || !entry[i]->f_stat) 
			++ ok;
	}
	
	return ((wait_all && (n_null + ok == n)) ||
	        (!wait_all && ok)) ? 0 : 1;
}


/* NOTE:
 * 		The user must ensure that the task objects are valid before
 * returning from @tpool_task_any_wait */
int  
tpool_task_wait_entry(struct tpool_t *pool, struct task_t *entry[], int n, int wait_all, long ms) 
{
	int i, error = 0, wokeup = 0;
	
	if (entry && !tpool_scan_task_entry(pool, entry, n, wait_all))
		return 0;
	
	OSPX_pthread_mutex_lock(&pool->mut);		
	for (;;) {	
		/* Scan the entry again if we have gotten the lock */
		if (!pool->n_qtrace || (entry && 
			!tpool_scan_task_entry(pool, entry, n, wait_all))) { 
			error = 0;
			break;
		}

		/* If it is overtime, we break */
		if (!ms || ETIMEDOUT == error) {
			error = 1;
			break;	
		}
		
		/* If @wokeup is a non-zero value, it indicates that 
		 * we have been requested to return from this function 
		 * imediately. */
		if (wokeup) {
			error = -1;
			break;
		}
		
		/* NOTE:
		 * 	   We increase the waiters of the task
		 */
		if (entry) {
			for (i=0; i<n; i++) {
				if (entry[i] && entry[i]->f_stat) {
					++ entry[i]->ref;
					break;
				}
			}
			assert (i < n);
		} 
		++ pool->waiters;
			
		/* Push our request into the wait queue and wait for
		 * the notification from the pool */
		WPUSH(pool, &pool->cond_comp)
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		WPOP(pool, wokeup)
		
		if (entry) 
			-- entry[i]->ref;
		-- pool->waiters;
	}		
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return error;
}

int  
tpool_task_wait_cb(struct tpool_t *pool, TWalk_cb twcb, void *twcb_arg, long ms) 
{
	int got = 0, error = ms ? 0 : ETIMEDOUT, wokeup = 0;
	struct task_t *ptsk;
	struct tpool_tskstat_t stat;

	OSPX_pthread_mutex_lock(&pool->mut);	
	for (;;got=0) {	
		/* Scan all of the tasks who has not been finished */
		list_for_each_entry(ptsk, &pool->trace_q, struct task_t, trace_link) {
			
			/* Acquire the task status and pass it
			 * to the user's MATCH function */
			ACQUIRE_TASK_STAT(pool, ptsk, &stat);
			if (twcb(&stat, twcb_arg)) {
				got = 1;
				break;
			}
		}
		
		/* If we have not found one, we break */
		if (!got) {
			error = 0;
			break;
		}

		/* If it is overtime, we break */
		if (ETIMEDOUT == error) {
			error = 1;
			break;	
		}
		
		/* If @wokeup is a non-zero value, it indicates that 
		 * we have been requested to return from this function 
		 * imediately. */
		if (wokeup) {
			error = -1;
			break;
		}
	
		++ ptsk->ref;
		++ pool->waiters;
		
		/* Push our request into the wait queue and wait for
		 * the notification from the pool */
		WPUSH(pool, &pool->cond_comp)
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		WPOP(pool, wokeup)

		-- ptsk->ref;
		-- pool->waiters;

		/* Are we responsible for cleaning the task object ? */
		if ((TASK_F_AUTO_FREE & ptsk->f_mask) && !ptsk->ref)
			smcache_addl(pool->cache_task, ptsk);
	}		
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error;
}

int  
tpool_status_wait(struct tpool_t *pool,  int n_max_pendings, long ms) 
{
	int error = ms ? 0 : ETIMEDOUT, wokeup = 0;
	
	/* Correct the parameter */
	if (n_max_pendings < 0)
		n_max_pendings = 0;

	OSPX_pthread_mutex_lock(&pool->mut);	
	for (;;) {		
		if (n_max_pendings >= pool->npendings) {
			error = 0;
			break;
		}
		
		/* If it is overtime, we break */
		if (ETIMEDOUT == error) {
			error = 1;
			break;	
		}
		
		/* If @wokeup is a non-zero value, it indicates that 
		 * we have been requested to return from this function 
		 * imediately. */
		if (wokeup) {
			error = -1;
			break;
		}
		pool->ev_wokeup = 0;

		/* Set the triggle condition */
		if (-1 != pool->npendings_ev)
			pool->npendings_ev = min(n_max_pendings, pool->npendings_ev);
		else
			pool->npendings_ev = n_max_pendings;
		
		/* Push our request into the wait queue and wait 
		 * for the notification from the pool */
		WPUSH(pool, &pool->cond_ev)
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_ev, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_ev, &pool->mut);	
		WPOP(pool, wokeup)
		
		/* Reset the @npendings_ev if we wake up from
		 * the condition variable */
		pool->npendings_ev = -1;
	}
	OSPX_pthread_mutex_unlock(&pool->mut);	
	
	return error;
}

static int
tpool_get_GC_task(struct tpool_t *pool, struct tpool_thread_t *self) 
{
	int ncached = smcache_nl(pool->cache_task);
	
	/* We should quit ourself imediately if we get the event
	 * that pool is being destroyed */
	if (!(POOL_F_CREATED & pool->status)) 
		return 0;

	/* We try to assign the thread to do the GC jobs */
	if ((self == pool->GC || !pool->GC) && ncached &&
		ncached > pool->cattr.nGC_cache) { 
				
		/* We do not free the task objects continuously */
		if (self->ncont_GC_counters) {
			assert (THREAD_STAT_GC & self->flags);
			self->ncont_GC_counters = 0;
		
		} else {	
			self->ncache_limit = ncached - pool->cattr.nGC_one_time;	
			++ self->ncont_GC_counters;
		}

		if (self->ncont_GC_counters) {
			__curtask = &pool->sys_GC_task;
			__curtask->thread = self;
			
			/* Mark the thread with THREAD_STAT_GC */
			if (!pool->GC) {
				pool->GC = self;
				self->flags |= THREAD_STAT_GC;
			}
			self->task_type = TASK_TYPE_GC;	
			return 1;
		}
	}
	return 0;
}

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
 *    It'll expand our library size if we mark @tpool_thread_status_changel 
 * inline, but a good new for us is that it'll improve our perfermance in 
 * the real test.
 */
static inline void 
tpool_thread_status_changel(struct tpool_t *pool, struct tpool_thread_t *self, uint16_t status) 
{	
	int lookup = 0;
	
	VERIFY(pool, self);
	assert (status != self->status);
	
	switch (status) {
	case THREAD_STAT_JOIN:	
		break;
	case THREAD_STAT_WAIT: 
		if (!(THREAD_STAT_RM & self->flags)) {
			-- pool->nthreads_real_free;
			++ pool->nthreads_real_sleeping;
		}

		/* Add the threads into the wait queue */
		self->b_waked = 0;
		list_add(&self->run_link, &pool->ths_waitq);
		++ pool->n_qths_wait;
		break;
	case THREAD_STAT_TIMEDOUT: {		
		assert (!self->last_to); 
		
		/* We try to remove the thread from the servering sets 
		 * if the threads should be stopped providing service.
		 *
		 * Warning: The situation below may happen.
		 * 	The thread has been waked up, but @OSPX_cond_timed_wait
		 * returns ETIMEDOUT. so we should process it carefully.
		 */	
		if (!self->b_waked) {
			list_del(&self->run_link);
			-- pool->n_qths_wait;
		} 
		if (!((THREAD_STAT_RM|THREAD_STAT_GC) & self->flags)) {
			if ((pool->nthreads_real_pool > pool->minthreads) && ((!pool->npendings) || pool->paused)) { 
				time_t n = time(NULL);
                /* We do not plan to exit if we have created servering threads recently */
				if (pool->crttime > n || pool->crttime + 4 <= n) {
					self->flags |= THREAD_STAT_RM;
					
					++ pool->nthreads_dying;
					-- pool->nthreads_real_pool;
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
			++ pool->nthreads_dying;
			-- pool->nthreads_real_pool;
			-- pool->nthreads_real_free;
			self->run = 0;		
		} 
		break;
	}
	case THREAD_STAT_RUN: 
		assert (__curtask);		
#if !defined(NDEBUG) && !defined(_WIN)
		if (__curtask->task_name)
			prctl(PR_SET_NAME, __curtask->task_name);
#endif
		++ pool->nthreads_running;
		
		/* Optimize */
		if (unlikely(self->flags)) { /* JOIN|RM ->gettask(RUN) */
			if (THREAD_STAT_RM & self->flags) 
				++ pool->nthreads_dying_run;
		
		 	else {
				assert (self->flags & THREAD_STAT_GC);

				-- pool->nthreads_real_free;
				self->flags &= ~THREAD_STAT_GC;
				self->ncont_GC_counters = 0;
				pool->GC = NULL;
			}
		} else
			-- pool->nthreads_real_free;
		/* We should look up the status of the pool to
		 * avoid starving the tasks in some cases */
		lookup = 1;
		break;
	case THREAD_STAT_COMPLETE:
		-- pool->nthreads_running;
		
		if (THREAD_STAT_RM & self->flags) 
			-- pool->nthreads_dying_run;
		else
			++ pool->nthreads_real_free; 

#ifdef CONFIG_STATICS_REPORT
		/* Has @task_run been executed ? */
		if (self->task_type == TASK_TYPE_DISPATCHED)
			++ pool->ntasks_dispatched;
		else
			++ pool->ntasks_done;
#endif
#ifndef NDEBUG
		++ self->ntasks_done;
#endif			
		/* We do not need to check @__curtask at present. 
         * so it is not neccessary to reset it to waste our
		 * CPU. */
		//__curtask = NULL;
		break;
	case THREAD_STAT_FREE: 
		break;
	case THREAD_STAT_LEAVE: 
		/* Remove current thread from the THREAD queue */
		list_del(&self->link);
		-- pool->n_qths;
		
		if (THREAD_STAT_RM & self->flags) {
			assert (pool->nthreads_dying > 0 && !self->run);
			
			/* Give @tpool_adjust_wait a notification */
			if (!-- pool->nthreads_dying)
				OSPX_pthread_cond_broadcast(&pool->cond_ths);
		} else 
			/* Give @tpool_release_ex a notification */
			if (list_empty(&pool->ths))
				OSPX_pthread_cond_broadcast(&pool->cond_ths);
		
		if (THREAD_STAT_GC & self->flags) {
			self->flags &= ~THREAD_STAT_GC;
			pool->GC = NULL;	
		}
	
#ifndef NDEBUG
		MSG_log(M_POOL, LOG_DEBUG,
			"{\"%s\"/%p} thread(%d) exits. <ndones:%d status:0x%lx=>0x%lx> (@nthreads:%d(%d) @npendings:"PRI64"u)\n", 
			pool->desc, pool, self, self->ntasks_done, (long)self->status, status, pool->n_qths, pool->nthreads_real_pool,
			pool->n_qtrace);
#endif			
		break;
	}
	
	/* Check the thread's privious status */
	if (THREAD_STAT_WAIT == self->status) {	
		if (self->b_waked) {
			assert (THREAD_STAT_FREE == status || 
			        THREAD_STAT_TIMEDOUT == status);

			assert (pool->n_qths_waked > 0 && self->b_waked);
			-- pool->n_qths_waked;
		}
		
		/* If the thread is waked up to exit since it is marked died by
		 * @tpool_adjust(_abs) or @tpool_flush, and meanwhile there are 
		 * a few tasks coming, we should check the pool status again to 
		 * ensure that there are servering threads in the pool. */
		if (THREAD_STAT_RM & self->flags && !self->run) 
			lookup = 1;
		 else {
			assert (pool->nthreads_real_sleeping > 0 &&
			       (!(THREAD_STAT_RM & self->flags) ||
				     THREAD_STAT_TIMEDOUT == status));
			-- pool->nthreads_real_sleeping;
			
			if (THREAD_STAT_RM & self->flags) 
				self->run = 0;
			else
				++ pool->nthreads_real_free;
		} 
		assert (!(THREAD_STAT_RM & self->flags) || !self->run);
	}	
	VERIFY(pool, self);
		
	self->status = status; 
	
	/* Try to create more threads to provide services 
	 * before our's executing any task. 
	 *    .Are there any FREE threads ? 
	 *    .Has the threads number arrived at the limit ? 
	 *    .Are there any pending tasks ?  (Optimize, Skip)
	 */
	if (!pool->nthreads_real_free && lookup && 
		pool->maxthreads > pool->nthreads_real_pool && 
		!pool->n_qths_waked)
		tpool_ensure_services(pool, self); 		
}

static inline void 
tpool_thread_status_change(struct tpool_t *pool, struct tpool_thread_t *self, uint16_t status) 
{
	OSPX_pthread_mutex_lock(&pool->mut);
	tpool_thread_status_changel(pool, self, status);
	OSPX_pthread_mutex_unlock(&pool->mut);
}

static inline int
tpool_gettask(struct tpool_t *pool, struct tpool_thread_t *self) 
{
	struct task_t *ptsk = NULL;
	int active = pool->npendings > pool->n_qdispatch;
	
	assert (pool->npendings >= pool->n_qdispatch);
	if (!pool->n_qdispatch && (pool->paused || !pool->npendings))
		return 0;

	/* We scan the dispaching queue firstly, and also
	 * we will give a chance to the pending tasks to 
	 * avoid starve them if there are so many dispatching 
	 * tasks here.
	 */
	if (pool->n_qdispatch) {
		assert (!list_empty(&pool->dispatch_q));
		if (!active || pool->ncont_completions < pool->limit_cont_completions) {
			ptsk = list_entry(pool->dispatch_q.next, struct task_t, wait_link);
			list_del(&ptsk->wait_link);
			self->task_type = TASK_TYPE_DISPATCHED;
			assert (pool->n_qdispatch > 0 && 
					pool->ndispatchings >= pool->n_qdispatch);
			-- pool->n_qdispatch;
			++ pool->ncont_completions;
		}
			
	} else if (!active) 
		/* If there are none any active tasks, we'll
		 * try to do the GC to pay back some memories
		 * to the system */
		return tpool_get_GC_task(pool, self);	
	
	/* Select a task from the pending queue if it is
	 * time to schedule the active tasks */
	if (!ptsk) {
		struct tpool_priq_t *priq;
		assert (pool->npendings > 0 && !list_empty(&pool->ready_q));

		/* Scan the priority queue */
		priq = list_entry(pool->ready_q.next, struct tpool_priq_t, link);

		/* Pop up a task from the task queue */
		ptsk = list_entry(priq->task_q.next, struct task_t, wait_link);
		list_del(&ptsk->wait_link);

		/* Should our priority queue leave the ready group ? */
		if (list_empty(&priq->task_q)) 
			list_del(&priq->link);
#ifndef NDEBUG
		else {
			struct task_t *n = list_entry(priq->task_q.next,
									struct task_t, wait_link);
			assert (ptsk->pri >= n->pri);
		}
#endif
		pool->ncont_completions = 0;
		self->task_type = TASK_TYPE_NORMAL;
	}
	
	/* Check whether there are someone who is watching the 
	 * pool status, or we should give them a notification if
	 * the status has been changed.
	 */
	if (pool->npendings_ev >= -- pool->npendings && 
		!pool->ev_wokeup) {
		pool->ev_wokeup = 1;
		OSPX_pthread_cond_broadcast(&pool->cond_ev);
	}
	
	/* Reset the task's status */
	ptsk->thread = self;
	ptsk->f_stat &= ~TASK_STAT_WAIT;
	ptsk->f_stat |= TASK_STAT_SCHEDULING;
	__curtask = ptsk;
		
	/* We change our thread's status if everything is ready */
	tpool_thread_status_changel(pool, self, THREAD_STAT_RUN);		

#ifndef NDEBUG
	MSG_log(M_POOL, LOG_TRACE,
			"{\"%s\"/%p} thread(%d) RUN(\"%s\"/%p|%d:%s). <ndones:%u npendings:"PRI64"d>\n", 
			pool->desc, pool, self, __curtask->task_name, __curtask, __curtask->pri, 
			P_SCHE_TOP == __curtask->pri_policy ?  "top" : "back", self->ntasks_done, pool->npendings);
#endif	
	return 1;
}

/* It seems that OSPX_random() can not work well on windows, so
 * we use @tpool_random to instead.
 */
#define tpool_random(pool, self) \
	(time(NULL) ^ (unsigned)self * 1927 * (unsigned)OSPX_random())

static long
tpool_get_restto(struct tpool_t *pool, struct tpool_thread_t *self) 
{
	/* If the thread has been markd with @THREAD_STAT_RM or
	 * the pool is being destroying, we should destroy the 
	 * thread imediately */
	if (THREAD_STAT_RM & self->flags || 
		!(POOL_F_CREATED & pool->status)) {
		self->last_to = 0;
		goto out;
	}
	
	/* If the thread is doing the GC, we should slow down
	 * its speed to reuse the cached objects if we have
	 * detected that there are tasks coming */
	if (THREAD_STAT_GC & self->flags) {
		if (smcache_nl(pool->cache_task) > pool->cattr.nGC_cache) {
			if (self->last_to <= 0) 
				self->last_to = pool->cattr.nGC_rest_to;
			
			if (pool->b_GC_delay) {
				self->last_to += pool->cattr.nGC_delay_to;
				pool->b_GC_delay = 0;
			}
			goto out;
		}
		
		/* Reset the GC env */
		self->flags &= ~THREAD_STAT_GC;
		self->ncont_GC_counters = 0;
		pool->GC = NULL;				 		
	} 
	
	/* If there are none any tasks existing in the pending
	 * queue and the number of waiting threads who should
	 * not be destroyed has not arrived at the limited number,
	 * we plan to take the thread into deep sleep */
	if (pool->nthreads_real_sleeping - pool->minthreads <= -1) 
		self->last_to = -1; 
	 
	 else {
		long nt = (long)time(NULL), t_interleaved;

		/* Initialize the random sed */
		OSPX_srandom(nt);

		self->last_to = pool->acttimeo + (unsigned)tpool_random(pool, self) % pool->randtimeo; 
		if (self->last_to < 0) 
			(self->last_to) &= 0x0000ffff;
		
		/* If we have created threads latest, maybe the thread 
		 * should wait for tasks for enough time to avoid destroying 
		 * and creating threads frequently, so we try to increase the 
		 * thread's sleeping time if it is too short. */
		if (nt <= pool->crttime) 
			pool->crttime = nt;
		t_interleaved = 4600 - 1000 * (nt - pool->crttime);

		while (self->last_to < t_interleaved)
			self->last_to += t_interleaved / 4;
	}

out:
#if !defined(NDEBUG) && defined(CONFIG_TRACE_THREAD_TIMEO)
	MSG_log(M_POOL, LOG_TRACE,
		"{\"%s\"/%p} thread(%p) gets WAIT timeo : %ld ms\n",
		pool->desc, pool, self, self->last_to);
#endif

	return self->last_to;
}

static int
tpool_thread_entry(void *arg) 
{
	struct tpool_thread_t *self = (struct tpool_thread_t *)arg;
	struct tpool_t *pool = self->pool;
	
	tpool_thread_status_change(pool, self, THREAD_STAT_JOIN);	
	do {
	#ifndef NDEBUG
		/* We use @n_reused to trace the thread's reused status */
		++ self->n_reused;
	#endif
		tpool_schedule(pool, self);
		
		/* The exiting threads may be reused by our pool.
		 *  <see @tpool_add_threads for more details> */
		OSPX_pthread_mutex_lock(&pool->mut);
		if (!self->run) 
			tpool_thread_status_changel(pool, self, THREAD_STAT_LEAVE);
		OSPX_pthread_mutex_unlock(&pool->mut);
		
	} while (THREAD_STAT_LEAVE != (self->status & ~THREAD_STAT_INNER));

	INIT_thread_structure(pool, self);
	smcache_add_limit(&pool->cache_thread, self, -1);
	
	/* We should decrease the references since we 
	 * have increased it in the @tpool_add_thread. */
	tpool_release_ex(pool, 0, 0);
	
	return 0;
}

static void
do_create_threads(struct tpool_thread_t *self) 
{
	int died, ok = 1, locked = 0;
	LIST_HEAD(exq);
	struct tpool_t *pool = self->pool;
	
	while (!list_empty(&self->thq)) {
		struct tpool_thread_t *thread = list_entry(self->thq.next, 
					struct tpool_thread_t, link_free);
		
		list_del(&thread->link_free);	
		
		died = THREAD_STAT_RM & thread->flags;
		/* Check whether the thread has been marked died, or
		 * we should kill it, and  We try to give a notification 
		 * to @tpool_adjust_wait */
	exerr:
		if (died || !ok) {
			if (!locked) 
				OSPX_pthread_mutex_lock(&pool->mut);
			
			/* We should check the thread status again if we
			 * detect that it has been marked died before */
			if (!(died && (THREAD_STAT_RM & thread->flags))) {
				-- pool->n_qths;
				list_del(&thread->link);
			
				if (died) {
					if (!-- pool->nthreads_dying)
						OSPX_pthread_cond_broadcast(&pool->cond_ths);
				
				} else {
					-- pool->nthreads_real_pool;
					-- pool->nthreads_real_free;
				}

				/* We decrease the pool's reference, and then put the 
				 * thread object into the cache again */
				tpool_releasel(pool, 0);
				list_add_tail(&thread->link_free, &exq);
				continue;
			}
			OSPX_pthread_mutex_unlock(&pool->mut);
			
			/* If it goes here, it indicates that the thread has been
			 * marked died, and at the same time @tpool_add_threads is 
			 * called by some modules to create more threads to provide
			 * services, and in this case, @tpol_add_threads will decide
			 * to reuse it */
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
	
		if ((errno = OSPX_pthread_create(&thread->thread_id, &pool->thattr, 
				tpool_thread_entry, thread))) {
			ok = 0;
			goto exerr;
		}
	#ifndef NDEBUG	
		MSG_log(M_POOL, LOG_DEBUG,
			"{\"%s\"%p} create thread(%d) @nthreads:%d\n",
			pool->desc, pool, thread, pool->nthreads_real_pool);
	#endif		
	}
	if (locked)
		OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* If the exception queue is not empty, we deal with it */ 
	if (!list_empty(&exq)) { 
		do {
			struct tpool_thread_t *thread = list_entry(self->thq.next, 
					struct tpool_thread_t, link_free);
		
			list_del(&thread->link_free);	
			smcache_add(&pool->cache_thread, thread);	
		} while (!list_empty(&exq));
		
		if (!ok)
			MSG_log2(M_POOL, LOG_ERR,
				"launch thread error:%s", OSPX_sys_strerror(errno));
	}
}

static void 
tpool_schedule(struct tpool_t *pool, struct tpool_thread_t *self) 
{	
	int  code;

	assert (self->flags & THREAD_STAT_RM || self->run);
	
	do {
		OSPX_pthread_mutex_lock(&pool->mut);	
		/* We try to select a task from the pending queue 
		 * to execute, and if we can not get one, we'll 
		 * go to sleep */
		if (!tpool_gettask(pool, self)) {
			/* We reset the thread's status before our's going to
			 * sleep */
			if (!tpool_get_restto(pool, self))
				tpool_thread_status_changel(pool, self, THREAD_STAT_FORCE_QUIT);
			else {
				tpool_thread_status_changel(pool, self, THREAD_STAT_WAIT);		
				
				/* Queue ourself if we have not gotten a task, and
				 * @tpool_wakeup_n_sleeping_threads will be responsible
				 * for waking us up if it is neccessary.
				 */
				if (self->last_to > 0) 	
					errno = OSPX_pthread_cond_timedwait(&self->cattr->cond, 
								&pool->mut, &self->last_to);
				else
					errno = OSPX_pthread_cond_wait(&self->cattr->cond, &pool->mut);
				/* Adjust the thread status if we have been woken up 
				 *    THREAD_STAT_WAIT -> |THREAD_STAT_TIMEDOUT
				 *                        |THREAD_STAT_FREE
				 */
				tpool_thread_status_changel(pool, self, (ETIMEDOUT == errno) ? 
										THREAD_STAT_TIMEDOUT : THREAD_STAT_FREE);
			}
			OSPX_pthread_mutex_unlock(&pool->mut);	
			continue;
		}
		OSPX_pthread_mutex_unlock(&pool->mut);
		/* Check whether there are system tasks for us, our threads are 
		 * responsible for creating the working threads in the background.
		 * see @tpool_add_threads for more details.
		 */
		if (!list_empty(&self->thq)) 
			do_create_threads(self);	

		/* Run the task if the task is not marked with DISPATCHED,
		 * and then we call @tpool_task_complete to tell the user
		 * that the task has been done completely */
		if (self->task_type != TASK_TYPE_DISPATCHED)
			code = __curtask->task_run(__curtask);
		
		if (self->task_type != TASK_TYPE_GC)
			tpool_task_complete(pool, self, __curtask, 
					(self->task_type != TASK_TYPE_DISPATCHED) ? 
					code : POOL_TASK_ERR_REMOVED);

	} while (self->run);
}

static void
tpool_task_complete(struct tpool_t *pool, struct tpool_thread_t *self, struct task_t *ptsk, int task_code) 
{		
	assert (!self || self->task_type != TASK_TYPE_GC);
	assert (self || ptsk->task_complete);
	assert (!self || (ptsk->thread == self && __curtask == ptsk));
	
	/* We reset the attached thread */
	ptsk->thread = NULL;

	/* Call the completition routine to dispatch the result */
	if (ptsk->task_complete) {
		uint8_t detached = 0;
		
		/* Set the variable address for the task in case that
		 * the user calls @tpool_detach in the task's completion
		 * routine */
		ptsk->pdetached = &detached;
		ptsk->task_complete(ptsk, 
				(TASK_VMARK_REMOVE & ptsk->f_vmflags) ?
				ptsk->f_vmflags : (ptsk->f_vmflags) | TASK_VMARK_DONE, 
				task_code);
	
	
		/* We do nothing here if the task has been detached */
		if (detached) {
			if (self) 
				tpool_thread_status_change(pool, self, THREAD_STAT_COMPLETE);
			return;	
		}
	}	
	
	/* We decrease the @ndispatchings if the task has been 
	 * requested to be removed from the pending queue */
	if (TASK_VMARK_REMOVE & ptsk->f_vmflags) {
		assert (pool->ndispatchings > 0);
		
		OSPX_pthread_mutex_lock(&pool->mut);	
		-- pool->ndispatchings;		
	} else 	
		OSPX_pthread_mutex_lock(&pool->mut);	
	
	/* We deliver the task into the pending queue if
	 * the user wants to reschedule it again */
	if (TASK_VMARK_DO_AGAIN & ptsk->f_vmflags) {				
		if (self && !(THREAD_STAT_RM & self->flags)) {
			
			/* @tpool_add_taskl may create threads to provide service,
			 * we increase the rescheduling tasks counter before the task's
			 * being delived to the task queue to make sure that the pool
			 * can compute the service threads number accurately. */
			pool->nthreads_going_rescheduling = 1;
			tpool_add_taskl(pool, ptsk);	
			
			/* We decrease the rescheduling tasks counter if the task
			 * has been added into the task queue. */
			pool->nthreads_going_rescheduling = 0;
		} else
			tpool_add_taskl(pool, ptsk);	
		
	} else {
		/* Remove the trace record and reset the status of the task */
		list_del(&ptsk->trace_link);
		-- pool->n_qtrace;
		
		/* We try to triggle a event for @tpool_task_wait(pool, NULL, -1), 
		 * @tpool_suspend(pool, 1) */
		TRY_wakeup_waiters(pool, ptsk);
		
		/* Free the temple task object if it is useless */
		if ((ptsk->f_mask & TASK_F_AUTO_FREE) && !ptsk->ref) {
			smcache_addl(pool->cache_task, ptsk);
			
			/* If @tpool_task_complete is not called by our working threads,
			 * we try to wake up a threads to do the GC to avoid holding so
			 * many memories */
			if (unlikely(!self && !pool->nthreads_real_free && 
					pool->nthreads_running == pool->nthreads_dying_run && 
					smcache_flushablel(pool->cache_task, pool->cattr.nGC_wakeup))) {
				if (pool->nthreads_real_sleeping)
					tpool_wakeup_n_sleeping_threads(pool, 1);
				else
					tpool_add_threads(pool, NULL, 1);
			}
		} else 
			ptsk->f_stat = 0;
	}

	if (self) 
		tpool_thread_status_changel(pool, self, THREAD_STAT_COMPLETE);	
	OSPX_pthread_mutex_unlock(&pool->mut);				
}


static int
tpool_add_threads(struct tpool_t *pool, struct tpool_thread_t *self, int nthreads) 
{
	int n;
	struct tpool_thread_t *thread;
	
	/* To improve the perfermance , we scan the thread queue to 
	 * try to reuse the dying threads */
	if (pool->nthreads_dying && nthreads > 0) {
	 	list_for_each_entry(thread, &pool->ths, struct tpool_thread_t, link) {	
			VERIFY(pool, thread);

			if (!(THREAD_STAT_RM & thread->flags)) 
				continue;

			thread->run = 1;
			thread->flags &= ~THREAD_STAT_RM;
			++ pool->nthreads_real_pool;
			-- pool->nthreads_dying;

			switch (thread->status) {
			case THREAD_STAT_WAIT:
				++ pool->nthreads_real_sleeping;
				
				/* We wake up it */
				if (!thread->b_waked) {
					thread->b_waked = 1;
					++ pool->n_qths_waked;
					-- pool->n_qths_wait;
					list_del(&thread->run_link);
					OSPX_pthread_cond_signal(&thread->cattr->cond);
				}
				break;
			case THREAD_STAT_RUN:
				-- pool->nthreads_dying_run;
				break;
			case THREAD_STAT_FORCE_QUIT:
				/* We reset the thread's status if it is in @QUIT status,
				 * or it may triggle the assertion in the DEBUG mode.
				 * 	(@tpool_thread_status_changel:
				 * 		assert (status != self->status)
				 *  ) */
				thread->status = THREAD_STAT_FREE;

			default:
				++ pool->nthreads_real_free;
				break;
			}
			VERIFY(pool, thread);

			if (!-- nthreads || !pool->nthreads_dying)
				break;
		}	
	}
	VERIFY(pool, self);
		
	/* Actually, In order to reduce the time to hold the global lock,
	 * we'll try to just add the threads structure into the threads sets, 
	 * and then we call @pthread_create in the background. */
	for (n=0; n<nthreads; n++) {
		if (!(thread = smcache_get(&pool->cache_thread, 1))) {
			MSG_log2(M_POOL, LOG_ERR,
				"smcache_get:None.");
			goto over;
		}
		assert (THREAD_STAT_INIT == thread->status && thread->run);
		
		/* If @tpool_add_thread is not called by threads, we just create 
		 * the wokring threads here directly, or we just assign the task 
		 * to the threads who triggles this function */
		if (!self) {
			if (!thread->cattr->initialized) {
				if ((errno = OSPX_pthread_cond_init(&thread->cattr->cond))) {
					MSG_log2(M_POOL, LOG_ERR,
						"cond_init:%s.", OSPX_sys_strerror(errno));
					goto over;
				}
				thread->cattr->initialized = 1;
			}

			if ((errno = OSPX_pthread_create(&thread->thread_id, &pool->thattr, 
								tpool_thread_entry, thread))) {
				MSG_log2(M_POOL, LOG_ERR,
					"launch thread:%s.", OSPX_sys_strerror(errno));
				goto over;
			}
			#ifndef NDEBUG	
				MSG_log(M_POOL, LOG_DEBUG,
					"{\"%s\"/%p} create thread(%d) @nthreads:%d\n", 
					pool->desc, pool, thread, pool->nthreads_real_pool + n + 1);
			#endif		
		} else  
			list_add_tail(&thread->link_free, &self->thq);	
		
		list_add_tail(&thread->link, &pool->ths);
		
		/* We increase the reference to make sure that 
		 * the task pool object will always exist before 
		 * the service thread's exitting. */
		tpool_addrefl(pool, 0);
		VERIFY(pool, thread);
	}
over:
	/* If we fail to create the working threads for some reasons,
	 * we should clean the resources */
	if (nthreads != n && thread) 
		smcache_add(&pool->cache_thread, thread);
	
	pool->n_qths += n;
	pool->nthreads_real_free += n;
	pool->nthreads_real_pool += n;
	
	/* Update the statics report */
	if (pool->nthreads_peak < pool->nthreads_real_pool) 
		pool->nthreads_peak = pool->nthreads_real_pool;
	
	/* Update the timer */
	pool->crttime = (long)time(NULL);
	
	VERIFY(pool, self);

	return n;
}

static int 
tpool_dec_threads(struct tpool_t *pool, int nthreads)
{
	struct tpool_thread_t *thread;
	int ndied = 0, nrunthreads_dec = 0;
	int nthreads_real_running, nthreads_real_others;
		
	if (nthreads <= 0 || !pool->nthreads_real_pool)
		return 0;

	nthreads = min(pool->nthreads_real_pool, nthreads);
	
	nthreads_real_running = pool->nthreads_running - pool->nthreads_dying_run;
	nthreads_real_others = pool->nthreads_real_pool - nthreads_real_running;
	assert (nthreads_real_others >= 0);
	
	/* Record the counter of running threads who is should 
	 * be stopped providing services */
	if (nthreads_real_others < nthreads)
		nrunthreads_dec = nthreads - nthreads_real_others;
	
	/* Mark some threads died to decrease the service threads,
	 * after doing their jobs the threads will quit imediately
	 * if they find that they have been marked died.
	 */
	list_for_each_entry(thread, &pool->ths, struct tpool_thread_t, link) {	
		VERIFY(pool, thread);
		
		/* Skip the threads who has been marked died */
		if (THREAD_STAT_RM & thread->flags) 
			continue;	
		
		if (THREAD_STAT_RUN != thread->status) { 
			-- nthreads;	
			thread->run = 0;
			
			/* If the thread is sleeping, we wake it up */
			if (THREAD_STAT_WAIT == thread->status) {
				-- pool->nthreads_real_sleeping;

				if (!thread->b_waked) {
					thread->b_waked = 1;
					++ pool->n_qths_waked;
					-- pool->n_qths_wait;
					list_del(&thread->run_link);
					OSPX_pthread_cond_signal(&thread->cattr->cond);
				}
			
			} else
				-- pool->nthreads_real_free;
		} else if (nrunthreads_dec) {
			-- nrunthreads_dec;;
			
			thread->run = 0;
			++ pool->nthreads_dying_run;
		}

		if (!thread->run) {
			thread->flags |= THREAD_STAT_RM;
			-- pool->nthreads_real_pool;
			++ pool->nthreads_dying;
			++ ndied;
		}
		VERIFY(pool, thread);

		if (!nthreads && !nrunthreads_dec)
			break;
	}
	VERIFY(pool, NULL);

	return ndied;
}


/* The core algorithms to create threads 
 *
 * Almost only a few parts of codes will be executed,
 * so we mark the function with inline. */
static inline void 
tpool_ensure_services(struct tpool_t *pool, struct tpool_thread_t *self) 
{	
	int64_t ntasks_pending = pool->paused ? pool->n_qdispatch : pool->npendings;
	
	/* We do nothing if there are a few kind of threads listed below:
	 *  1. Threads that has been woken up by @tpool_wakeup_n_sleeping_threads 
	 *  2. Threads who is free now 
	 */
	if (pool->n_qths_waked || pool->nthreads_real_free ||
		pool->nthreads_going_rescheduling || !ntasks_pending) 
		return;
	
	
	/* We try to wake up the sleeping threads firstly rather than
	 * creating some new threads to provide services */
	if (!list_empty(&pool->ths_waitq)) {
		assert (pool->n_qths_wait > 0);
		
		/* Wake up a few threads to provide service */
		tpool_wakeup_n_sleeping_threads(pool, min(2, pool->limit_threads_create_per_time));
		return;
	}
		
	/* Verify the @maxthreads */
	if (pool->maxthreads > pool->nthreads_real_pool) {
		int nthreads = pool->maxthreads - pool->nthreads_real_pool;
		
		/* Acquire the number of pending tasks */
		assert (ntasks_pending >= pool->n_qdispatch);	
		ntasks_pending -= pool->n_qdispatch;
		if (!list_empty(&pool->dispatch_q)) 
			ntasks_pending += 1;

		/* Compute the number of threads who should be created according 
		 * to @limit_threads_create_per_time and @ntasks_pending */
		if (nthreads > pool->limit_threads_create_per_time)
			nthreads = pool->limit_threads_create_per_time;

		if (nthreads > ntasks_pending) 
			nthreads = ntasks_pending;
	
		/* Call @tpool_add_threads to create @nthreads threads to 
		 * provide service */
		tpool_add_threads(pool, self, nthreads);		
	}
}
