/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *	  Stpool is portable and efficient tasks pool library, it can works on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 * 	  blog: http://www.oschina.net/code/snippet_1987090_44422
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#if !defined(NDEBUG) && !defined(_WIN32)
#include <sys/prctl.h>
#endif

#include "mpool.h"
#include "tpool.h"
#include "ospx_errno.h"

#ifndef min
/* VS has defined the MARCO in stdlib.h */
#define min(a, b) ((a) < (b)) ? (a) : (b)
#define max(a, b) ((a) > (b)) ? (a) : (b)
#endif

#ifdef _WIN32
	#define PRI64 "%I64"
#else	
	#define PRI64 "%ll"
#endif

#define __SHOW_WARNING__(prompt) \
	fprintf(stderr, "WARNING: %s:%s:%s:%d\n", prompt, __FILE__, __FUNCTION__, __LINE__)
#define __SHOW_ERR__(prompt) \
	fprintf(stderr, "ERR: %s:%s:%s:%d:%s\n", prompt, __FILE__, __FUNCTION__, __LINE__, strerror(errno))

#define __curtask  self->current_task
#define tpool_thread_setstatus(self, status)    tpool_thread_status_change(self->pool, self, status, 0)
#define tpool_thread_setstatus_l(self, status)  tpool_thread_status_change(self->pool, self, status, 1)

static long *dummy_null_lptr = NULL;
static struct tpool_thread_t *th_dummy_null = NULL;

#define tpool_addref_l(pool, increase_user, p_user_ref)  \
	do {\
		++pool->ref; \
		if (increase_user) \
			++ pool->user_ref;\
		if (p_user_ref)\
			*p_user_ref = pool->user_ref; \
	} while (0)

#define tpool_release_l(pool, decrease_user, p_user_ref)  \
	do {\
		-- pool->ref;\
		if (decrease_user)\
			-- pool->user_ref;\
		if (p_user_ref)\
			*p_user_ref = pool->user_ref; \
	} while (0)

struct task_t *
tpool_new_task(struct tpool_t *pool) {
	struct task_t *ptsk = NULL;

	/* NOTE: We can create a memory pool to improve our
	 * 		 perfermence !
	 */
	if (!list_empty(&pool->clq)) {
		/* Acquire a object from the garbage cache */
		OSPX_pthread_mutex_lock(&pool->mut);
		if (!list_empty(&pool->clq)) {
			ptsk = list_entry(pool->clq.next, struct task_t, wait_link);
			list_del(&ptsk->wait_link);
		}
		OSPX_pthread_mutex_unlock(&pool->mut);

		if (ptsk) {
			if (TASK_F_MPOOL & ptsk->f_mask) {
				ptsk->f_vmflags = 0;
				ptsk->f_mask = TASK_F_MPOOL;
			} else
				ptsk->f_flags = 0;
			ptsk->pri = 0;
			ptsk->f_mask |= TASK_F_PUSH;
			
			return ptsk;
		}
	} 		
	
	/* Allocate a object from the memory pool */
	if (pool->mp) {
		ptsk = (struct task_t *)mpool_new(pool->mp);
		memset(ptsk, 0, sizeof(*ptsk));
		ptsk->f_mask = TASK_F_MPOOL;
	} else 
		ptsk = (struct task_t *)calloc(sizeof(struct task_t), 1);
	
	if (ptsk)
		ptsk->f_mask |= TASK_F_PUSH;
	
	return ptsk;
}

#define tpool_delete_task_l(pool, ptsk)  list_add_tail(&(ptsk)->wait_link, &(pool)->clq)
#define tpool_delete_tasks_l(pool, tskq) list_splice(&(pool)->clq, tskq)

void 
tpool_delete_task(struct tpool_t *pool, struct task_t *ptsk) {
	/* If there are working threads existing in the pool,
	 * we just put the task object into the garbage cache,
	 * and the working threads will do the GC jobs at the
	 * rest time, Or we free the object directly.
	 */
	OSPX_pthread_mutex_lock(&pool->mut);
	if (list_empty(&pool->ths)) {
		OSPX_pthread_mutex_unlock(&pool->mut); 
		if (ptsk->f_mask & TASK_F_MPOOL) 
			mpool_delete(pool->mp, ptsk); 
		else 
			free(ptsk);
	} else {
		tpool_delete_task_l(pool, ptsk);
		OSPX_pthread_mutex_unlock(&(pool)->mut);
	}
} 

static long tpool_release_ex(struct tpool_t *pool, int decrease_user, int wait_threads_on_clean);
static void tpool_add_task_l(struct tpool_t *pool, struct task_t *ptsk);
static inline int  tpool_add_threads(struct tpool_t *pool, struct tpool_thread_t *th, int nthreads, long lflags); 
static inline void tpool_increase_threads(struct tpool_t *pool, struct tpool_thread_t *self);
static void tpool_schedule(struct tpool_t *pool, struct tpool_thread_t *self);
static void tpool_task_complete(struct tpool_t *pool, struct tpool_thread_t *self, struct task_t *ptsk, int task_code); 

static void 
tpool_setstatus(struct tpool_t *pool, long status, int synchronized) {
	if (synchronized)
		pool->status = status;
	else {
		OSPX_pthread_mutex_lock(&pool->mut);
		pool->status = status;
		OSPX_pthread_mutex_unlock(&pool->mut);
	}	
}

#ifndef NDEBUG
#define INIT_thread_structure(pool, self, release) \
	do {\
		(self)->status = THREAD_STAT_INIT;\
		INIT_LIST_HEAD(&(self)->thq);\
		(self)->structure_release = release;\
		(self)->ntasks_done = 0;\
		(self)->ncont_rest_counters = 0;\
		(self)->pool = pool;\
		(self)->run = 1;\
	} while (0)
#else
#define INIT_thread_structure(pool, self, release) \
	do {\
		(self)->status = THREAD_STAT_INIT;\
		INIT_LIST_HEAD(&(self)->thq);\
		(self)->structure_release = release;\
		(self)->ncont_rest_counters = 0;\
		(self)->pool = pool;\
		(self)->run = 1;\
	} while (0)
#endif

static int
tpool_GC_run(struct task_t *ptsk) {
	struct task_t *obj;
	struct tpool_t *pool = ptsk->task_arg;
	
	while (!list_empty(&pool->gcq)) {
		obj = list_entry(pool->gcq.next, struct task_t, wait_link);
		list_del(&obj->wait_link);
		
		assert(!obj->ref);
		if (obj->f_mask & TASK_F_MPOOL)
			mpool_delete(pool->mp, obj);
		else
			free(obj);
	}

	/* Reset the GC owner */
	pool->GC = NULL;

	return 0;
}


void 
tpool_task_setschattr(struct task_t *ptsk, struct xschattr_t *attr) {
	if (attr->pri < 0)
		attr->pri = 0;
	if (attr->pri > 99)
		attr->pri = 99;
	
	if (!attr->permanent) 
		ptsk->f_mask |= TASK_F_PRI_ONCE;	

	if (!attr->pri_policy || (!attr->pri && P_SCHE_BACK == attr->pri_policy)) {
		ptsk->f_mask |= TASK_F_PUSH;
		ptsk->f_mask &= ~TASK_F_PRI;
		ptsk->pri = 0;
		ptsk->pri_q = 0;
	
	} else {
		ptsk->f_mask |= (TASK_F_PRI|TASK_F_ADJPRI);
		ptsk->f_mask &= ~TASK_F_PUSH;
		ptsk->pri = attr->pri;	
	}
	ptsk->pri_policy = attr->pri_policy;	
}

void 
tpool_task_getschattr(struct task_t *ptsk, struct xschattr_t *attr) {
	attr->pri = ptsk->pri;
	attr->pri_policy = ptsk->pri_policy;
	
	if (ptsk->f_mask & TASK_F_PRI_ONCE)
		attr->permanent = 0;
	else
		attr->permanent = 1;
}

int  
tpool_create(struct tpool_t  *pool, int q_pri, int maxthreads, int minthreads, int suspend) {
	int  error, index, mem;
	struct tpool_thread_t *ths;

	/* Connrect the param */
	if (maxthreads < minthreads)
		minthreads = maxthreads;
	
	if (maxthreads <=0)
		maxthreads = 1;
	if (minthreads <= 0)
		minthreads = 0;

	/* We reset the memory */
	memset(pool, 0, sizeof(*pool));
	pool->tpool_created = time(NULL);
	
	/* If the os supports recursive mutex, it will be more convenient 
	 * for users to use our APIs. 
	 */
	if ((errno = OSPX_pthread_mutex_init(&pool->mut, 1))) {
		fprintf(stderr, "WARNING: OS does not support RECURSIVE MUTEX:%s\n",
			strerror(errno));
		if ((errno = OSPX_pthread_mutex_init(&pool->mut, 0)))
			return POOL_ERR_ERRNO;
	}
	tpool_setstatus(pool, POOL_F_CREATING, 0);
	
	/* Preallocate memory for threads. 
     * (We reserved 200 bytes for the implemention of the malloc)
	 */
	mem   = 1024 * 8 - 200; 
	index = mem / sizeof(struct tpool_thread_t);
	INIT_LIST_HEAD(&pool->freelst);
	ths = (struct tpool_thread_t *)calloc(index * sizeof(struct tpool_thread_t), 1);
	if (ths) {
		for (--index;index>=0; --index) {
			INIT_thread_structure(pool, &ths[index], 0);
			list_add(&ths[index].link_free, &pool->freelst);
		}
		pool->buffer = (char *)ths;
	}

	/* Initialize the queue */
	INIT_LIST_HEAD(&pool->wq);
	INIT_LIST_HEAD(&pool->ths);
	INIT_LIST_HEAD(&pool->ths_waitq);
	INIT_LIST_HEAD(&pool->ready_q);
	INIT_LIST_HEAD(&pool->trace_q);
	INIT_LIST_HEAD(&pool->dispatch_q);
	
	/* Initialize the GC env */
	INIT_LIST_HEAD(&pool->clq);
	INIT_LIST_HEAD(&pool->gcq);
	tpool_task_init(&pool->sys_GC_task, "GC", tpool_GC_run, NULL, pool);
	
	error = POOL_ERR_ERRNO;
	if ((errno = OSPX_pthread_cond_init(&pool->cond)))
		goto err1;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_comp)))
		goto err2;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_ths)))
		goto err3;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_ev))) 
		goto err4;
	
	/* Initialzie the default env */
	pool->ref = pool->user_ref = 1;
	pool->paused = suspend;
	pool->maxthreads = maxthreads;
	pool->minthreads = minthreads;
	pool->limit_cont_completions = max(10, pool->maxthreads * 2 / 3);
	pool->throttle_enabled = 0;
	pool->acttimeo = 1000 * 20;
	pool->randtimeo = 1000 * 60;
	pool->threads_wait_throttle = 9;

	/* Try to initiailize the priority queue */
	if (q_pri <= 0)
		q_pri = 1;
	if (q_pri > 99)
		q_pri = 99;
	pool->pri_q_num = q_pri;
	pool->pri_q = (struct tpool_priq_t *)malloc(sizeof(struct tpool_priq_t) * pool->pri_q_num);
	if (!pool->pri_q) {
		errno = ENOMEM;
		goto err5;
	}

	for (index=0; index<pool->pri_q_num; index++) {
		INIT_LIST_HEAD(&pool->pri_q[index].task_q);
		pool->pri_q[index].index = index;
	}
	pool->avg_pri = 100 / pool->pri_q_num;
	tpool_setstatus(pool, POOL_F_CREATED, 0);
	
	/* Load the variables from the environment */
	tpool_load_env(pool);

#ifndef NDEBUG
	fprintf(stderr, ">@limit_threads_free=%d\n"
				    ">@limit_threads_create_per_time=%d\n"
					">@threads_randtimeo=%ld\n"
					">@threads_wait_throttle=%ld\n",
			pool->limit_threads_free, 
			pool->limit_threads_create_per_time,
			pool->randtimeo,
			pool->threads_wait_throttle);
#endif
	/* Start up the reserved threads */
	if (pool->minthreads > 0) {
		OSPX_pthread_mutex_lock(&pool->mut);
		tpool_add_threads(pool, NULL, pool->minthreads, 0);
		OSPX_pthread_mutex_unlock(&pool->mut);
	}
	
	return 0;

err5:
	OSPX_pthread_cond_destroy(&pool->cond_ev);
err4:
	OSPX_pthread_cond_destroy(&pool->cond_ths);
err3:	
	OSPX_pthread_cond_destroy(&pool->cond_comp);
err2:
	OSPX_pthread_cond_destroy(&pool->cond);
err1:
	tpool_setstatus(pool, POOL_F_DESTROYED, 0);
	OSPX_pthread_mutex_destroy(&pool->mut);	
	__SHOW_WARNING__("Err");
	return error;
}

void
tpool_atexit(struct tpool_t *pool, void (*atexit_func)(struct tpool_t *, void *), void *arg) {
	assert(POOL_F_CREATED & pool->status);
	pool->atexit = atexit_func;
	pool->atexit_arg = arg;
}

void 
tpool_use_mpool(struct tpool_t *pool) {
	OSPX_pthread_mutex_lock(&pool->mut);
	if (POOL_F_CREATED & pool->status) {
		if (!pool->mp) {
			struct mpool_t *mp = (struct mpool_t *)malloc(sizeof(*mp));
			
			if (mp) {
				if (mpool_init(mp, sizeof(struct task_t))) {
					__SHOW_WARNING__("mpool_init");
					free(mp);
				} else
					pool->mp = mp;
			}
		}		
	}
	OSPX_pthread_mutex_unlock(&pool->mut);	
}

void 
tpool_load_env(struct tpool_t *pool) {
	const char *env;
	
	/* Load the @limit_threads_free */
	env = getenv("LIMIT_THREADS_FREE");
	if (!env || atoi(env) < 0)
		pool->limit_threads_free = 1;
	else 
		pool->limit_threads_free = atoi(env);

	/* Load the @limit_threads_create_per_time */
	env = getenv("LIMIT_THREADS_CREATE_PER_TIME");
	if (!env || atoi(env) <= 0)	
		pool->limit_threads_create_per_time = 2;
	else
		pool->limit_threads_create_per_time = atoi(env);
	
	/* Load the @randomtimeo */
	env = getenv("THREADS_RANDTIMEO");
	if (!env || atoi(env) <= 0)	
		pool->randtimeo = 60;
	else
		pool->randtimeo = atoi(env);
	
	/* Load the @threads_wait_throttle */
	env = getenv("THREADS_WAIT_THROTTLE");
	if (!env || atoi(env) <= 0)	
		pool->threads_wait_throttle = 9;
	else
		pool->threads_wait_throttle = atoi(env);
}

static void 
tpool_free(struct tpool_t *pool) {
	struct task_t *ptsk;

	assert(list_empty(&pool->ths) &&
	       list_empty(&pool->trace_q) &&
		   !pool->nthreads_running &&
		   !pool->nthreads_real_sleeping &&
		   !pool->npendings && 
		   !pool->ndispatchings &&
		   !pool->n_qtrace && !pool->n_qdispatch); 	
	
	/* Free the priority queue */
	free(pool->pri_q);
	
	/* Do the gargabe collection */
	while (!list_empty(&pool->clq)) {
		ptsk = list_entry(pool->clq.next, struct task_t, wait_link);
		list_del(&ptsk->wait_link);
		
		if (ptsk->f_mask & TASK_F_MPOOL)
			mpool_delete(pool->mp, ptsk);
		else
			free(ptsk);
	}
	
	/* Free the memory pool */
	if (pool->mp) {
#ifndef NDEBUG
		fprintf(stderr, "-----MP----\n%s\n",
			mpool_stat_print(pool->mp, NULL, 0));
#endif	
		mpool_destroy(pool->mp, 1);
		free(pool->mp);
	}
	
	/* Free the preallocated memory */
	if (pool->buffer)
		free(pool->buffer);
	
	OSPX_pthread_mutex_destroy(&pool->mut);
	OSPX_pthread_cond_destroy(&pool->cond);
	OSPX_pthread_cond_destroy(&pool->cond_comp);
	OSPX_pthread_cond_destroy(&pool->cond_ths);
	OSPX_pthread_cond_destroy(&pool->cond_ev);
}


static long 
tpool_release_ex(struct tpool_t *pool, int decrease_user, int wait_threads_on_clean) {
	long user_ref, clean = 0;

	OSPX_pthread_mutex_lock(&pool->mut);
	tpool_release_l(pool, decrease_user, &user_ref);
	if (decrease_user) {
		if (0 == user_ref) {
			struct task_t *ptsk;
		#ifndef NDEBUG
			{
				time_t now = time(NULL);

				fprintf(stderr, "POOL:%p is being destroyed ... %s\n",
					pool, ctime(&now));
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
				
			/* Are we responsible for cleaning the resources ? */
			if (list_empty(&pool->ths)) {
				/* @tpool_resume can not work if the pool is being destroyed. 
                 * (see @tpool_resume for more details)
				 */
				if (!list_empty(&pool->ready_q)) {
					assert(pool->paused);
					
					/* We create service threads to dispatch the task in the 
					 * background if there are one more tasks existing in the 
					 * pool. */
					OSPX_pthread_mutex_unlock(&pool->mut);
					tpool_remove_pending_task(pool, 1);
					OSPX_pthread_mutex_lock(&pool->mut);
					clean = list_empty(&pool->ths);
				} else
					clean = 1;
				if (clean)
					pool->release_cleaning = 1;
			}

			/* Wake up all working threads */
			OSPX_pthread_cond_broadcast(&pool->cond);
			if (wait_threads_on_clean) {
				/* Tell the pool that we are responsible for 
				 * releasing the resources. */
				clean = 1;
				assert(!pool->release_cleaning);
				pool->release_cleaning = 1;
				for (;!list_empty(&pool->ths);) {
					OSPX_pthread_cond_broadcast(&pool->cond);
					OSPX_pthread_cond_wait(&pool->cond_ths, &pool->mut);
				}
			}
		}
	} else if (!pool->release_cleaning)
		clean = (0 == pool->ref);	
	OSPX_pthread_mutex_unlock(&pool->mut);
		
	if (clean) {
		/* We delete the pool object if its reference is zero */
		assert((0 == user_ref) && (decrease_user || pool->ref == 0));
		
		assert(list_empty(&pool->ths)); 	
		tpool_setstatus(pool, POOL_F_DESTROYED, 0);		
		
		/* DEBUG check */
		assert(list_empty(&pool->trace_q));
					
		/* Now we can free the pool env safely */
#ifndef NDEBUG
		{
			time_t now;
			
			fprintf(stderr, "%s\n",
				tpool_status_print(pool, NULL, 0));
			now = time(NULL);
			fprintf(stderr, "POOL:%p has been destroyed ! %s",
				pool, ctime(&now));
		}
#endif
		tpool_free(pool);
		
		/* Call the exit function */
		if (pool->atexit) 
			pool->atexit(pool, pool->atexit_arg);
	}

	return decrease_user ? user_ref : pool->ref;
}

long 
tpool_addref(struct tpool_t *pool) {
	long ref;

	OSPX_pthread_mutex_lock(&pool->mut);
	if (!(POOL_F_CREATED & pool->status)) {
		fprintf(stderr, "WARNING/@%s: Has the pool:%p been marked destroyed ?\n",
			__FUNCTION__, pool);

		ref = 0;
	} else
		tpool_addref_l(pool, 1, &ref);
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return ref;
}

long 
tpool_release(struct tpool_t *pool, int clean_wait) {
	return tpool_release_ex(pool, 1, clean_wait);	
}

void 
tpool_set_activetimeo(struct tpool_t *pool, long acttimeo) {
	pool->acttimeo = acttimeo * 1000;
}

struct tpool_stat_t *
tpool_getstat(struct tpool_t *pool, struct tpool_stat_t *stat) {	
	memset(stat, 0, sizeof(*stat));
	
	stat->created = pool->tpool_created;
	stat->pri_q_num = pool->pri_q_num;
	OSPX_pthread_mutex_lock(&pool->mut);	
	stat->ref = pool->user_ref;
	stat->throttle_enabled = pool->throttle_enabled;
	stat->suspended = pool->paused;
	stat->maxthreads = pool->maxthreads;
	stat->minthreads = pool->minthreads;
	stat->curthreads = pool->n_qths;
	stat->curthreads_active = pool->nthreads_running;
	stat->curthreads_dying  = pool->nthreads_dying;
	stat->acttimeo = pool->acttimeo;
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
tpool_status_print(struct tpool_t *pool, char *buffer, size_t bufferlen) {
	static char sbuffer[490] = {0};
	struct tm *p_tm;
	struct tpool_stat_t pstat;
#ifdef _WIN32
	#define snprintf _snprintf
#endif
	if (!buffer) {
		buffer = sbuffer;
		bufferlen = sizeof(sbuffer);
	}
	tpool_getstat(pool, &pstat);
	p_tm = localtime(&pstat.created);
	snprintf(buffer, bufferlen, 
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
			" threads_actto : %.2f (s)\n"
			" threads_peak  : %u\n"
			"   tasks_peak  : %u\n"
			"tasks_added: %d\n"
			" tasks_done: %d\n"
			"tasks_dispatched: %d\n"
			"  cur_tasks: %d\n"
			"cur_tasks_pending: %d\n"
			"cur_tasks_scheduling: %d\n"
			"cur_tasks_removing: %d\n",
			p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
			p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec,
			pstat.ref,
			pstat.pri_q_num,
			pstat.throttle_enabled ? "on" : "off",
			pstat.suspended ? "yes" : "no",
			pstat.maxthreads,
			pstat.minthreads,
			pstat.curthreads,
			pstat.curthreads_active,
			pstat.curthreads_dying,
			(double)pstat.acttimeo / 1000,
			pstat.threads_peak,
			pstat.tasks_peak,
			pstat.tasks_added,
			pstat.tasks_done,
			pstat.tasks_dispatched,
			pstat.cur_tasks,
			pstat.cur_tasks_pending,
			pstat.cur_tasks_scheduling,
			pstat.cur_tasks_removing
			);
	return buffer;
}

#define ACQUIRE_TASK_STAT(pool, ptsk, st) \
	do {\
		/* If f_removed has been set, it indicates 
		 * that the task is being dispatching.
		 */\
		if (TASK_STAT_WAIT & (ptsk)->f_stat) \
			(st)->stat = (pool)->paused ? TASK_STAT_SWAPED : \
				TASK_STAT_WAIT;\
		else\
			(st)->stat = (ptsk)->f_stat;\
		if ((st)->stat && ((ptsk)->do_again || (TASK_VMARK_DO_AGAIN & (ptsk)->f_vmflags)))\
			(st)->stat |= TASK_STAT_WAIT_PENDING;\
		(st)->vmflags = (ptsk)->f_vmflags;\
		(st)->task = (ptsk);\
		(st)->pri  = (ptsk)->pri;\
	} while (0)


long 
tpool_gettskstat(struct tpool_t *pool, struct tpool_tskstat_t *st) {
	st->stat = 0;	
	
	if (st->task->f_stat) {
		OSPX_pthread_mutex_lock(&pool->mut);
		ACQUIRE_TASK_STAT(pool, st->task, st);
		OSPX_pthread_mutex_unlock(&pool->mut);
	}

	return st->stat;
}

static void 
tpool_adjust_abs_l(struct tpool_t *pool, int maxthreads, int minthreads) {
	int nthreads, mindistance, maxdistance;
	int nthreads_pool, nthreads_pool_free;

	/* Verify the params */	
	assert(maxthreads >= 1 && minthreads >= 0);
	if (!((POOL_F_CREATED|POOL_F_DESTROYING) & pool->status) ||
		((POOL_F_DESTROYING & pool->status) && (maxthreads > 0 || minthreads > 0))) {
		fprintf(stderr, "WARNING/@%s: Has the pool:%p been marked destroyed ?\n",
			__FUNCTION__, pool);

		return;
	}
	pool->maxthreads = maxthreads;
	pool->minthreads = minthreads;
	
	/* Update the @limit_cont_completions */
	pool->limit_cont_completions = max(10, pool->maxthreads * 2 / 3);

	/* Compute the number of threads who is alive */
	nthreads_pool = pool->nthreads_real_pool;
	nthreads_pool_free = nthreads_pool - pool->nthreads_running - pool->nthreads_dying_run;
	assert(nthreads_pool >= 0 && nthreads_pool_free >= 0 &&
		   nthreads_pool >= nthreads_pool_free);

	pool->minthreads = min(pool->maxthreads, pool->minthreads);
	mindistance = pool->minthreads - nthreads_pool;
	maxdistance = pool->maxthreads - nthreads_pool; 
	if (maxdistance <= 0)
		nthreads = maxdistance;
	else 
		nthreads = mindistance;
	
	if (nthreads > 0) 
		tpool_add_threads(pool, NULL, nthreads, 0);

	else if (nthreads < 0) {
		struct tpool_thread_t *th;
		int runthreads_dec = 0, waitthreads_dec = 0;
		
		/* Record the counter of threads who is should
		 * be stopped providing services */
		nthreads = -nthreads;
		if (nthreads_pool_free) {
			waitthreads_dec = min(nthreads_pool_free, nthreads);
			nthreads -= waitthreads_dec;
		}
		runthreads_dec = nthreads;
		
		/* Decrease the service threads */
		list_for_each_entry(th, &pool->ths, struct tpool_thread_t, link) {
			long status = th->status & ~THREAD_STAT_INNER;

			if (THREAD_STAT_RM & th->status)
				continue;	
			assert(th->run);
			if (THREAD_STAT_RUN != status) { 
				if (waitthreads_dec) {
					th->run = 0;
					th->status |= THREAD_STAT_RM;

					/* Is the service thread sleeping ? */
					if (THREAD_STAT_WAIT == status) {
						assert(pool->nthreads_real_sleeping > 0);
						-- pool->nthreads_real_sleeping;
					}
					++ pool->nthreads_dying;
					-- waitthreads_dec;
					-- pool->nthreads_real_pool;
					continue;
				}
			} else if (runthreads_dec) {
				th->run = 0;
				th->status |= THREAD_STAT_RM;
				++ pool->nthreads_dying_run;
				++ pool->nthreads_dying;
				-- runthreads_dec;;
				-- pool->nthreads_real_pool;
			}

			if (!runthreads_dec && !waitthreads_dec)
				break;
		}
		assert(!runthreads_dec && !waitthreads_dec);

		/* Wake up all sleeping threads */
		OSPX_pthread_cond_broadcast(&pool->cond);	
		pool->nthreads_waiters = 0;
	}

	/* Reset the statics report */
	pool->nthreads_peak = pool->nthreads_real_pool;
	pool->ntasks_peak   = (int)pool->npendings;
}


void 
tpool_adjust_abs(struct tpool_t *pool, int maxthreads, int minthreads) {
	OSPX_pthread_mutex_lock(&pool->mut);	
	/*  Correct the param */	
	if (maxthreads < 0)
		maxthreads = pool->maxthreads;
	if (minthreads < 0)
		minthreads = pool->minthreads;
	
	if (maxthreads == 0 && !(POOL_F_DESTROYING & pool->status))
		maxthreads = 1;
	minthreads = min(maxthreads, minthreads);
	tpool_adjust_abs_l(pool, maxthreads, minthreads);
	OSPX_pthread_mutex_unlock(&pool->mut);
}


void 
tpool_adjust(struct tpool_t *pool, int maxthreads, int minthreads) {
	OSPX_pthread_mutex_lock(&pool->mut);
	maxthreads += pool->maxthreads;
	minthreads += pool->minthreads;
	
	/*  Correct the param */	
	if (maxthreads >= 0 && minthreads >= 0)
		minthreads = min(maxthreads, minthreads);
	
	if (maxthreads <= 0)
		maxthreads = 1;

	if (minthreads <= 0)
		minthreads = 0;
	tpool_adjust_abs_l(pool, maxthreads, minthreads);
	OSPX_pthread_mutex_unlock(&pool->mut);
}

int
tpool_flush(struct tpool_t *pool) {
	int n = 0, exitthreads, exitthreads_free;

	OSPX_pthread_mutex_lock(&pool->mut);
	exitthreads = pool->nthreads_real_pool - pool->minthreads;
	if (exitthreads > 0 && pool->nthreads_real_sleeping > 0) {
		struct tpool_thread_t *th;

		if (exitthreads <= pool->nthreads_real_sleeping) 
			exitthreads_free = 0;
		else {
			int curthreads_pool_running, curthreads_pool_free;

			exitthreads_free = exitthreads - pool->nthreads_real_sleeping;
			exitthreads = pool->nthreads_real_sleeping;
	
			curthreads_pool_running = pool->nthreads_running - pool->nthreads_going_rescheduling 
				- pool->nthreads_dying_run;
			assert(pool->nthreads_real_pool >= curthreads_pool_running);	
			curthreads_pool_free = pool->nthreads_real_pool - curthreads_pool_running;

			if (curthreads_pool_free > 0 && pool->npendings < curthreads_pool_free) 
				exitthreads_free = min(exitthreads_free, curthreads_pool_free - pool->npendings);
			else
				exitthreads_free = 0;
		}
		assert(exitthreads >= 0 && exitthreads_free >= 0);

		/* Decrease the service threads */
		list_for_each_entry(th, &pool->ths, struct tpool_thread_t, link) {
			long status = th->status & ~THREAD_STAT_INNER;

			if (THREAD_STAT_RM & th->status)
				continue;	
			
			assert(th->run);
			if (THREAD_STAT_RUN == status)
				continue;
			
			if (THREAD_STAT_WAIT == status) {
				if (exitthreads) {
					assert(pool->nthreads_real_sleeping > 0);
					-- pool->nthreads_real_sleeping;
					-- exitthreads;
					th->run = 0;
				}
			} else if (exitthreads_free) {
				-- exitthreads_free;
				th->run = 0;
			}
				
			if (!th->run) {
				th->status |= THREAD_STAT_RM;
				++ pool->nthreads_dying;
				++ n;
				-- pool->nthreads_real_pool;
				
				if (!exitthreads && !exitthreads_free)
					break;
			}
		}
		assert(!exitthreads && !exitthreads_free);
		/* Wake up all sleeping threads */
		OSPX_pthread_cond_broadcast(&pool->cond);	
	}	
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return n;
}

void 
tpool_adjust_wait(struct tpool_t *pool) {	
	OSPX_pthread_mutex_lock(&pool->mut);
	for (;pool->nthreads_dying;)
		OSPX_pthread_cond_wait(&pool->cond_ths, &pool->mut);
	OSPX_pthread_mutex_unlock(&pool->mut);
}

#define TRY_wakeup_waiters(pool, ptsk) \
	do {\
	 	if (pool->waiters) {\
			if (ptsk->ref) \
				OSPX_pthread_cond_broadcast(&pool->cond_comp);\
			else if (list_empty(&pool->trace_q) ||\
				(pool->suspend_waiters && !pool->nthreads_running && !pool->ndispatchings)) {\
				OSPX_pthread_cond_broadcast(&pool->cond_comp);\
			}\
		}\
	} while (0)

static void
tpool_task_complete_nocallback_l(struct tpool_t *pool, struct list_head *rmq) {
	struct task_t *ptsk;
		
	while (!list_empty(rmq)) {
		ptsk = list_entry(rmq->next, struct task_t, wait_link);
		list_del(rmq->next);

		-- pool->ndispatchings;
		
		if (TASK_VMARK_REMOVE_BYPOOL & ptsk->f_vmflags)
			++ pool->ntasks_dispatched;
		
		if (ptsk->do_again)
			tpool_add_task_l(pool, ptsk);
		else {
			list_del(&ptsk->trace_link);
			-- pool->n_qtrace;

			ptsk->f_stat = 0;
			TRY_wakeup_waiters(pool, ptsk);

			if (!ptsk->ref && (ptsk->f_mask & TASK_F_ONCE))
				tpool_delete_task_l(pool, ptsk);
		}
	}
}

/* NOTE:
 * 		@tpool_task_detach is only allowed being called in the
 * task's completion.
 */
void
tpool_detach_task(struct tpool_t *pool, struct task_t *ptsk) {
	if (ptsk->hp_last_attached != pool) { 
		fprintf(stderr, "Wrong status: hp_last_attached(%p) HP=%p\n",
			pool, ptsk->hp_last_attached);
		abort();
	}
	
	/* Skip the routine tasks */
	if ((ptsk->f_mask & TASK_F_ONCE) || ptsk->detached)
		return;
	
	/* Deattach the resources */
	OSPX_pthread_mutex_lock(&pool->mut);
	/* Set the status */
	ptsk->detached = 1;
	
	/* Pass the detached status to the external module */
	if (ptsk->pdetached)
		*ptsk->pdetached = 1;
	
	/* Remove the trace record */
	list_del(&ptsk->trace_link);	
	-- pool->n_qtrace;

	/* We decrease the @ndispatchings if the task has been marked with
	 * TASK_VMARK_REMOVE 
	 */
	if (TASK_VMARK_REMOVE & ptsk->f_vmflags) {
		assert(pool->ndispatchings > 0);
		-- pool->ndispatchings;	
	}
	
	/* Reset the status of the task */
	ptsk->f_stat = 0;

	/* We triggle a event for @tpool_task_wait(pool, NULL, -1), @tpool_suspend(pool, 1) */
	TRY_wakeup_waiters(pool, ptsk);
	OSPX_pthread_mutex_unlock(&pool->mut);
}


void
tpool_rmq_dispatch(struct tpool_t *pool, struct list_head *rmq, struct list_head *no_callback_q, int code) {	
	struct task_t *ptsk, *n;

	/* We dispatch the tasks who has no completion routine firstly */
	if (no_callback_q && !list_empty(no_callback_q)) {
		OSPX_pthread_mutex_lock(&pool->mut);	
		tpool_task_complete_nocallback_l(pool, no_callback_q);
		OSPX_pthread_mutex_unlock(&pool->mut);	
	} 

	list_for_each_entry_safe(ptsk, n, rmq, struct task_t, wait_link) {
		tpool_task_complete(pool, NULL, ptsk, code);
	}
}

long
tpool_mark_task(struct tpool_t *pool, struct task_t *ptsk, long lflags) {	
	int q, do_complete = 0;
	struct tpool_tskstat_t stat;

	if (!ptsk || !ptsk->f_stat)
		return 0;
	
	/* Set the vmflags properly */
	lflags &= TASK_VMARK_REMOVE;
	q = ptsk->pri_q;

	OSPX_pthread_mutex_lock(&pool->mut);
	ACQUIRE_TASK_STAT(pool, ptsk, &stat);
	
	/* Check whether the task should be removed */
	if (!stat.stat) {
		OSPX_pthread_mutex_unlock(&pool->mut);
		return 0;
	}
	
	if (TASK_VMARK_REMOVE & lflags) {
		if ((TASK_STAT_WAIT|TASK_STAT_SWAPED|TASK_STAT_WAIT_PENDING) & stat.stat) {
			
			/* Remove the @DO_AGAIN flag */
			if (TASK_VMARK_DO_AGAIN & ptsk->f_vmflags) 
				ptsk->f_vmflags &= ~TASK_VMARK_DO_AGAIN;	
			
			else if (ptsk->do_again)
				ptsk->do_again = 0;
			
			else {
				assert(q >= 0 && q < pool->pri_q_num);
				list_del(&ptsk->wait_link);
				if (list_empty(&pool->pri_q[q].task_q))
					list_del(&pool->pri_q[q].link);	

				if (lflags & TASK_VMARK_REMOVE_DIRECTLY) 
					ptsk->f_vmflags |= TASK_VMARK_REMOVE_DIRECTLY;
				else 
					ptsk->f_vmflags |= TASK_VMARK_REMOVE_BYPOOL;
				
				/* Decrease the number of pending tasks */
				-- pool->npendings;
				
				if (!ptsk->task_complete) {	
					/* We just remove the task directly if it has no
				 	 * completion routine */
					list_del(&ptsk->trace_link);
					-- pool->n_qtrace;
		
					if (TASK_VMARK_REMOVE_BYPOOL & ptsk->f_vmflags)
						++ pool->ntasks_dispatched;
				
					ptsk->f_stat = 0;
					TRY_wakeup_waiters(pool, ptsk);

					if ((ptsk->f_mask & TASK_F_ONCE) && !ptsk->ref)
						tpool_delete_task_l(pool, ptsk);

				} else if (lflags & TASK_VMARK_REMOVE_BYPOOL) {
					++ pool->ndispatchings;
					
					/* Wake up threads to schedule the callback */
					ptsk->f_stat = TASK_STAT_DISPATCHING;
					list_add_tail(&ptsk->wait_link, &pool->dispatch_q);
					++ pool->n_qdispatch;
					++ pool->npendings;
					tpool_increase_threads(pool, NULL);

				} else {
					do_complete = 1;
					++ pool->ndispatchings;
					ptsk->f_stat = TASK_STAT_DISPATCHING;
				}
				
				/* Try to give a notification to @tpool_free_wait */
				if (pool->npendings_ev >= pool->npendings)
					OSPX_pthread_cond_broadcast(&pool->cond_ev);
			}
		}
	}	
	lflags = ptsk->f_vmflags;
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* Call the completion routine */
	if (do_complete) 
		tpool_task_complete(pool, NULL, ptsk, POOL_TASK_ERR_REMOVED);
	
	return lflags;
}
	
int  
tpool_mark_task_ex(struct tpool_t *pool, 
			long (*tskstat_walk)(struct tpool_tskstat_t *, void *),
			void *arg) {	
	long vmflags;
	int  effected = 0, removed = 0, n_qdispatch = 0;
	struct list_head rmq, no_callback_q, pool_q, *q;
	struct task_t *ptsk, *n;
	struct tpool_tskstat_t stat;

	INIT_LIST_HEAD(&rmq);
	INIT_LIST_HEAD(&no_callback_q);
	INIT_LIST_HEAD(&pool_q);
	OSPX_pthread_mutex_lock(&pool->mut);
	list_for_each_entry_safe(ptsk, n, &pool->trace_q, struct task_t, trace_link) {
		ACQUIRE_TASK_STAT(pool, ptsk, &stat);
		vmflags = tskstat_walk(&stat, arg);
		if (-1 == vmflags)
			break;
		
		assert(stat.stat);

		/* Set the vmflags properly */
		vmflags &= TASK_VMARK_REMOVE;
		if (!vmflags || !((TASK_STAT_WAIT|TASK_STAT_SWAPED|TASK_STAT_WAIT_PENDING) & stat.stat)) 
			continue;
		
		++ effected;
		if (TASK_VMARK_DO_AGAIN & ptsk->f_vmflags) {
			ptsk->f_vmflags &= ~TASK_VMARK_DO_AGAIN;
			continue;
		}

		if (ptsk->do_again) {
			assert(TASK_VMARK_REMOVE & ptsk->f_vmflags);
			ptsk->do_again = 0;
			continue;
		}
		
		/* Check whether the task should be removed */
		if (vmflags & TASK_VMARK_REMOVE_DIRECTLY) {
			q = &rmq;
			ptsk->f_vmflags |= TASK_VMARK_REMOVE_DIRECTLY;
		} else {
			q = &pool_q;
			ptsk->f_vmflags |= TASK_VMARK_REMOVE_BYPOOL;
		}
		ptsk->f_stat = TASK_STAT_DISPATCHING;
				
		assert(ptsk->pri_q >= 0 && ptsk->pri_q < pool->pri_q_num);
		/* Remove the task from the pending queue */
		list_del(&ptsk->wait_link);
		if (list_empty(&pool->pri_q[ptsk->pri_q].task_q))
			list_del(&pool->pri_q[ptsk->pri_q].link);
		++ removed;
		
		if (ptsk->task_complete) {
			list_add_tail(&ptsk->wait_link, q);
			if (q == &pool_q)
				++ n_qdispatch;
		} else
			list_add_tail(&ptsk->wait_link, &no_callback_q);
	}	
	
	assert(pool->npendings >= removed); 
	pool->ndispatchings += removed;	
	pool->npendings -= removed;
		
	assert(pool->ndispatchings <= pool->n_qtrace);
	if (!list_empty(&pool_q)) {
		/* Move the tasks into the dispatching queue */
		pool->npendings += n_qdispatch;
		list_splice(&pool_q, &pool->dispatch_q);
		pool->n_qdispatch += n_qdispatch;
		
		/* Wake up threads to schedule the callbacks */
		tpool_increase_threads(pool, NULL);
	}
		
	if (!list_empty(&no_callback_q)) 
		tpool_task_complete_nocallback_l(pool, &no_callback_q);	
	assert(pool->npendings >= pool->n_qdispatch);
	OSPX_pthread_mutex_unlock(&pool->mut);
		
	if (!list_empty(&rmq))
		tpool_rmq_dispatch(pool, &rmq, NULL, POOL_TASK_ERR_REMOVED);	
	
	return effected;
}

void 
tpool_throttle_enable(struct tpool_t *pool, int enable) {
	OSPX_pthread_mutex_lock(&pool->mut);
	if (pool->throttle_enabled && !enable) {
		pool->throttle_enabled = enable;
		OSPX_pthread_cond_broadcast(&pool->cond_ev);
	} else
		pool->throttle_enabled = enable;
	OSPX_pthread_mutex_unlock(&pool->mut);
}

void 
tpool_suspend(struct tpool_t *pool, int wait) {	
	/* We set the flag with no locking the pool to speed the progress */
	pool->paused = 1;

	OSPX_pthread_mutex_lock(&pool->mut);
#ifndef NDEBUG	
	fprintf(stderr, "Suspend pool. <ntasks_pending:"PRI64"d ntasks_running:%d ntasks_removing:%d> @threads_in_pool:%d>\n", 
			pool->npendings, pool->nthreads_running, pool->ndispatchings, pool->n_qths);
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
tpool_resume(struct tpool_t *pool) {
	OSPX_pthread_mutex_lock(&pool->mut);	
#ifndef NDEBUG	
	fprintf(stderr, "Resume pool. <ntasks_pending:"PRI64"d ntasks_running:%d ntasks_removing:%d> @threads_in_pool:%d>\n", 
			pool->npendings, pool->nthreads_running, pool->ndispatchings, pool->n_qths);
#endif	
	/* Notify the server threads that we are ready now. */
	if (pool->paused && (POOL_F_CREATED & pool->status)) {
		pool->paused = 0;
		pool->launcher = 0;

		assert(pool->npendings >= pool->n_qdispatch);
		if (pool->npendings > pool->n_qdispatch) {
			pool->ncont_completions = 0;
			tpool_increase_threads(pool, NULL);
		}
	}
	OSPX_pthread_mutex_unlock(&pool->mut);
}

int
tpool_add_task(struct tpool_t *pool, struct task_t *ptsk) {
	int err = 0;
		
	OSPX_pthread_mutex_lock(&pool->mut);
	if (ptsk->f_stat) { 		
		/* Has the task been connected with another pool ? */
		if (ptsk->hp_last_attached != pool) {
			OSPX_pthread_mutex_unlock(&pool->mut);
			return POOL_TASK_ERR_BUSY;
		}
		
		/* Is the task in the pending queue ? */
		if (TASK_STAT_WAIT == ptsk->f_stat) {
			OSPX_pthread_mutex_unlock(&pool->mut);
			return 0;
		}

		/* Has the task been removed from the pending queue ? */
		if ((TASK_VMARK_DO_AGAIN & ptsk->f_vmflags) ||
			((TASK_VMARK_REMOVE & ptsk->f_vmflags) && ptsk->do_again)) {
			OSPX_pthread_mutex_unlock(&pool->mut);
			return 0;
		}
		goto ck;
	}
		
	/* Record the pool */
	ptsk->hp_last_attached = pool;
	ptsk->th = NULL;
	ptsk->detached = 0;
ck:	
	/* Check the pool status */
	if (!(POOL_F_CREATED & pool->status)) {	
		/* Reset the vmflags */
		if (!ptsk->f_stat)
			ptsk->f_vmflags = 0;
		
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

	/* Check the throttle */
	if (!err && pool->throttle_enabled)
		err = POOL_ERR_THROTTLE;
	
	if (ptsk->f_stat) {
		if (!err) {
			if (TASK_VMARK_REMOVE & ptsk->f_vmflags)
				ptsk->do_again = 1;
			else 
				ptsk->f_vmflags |= TASK_VMARK_DO_AGAIN;
		}
	} else if (!err) {
		list_add_tail(&ptsk->trace_link, &pool->trace_q);
		++ pool->n_qtrace;
		tpool_add_task_l(pool, ptsk);
	}
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return err;
}

static void
tpool_add_task_l(struct tpool_t *pool, struct task_t *ptsk) {		
	int q = 0, empty;

	if (ptsk->do_again > 0) 
		ptsk->do_again = 0;

	/* Reset the task's flag */
	ptsk->f_stat = TASK_STAT_WAIT;
	ptsk->f_vmflags = 0;
	
	/* The flag TASK_F_ADJPRI is always be set when the task
	 * is requested being rescheduled. */
	if (TASK_F_ADJPRI & ptsk->f_mask) {
		ptsk->f_mask &= ~TASK_F_ADJPRI;
		if (ptsk->pri) 
			ptsk->pri_q = (ptsk->pri < pool->avg_pri) ? 0 : 
				((ptsk->pri + pool->avg_pri -1) / pool->avg_pri -1);
	
	} else if ((TASK_F_PRI_ONCE & ptsk->f_mask) && 
		!(TASK_F_PUSH & ptsk->f_mask)) {
		ptsk->f_mask |= TASK_F_PUSH;
		ptsk->pri_q = 0;
		ptsk->pri = 0;
		ptsk->pri_policy = P_SCHE_BACK;
	}	
	++ pool->ntasks_added;
	++ pool->npendings;
	q = ptsk->pri_q;
	
	empty = list_empty(&pool->pri_q[q].task_q);

	/* Sort the task according to the priority */
	assert(q >= 0 && q < pool->pri_q_num);
	if ((ptsk->f_mask & TASK_F_PUSH) || empty)
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

			if (nptsk->pri == ptsk->pri) {
				assert(P_SCHE_TOP == ptsk->pri_policy);
				list_for_each_entry_continue_reverse(nptsk, &pool->pri_q[q].task_q,
					struct task_t, wait_link) {
					
					if (nptsk->pri != ptsk->pri) {
						list_add(&ptsk->wait_link, &nptsk->wait_link);
						goto out;
					}
				}
			}
		} 
		
		/* Scan the tasks from the the head */
		list_for_each_entry(nptsk, &pool->pri_q[q].task_q, struct task_t, wait_link) {
			if ((ptsk->pri > nptsk->pri) || (ptsk->pri == nptsk->pri &&
				 P_SCHE_TOP == ptsk->pri_policy)) {
				list_add_tail(&ptsk->wait_link, &nptsk->wait_link);
				break;
			}
		}
	}

out:
	/* Should our task queue join in the ready group ? */
	if (empty) {	
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
	 *  .Is pool paused ?
	 *  .Are there any FREE threads ?
	 *  .Has the threads number arrived at the limit ?
	 */
	if ((pool->n_qths + pool->nthreads_going_rescheduling) ==
		(pool->nthreads_waiters + pool->nthreads_running) &&	
		pool->maxthreads > pool->nthreads_real_pool &&
		!pool->paused) 
		tpool_increase_threads(pool, NULL);		

	/* Update the statics report */
	if (pool->ntasks_peak < pool->npendings) 
		pool->ntasks_peak = pool->npendings;	
}

int  
tpool_add_routine(struct tpool_t *pool, 
				const char *name, int (*task_run)(struct task_t *ptsk),
				void (*task_complete)(struct task_t *ptsk, long, int),
				void *task_arg, struct xschattr_t *attr) {	
	int err;
	struct task_t *ptsk;
	
	ptsk = tpool_new_task(pool);
	if (!ptsk)
		return POOL_ERR_NOMEM;
	
	tpool_task_init(ptsk, name, task_run, task_complete, task_arg);
	if (attr)
		tpool_task_setschattr(ptsk, attr);
	ptsk->f_mask |= TASK_F_ONCE;
	err = tpool_add_task(pool, ptsk);
	if (err) 
		tpool_delete_task(pool, ptsk);

	return err;
}


static long
rmwalk(struct tpool_tskstat_t *stat, void *arg) {
	return (long)arg;
}

int  
tpool_remove_pending_task(struct tpool_t *pool, int dispatched_by_pool) {
	return tpool_mark_task_ex(pool, rmwalk, dispatched_by_pool ?
				(void *)TASK_VMARK_REMOVE_BYPOOL : (void *)TASK_VMARK_REMOVE_DIRECTLY);
}

struct waiter_link_t {
	int  pushed;
	long call;
	long wkid;
	struct list_head link;
};

#define WPUSH(pool, call, id) \
	{\
		struct waiter_link_t wl = {\
			1, call, id, {0, 0}\
		};\
		list_add_tail(&wl.link, &(pool)->wq);

#define WPOP(pool, wokeup) \
		list_del(&wl.link);\
		wokeup = (1 != wl.pushed); \
	} 

void 
tpool_wakeup(struct tpool_t *pool, long wkid) {
	long lwoke = 0;
	struct waiter_link_t *wl;

	OSPX_pthread_mutex_lock(&pool->mut);
	list_for_each_entry(wl, &pool->wq, struct waiter_link_t, link) {
		if (wkid != -1 && wl->wkid != wkid)
			continue;
		
		if (2 == ++ wl->pushed && !(lwoke & wl->call)) {
			/* Wake up the waiters */
			switch (wl->call) {
			case WK_T_THROTTLE_WAIT: 
			case WK_T_PENDING_WAIT:
				OSPX_pthread_cond_broadcast(&pool->cond_ev);
				lwoke |= WK_T_THROTTLE_WAIT|WK_T_PENDING_WAIT;
				break;
			case WK_T_WAIT:   
			case WK_T_WAIT2:
			case WK_T_WAIT3:
			case WK_T_WAITEX:
				 OSPX_pthread_cond_broadcast(&pool->cond_comp);
				lwoke |= WK_T_WAIT|WK_T_WAIT2|
						 WK_T_WAIT3|WK_T_WAITEX;
				 break;
			case WK_T_WAIT_ALL: 	
				OSPX_pthread_cond_broadcast(&pool->cond_ev);
				OSPX_pthread_cond_broadcast(&pool->cond_comp);
				lwoke |= WK_T_WAIT_ALL;
				break;
			default:
				/* It'll never be done */
				abort();
			}
		}
	}
	OSPX_pthread_mutex_unlock(&pool->mut);
}

long
tpool_wkid() {
	return (long)OSPX_pthread_id();
}

int  
tpool_throttle_wait(struct tpool_t *pool, long ms) {
	int error, wokeup = 0;

	OSPX_pthread_mutex_lock(&pool->mut);
	if (!ms)
		error = pool->throttle_enabled ? 1 : 0;
	else
		for (error=0;;) {
			if (!pool->throttle_enabled || !(POOL_F_CREATED & pool->status)) {
				error = 0;
				break;
			}
			if (ETIMEDOUT == error)
				break;
			
			if (wokeup) {
				error = -1;
				break;
			}

			WPUSH(pool, WK_T_THROTTLE_WAIT, tpool_wkid())
			if (-1 != ms)
				error = OSPX_pthread_cond_timedwait(&pool->cond_ev, &pool->mut, &ms);
			else
				error = OSPX_pthread_cond_wait(&pool->cond_ev, &pool->mut);
			WPOP(pool, wokeup)
		}
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error ? 1 : 0;
}

int  
tpool_task_wait(struct tpool_t *pool, struct task_t *ptsk, long ms) {
	int error, wokeup = 0;
	
	if (ptsk && (ptsk->hp_last_attached != pool || !ptsk->f_stat))
		return 0;

	OSPX_pthread_mutex_lock(&pool->mut);	
	for (error=0;;) {	
		if (list_empty(&pool->trace_q) ||
			(ptsk && !ptsk->f_stat)) {
			error = 0;
			break;
		}

		if (!ms) 
			error = ETIMEDOUT;

		if (ETIMEDOUT == error) {
			error = 1;
			break;	
		}

		if (wokeup) {
			error = -1;
			break;
		}

		if (ptsk)
			++ ptsk->ref;
		++ pool->waiters;	

		/* Wait for the tasks' completions in ms millionseconds */
		WPUSH(pool, WK_T_WAIT, tpool_wkid())
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		WPOP(pool, wokeup)

		if (ptsk)
			-- ptsk->ref;
		-- pool->waiters;
	}		
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error;
}


/* NOTE:
 * 		The user must ensure that the task objects are valid before
 * returning from @tpool_task_wait_ex */
static int  
tpool_task_wait_ex(struct tpool_t *pool, long call, struct task_t *entry, int *n, int nlimit, long ms) {
	int error, ok;
	int i, wokeup = 0, num = *n;
	
	/* Verify the param */
	if (!entry || !n || !*n)
		return 0;

	/* Fix the param */
	if (num < nlimit)
		nlimit = num;

	/* Scan the tasks' entry */
	for (i=0, ok=0; i<num; i++) {
		if (entry[i].hp_last_attached != pool || !entry[i].f_stat) {
			++ ok;
		}

		/* It's not safe here to wait the task who is added into 
		 * the pool by tpool_add_routine */
		assert(!(entry[i].f_mask & TASK_F_ONCE));
	}
	
	if (ok >= nlimit) {
		*n = ok;
		return 0;
	}
	*n = 0;

	OSPX_pthread_mutex_lock(&pool->mut);		
	for (error=0;;) {	
		if (!ms) 
			error = ETIMEDOUT;

		if (ETIMEDOUT == error) {
			error = 1;
			break;	
		}

		if (wokeup) {
			error = -1;
			break;
		}
		
		/* NOTE:
		 * 	   We increase the waiters of all tasks 
		 */
		for (i=0; i<num; i++) 
			++ entry[i].ref;
		pool->waiters += num;

		/* Wait for the tasks' completions in ms millionseconds */
		WPUSH(pool, call, tpool_wkid())
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		WPOP(pool, wokeup)

		for (i=0; i<num; i++) 
			-- entry[i].ref;
		pool->waiters -= num;
	}			
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* Check the result */
	for (i=0; i<num; i++) {
		if (entry[i].hp_last_attached != pool || !entry[i].f_stat) {
			++ *n;
			continue;
		}
	}

	if (*n >= nlimit)
		error = 0;

	return error;
}


int  
tpool_task_wait2(struct tpool_t *pool, struct task_t *entry, int n, long ms) {
	return tpool_task_wait_ex(pool, WK_T_WAIT2, entry, &n, n, ms);
}

int  
tpool_task_wait3(struct tpool_t *pool, struct task_t *entry, int *n, long ms) {
	return tpool_task_wait_ex(pool, WK_T_WAIT3, entry, n, 1, ms);
}

int  
tpool_task_waitex(struct tpool_t *pool, int (*task_match)(struct tpool_tskstat_t *, void *), void *arg, long ms) {
	int got, error, wokeup = 0;
	struct task_t *ptsk;
	struct tpool_tskstat_t stat;

	OSPX_pthread_mutex_lock(&pool->mut);	
	for (error=0, got=0;;) {	
		list_for_each_entry(ptsk, &pool->trace_q, struct task_t, trace_link) {
			ACQUIRE_TASK_STAT(pool, ptsk, &stat);
			if (task_match(&stat, arg)) {
				if (!ms) 
					error = ETIMEDOUT;
				got = 1;
				break;
			}
		}

		if (!got) {
			error = 0;
			break;
		}

		if (ETIMEDOUT == error) {
			error = 1;
			break;	
		}
		
		if (wokeup) {
			error = -1;
			break;
		}
		got = 0;
	
		++ ptsk->ref;
		++ pool->waiters;
		
		WPUSH(pool, WK_T_WAITEX, tpool_wkid())
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		WPOP(pool, wokeup)

		-- ptsk->ref;
		-- pool->waiters;

		/* Are we responsible for cleaning the task object */
		if ((TASK_F_ONCE & ptsk->f_mask) && !ptsk->ref)
			tpool_delete_task_l(pool, ptsk);
	}		
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error;
}

int  
tpool_pending_leq_wait(struct tpool_t *pool,  int n_max_pendings, long ms) {
	int error, wokeup = 0;
	
	if (n_max_pendings < 0)
		n_max_pendings = 0;

	OSPX_pthread_mutex_lock(&pool->mut);	
	for (error=0;;) {		
		if (n_max_pendings >= pool->npendings) {
			error = 0;
			break;
		}
		
		if (ETIMEDOUT == error) {
			error = 1;
			break;	
		}

		if (wokeup) {
			error = -1;
			break;
		}

		if (-1 != pool->npendings_ev)
			pool->npendings_ev = min(n_max_pendings, pool->npendings_ev);
		else
			pool->npendings_ev = n_max_pendings;

		WPUSH(pool, WK_T_PENDING_WAIT, tpool_wkid())
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		WPOP(pool, wokeup)
		pool->npendings_ev = -1;
	}
	OSPX_pthread_mutex_unlock(&pool->mut);	
	
	return error;
}


/* It seems that OSPX_random() can not work well on windows, so
 * we use @tpool_random to instead.
 */
#define tpool_random(pool, self) \
	(time(NULL) ^ (unsigned)self * 1927 * (unsigned)OSPX_random())

#define tpool_get_restto(pool, self, to) \
	do {\
		int extra = pool->nthreads_real_pool - (pool)->minthreads - pool->nthreads_running; \
		if ((THREAD_STAT_RM & (self)->status) ||\
			!(POOL_F_CREATED & pool->status)) \
			*to = 0;\
		else if (extra > (pool)->threads_wait_throttle) \
			*to = 0; \
		else if (extra <= 0) \
			*to = -1; \
		else if ((self)->ncont_rest_counters) \
			/* We take the @ncont_rest_counters into consideration,
			 * think about situations like that.
			 *    All threads go to sleep for none tasks existing in 
			 * the pool, and then users deliver few tasks into the pool
			 * continuously, and as a result, the threads will be woke
			 * up, But if the tasks number is less than the threads number,
			 * the bad situation that some threads can not get any tasks
			 * all the way may happen.
			 */\
			*to = self->last_to;\
		else {\
			/* Initialize the random sed */\
			OSPX_srandom(time(NULL));\
			if (extra <= 5) \
				*to = (pool)->acttimeo + tpool_random(pool, self) % pool->randtimeo;\
			else\
				*to = (long)tpool_random(pool, self) % 35000;\
			if ((*to) < 0) \
				*to = (*to) & 0x0000ffff;\
		} \
	} while (0)


static inline void 
tpool_thread_status_change(struct tpool_t *pool, struct tpool_thread_t *self, uint16_t status, int synchronized) {	
	if (!synchronized)
		OSPX_pthread_mutex_lock(&pool->mut);
	
	switch (status) {
	case THREAD_STAT_JOIN:	
		break;
	case THREAD_STAT_WAIT: 
		if (!(THREAD_STAT_RM & self->status))
			++ pool->nthreads_real_sleeping;
		++ self->ncont_rest_counters;
		
		/* Add the threads into the wait queue */
		list_add_tail(&self->run_link, &pool->ths_waitq);
		++ pool->nthreads_waiters;
		++ pool->n_qths_wait;
		
		/* Reset the thread launcher */
		if (pool->launcher && pool->launcher == OSPX_pthread_id())
			pool->launcher = 0;
		break;
	case THREAD_STAT_TIMEDOUT: {	
		int nthreads_pool = pool->n_qths - pool->nthreads_dying;
		
		/* We try to remove the thread from the servering sets 
		 * if the threads should be stopped providing service.
		 */	
		if (!self->last_to && !(THREAD_STAT_RM & self->status)) {
			if ((nthreads_pool > pool->minthreads) && ((!pool->npendings) || pool->paused)) { 
				status = THREAD_STAT_RM;
				++ pool->nthreads_dying;
				-- pool->nthreads_real_pool;
				self->run = 0;
				break;
			}
		}
		assert(self->last_to > 0);
		status = THREAD_STAT_FREE;	
		break;
	}
	case THREAD_STAT_FORCE_QUIT: {
		assert(self->run && !(THREAD_STAT_RM & self->status));
		status = THREAD_STAT_RM;
		++ pool->nthreads_dying;
		-- pool->nthreads_real_pool;
		self->run = 0;		
		
		/* WAIT-->FREE (woke up)-->QUIT */
		/* We look up the task status before the thread's exiting since we do 
		 * not know that which thread will be woke up by @tpool_increase_thread
		 */
		if (!list_empty(&pool->dispatch_q) || (!pool->paused && pool->npendings))
			tpool_increase_threads(pool, self);
		break;
	}
	case THREAD_STAT_RUN: 
		assert(__curtask);		
#if !defined(NDEBUG) && !defined(_WIN32)
		if (__curtask->task_name && strlen(__curtask->task_name))
			prctl(PR_SET_NAME, __curtask->task_name);
#endif
		++ pool->nthreads_running;
		if (THREAD_STAT_RM & self->status) {
			assert(!self->run);
			++ pool->nthreads_dying_run;
		}
				
		/* Reset the @ncont_rest_counters */
		if (self->ncont_rest_counters)
			self->ncont_rest_counters = 0;

		/* Try to create more threads to provide services 
		 * before our's executing the task. 
		 *    .Are there any FREE threads ? 
		 *    .Has the threads number arrived at the limit ? 
	     *    .Are there any pending tasks ? 
		 */
		if ((pool->n_qths + pool->nthreads_going_rescheduling) ==
			(pool->nthreads_waiters + pool->nthreads_running) &&
			pool->maxthreads > pool->nthreads_real_pool && 
			pool->npendings) 
			tpool_increase_threads(pool, self); 
		break;
	case THREAD_STAT_COMPLETE:
		-- pool->nthreads_running;
		status = THREAD_STAT_FREE;	
		
		if (THREAD_STAT_RM & self->status) {
			assert(!self->run);
			-- pool->nthreads_dying_run;
		}

		/* Has @task_run been executed ? */
		if (self->task_type & TASK_TYPE_DISPATCHED)
			++ pool->ntasks_dispatched;
		else
			++ pool->ntasks_done;
#ifndef NDEBUG
		++ self->ntasks_done;
#endif			
		/* We do not need to check @__curtask at present. 
         * so it is not neccessary to reset it to waste our
		 * CPU.
		 */
		//__curtask = NULL;
		break;
	case THREAD_STAT_FREE: 
		break;
	case THREAD_STAT_LEAVE: 
		/* Remove current thread from the THREAD queue */
		list_del(&self->link);
		-- pool->n_qths;

		/* Remove current thread from the RM queue */
		if (THREAD_STAT_RM & self->status) {
			assert(pool->nthreads_dying > 0 && !self->run);
			
			/* Give @tpool_adjust_wait a notification */
			if (!-- pool->nthreads_dying)
				OSPX_pthread_cond_broadcast(&pool->cond_ths);
		} else if (list_empty(&pool->ths))
			/* Give @tpool_release_ex a notification */
			OSPX_pthread_cond_broadcast(&pool->cond_ths);
		
		/* Reset the thread launcher */
		if (pool->launcher && pool->n_qths == 1) 
			pool->launcher = 0;		
#ifndef NDEBUG
		fprintf(stderr, "THREAD:%p exits. <ntasks_done:%d status:0x%lx> (@threads_in_pool:%d(%d) @tasks_in_pool:"PRI64"u)\n", 
			self, self->ntasks_done, (long)self->status, pool->n_qths, pool->nthreads_real_pool,
			pool->n_qtrace);
#endif			
		break;
	}

	/* Decrease the rest counter */
	if (THREAD_STAT_WAIT == (self->status & ~THREAD_STAT_INNER)) {	
		if (!(THREAD_STAT_RM & self->status)) { 
			assert(pool->nthreads_real_sleeping > 0);
			-- pool->nthreads_real_sleeping;
		}
		
		/* Remove the thread from the wait queue */
		list_del(&self->run_link);
		-- pool->n_qths_wait;

		/* 1. @OSPX_pthread_cond_signal may wake up more than one sleeping threads 
		 * 2. Thread is changing status from WAIT to WAIT_TIMEDOUT.
		 */
		if (pool->nthreads_waiters > pool->n_qths_wait)
			pool->nthreads_waiters = pool->n_qths_wait;
	}	
	self->status = status | (self->status & THREAD_STAT_INNER);
	
	assert(pool->n_qths >= pool->n_qths_wait + pool->nthreads_running);
	if (!synchronized)
		OSPX_pthread_mutex_unlock(&pool->mut);
}

static inline int
tpool_gettask(struct tpool_t *pool, struct tpool_thread_t *self) {
	struct task_t *ptsk = NULL;
	int ready = pool->paused ? 0 : (pool->npendings > pool->n_qdispatch);
	
	/* Scan the dispaching queue firstly */
	if (!list_empty(&pool->dispatch_q)) {
		/* We give a chance to the ready tasks */
		if (!ready || pool->ncont_completions < pool->limit_cont_completions) {
			ptsk = list_entry(pool->dispatch_q.next, struct task_t, wait_link);
			list_del(&ptsk->wait_link);
			-- pool->n_qdispatch;
			++ pool->ncont_completions;
			self->task_type = TASK_TYPE_DISPATCHED;
		}
	
	} else if (!ready) { 
		/* Try to free the objects */
		if (!pool->GC && !list_empty(&pool->clq)) {
			pool->GC  = self;
			list_splice_init(&pool->clq, &pool->gcq);
			__curtask = &pool->sys_GC_task;
			__curtask->th = self;
			self->task_type = TASK_TYPE_GC;
			return 1;
		}
		return 0;
	}
	
	/* Scan the ready queue */
	if (!ptsk) {
		struct tpool_priq_t *priq;
		assert(pool->npendings > 0 && !list_empty(&pool->ready_q));

		/* Scan the priority queue */
		priq = list_entry(pool->ready_q.next, struct tpool_priq_t, link);

		/* Pop up a task from the task queue */
		ptsk = list_entry(priq->task_q.next, struct task_t, wait_link);
		list_del(&ptsk->wait_link);

		/* Should our queue leave the ready group ? */
		if (list_empty(&priq->task_q)) 
			list_del(&priq->link);
#ifndef NDEBUG
		else {
			struct task_t *n = list_entry(priq->task_q.next,
									struct task_t, wait_link);
			assert(ptsk->pri >= n->pri);
		}
#endif
		pool->ncont_completions = 0;
		self->task_type = 0;
	}

	/* Give a notification to @tpool_free_wait */
	if (pool->npendings_ev >= -- pool->npendings)
		OSPX_pthread_cond_broadcast(&pool->cond_ev);

	ptsk->th = self;
	ptsk->f_stat &= ~TASK_STAT_WAIT;
	ptsk->f_stat |= TASK_STAT_SCHEDULING;
	__curtask = ptsk;
		
	/* Push the task into the running queue and
	 * mark our thread status with THREAD_STAT_RUN
	 */
	tpool_thread_setstatus_l(self, THREAD_STAT_RUN);		

#ifndef NDEBUG
	fprintf(stderr, "THREAD:%p running task(%s/%p|%d:%s). <ntasks_done:%u ntasks_pending:"PRI64"d>\n", 
			self, __curtask->task_name, __curtask, __curtask->pri, P_SCHE_TOP == __curtask->pri_policy ? 
			"top" : "back", self->ntasks_done, pool->npendings);
#endif	
	return 1;
}

static int
tpool_thread_entry(void *arg) {
	int structure_release = 0;
	struct tpool_thread_t *self = (struct tpool_thread_t *)arg;
	struct tpool_t *pool = self->pool;
	
	tpool_thread_setstatus(self, THREAD_STAT_JOIN);	
	for (;;) {
		tpool_schedule(pool, self);
		
		/* The exiting threads may be reused by our pool.
		 *  <see @tpool_add_threads for more details>
		 */
		OSPX_pthread_mutex_lock(&pool->mut);
		if (!self->run) {
			assert(THREAD_STAT_RM & self->status);
			tpool_thread_setstatus_l(self, THREAD_STAT_LEAVE);
			
			if (self->structure_release)
				structure_release = 1;
			else {
				/* Reset the thread structure and push it back to
				 * the free queue.
				 */
				INIT_thread_structure(pool, self, 0);
				list_add_tail(&self->link_free, &pool->freelst);
			}

			OSPX_pthread_mutex_unlock(&self->pool->mut);
			break;
		}
		OSPX_pthread_mutex_unlock(&pool->mut);
	}

	/* We should decrease the references since we 
	 * have increased it in the @tpool_add_thread.
	 */	
	tpool_release_ex(pool, 0, 0);
	
	if (structure_release)
		free(self);	
	return 0;
}

static inline void
thcreater_exception_handler(struct tpool_thread_t *self) {
	int release;
	struct tpool_thread_t *th;
	
	while (!list_empty(&self->thq)) {
		list_del(&th->link_free);
		th = list_entry(self->thq.next, struct tpool_thread_t, link_free);
		release = th->structure_release;

		OSPX_pthread_mutex_lock(&self->pool->mut);
		/* Remove current thread from the RM queue */
		if (THREAD_STAT_RM & th->status) {
			assert(self->pool->nthreads_dying > 0 && !th->run);
			
			/* Give @tpool_adjust_wait a notification */
			if (!-- self->pool->nthreads_dying)
				OSPX_pthread_cond_broadcast(&self->pool->cond_ths);
		} else {
			-- self->pool->nthreads_real_pool;
			/* Give @tpool_release_ex a notification */
			if (list_empty(&self->pool->ths))
				OSPX_pthread_cond_broadcast(&self->pool->cond_ths);
		}

		if (!release)
			list_add_tail(&th->link_free, &self->pool->freelst);
		
		/* Remove current thread from the THREAD queue */
		list_del(&th->link);
		-- self->pool->n_qths;
		tpool_release_l(self->pool, 0, dummy_null_lptr);
		OSPX_pthread_mutex_unlock(&self->pool->mut);
			
		if (release)
			free(th);
	}
}

static inline void
do_create_threads(struct tpool_thread_t *self) {
	int error = 0;

	/* Do pthread_create here */
	while (!list_empty(&self->thq)) {
		struct tpool_thread_t *th = list_entry(self->thq.next, 
					struct tpool_thread_t, link_free);
		
		list_del(&th->link_free);	
		if ((error = OSPX_pthread_create(&th->thread_id, 0, tpool_thread_entry, th))) {
			__SHOW_ERR__("pthread_create error");
			list_add_tail(&th->link_free, &self->thq);
			break;
		}
		#ifndef NDEBUG	
			fprintf(stderr, "create THREAD:0x%p\n", th);
		#endif		
	}

	if (error)
		thcreater_exception_handler(self);
}

static void 
tpool_schedule(struct tpool_t *pool, struct tpool_thread_t *self) {	
	int  code;

	for (;self->run;) {
		OSPX_pthread_mutex_lock(&pool->mut);	
		/* Get a task to execute */
		if (!tpool_gettask(pool, self)) {
			/* Check whether we should exit now before our's waiting for tasks. */
			if (!self->run) {
				OSPX_pthread_mutex_unlock(&pool->mut);
				break;
			}
			tpool_get_restto(pool, self, &self->last_to);
			if (!self->last_to) 
				tpool_thread_setstatus_l(self, THREAD_STAT_FORCE_QUIT);
			else {
				tpool_thread_setstatus_l(self, THREAD_STAT_WAIT);		
				/* Queue ourself if we have not gotten a task */
				if (self->last_to > 0) 	
					errno = OSPX_pthread_cond_timedwait(&pool->cond, &pool->mut, &self->last_to);
				else
					errno = OSPX_pthread_cond_wait(&pool->cond, &pool->mut);
				/* Adjust the thread status if we have been woken up */
				tpool_thread_setstatus_l(self, (ETIMEDOUT == errno) ? 
										THREAD_STAT_TIMEDOUT : THREAD_STAT_FREE);
			}
			OSPX_pthread_mutex_unlock(&pool->mut);	
			continue;
		}
		OSPX_pthread_mutex_unlock(&pool->mut);
		
		/* Check the thread queue */
		if (!list_empty(&self->thq)) 
			do_create_threads(self);
		
		self->task_complete = __curtask->task_complete;
		__curtask->pdetached = &self->detached;

		/* Run the task if the task is not marked with DISPATCHED */
		if (!(TASK_TYPE_DISPATCHED & self->task_type))
			code = __curtask->task_run(__curtask);
		
		if (TASK_TYPE_GC & self->task_type) {
			if (__curtask->task_complete)
				__curtask->task_complete(__curtask, TASK_VMARK_DONE, code);
		} else 
			/* Dispatch the task */
			tpool_task_complete(pool, self, __curtask, 
					(self->task_type & TASK_TYPE_DISPATCHED) ? POOL_TASK_ERR_REMOVED :
					code);
	}
}

static void
tpool_task_complete(struct tpool_t *pool, struct tpool_thread_t *self, struct task_t *ptsk, int task_code) {	
	assert(self || ptsk->task_complete);

	/* Call the completition routine to dispatch the result */
	if (self) {
		if (self->task_complete) 	
			self->task_complete(ptsk, (self->task_type & TASK_TYPE_DISPATCHED) ?
				ptsk->f_vmflags : (ptsk->f_vmflags | TASK_VMARK_DONE), task_code);		
		
		if (self->detached) {
			self->detached = 0;	
			tpool_thread_setstatus(self, THREAD_STAT_COMPLETE);
			return;
		}
	} else {
		uint8_t detached = 0;
		
		/* Set the variable address for the task */
		ptsk->pdetached = &detached;
		ptsk->task_complete(ptsk, ptsk->f_vmflags, task_code);
	
		/* We do nothing here if the task has been detached */
		if (detached)
			return;	
	} 

	OSPX_pthread_mutex_lock(&pool->mut);	
	/* We decrease the @ndispatchings if the task has been marked with
	 * TASK_VMARK_REMOVE 
	 */
	if (TASK_VMARK_REMOVE & ptsk->f_vmflags) {
		assert(pool->ndispatchings > 0);
		-- pool->ndispatchings;		
	} 	
	assert(!((TASK_VMARK_REMOVE & ptsk->f_vmflags) &&
		   (TASK_VMARK_DO_AGAIN & ptsk->f_vmflags)));
	
	/* We deliver the task into the ready queue if
	 * the user wants to reschedule it again
	 */
	if ((TASK_VMARK_DO_AGAIN & ptsk->f_vmflags) || ptsk->do_again) {				
		/* Remove the trace record */
		if (self && !(THREAD_STAT_RM & self->status)) {
			/* @tpool_add_task_l may create threads to provide service,
			 * we increase the rescheduling tasks counter before the task's
			 * being delived to the task queue to make sure that the pool
			 * can compute the service threads number accurately.
			 */
			++ pool->nthreads_going_rescheduling;
			tpool_add_task_l(pool, ptsk);	
			
			/* We decrease the rescheduling tasks counter if the task
			 * has been added into the task queue. */
			-- pool->nthreads_going_rescheduling;
		} else
			tpool_add_task_l(pool, ptsk);	
		
	} else {
		/* Reset the status of the task */
		ptsk->f_stat = 0;
		list_del(&ptsk->trace_link);
		-- pool->n_qtrace;

		/* We triggle a event for @tpool_task_wait(pool, NULL, -1), @tpool_suspend(pool, 1)*/
		TRY_wakeup_waiters(pool, ptsk);
		if ((ptsk->f_mask & TASK_F_ONCE) && !ptsk->ref)
			tpool_delete_task_l(pool, ptsk);
	}

	if (self) 
		tpool_thread_setstatus_l(self, THREAD_STAT_COMPLETE);	
	OSPX_pthread_mutex_unlock(&pool->mut);			
}


static inline int
tpool_add_threads(struct tpool_t *pool, struct tpool_thread_t *th, int nthreads, long lflags /* reserved */) {
	int n;
	
	assert(!lflags);
	
	/* Check whether we can reuse the dying threads */
	if (nthreads > 0 && pool->nthreads_dying) {
		struct tpool_thread_t *th;
		
		list_for_each_entry(th, &pool->ths, struct tpool_thread_t, link) {
			if (!(THREAD_STAT_RM & th->status)) 
				continue;
			assert(!th->run);
			th->run = 1;
			th->status &= ~THREAD_STAT_RM;
			th->status |= lflags;

			switch (th->status & ~THREAD_STAT_INNER) {
			case THREAD_STAT_RUN:
				-- pool->nthreads_dying_run;
				break;
			case THREAD_STAT_WAIT:
				++ pool->nthreads_real_sleeping;
				break;
			default:
				;
			}
			++ pool->nthreads_real_pool;
			
			/* Give a notification to @tpool_adjust_wait */
			if (!-- pool->nthreads_dying) 
				OSPX_pthread_cond_broadcast(&pool->cond_ths);
					
			-- nthreads;
			if (!pool->nthreads_dying || !nthreads)
				break;
		}

		/* Try to wake up the service threads if there are tasks
		 * wait for being scheduled. */
		if (pool->nthreads_real_sleeping && 
			((!pool->paused && pool->npendings) || !list_empty(&pool->dispatch_q))) {
			OSPX_pthread_cond_broadcast(&pool->cond);
			pool->nthreads_waiters = 0 ;
		}
	}
	
	/* Actually, In order to reduce the time to hold the global lock,
	 * we'll try to just add the threads structure into the threads sets, 
	 * and then we call @pthread_create in the background. 
	 */
	for (n=0; n<nthreads; n++) {
		struct tpool_thread_t *self;
		
		if (list_empty(&pool->freelst)) {
			self = (struct tpool_thread_t *)malloc(sizeof(*self));
			if (!self) {
				errno = ENOMEM;
				__SHOW_ERR__(__FUNCTION__);
				break;
			}
			INIT_thread_structure(pool, self, 1);	
		} else {
			self = list_entry(pool->freelst.next, struct tpool_thread_t, link_free);
			list_del(&self->link_free);
			assert(!self->structure_release);
		}

		/* We increase the reference to make sure that 
		 * the task pool object will always exist before 
		 * the service thread's exitting.
		 */
		tpool_addref_l(pool, 0, dummy_null_lptr);	
		if (!th) {
			if ((errno = OSPX_pthread_create(&self->thread_id, 0, tpool_thread_entry, self))) {
				__SHOW_ERR__("pthread_create error");
				tpool_release_l(pool, 0, dummy_null_lptr);
				if (self->structure_release)
					free(self);
				else 
					list_add_tail(&self->link_free, &pool->freelst);
				break;
			}
			#ifndef NDEBUG	
				fprintf(stderr, "create THREAD:0x%x\n", (int)self);
			#endif		
		} else  
			list_add_tail(&self->link_free, &th->thq);	
		
		list_add_tail(&self->link, &pool->ths);
		++ pool->n_qths;
	}
	pool->nthreads_real_pool += n;
	
	/* Update the statics report */
	if (pool->nthreads_peak < pool->nthreads_real_pool) 
		pool->nthreads_peak = pool->nthreads_real_pool;
	
	return n;
}

/* The core algorithms to create threads 
 *
 * Almost only a few parts of codes will be executed,
 * Although there are so many codes in this function, 
 * so we mark the function with inline.
 */
static inline void 
tpool_increase_threads(struct tpool_t *pool, struct tpool_thread_t *self) {	
	int64_t ntasks_pending = pool->paused ? pool->n_qdispatch : pool->npendings;
	
	/* Check the pool status */
	assert(ntasks_pending); 
		
	/* We forbidden the same thread creating working threads continuously */
	if (pool->launcher) {
		if ((self && self->thread_id == pool->launcher) ||
			(!self && OSPX_pthread_id() == pool->launcher)) {
			pool->launcher = 0;
			return;
		}
		pool->launcher = 0;
	}

	/* We wake up the sleeping threads */
	assert(pool->nthreads_waiters >= 0 && 
		pool->nthreads_waiters <= pool->n_qths_wait);
	if (!list_empty(&pool->ths_waitq)) {
		/* Caculate the number of threads who should be woke up to provide services */
		ntasks_pending -= (pool->nthreads_real_pool + pool->nthreads_going_rescheduling 
			
			/* We decrease the number of threads who is running or sleeping */
			+ pool->nthreads_dying_run - pool->nthreads_running - pool->nthreads_real_sleeping 

			/* We should decrease the number of threads that has been woke up by us */
			+ pool->n_qths_wait - pool->nthreads_waiters);
		
		if (ntasks_pending > 0 && pool->nthreads_waiters == pool->n_qths_wait) {
			int nwake = (int)min(ntasks_pending, 3);
			/* NOTE:
			 * 		We do not care about that which thread will be woke up, we just 
			 * wake up no more than 3 threads to provide services. so the threads
			 * should check the pool status again if they are going to exit after 
			 * its having been woke up.
			 */
			if (pool->nthreads_waiters  >= nwake) {
				pool->nthreads_waiters -= nwake;
				
				/* We just simply give threads a notification */
				for (;nwake > 0; --nwake)
					OSPX_pthread_cond_signal(&pool->cond);
			} else {
				pool->nthreads_waiters = 0;
				OSPX_pthread_cond_broadcast(&pool->cond);
			}
			pool->launcher = self ? self->thread_id : OSPX_pthread_id();
		} 
		return;
	}
	
	assert(pool->nthreads_running >= pool->nthreads_going_rescheduling &&
		   pool->nthreads_real_pool <= pool->n_qths);
	
	/* Verify the @maxthreads */
	if (pool->maxthreads > pool->nthreads_real_pool) {
		int curthreads_pool_free = pool->nthreads_real_pool + pool->nthreads_going_rescheduling 
			+ pool->nthreads_dying_run - pool->nthreads_running;
		assert(curthreads_pool_free >= 0);
		
		/* Compute the number of threads who should be
		 * created to provide services */
		if (curthreads_pool_free < pool->limit_threads_free) {	
			int n = pool->maxthreads - pool->nthreads_real_pool;
			/* Compute the number of threads who is should be created
			 * according to the @limit_threads_free. */
			int nthreads = pool->limit_threads_free - curthreads_pool_free;	
			assert(n >= 0);
			if (n < nthreads)
				nthreads = n;

			/* Acquire the number of pending tasks */
			assert(pool->npendings >= pool->n_qdispatch);	
			ntasks_pending = pool->npendings - pool->n_qdispatch;
			if (!list_empty(&pool->dispatch_q)) 
				ntasks_pending += 1;
			
			/* Create the service threads propriately according to the 
			 * threads' status. */
			n = ntasks_pending - curthreads_pool_free;
			if (n < nthreads) 
				nthreads = n;

			if (nthreads > 0) {
				if (nthreads > pool->limit_threads_create_per_time)
					nthreads = pool->limit_threads_create_per_time;
				
				/* Call tpool_add_threads to create @nthreads threads to 
				 * provide service */
				nthreads = tpool_add_threads(pool, self, nthreads, 0);		
				pool->launcher = self ? self->thread_id : OSPX_pthread_id();
			}
		}
	}	
}

