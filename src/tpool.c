#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if !defined(NDEBUG) && !defined(_WIN32)
#include <sys/prctl.h>
#endif

#ifdef _USE_MPOOL	
	#include "mpool.h"
#endif
#include "tpool.h"
#include "ospx_errno.h"

#ifndef min
/* VS has defined the MARCO in stdlib.h */
#define min(a, b) ((a) < (b)) ? (a) : (b)
#endif

#ifndef max
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

#define RUNNING(pool) (pool)->nthreads_running
#define REAL(pool)    (pool)->nthreads_real_pool

#define tpool_thread_setstatus(self, status)    tpool_thread_status_change(self->pool, self, status, 0)
#define tpool_thread_setstatus_l(self, status)  tpool_thread_status_change(self->pool, self, status, 1)

#if defined(_USE_MPOOL) && !defined(NDEBUG)
static void tpool_verify(struct tpool_t *pool, struct task_ex_t *tskex);
static void tpool_verifyq(struct tpool_t *pool, XLIST *assertq);
#else
#define tpool_verify(pool, tskex)
#define tpool_verifyq(pool, tskex)
#endif

#ifdef _CLEAN_RUBBISH_INBACKGROUND
/* NOTE:
 * 	  @tpool_rubbish_clean is a routine who is responsible for
 * recycling the memory that has been allocated for storing the
 * tasks' informations. it can improve our perfermance since our
 * working threads will not free the tasks by itself, it means
 * that our working threads will not take so much times to wait 
 * for the lock to free the tasks. so we expect that the pool will
 * go faster.
 */
static int tpool_rubbish_clean(void *);

/* @CLEAN_1 is used to recycle one task */
#define CLEAN_1(pool, tskex) \
	do {\
		int _xnotify = XLIST_EMPTY(&(pool)->clq); \
		tpool_verify(pool, tskex);\
		XLIST_PUSHBACK(&(pool)->clq, &(tskex)->wait_link);\
		tpool_verifyq(pool, &(pool)->clq);\
		if (!(pool)->rubbish_run) {\
			OSPX_pthread_t _xdummy; \
			if ((errno = OSPX_pthread_create(&_xdummy, 0, tpool_rubbish_clean, pool))) {\
				__SHOW_ERR__("pthread_create");\
				tpool_delete_tasks(pool, &(pool)->clq); \
			} else \
				(pool)->rubbish_run = 1; \
		} else if (_xnotify) \
			OSPX_pthread_cond_signal(&pool->cond_garbage); \
	} while (0)

/* @CLEAN_2 is used to recycle a task queue */
#define CLEAN_2(pool, deleteq) \
	do {\
		int _xnotify = XLIST_EMPTY(&(pool)->clq); \
		tpool_verifyq(pool, deleteq);\
		XLIST_MERGE(&(pool)->clq, deleteq); \
		tpool_verifyq(pool, &(pool)->clq);\
		if (!(pool)->rubbish_run) {\
			OSPX_pthread_t _xdummy; \
			if ((errno = OSPX_pthread_create(&_xdummy, 0, tpool_rubbish_clean, pool))) {\
				__SHOW_ERR__("pthread_create");\
				tpool_delete_tasks(pool, &(pool)->clq); \
			} else \
				(pool)->rubbish_run = 1; \
		} else if (_xnotify) \
			OSPX_pthread_cond_signal(&pool->cond_garbage); \
	} while (0)
#endif

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

static struct task_ex_t *tpool_new_task(struct tpool_t *pool, struct task_t *task); 
static void tpool_delete_task(struct tpool_t *pool, struct task_ex_t *taskex);
static void tpool_delete_tasks(struct tpool_t *pool, XLIST *deleteq); 
static long tpool_release_ex(struct tpool_t *pool, int decrease_user, int wait_threads_on_clean);
static int  tpool_add_task_ex(struct tpool_t *pool, struct task_ex_t *tskex, int pri, int pri_policy);
static void tpool_increase_threads(struct tpool_t *pool, struct tpool_thread_t *self);
static int  tpool_add_threads(struct tpool_t *pool, int nthreads, long lflags); 
static void tpool_schedule(struct tpool_t *pool, struct tpool_thread_t *self);
static void tpool_thread_status_change(struct tpool_t *pool, struct tpool_thread_t *self, long status, int synchronized);
static int  tpool_gettask(struct tpool_t *pool, struct tpool_thread_t *self);
static int  tpool_ev_busy(struct tpool_t *pool);
static int  tpool_ev_wait_l(struct tpool_t *pool, long ms, int return_if_wokeup);

/* Anonymous tasks */
struct tpool_task_t {
	struct task_t task;
	int (*task_run)(void *arg);
	int (*task_complete)(long vmflags, int task_code, void *arg, struct priority_t *pri);
}; 

static int 
tpool_task_default_run(struct task_t *tsk) {
	struct tpool_task_t *p_task = (struct tpool_task_t *)tsk;

	return p_task->task_run(p_task->task.task_arg);
}

static int 
tpool_task_default_complete(struct task_t *tsk, long vmflags, int task_code, struct priority_t *pri) {
	struct tpool_task_t *p_task = (struct tpool_task_t *)tsk;
	
	return p_task->task_complete(vmflags, task_code, p_task->task.task_arg, pri);
}

static void
tpool_fill_task(struct tpool_t *pool, struct tpool_task_t *p_task, 
	const char *task_name, int (*task_run)(void *), int (*task_complete)(long, int, void *, struct priority_t *), void *arg) {
	assert(p_task && task_run);

	p_task->task.task_name = task_name;
	p_task->task.task_run  = tpool_task_default_run;
	p_task->task.task_arg  = arg;

	/* Save the user's proc */
	p_task->task_run = task_run;
	
	if (task_complete) {
		p_task->task.task_complete = tpool_task_default_complete;
		p_task->task_complete = task_complete;
	} else
		p_task->task.task_complete = NULL;
}

void
tpool_extract(struct task_t *task, void **task_run, void **task_complete, void **task_arg) {
	if (task->task_run == tpool_task_default_run) {
		/* Acquire the real object address */
		struct tpool_task_t *tptask = XCOBJEX(task, struct tpool_task_t, task);

		if (task_run)
			*task_run = (void *)tptask->task_run;
		
		if (task_complete)
			*task_complete = (void *)tptask->task_complete;	
	} else {
		if (task_run)
			*task_run = (void *)task->task_run;
		
		if (task_complete)
			*task_complete = (void *)task->task_complete;
	}	
	
	if (task_arg)
		*task_arg = task->task_arg;
}

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

/* @FIX me.
 */
static void
tpool_init_cpuinfo(struct tpool_t *pool) {
	int i, sum;

	pool->ncpus = 1;
	pool->cpu = (struct cpu_t *)malloc(pool->ncpus * sizeof(struct cpu_t));	
	
	if (!pool->cpu) {
		pool->ntasks_cpu_suggest = pool->ncpus * 2;
		pool->ncpu_threads = pool->ncpus * 2;
	} else {
#ifdef _WIN32
		SYSTEM_INFO sysi;

		GetSystemInfo(&sysi);
		pool->cpu[0].nlogic_cores = sysi.dwNumberOfProcessors;
#else
		pool->cpu[0].nlogic_cores = sysconf(_SC_NPROCESSORS_ONLN);
		if (pool->cpu[0].nlogic_cores < 0) 
			pool->cpu[0].nlogic_cores = 2;
#endif	
		pool->cpu[0].physical_cpu_id = 0;
		pool->cpu[0].nphysical_cores = pool->cpu[0].nlogic_cores;
		pool->cpu[0].nthreads_parallel = pool->cpu[0].nlogic_cores;
		pool->cpu[0].ht_support = (pool->cpu[0].nlogic_cores > pool->cpu[0].nphysical_cores);
		
		for (i=0, sum=0; i<pool->ncpus; i++)
			sum += pool->cpu[i].nthreads_parallel;
		pool->ncpu_threads = sum;

		if (sum >= 3 && sum > pool->ncpus)
			pool->ntasks_cpu_suggest = sum * 2 / 3;
		else
			pool->ntasks_cpu_suggest = max(2, sum);
	}
}

#ifndef NDEBUG
#define INIT_thread_structure(pool, self, release) \
	do {\
		(self)->structure_release = release;\
		(self)->ntasks_done = 0;\
		(self)->ncont_rest_counters = 0;\
		(self)->status = THREAD_STAT_INIT;\
		(self)->pool = pool;\
		(self)->run = 1;\
	} while (0)
#else
#define INIT_thread_structure(pool, self, release) \
	do {\
		(self)->structure_release = release;\
		(self)->ncont_rest_counters = 0;\
		(self)->status = THREAD_STAT_INIT;\
		(self)->pool = pool;\
		(self)->run = 1;\
	} while (0)
#endif

int  
tpool_create(struct tpool_t  *pool, int q_pri, int maxthreads, int minthreads, int suspend) {
	int  error, index, mem;
	struct tpool_thread_t *ths;

	/* Connrect the param */
	if (maxthreads < minthreads)
		minthreads = maxthreads;
	memset(pool, 0, sizeof(*pool)); 
	
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
	XLIST_INIT(&pool->freelst);
	ths = (struct tpool_thread_t *)malloc(index * sizeof(struct tpool_thread_t));
	if (ths) {
		for (--index;index>=0; --index) {
			INIT_thread_structure(pool, &ths[index], 0);
			XLIST_PUSHBACK(&pool->freelst, &ths[index].link_free);
		}
		pool->buffer = (char *)ths;
	}

	/* Initialize the queue */
	XLIST_INIT(&pool->ths);
	XLIST_INIT(&pool->ths_waitq);
	XLIST_INIT(&pool->ready_q);
	XLIST_INIT(&pool->trace_q);
	XLIST_INIT(&pool->dispatch_q);
	
	error = POOL_ERR_ERRNO;
	if ((errno = OSPX_pthread_cond_init(&pool->cond)))
		goto err1;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_comp)))
		goto err2;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_ths)))
		goto err3;
	
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
		goto err4;
	}
	if ((errno = OSPX_pthread_cond_init(&pool->cond_ev)))
		goto err5;
	
#ifdef _CLEAN_RUBBISH_INBACKGROUND
	pool->rubbish_run = 0;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_garbage)))
		goto err6;	
	XLIST_INIT(&pool->clq);
#endif	

#ifdef _OPTIMIZE_PTHREAD_CREATE
	pool->launcher_run = 0;
	if ((errno = OSPX_pthread_cond_init(&pool->cond_launcher)))
		goto err7;	
	XLIST_INIT(&pool->launcherq);
#endif

	for (index=0; index<pool->pri_q_num; index++) {
		XLIST_INIT(&pool->pri_q[index].task_q);
		pool->pri_q[index].index = index;
	}
	pool->avg_pri = 100 / pool->pri_q_num;
	pool->pri_reschedule = 100 * 5 / 6;
	tpool_setstatus(pool, POOL_F_CREATED, 0);
	
	/* Acquire the CPUs' information */
	tpool_init_cpuinfo(pool);
	tpool_load_env(pool);

#ifndef NDEBUG
	fprintf(stderr, ">@limit_threads_free=%d\n"
				    ">@limit_threads_create_per_time=%d\n"
					">@threads_randtimeo=%ld\n"
					">@threads_wait_throttle=%d\n",
			pool->limit_threads_free, 
			pool->limit_threads_create_per_time,
			pool->randtimeo,
			pool->threads_wait_throttle);
#endif
	/* Start up the reserved threads */
	if (pool->minthreads > 0) {
		OSPX_pthread_mutex_lock(&pool->mut);
		tpool_add_threads(pool, pool->minthreads, 0);
		OSPX_pthread_mutex_unlock(&pool->mut);
	}
	
	return 0;

#ifdef _OPTIMIZE_PTHREAD_CREATE
err7:
#ifdef _CLEAN_RUBBISH_INBACKGROUND
	OSPX_pthread_cond_destroy(&pool->cond_garbage);
#else	
#endif
#endif

#ifdef _CLEAN_RUBBISH_INBACKGROUND
err6:
#endif
	OSPX_pthread_cond_destroy(&pool->cond_ev);
err5:
	free(pool->pri_q);
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
#ifdef _USE_MPOOL	
	OSPX_pthread_mutex_lock(&pool->mut);
	if (POOL_F_CREATED & pool->status) {
		if (!pool->mp1) {
			struct mpool_t *mp1 = (struct mpool_t *)malloc(sizeof(*mp1));
			
			if (mp1) {
				if (mpool_init(mp1, sizeof(struct task_ex_t))) {
					__SHOW_WARNING__("mpool_init");
					free(mp1);
				} else
					pool->mp1 = mp1;
			}
		}
		
		if (!pool->mp2) {
			struct mpool_t *mp2 = malloc(sizeof(*mp2));
			
			if (mp2) {
				if (mpool_init(mp2, sizeof(struct task_ex_t) + sizeof(struct tpool_task_t))) {
					__SHOW_WARNING__("mpool_init");
					free(mp2);
				} else
					pool->mp2 = mp2;
			}
		}
	}
	OSPX_pthread_mutex_unlock(&pool->mut);	
#else
	fprintf(stderr, "The pool does not support mpool: use -D_USE_MPOOL to"
			" complier the library\n");
#endif
}

void 
tpool_load_env(struct tpool_t *pool) {
	const char *env;
	
	/* Load the @limit_threads_free from the env */
	env = getenv("LIMIT_THREADS_FREE");
	if (!env || atoi(env) < 0)
		pool->limit_threads_free = pool->ntasks_cpu_suggest;
	else 
		pool->limit_threads_free = atoi(env);

	/* Load the @limit_threads_create_per_time from the env */
	env = getenv("LIMIT_THREADS_CREATE_PER_TIME");
	if (!env || atoi(env) <= 0)	
		pool->limit_threads_create_per_time = min(4, max(2, pool->ncpus - 1));
	else
		pool->limit_threads_create_per_time = atoi(env);
	
	/* Load the @randomtimeo */
	env = getenv("THREADS_RANDTIMEO");
	if (!env || atoi(env) <= 0)	
		pool->randtimeo = 60;
	else
		pool->randtimeo = atoi(env);
	
	env = getenv("THREADS_WAIT_THROTTLE");
	if (!env || atoi(env) <= 0)	
		pool->threads_wait_throttle = 9;
	else
		pool->threads_wait_throttle = atoi(env);
}

static void 
tpool_free(struct tpool_t *pool) {
	assert(XLIST_EMPTY(&pool->ths) &&
	       XLIST_EMPTY(&pool->trace_q) &&
		   !RUNNING(pool) &&
		   (!pool->nthreads_real_sleeping) &&
		   (!pool->npendings) &&
		   (!pool->ndispatchings)); 	
	free(pool->pri_q);
	
#ifdef _CLEAN_RUBBISH_INBACKGROUND
	OSPX_pthread_mutex_lock(&pool->mut_garbage);
	for (;pool->rubbish_run;) {
		OSPX_pthread_cond_signal(&pool->cond_garbage);
		OSPX_pthread_cond_wait(&pool->cond_ths, &pool->mut_garbage);
	}
	OSPX_pthread_mutex_unlock(&pool->mut_garbage);
	tpool_delete_tasks(pool, &pool->clq);	
	OSPX_pthread_cond_destroy(&pool->cond_garbage);
#endif	

#ifdef _OPTIMIZE_PTHREAD_CREATE
	OSPX_pthread_mutex_lock(&pool->mut_launcher);
	for (;pool->launcher_run;) {
		OSPX_pthread_cond_signal(&pool->cond_launcher);
		OSPX_pthread_cond_wait(&pool->cond_ths, &pool->mut_launcher);
	}
	OSPX_pthread_mutex_unlock(&pool->mut_launcher);
	assert(XLIST_EMPTY(&pool->launcherq));
	OSPX_pthread_cond_destroy(&pool->cond_launcher);
#endif

#ifdef _USE_MPOOL	
	if (pool->mp1) {
#ifndef NDEBUG
		fprintf(stderr, "-----MP1----\n%s\n",
			mpool_stat_print(pool->mp1, NULL, 0));
#endif	
		mpool_destroy(pool->mp1, 1);
		free(pool->mp1);
	}
	if (pool->mp2) {
#ifndef NDEBUG
		fprintf(stderr, "-----MP2----\n%s\n",
			mpool_stat_print(pool->mp2, NULL, 0));
#endif	
		mpool_destroy(pool->mp2, 1);
		free(pool->mp2);
	}
#endif
	/* Free the preallocated memory */
	if (pool->buffer)
		free(pool->buffer);
	if (pool->cpu)
		free(pool->cpu);
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
			struct xlink *link;
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
			XLIST_FOREACH(&pool->trace_q, &link) {
				POOL_TRACEQ_task(link)->f_vmflags |= TASK_VMARK_POOL_DESTROYING;
			}
			
			/* Wake up all filter waiters */	
			OSPX_pthread_cond_broadcast(&pool->cond_ev);
			
			/* Are we responsible for cleaning the resources ? */
			if (XLIST_EMPTY(&pool->ths)) {
				/* @tpool_resume can not work if the pool is being destroyed. 
                 * (see @tpool_resume for more details)
				 */
				if (!XLIST_EMPTY(&pool->ready_q)) {
					assert(pool->paused);
					
					/* We create service threads to dispatch the task in the 
					 * background if there are one more tasks existing in the 
					 * pool.
					 */
					OSPX_pthread_mutex_unlock(&pool->mut);
					tpool_remove_pending_task2(pool, NULL);
					OSPX_pthread_mutex_lock(&pool->mut);
					clean = XLIST_EMPTY(&pool->ths);
				} else
					clean = 1;
				if (clean)
					pool->release_cleaning = 1;
			}

			/* Wake up all working threads */
			OSPX_pthread_cond_broadcast(&pool->cond);
			if (wait_threads_on_clean) {
				/* Tell the pool that we are responsible for 
				 * releasing the resources.
				 */
				clean = 1;
				assert(!pool->release_cleaning);
				pool->release_cleaning = 1;
				for (;!XLIST_EMPTY(&pool->ths);) {
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
		
		assert(XLIST_EMPTY(&pool->ths)); 	
		tpool_setstatus(pool, POOL_F_DESTROYED, 0);		
		
		/* Remove all pending tasks since none servering threads
		 * existing in the pool.
		 */
		if (pool->paused)
			tpool_remove_pending_task(pool, NULL);	
		/* @tpool_remove_pending_task may be called by user,
		 * so we call @tpool_wait here to make sure that all
		 * tasks have been done before our's destroying the
		 * pool env.
		 */
		tpool_wait(pool, NULL, -1);
	
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
	stat->curthreads = XLIST_SIZE(&pool->ths);
	stat->curthreads_active = RUNNING(pool);
	stat->curthreads_dying  = pool->nthreads_dying;
	stat->acttimeo = pool->acttimeo;
	stat->threads_peak = pool->nthreads_peak;
	stat->tasks_peak = pool->ntasks_peak;
	stat->tasks_added = pool->ntasks_added;
	stat->tasks_done  = pool->ntasks_done;
	stat->tasks_dispatched = pool->ntasks_dispatched;
	stat->cur_tasks = XLIST_SIZE(&pool->trace_q);
	stat->cur_tasks_pending = pool->npendings - XLIST_SIZE(&pool->dispatch_q);   
	stat->cur_tasks_scheduling = RUNNING(pool);
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

static int
tpool_tskstat_walk(struct tpool_tskstat_t *stat, void *arg) {
	*(struct tpool_tskstat_t *)arg = *stat;
	
	/* We have found our task, so we do not need to 
	 * scan the queue any more */
	return -1; 
}

long 
tpool_gettskstat(struct tpool_t *pool, struct tpool_tskstat_t *st) {
	st->stat = 0;	

	tpool_mark_task(pool, st->task, tpool_tskstat_walk, (void *)st);
	
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
	nthreads_pool = REAL(pool);
	nthreads_pool_free = nthreads_pool - RUNNING(pool) - pool->nthreads_dying_run;
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
		tpool_add_threads(pool, nthreads, 0);

	else if (nthreads < 0) {
		struct xlink *link;
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
		XLIST_FOREACH(&pool->ths, &link) {
			struct tpool_thread_t *th = POOL_Q_thread(link);
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
					-- waitthreads_dec;;
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
	}

	/* Reset the statics report */
	pool->nthreads_peak = REAL(pool);
	pool->ntasks_peak   = pool->npendings;
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

#define TRY_wakeup_filter(pool) \
	do {\
		if (!(pool)->ev_filter_wokeup) {\
			switch (pool->ev.ev_triggle_type) {\
			case EV_TRG_THREADS:\
				(pool)->ev_filter_wokeup = RUNNING(pool) < (pool)->ev.ev_threads_num; \
				break;\
			case EV_TRG_TASKS: \
				(pool)->ev_filter_wokeup = (pool)->npendings < (pool)->ev.ev_tasks_num; \
				break;\
			case EV_TRG_THREADS_OR_TASKS:\
				(pool)->ev_filter_wokeup = (RUNNING(pool) < (pool)->ev.ev_threads_num) || \
								((pool)->npendings < (pool)->ev.ev_tasks_num); \
				break;\
			case EV_TRG_THREADS_AND_TASKS:\
				(pool)->ev_filter_wokeup = (RUNNING(pool) < (pool)->ev.ev_threads_num) && \
								((pool)->npendings < (pool)->ev.ev_tasks_num); \
				break;\
			}\
			if ((pool)->ev_filter_wokeup) \
				OSPX_pthread_cond_broadcast(&(pool)->cond_ev);\
		}\
	} while (0)

int
tpool_flush(struct tpool_t *pool) {
	int n = 0, exitthreads, exitthreads_free;

	OSPX_pthread_mutex_lock(&pool->mut);
	exitthreads = REAL(pool) - pool->minthreads;
	if (exitthreads > 0 && pool->nthreads_real_sleeping > 0) {
		struct xlink *link;

		if (exitthreads <= pool->nthreads_real_sleeping) 
			exitthreads_free = 0;
		else {
			int curthreads_pool_running, curthreads_pool_free;

			exitthreads_free = exitthreads - pool->nthreads_real_sleeping;
			exitthreads = pool->nthreads_real_sleeping;
	
			curthreads_pool_running = RUNNING(pool) - pool->nthreads_going_rescheduling 
				- pool->nthreads_dying_run;
			assert(REAL(pool) >= curthreads_pool_running);	
			curthreads_pool_free = REAL(pool) - curthreads_pool_running;

			if (curthreads_pool_free > 0 && pool->npendings < curthreads_pool_free) 
				exitthreads_free = min(exitthreads_free, curthreads_pool_free - pool->npendings);
			else
				exitthreads_free = 0;
		}
		assert(exitthreads >= 0 && exitthreads_free >= 0);

		/* Decrease the service threads */
		XLIST_FOREACH(&pool->ths, &link) {
			struct tpool_thread_t *th = POOL_Q_thread(link);
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

#define TRY_wakeup_waiters(pool, tskex) \
	do {\
	 	if (pool->waiters_all) {\
			if ((tskex)->f_wait) {\
				(tskex)->f_wait = 0;\
				OSPX_pthread_cond_broadcast(&pool->cond_comp);\
			} else if ((pool->waiters && XLIST_EMPTY(&pool->trace_q)) ||\
				(pool->suspend_waiters && !RUNNING(pool) && !pool->ndispatchings)) {\
				OSPX_pthread_cond_broadcast(&pool->cond_comp);\
			}\
		}\
	} while (0)

static void
tpool_task_complete_nocallback_l(struct tpool_t *pool, XLIST *rmq) {
	struct xlink *link;
	struct task_ex_t *tskex;
	
	assert((pool->ndispatchings >= XLIST_SIZE(rmq)) &&
		   (pool->ndispatchings <= XLIST_SIZE(&pool->trace_q)));
	XLIST_FOREACH(rmq, &link) {
		tskex = POOL_READYQ_task(link);
		XLIST_REMOVE(&pool->trace_q, &tskex->trace_link);
		-- pool->ndispatchings;
		
		if (TASK_VMARK_REMOVE_BYPOOL & tskex->f_vmflags)
			++ pool->ntasks_dispatched;
		TRY_wakeup_waiters(pool, tskex);
	}
}

static void
tpool_task_complete(struct tpool_t *pool, struct tpool_thread_t *self, struct task_ex_t *tskex, int task_code) {
	long vmflags = tskex->f_vmflags;
	int reschedule = 0, code = 0;
	int  is_dispatched_task = TASK_VMARK_REMOVE & tskex->f_vmflags;
	struct priority_t pri = {
		tskex->pri, tskex->pri_policy
	};
			
	/* Call the complete routine to dispatch the result */
	if (tskex->tsk->task_complete) {	
		tskex->f_stat |= TASK_F_DISPATCHING;
		
		if (is_dispatched_task) {
			task_code = POOL_TASK_ERR_REMOVED;
			assert(!(tskex->f_vmflags & TASK_VMARK_DONE));
		} else
			vmflags |= TASK_VMARK_DONE;
		reschedule = tskex->tsk->task_complete(tskex->tsk, vmflags, task_code, tskex->f_pri ? &pri : NULL); 
#if 0
		/* If the task has been marked TASK_VMARK_REMOVE, we
		 * prevent it from being rescheduled again.
		 */
		if (TASK_VMARK_REMOVE & vmflags)
			reschedule = 0;
#endif
		/* Reduce the priority of the task */
		if (tskex->f_pri && reschedule && (2 != reschedule)) {
			if (tskex->pri > pool->pri_reschedule)
				pri.pri = tskex->pri - pool->pri_reschedule;
			else if (tskex->pri < 20)
				pri.pri = 0;
			else
				pri.pri = tskex->pri / 2;
			
			/* We change the policy from POLICY_PRI_SORT_INSERTBEFORE to
			 * POLICY_PRI_SORT_INSERTAFTER if the user wants the task
			 * being rescheduled.
			 */
			if (POLICY_PRI_SORT_INSERTBEFORE == pri.pri_policy)
				pri.pri_policy = POLICY_PRI_SORT_INSERTAFTER;
			else 
				pri.pri_policy = tskex->pri_policy;
		}
		
		/* Verify the marked flags of the task */
		if (reschedule && (TASK_VMARK_DISABLE_RESCHEDULE & vmflags))
			reschedule = 0;
	}	
	OSPX_pthread_mutex_lock(&pool->mut);	
	/* Remove the trace record */
	XLIST_REMOVE(&pool->trace_q, &tskex->trace_link);	
		
	/* We decrease the @ndispatchings if the task has been marked with
	 * TASK_VMARK_REMOVE 
	 */
	if (is_dispatched_task) {
		assert(pool->ndispatchings > 0);
		-- pool->ndispatchings;	
	} 	
	/* We deliver the task into the ready queue if
	 * the user wants to reschedule it again
	 */
	if (reschedule) {
		/* If the task is marked with TASK_VMARK_DISABLE_RESCHEDULE while
		 * the pool is dispatching the completion, we dispatch the completion 
		 * again to give user a notification.
		 */
		if (TASK_VMARK_DISABLE_RESCHEDULE & tskex->f_vmflags) 
			code = POOL_TASK_ERR_DISABLE_RESCHEDULE;

		else if (self && !(THREAD_STAT_RM & self->status)) {
			/* @tpool_add_task_ex may create threads to provide service,
			 * we increase the rescheduling tasks counter before the task's
			 * being delived to the task queue to make sure that the pool
			 * can compute the service threads number accurately.
			 */
			++ pool->nthreads_going_rescheduling;
			code = tpool_add_task_ex(pool, tskex, pri.pri, pri.pri_policy);	
			
			/* We decrease the rescheduling tasks counter if the task
			 * has been added into the task queue.
			 */
			-- pool->nthreads_going_rescheduling;
		} else
			code = tpool_add_task_ex(pool, tskex, pri.pri, pri.pri_policy);	
		/* We record the task again if we fail to reschedule it to make 
		 * sure that @tpool_wait works perfectly.
		 */
		if (code) {
			if (is_dispatched_task) 
				++ pool->ndispatchings;	

			vmflags = tskex->f_vmflags;
			XLIST_PUSHBACK(&pool->trace_q, &tskex->trace_link);
		}
	} 
	
	if (!code) {
		/* If we fail to reschedule the task, we are responsible for calling the
		 * complete routine to give the user a notification before our's setting 
		 * the thread status 
		 */
		if (self) 
			tpool_thread_setstatus_l(self, THREAD_STAT_COMPLETE);	
		
		/* We triggle a event for @tpool_wait(pool, NULL, -1), @tpool_suspend(pool, 1)*/
		if (!reschedule) {
			TRY_wakeup_waiters(pool, tskex);
#ifdef _CLEAN_RUBBISH_INBACKGROUND
			CLEAN_1(pool, tskex);
#endif
		}	
	}
	OSPX_pthread_mutex_unlock(&pool->mut);		
	
	/* If we get an error while delivering the task, we
	 * call @task_complete to notify the user
	 */
	if (code) {
		assert(reschedule && tskex->tsk->task_complete);
		tskex->tsk->task_complete(tskex->tsk, vmflags & ~TASK_VMARK_DONE, code, tskex->f_pri ? &pri : NULL);
		
		/* Clear the records */
		OSPX_pthread_mutex_lock(&pool->mut);
		XLIST_REMOVE(&pool->trace_q, &tskex->trace_link);	
		if (is_dispatched_task) {
			assert(0 < pool->ndispatchings);
			-- pool->ndispatchings;		
		}
		if (self)
			tpool_thread_setstatus_l(self, THREAD_STAT_COMPLETE);	
		TRY_wakeup_waiters(pool, tskex);
#ifdef _CLEAN_RUBBISH_INBACKGROUND
		CLEAN_1(pool, tskex);
#endif	
		OSPX_pthread_mutex_unlock(&pool->mut);
#ifndef _CLEAN_RUBBISH_INBACKGROUND
		tpool_delete_task(pool, tskex);
	} else if (!reschedule) {
		tpool_delete_task(pool, tskex);
#endif
	}	
}

void
tpool_rmq_dispatch(struct tpool_t *pool, XLIST *rmq, XLIST *no_callback_q, int code) {
	int ele;
	struct xlink *link;
	
	/* We dispatch the tasks who has none complete routine firstly */
	if (no_callback_q && !XLIST_EMPTY(no_callback_q)) {
		OSPX_pthread_mutex_lock(&pool->mut);	
		tpool_task_complete_nocallback_l(pool, no_callback_q);
#ifdef _CLEAN_RUBBISH_INBACKGROUND
		CLEAN_2(pool, no_callback_q);
#endif
		OSPX_pthread_mutex_unlock(&pool->mut);	
#ifndef _CLEAN_RUBBISH_INBACKGROUND
		tpool_delete_tasks(pool, no_callback_q);
#endif	
	} 
	ele = XLIST_SIZE(rmq);
	for (;ele; --ele) {
		XLIST_POPFRONT(rmq, link); 
		tpool_task_complete(pool, NULL, POOL_READYQ_task(link), code);
	}
}

static int
tpool_has_task(struct tpool_t *pool, struct task_t *tsk, struct task_ex_t **tskex) {
	int got = 0;	
	
	assert(pool->ndispatchings <= XLIST_SIZE(&pool->trace_q));
	if (!tsk)
		got = XLIST_SIZE(&pool->trace_q);
	else {
		struct xlink *link;
		
		XLIST_FOREACH(&pool->trace_q, &link) {
			if (tsk == POOL_TRACEQ_task(link)->tsk) { 
				if (tskex)
					*tskex = POOL_TRACEQ_task(link);
				got = 1;
				break;
			}
		}
	}
	
	return got;
}

#define ACQUIRE_TASK_STAT(pool, tskex, st) \
	do {\
		/* If f_removed has been set, it indicates 
		 * that the task is being dispatching.
		 */\
		if ((tskex)->f_vmflags & TASK_VMARK_REMOVE) \
			(st)->stat = TASK_F_DISPATCHING;\
		else if (TASK_F_DISPATCHING & (tskex)->f_stat)\
			(st)->stat = TASK_F_DISPATCHING;\
		else if (TASK_F_SCHEDULING & (tskex)->f_stat)\
			(st)->stat = TASK_F_SCHEDULING; \
		else if ((pool)->paused)\
			(st)->stat = TASK_F_SWAPED; \
		/* All any other cases, we regard the
		 * task as a waiter who is waiting for
		 * being scheduled by pool. 
		 */\
		else\
			(st)->stat = TASK_F_WAIT;\
		(st)->vmflags = (tskex)->f_vmflags;\
		(st)->task = (tskex)->tsk;\
		(st)->pri  = (tskex)->pri;\
	} while (0)

int  
tpool_mark_task(struct tpool_t *pool, struct task_t *tsk,
				int (*tskstat_walk)(struct tpool_tskstat_t *, void *),
				void *arg) {	
	long  vmflags;
	int ntasks = 0, removed = 0;
	XLIST rmq, no_callback_q, pool_q, *q;
	struct xlink *link;
	struct task_ex_t *tskex;
	struct tpool_tskstat_t stat;

	XLIST_INIT(&rmq);
	XLIST_INIT(&no_callback_q);
	XLIST_INIT(&pool_q);
	OSPX_pthread_mutex_lock(&pool->mut);
	XLIST_FOREACH(&pool->trace_q, &link) {
		tskex = POOL_TRACEQ_task(link);

		/* Does the task match our condition ? */
		if (tsk && tskex->tsk != tsk)
			continue;	
		++ ntasks;
		
		ACQUIRE_TASK_STAT(pool, tskex, &stat);
		vmflags = tskstat_walk(&stat, arg);
		if (-1 == vmflags)
			break;
		
		/* Set the vmflags properly */
		vmflags &= (TASK_VMARK_REMOVE|TASK_VMARK_DISABLE_RESCHEDULE);
		if (!vmflags)
			continue;
		
		/* Check whether the task should be removed */
		if ((TASK_F_WAIT|TASK_F_SWAPED) & stat.stat) {
			if (TASK_VMARK_REMOVE & vmflags) {
				if (vmflags & TASK_VMARK_REMOVE_DIRECTLY) {
					q = &rmq;
					tskex->f_vmflags |= TASK_VMARK_REMOVE_DIRECTLY;
				} else {
					q = &pool_q;
					tskex->f_vmflags |= TASK_VMARK_REMOVE_BYPOOL;
				}
				
				assert(tskex->pri_q >= 0 && tskex->pri_q < pool->pri_q_num);
				XLIST_REMOVE(&pool->pri_q[tskex->pri_q].task_q, &tskex->wait_link);
				if (XLIST_EMPTY(&pool->pri_q[tskex->pri_q].task_q))
					XLIST_REMOVE(&pool->ready_q, &pool->pri_q[tskex->pri_q].link);
				
				if (tskex->tsk->task_complete) 
					XLIST_PUSHBACK(q, &tskex->wait_link);	
				else
					XLIST_PUSHBACK(&no_callback_q, &tskex->wait_link);
				++ removed;
			}
		}

		/* Deal with the TASK_VMARK_DISABLE_RESCHEDULE mask */
		if (vmflags & TASK_VMARK_DISABLE_RESCHEDULE)
			tskex->f_vmflags |= TASK_VMARK_DISABLE_RESCHEDULE;	
	}	
	
	assert((pool->npendings >= removed) &&
		   (removed == (XLIST_SIZE(&rmq) + XLIST_SIZE(&no_callback_q) +
		                XLIST_SIZE(&pool_q))));
	pool->ndispatchings += removed;	
	pool->npendings -= removed;
		
	assert(pool->ndispatchings <= XLIST_SIZE(&pool->trace_q));
	if (!XLIST_EMPTY(&pool_q)) {
		/* Wake up threads to schedule the callback */
		pool->npendings += XLIST_SIZE(&pool_q);
		XLIST_MERGE(&pool->dispatch_q, &pool_q);
		tpool_increase_threads(pool, NULL);
	}
		
	if (!XLIST_EMPTY(&no_callback_q)) {
		tpool_task_complete_nocallback_l(pool, &no_callback_q);	
	#ifdef _CLEAN_RUBBISH_INBACKGROUND
		CLEAN_2(pool, &no_callback_q);
	#endif
	}
	
	if (pool->ev_task_waiters) 
		TRY_wakeup_filter(pool);
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* Free the task nodes */
#ifndef _CLEAN_RUBBISH_INBACKGROUND
	if (!XLIST_EMPTY(&no_callback_q))
		tpool_delete_tasks(pool, &no_callback_q);
#endif			
	if (!XLIST_EMPTY(&rmq))
		tpool_rmq_dispatch(pool, &rmq, NULL, POOL_TASK_ERR_REMOVED);	
	
	return ntasks;
}

static int
disable_rescheduling_walk(struct tpool_tskstat_t *stat, void *arg) {
	++ *(int *)arg;
	
	return TASK_VMARK_DISABLE_RESCHEDULE; 
}

int  
tpool_disable_rescheduling(struct tpool_t *pool, struct task_t *tsk) {
	int ele = 0;

	tpool_mark_task(pool, tsk, disable_rescheduling_walk, (void *)&ele);
	
	return ele;
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

int  
tpool_throttle_disabled_wait(struct tpool_t *pool, long ms) {
	int error;

	OSPX_pthread_mutex_lock(&pool->mut);
	if (!ms)
		error = pool->throttle_enabled ? 1 : 0;
	else
		for (error=0;;) {
			if (!pool->throttle_enabled || !(POOL_F_CREATED & pool->status)) {
				error = pool->throttle_enabled ? 2 : 0;
				break;
			}
			if (ETIMEDOUT == error)
				break;
				
			if (-1 != ms)
				error = OSPX_pthread_cond_timedwait(&pool->cond_ev, &pool->mut, &ms);
			else
				error = OSPX_pthread_cond_wait(&pool->cond_ev, &pool->mut);
		}
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error ? 1 : 0;
}

void 
tpool_suspend(struct tpool_t *pool, int wait) {	
	/* We set the flag with no locking the pool to speed the progress */
	pool->paused = 1;

	OSPX_pthread_mutex_lock(&pool->mut);
#ifndef NDEBUG	
	fprintf(stderr, "Suspend pool. <ntasks_pending:"PRI64"d ntasks_running:%d ntasks_removing:%d> @threads_in_pool:%d\n", 
			pool->npendings, RUNNING(pool), pool->ndispatchings, XLIST_SIZE(&pool->ths));
#endif	
	/* Mark the pool paused */
	pool->paused = 1;	
	for (;wait && (RUNNING(pool) || pool->ndispatchings);) {
		++ pool->suspend_waiters;	
		++ pool->waiters_all;
		OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);
		-- pool->suspend_waiters;
		-- pool->waiters_all;
	}
	OSPX_pthread_mutex_unlock(&pool->mut);	
}

void 
tpool_resume(struct tpool_t *pool) {
	OSPX_pthread_mutex_lock(&pool->mut);	
#ifndef NDEBUG	
	fprintf(stderr, "Resume pool. <ntasks_pending:"PRI64"d ntasks_running:%d ntasks_removing:%d> @threads_in_pool:%d\n", 
			pool->npendings, RUNNING(pool), pool->ndispatchings, XLIST_SIZE(&pool->ths));
#endif	
	/* Notify the server threads that we are ready now. */
	if (pool->paused && (POOL_F_CREATED & pool->status)) {
		pool->paused = 0;
		pool->launcher = 0;

		assert(pool->npendings >= XLIST_SIZE(&pool->dispatch_q));
		if (pool->npendings - XLIST_SIZE(&pool->dispatch_q)) {
			pool->ncont_completions = 0;
			tpool_increase_threads(pool, NULL);
		}
	}
	OSPX_pthread_mutex_unlock(&pool->mut);
}

static int
tpool_add_task_ex(struct tpool_t *pool, struct task_ex_t *tskex, int pri, int pri_policy) {
	int  tsk_code = 0;
		
	if (!tskex->f_flags) {
		int restart = 0;
		
		/* Check the filter */
		for (;pool->ev_enable;) {
			struct evflt_res_t res = {0, 0};
			struct brf_stat_t brfstat = {pool->npendings, RUNNING(pool), tpool_ev_busy(pool)};
			
			if (!pool->ev.ev_filter) 
				res.ev_flttyp = EV_FLT_WAIT;
			else
				res = pool->ev.ev_filter(pool, &pool->ev, &brfstat, tskex->tsk);
			
			switch (res.ev_flttyp) {
			case EV_FLT_WAIT: {
				/* Should we fix the time stamp if the @tpool_ev_wait_l is
				 * woke up by @tpool_event_pulse ?
				 */
				long ms = res.ev_param;
				
				if (ms <= 0)
					ms = -1;
				
				if (2 == tpool_ev_wait_l(pool, ms, 1))
					++ restart;
				else 
					restart = 0;
				break;
			} 
			case EV_FLT_PASS:
				restart = 0;
				break;
			case EV_FLT_WAIT2:
				tsk_code = POOL_TASK_ERR_FILTER_WAIT;
				break;
			case EV_FLT_DISCARD:
			default:
				tsk_code = POOL_TASK_ERR_FILTER_DISCARD;
				break;
			}
			
			if (tsk_code)
				return tsk_code;

			if (!restart || restart > 4) 
				break;	
		}
	}
	
	/* Check the pool status */
	if (!(POOL_F_CREATED & pool->status)) {
		long status = pool->status;
		
		/* Reset the vmflags */
		tskex->f_vmflags = 0;
		switch (status) {
		case POOL_F_DESTROYING:
			tsk_code = POOL_ERR_DESTROYING;
			tskex->f_vmflags |= TASK_VMARK_POOL_DESTROYING;
			break;
		case POOL_F_DESTROYED:
		default:
			tsk_code = POOL_ERR_NOCREATED;
		}

		return tsk_code;
	} 
	
	/* Check the throttle */
	if (pool->throttle_enabled)
		return POOL_ERR_THROTTLE;

	/* Reset the task's flag */
	if (tskex->f_wait) {		
		tskex->f_flags = 0;
		/* FIX BUGs: we'll lost @f_wait */
		tskex->f_wait  = 1;	
	} else
		tskex->f_flags = 0;	
	
	++ pool->ntasks_added;
	++ pool->npendings;
	XLIST_PUSHBACK(&pool->trace_q, &tskex->trace_link);
	
	/* Initialize the priority */
	if (!pri_policy || (!pri && (POLICY_PRI_SORT_INSERTAFTER == pri_policy))) {
		tskex->f_pri  = pri_policy ? 1 : 0;
		tskex->f_push = 1;
		tskex->pri = 0;
		tskex->pri_q = 0;
		tskex->pri_policy = pri_policy;
	
	} else {
		assert(!tskex->f_push);
		tskex->f_pri = 1;
		tskex->pri = min((uint16_t)pri, 99);
		tskex->pri_q = (tskex->pri < pool->avg_pri) ? 0 : ((tskex->pri + pool->avg_pri -1) / pool->avg_pri -1);
		tskex->pri_policy = pri_policy;
	} 		

	/* Sort the task according to the priority */
	assert(tskex->pri_q >= 0 && tskex->pri_q < pool->pri_q_num);
	if (tskex->f_push || XLIST_EMPTY(&pool->pri_q[tskex->pri_q].task_q))
		XLIST_PUSHBACK(&pool->pri_q[tskex->pri_q].task_q, &tskex->wait_link);	
	else {
		int got = 0;
		struct xlink *link;
		
		assert(tskex->f_pri);
		link = XLIST_BACK(&pool->pri_q[tskex->pri_q].task_q);
		if (POOL_READYQ_task(link)->pri >= tskex->pri) {
			struct task_ex_t *ntsk = POOL_READYQ_task(link);
			
			if ((tskex->pri < ntsk->pri) || (POLICY_PRI_SORT_INSERTAFTER == pri_policy)) {
				XLIST_PUSHBACK(&pool->pri_q[tskex->pri_q].task_q, &tskex->wait_link);
				got = 1;
			}
		}

		if (!got) {
			XLIST_FOREACH(&pool->pri_q[tskex->pri_q].task_q, &link) {
				struct task_ex_t *ntsk = POOL_READYQ_task(link);

				if ((tskex->pri > ntsk->pri) ||
					((POLICY_PRI_SORT_INSERTBEFORE == tskex->pri_policy) &&
					 (tskex->pri == ntsk->pri))) {
					got = 1;
					XLIST_INSERTBEFORE(&pool->pri_q[tskex->pri_q].task_q, link, &tskex->wait_link);
					break;
				}
			}
		}
		assert(got);
	}

	/* Should our task queue join in the ready group ? */
	if (1 == XLIST_SIZE(&pool->pri_q[tskex->pri_q].task_q)) {
		struct tpool_priq_t *qfirst = XCOBJ(XLIST_FRONT(&pool->ready_q), struct tpool_priq_t);
		
		/* Compare our priority with the priority of the top queue */
		if (XLIST_EMPTY(&pool->ready_q) || (tskex->pri_q > qfirst->index)) 
			XLIST_PUSHFRONT(&pool->ready_q, &pool->pri_q[tskex->pri_q].link);
		
		/* Compare our priority with the priority of the tail queue */
		else if (tskex->pri_q < XCOBJ(XLIST_PRE(&qfirst->link), struct tpool_priq_t)->index) 
			XLIST_PUSHBACK(&pool->ready_q, &pool->pri_q[tskex->pri_q].link);
		
		else {
			struct xlink *link;

			XLIST_FOREACH_EX(&pool->ready_q, &qfirst->link, &link) {
				if (tskex->pri_q > XCOBJ(link, struct tpool_priq_t)->index) {
					XLIST_INSERTBEFORE(&pool->ready_q, link, &pool->pri_q[tskex->pri_q].link);
					break;
				}
			}
			assert(link);
		}
	}
	if (!pool->paused) {
		if (pool->npendings == XLIST_SIZE(&pool->dispatch_q) - 1) 
			pool->ncont_completions = 0;
		
		if (pool->maxthreads > REAL(pool) 
			/* FIX BUGS. (2015-2-09) 
		 	 *    The pool should be woke up if all servering threads are
		 	 * sleeping.
		     */
			|| (pool->nthreads_waiters && pool->nthreads_waiters >= XLIST_SIZE(&pool->ths_waitq))
			)
			tpool_increase_threads(pool, NULL);	
	}

	/* Update the statics report */
	if (pool->ntasks_peak < pool->npendings) 
		pool->ntasks_peak = pool->npendings;
	
	return 0;
}

int
tpool_add_task(struct tpool_t *pool, struct task_t *tsk) {
	return tpool_add_pri_task(pool, tsk, 0, 0);
}

int  
tpool_add_routine(struct tpool_t *pool, 
				int (*task_run)(void *), 
				int (*task_complete)(long, int, void *, struct priority_t *pri),
				void *arg) {
	return tpool_add_pri_routine(pool, task_run, 
				task_complete, arg, 0, 0);
}

int  
tpool_add_pri_task(struct tpool_t *pool, struct task_t *tsk, int pri, int pri_policy) {
	int tsk_code;
	struct task_ex_t *tskex;
	
	/* Creat a task node to record the task */
	tskex = tpool_new_task(pool, tsk);
	if (!tskex) {
		errno = ENOMEM;
		__SHOW_ERR__("@tpool_add_task");
		return POOL_ERR_NOMEM;
	}

	/* Adjust the param @pri_policy */
	if (pri_policy > 2 || pri_policy < 0)
		pri_policy = POLICY_PRI_SORT_INSERTAFTER;
	
	/* Deliver the task into the ready queue */
	OSPX_pthread_mutex_lock(&pool->mut);
	tsk_code = tpool_add_task_ex(pool, tskex, pri, pri_policy);
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	if (tsk_code)
		tpool_delete_task(pool, tskex);
	
	return tsk_code;
}

int  
tpool_add_pri_routine(struct tpool_t *pool, 
					int (*task_run)(void *arg), 
					int (*task_complete)(long vmflags, int task_code, void *arg, struct priority_t *pri),
					void *arg,
					int pri, int pri_policy) {
	int tsk_code;
	struct task_ex_t *tskex;
	
	/* Creat a task node and fill it up with our default param */
	tskex = tpool_new_task(pool, NULL);	
	if (!tskex) {
		errno = ENOMEM;
		__SHOW_ERR__("@tpool_add_routine");
		return POOL_ERR_NOMEM;
	}
	
	/* Adjust the param @pri_policy */
	if (pri_policy > 2 || pri_policy < 0)
		pri_policy = POLICY_PRI_SORT_INSERTAFTER;

	tpool_fill_task(pool, (struct tpool_task_t *)tskex->tsk, 
		"anonymousTask", task_run, task_complete, arg);
			
	/* Deliver the task into the ready queue */
	OSPX_pthread_mutex_lock(&pool->mut);
	tsk_code = tpool_add_task_ex(pool, tskex, pri, pri_policy);
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	if (tsk_code)
		tpool_delete_task(pool, tskex);
	
	return tsk_code;
}

static int  
tpool_remove_pending_task_ex(struct tpool_t *pool, struct task_t *tsk, XLIST *rmq, XLIST *no_callback_q, long rmflags) {
	size_t elements = 0, has_callback = 0;
	struct xlink *link;
	struct task_ex_t *tskex;
	
	assert(rmq);
	if (!tsk) {
		int index = pool->pri_q_num -1;	
		/* We scan the ready queue */
		for (;index>=0; --index) {	
			if (XLIST_EMPTY(&pool->pri_q[index].task_q))
				continue;
			
			has_callback = 0;
			XLIST_FOREACH(&pool->pri_q[index].task_q, &link) {
				tskex = POOL_READYQ_task(link);
				assert(!(TASK_VMARK_REMOVE & tskex->f_vmflags)); 
				tskex->f_vmflags |= rmflags;
				if ((!has_callback) && tskex->tsk->task_complete)
					has_callback = 1;
			}
			elements += XLIST_SIZE(&pool->pri_q[index].task_q);
			if (!no_callback_q)
				XLIST_MERGE(rmq, &pool->pri_q[index].task_q);
			else if (!has_callback) 
				XLIST_MERGE(no_callback_q, &pool->pri_q[index].task_q);
			else {
				size_t left = XLIST_SIZE(&pool->pri_q[index].task_q);

				while (left --) {
					XLIST_POPFRONT(&pool->pri_q[index].task_q, link);
				
					if (POOL_READYQ_task(link)->tsk->task_complete)
						XLIST_PUSHBACK(rmq, link);
					else
						XLIST_PUSHBACK(no_callback_q, link);
				}
			}
			assert(XLIST_EMPTY(&pool->pri_q[index].task_q));
		}
		assert(elements == pool->npendings - XLIST_SIZE(&pool->dispatch_q));
		XLIST_INIT(&pool->ready_q);
		pool->npendings = 0;
		
	} else {
		int index = pool->pri_q_num -1;
		struct task_ex_t *tskex;
		
		XLIST_FOREACH(&pool->trace_q, &link) {
			tskex = POOL_TRACEQ_task(link);
			if (tskex->tsk != tsk)
				continue;

			/* Is the task being removed ? */
			if (TASK_VMARK_REMOVE & tskex->f_vmflags) 
				continue;
						
			/* We ignore the task who is being scheduled or is being dispatched */
			if ((TASK_F_SCHEDULING|TASK_F_DISPATCHING) & tskex->f_stat)
				continue;

			tskex->f_vmflags |= rmflags;	
			++ elements;
			/* Mark the task removed and push it 
			 * into our delete queue.
			 */
			assert(tskex->pri_q >= 0 && tskex->pri_q < pool->pri_q_num);
			XLIST_REMOVE(&pool->pri_q[tskex->pri_q].task_q, &tskex->wait_link);
			if (XLIST_EMPTY(&pool->pri_q[tskex->pri_q].task_q))
				XLIST_REMOVE(&pool->ready_q, &pool->pri_q[tskex->pri_q].link);
			
			if (tskex->tsk->task_complete || !no_callback_q) 
				XLIST_PUSHBACK(rmq, &tskex->wait_link);
			else	
				XLIST_PUSHBACK(no_callback_q, &tskex->wait_link);
		}
		pool->npendings -= elements;
	}
	
	if (pool->ev_task_waiters) 
		TRY_wakeup_filter(pool);
		
	return elements;
}

int  
tpool_remove_pending_task(struct tpool_t *pool, struct task_t *tsk) {
	XLIST  rmq, no_callback_q;
	int elements;
	
	XLIST_INIT(&rmq);
	XLIST_INIT(&no_callback_q);
	OSPX_pthread_mutex_lock(&pool->mut);
	elements = tpool_remove_pending_task_ex(pool, tsk, &rmq, &no_callback_q, TASK_VMARK_REMOVE_DIRECTLY);
	assert(elements == XLIST_SIZE(&rmq) + XLIST_SIZE(&no_callback_q));
	pool->ndispatchings += elements;
	assert(pool->ndispatchings <= XLIST_SIZE(&pool->trace_q));
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* We dispatch the tasks directly */
	if (elements) 
		tpool_rmq_dispatch(pool, &rmq, &no_callback_q, (POOL_F_CREATED & pool->status) ? 
			POOL_TASK_ERR_REMOVED : POOL_ERR_DESTROYING);

	return elements;
}

int  
tpool_remove_pending_task2(struct tpool_t *pool, struct task_t *tsk) {	
	XLIST rmq, no_callback_q;
	int elements;

	XLIST_INIT(&rmq);
	XLIST_INIT(&no_callback_q);
	OSPX_pthread_mutex_lock(&pool->mut);
	elements = tpool_remove_pending_task_ex(pool, tsk, &rmq, &no_callback_q, TASK_VMARK_REMOVE_BYPOOL);	
	assert(elements == XLIST_SIZE(&rmq) + XLIST_SIZE(&no_callback_q));
	
	pool->ndispatchings += elements;
	/* Wake up threads to schedule the callback */
	if (!XLIST_EMPTY(&rmq)) {
		pool->npendings += XLIST_SIZE(&rmq);
		XLIST_MERGE(&pool->dispatch_q, &rmq);
		
		if (pool->maxthreads > REAL(pool))
			tpool_increase_threads(pool, NULL);
	}
	
	if (!XLIST_EMPTY(&no_callback_q)) {
		tpool_task_complete_nocallback_l(pool, &no_callback_q);		
		/* Try to recycle the task nodes in background */
#ifdef _CLEAN_RUBBISH_INBACKGROUND
		CLEAN_2(pool, &no_callback_q);
#endif	
	}

	OSPX_pthread_mutex_unlock(&pool->mut);
	
	/* Free the task nodes */
#ifndef _CLEAN_RUBBISH_INBACKGROUND
	if (!XLIST_EMPTY(&no_callback_q))
		tpool_delete_tasks(pool, &no_callback_q);
#endif
	return elements;
}

int  
tpool_wait(struct tpool_t *pool, struct task_t *tsk, long ms) {
	int error;
	struct task_ex_t *tskex = NULL;

	OSPX_pthread_mutex_lock(&pool->mut);	
	for (error=0;;) {
		if (!(POOL_F_CREATED & pool->status)) {
			error = 2;
			break;
		}
		
		if (!tpool_has_task(pool, tsk, &tskex)) {
			error = 0;
			break;
		}

		if (!ms) 
			error = ETIMEDOUT;

		if (ETIMEDOUT == error) {
			/* Try to remove the task's wait flag */
			if (tskex && (0 == pool->waiters_all))
				tskex->f_wait = 0;
			error = 1;
			break;	
		}

		if (tskex)
			tskex->f_wait = 1;
		else
			++ pool->waiters;	
		++ pool->waiters_all;

		/* Wait for the tasks' completions in ms millionseconds */
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		
		if (!tskex)
			-- pool->waiters;
		-- pool->waiters_all;
	}		
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error;
}

int  
tpool_waitex(struct tpool_t *pool, int (*task_match)(struct tpool_tskstat_t *, void *), void *arg, long ms) {
	int got, error;
	struct xlink *link;
	struct task_ex_t *tskex;
	struct tpool_tskstat_t stat;

	OSPX_pthread_mutex_lock(&pool->mut);	
	for (error=0, got=0;;) {
		if (!(POOL_F_CREATED & pool->status)) {
			error = 2;
			break;
		}

		XLIST_FOREACH(&pool->trace_q, &link) {
			tskex = POOL_TRACEQ_task(link);
			ACQUIRE_TASK_STAT(pool, tskex, &stat);
			if (task_match(&stat, arg)) {
				if (!ms) 
					error = ETIMEDOUT;
				else
					tskex->f_wait = 1;
				got = 1;
				break;
			}
		}
		if (!got) {
			error = 0;
			break;
		}
		if (ETIMEDOUT == error) {
			/* Try to remove the task's wait flag */
			if (0 == pool->waiters_all) 
				tskex->f_wait = 0;
			error = 1;
			break;	
		}
		got = 0;
	
		++ pool->waiters_all;
		if (-1 != ms)
			error = OSPX_pthread_cond_timedwait(&pool->cond_comp, &pool->mut, &ms);
		else
			error = OSPX_pthread_cond_wait(&pool->cond_comp, &pool->mut);	
		-- pool->waiters_all;
	}		
	OSPX_pthread_mutex_unlock(&pool->mut);

	return error;
}

struct 
event_t *tpool_event_get(struct tpool_t *pool, struct event_t *ev) {
	static struct event_t slev = {0};
	
	if (!ev)
		ev = &slev;
	
	OSPX_pthread_mutex_lock(&pool->mut);
	*ev = pool->ev;
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return ev;
}

void
tpool_event_set(struct tpool_t *pool, struct event_t *ev) {
	OSPX_pthread_mutex_lock(&pool->mut);
	pool->ev_enable = ev && ev->ev_triggle_type &&
		(ev->ev_triggle_type >= 1 && ev->ev_triggle_type <= 4);
	if (ev) 
		pool->ev = *ev;
	pool->ev_enable = ev && pool->ev.ev_triggle_type;
	OSPX_pthread_mutex_unlock(&pool->mut);
}

int
tpool_event_wait(struct tpool_t *pool, long ms) {
	int error;

	OSPX_pthread_mutex_lock(&pool->mut);
	error = tpool_ev_wait_l(pool, ms, 1);
	OSPX_pthread_mutex_unlock(&pool->mut);
	
	return (1 != error) ? 0 : 1;
}

int
tpool_ev_busy(struct tpool_t *pool) {
	int busy;

	switch (pool->ev.ev_triggle_type) {
	case EV_TRG_THREADS:
		busy = RUNNING(pool) >= pool->ev.ev_threads_num;
		break;
	case EV_TRG_TASKS: 
		busy = pool->npendings >= pool->ev.ev_tasks_num;
		break;
	case EV_TRG_THREADS_OR_TASKS:
		busy = (RUNNING(pool) >= (pool)->ev.ev_threads_num) &&
			   ((pool)->npendings >= (pool)->ev.ev_tasks_num);
		break;
	case EV_TRG_THREADS_AND_TASKS:
		busy = (RUNNING(pool) >= (pool)->ev.ev_threads_num) ||
			   ((pool)->npendings >= (pool)->ev.ev_tasks_num);
		break;
	default:
		busy = 1;
	}

	return busy;
}

int
tpool_ev_wait_l(struct tpool_t *pool, long ms, int return_if_wokeup) {
	int cnt = 0, wait = 0;
	
	for (errno=0;;){
		switch (pool->ev.ev_triggle_type) {
		case EV_TRG_THREADS:
			if (RUNNING(pool) >= pool->ev.ev_threads_num) {
				wait = 1;
				++ pool->ev_thread_waiters;
			}
			break;
		case EV_TRG_TASKS: 
			if (pool->npendings >= pool->ev.ev_tasks_num) {
				wait = 2;
				++ pool->ev_task_waiters;
			}
			break;
		case EV_TRG_THREADS_OR_TASKS:
			if ((RUNNING(pool) >= (pool)->ev.ev_threads_num) &&
				((pool)->npendings >= (pool)->ev.ev_tasks_num)) {
				++ pool->ev_thread_waiters;
				++ pool->ev_task_waiters;
				wait = 3;
			}
			break;
		case EV_TRG_THREADS_AND_TASKS:
			if (RUNNING(pool) >= (pool)->ev.ev_threads_num) {
				wait = 1;
				++ pool->ev_thread_waiters;
			} else if (pool->npendings >= pool->ev.ev_tasks_num) {
				wait = 2;
				++ pool->ev_task_waiters;
			}
			break;
		}

		if (!wait) 
			return 0;
		
		if (ETIMEDOUT == errno)
			return 1;

		assert(!errno);
		if (2 == ++ cnt && return_if_wokeup && !errno)
			return 2;
	
		pool->ev_filter_wokeup = 0;
		++ pool->waiters_all;
		if (ms < 0)
			errno = OSPX_pthread_cond_wait(&pool->cond_ev, &pool->mut);	
		else 
			errno = OSPX_pthread_cond_timedwait(&pool->cond_ev, &pool->mut, &ms);	
		-- pool->waiters_all;
			
		switch (wait) {
		case 1:
			-- pool->ev_thread_waiters;
			break;
		case 2:
			-- pool->ev_task_waiters;
			break;
		case 3:
			-- pool->ev_thread_waiters;
			-- pool->ev_task_waiters;
			break;
		}
	} 
}

void 
tpool_event_pulse(struct tpool_t *pool) {
	OSPX_pthread_mutex_lock(&pool->mut);
	if (pool->ev_task_waiters || pool->ev_thread_waiters)
		OSPX_pthread_cond_broadcast(&pool->cond_ev);
	OSPX_pthread_mutex_unlock(&pool->mut);
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
				XLIST_PUSHBACK(&pool->freelst, &self->link_free);
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

/* OPTIMIZE @tpool_add_threads */
#ifdef _OPTIMIZE_PTHREAD_CREATE
static int tpool_thread_launcher(void *arg); 
#define tpool_thread_launcher_request(pool, req, res) \
	do { \
		if (!(pool)->launcher_run) { \
			OSPX_pthread_t _xdummy; \
			if ((errno = OSPX_pthread_create(&_xdummy, 0, tpool_thread_launcher, pool))) \
				__SHOW_ERR__("pthread_create");\
			else \
				(pool)->launcher_run = 1; \
		}\
		*res = (pool)->launcher_run;\
		if (*res) {\
			if (XLIST_EMPTY(&(pool)->launcherq)) {\
				XLIST_SWAP(&(pool)->launcherq, req); \
				OSPX_pthread_cond_signal(&pool->cond_launcher); \
			} else \
				XLIST_MERGE(&(pool)->launcherq, req); \
		} \
	} while (0)
#endif

static int
tpool_add_threads(struct tpool_t *pool, int nthreads, long lflags /* reserved */) {
	int n, res;
	OSPX_pthread_t id;
#ifdef _OPTIMIZE_PTHREAD_CREATE
	XLIST launcherq;
#endif	
	
	assert(!lflags);
	
	/* Check whether we can reuse the dying threads */
	if ((nthreads > 0) && pool->nthreads_dying) {
		struct xlink *link;
		struct tpool_thread_t *th;

		XLIST_FOREACH(&pool->ths, &link) {
			th = POOL_Q_thread(link);

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
			if (!-- pool->nthreads_dying) {
				pool->nthreads_waiters = 0 ;
				OSPX_pthread_cond_broadcast(&pool->cond_ths);
			}
			
			-- nthreads;
			if (!pool->nthreads_dying || !nthreads)
				break;
		}

		/* Try to wake up the service threads if there are tasks
		 * wait for being scheduled.
		 */
		if (pool->nthreads_real_sleeping && 
			((!pool->paused && pool->npendings) || !XLIST_EMPTY(&pool->dispatch_q)))
			OSPX_pthread_cond_broadcast(&pool->cond);
	}
	
	/* Actually, In order to reduce the time to hold the global lock,
	 * we'll try to just add the threads structure into the threads sets, 
	 * and call @pthread_create in the background. 
	 */
#ifdef _OPTIMIZE_PTHREAD_CREATE
	XLIST_INIT(&launcherq);
#endif	
	for (n=0; n<nthreads; n++) {
		struct xlink *link;
		struct tpool_thread_t *self;
		
		if (XLIST_EMPTY(&pool->freelst)) {
			self = (struct tpool_thread_t *)malloc(sizeof(*self));
			if (!self) {
				errno = ENOMEM;
				__SHOW_ERR__(__FUNCTION__);
				break;
			}
			INIT_thread_structure(pool, self, 1);	
		} else {
			XLIST_POPFRONT(&pool->freelst, link);
			self = XCOBJEX(link, struct tpool_thread_t, link_free);
			assert(!self->structure_release);
		}

		/* We increase the reference to make sure that 
		 * the task pool object will always exist before 
		 * the service thread's exitting.
		 */
		tpool_addref_l(pool, 0, dummy_null_lptr);		
#ifdef _OPTIMIZE_PTHREAD_CREATE
		/* If none threads have been created, we call 
		 * @pthread_create direcly.
		 */
		if (REAL(pool)) {
			XLIST_PUSHBACK(&launcherq, &self->link_launcher);
			XLIST_PUSHBACK(&pool->ths, &self->link);	
		} else {
#endif	
			if ((errno = OSPX_pthread_create(&self->thread_id, 0, tpool_thread_entry, self))) {
				__SHOW_ERR__("pthread_create error");
				tpool_release_l(pool, 0, dummy_null_lptr);
				if (self->structure_release)
					free(self);
				else 
					XLIST_PUSHBACK(&pool->freelst, &self->link_free);
				break;
			}
			#ifndef NDEBUG	
				fprintf(stderr, "create THREAD:0x%x\n", (int)id);
			#endif		
			XLIST_PUSHBACK(&pool->ths, &self->link);	
#ifdef _OPTIMIZE_PTHREAD_CREATE
		}
#endif
	}
	pool->nthreads_real_pool += n;
	
#ifdef _OPTIMIZE_PTHREAD_CREATE
	if (!XLIST_EMPTY(&launcherq))
		tpool_thread_launcher_request(pool, &launcherq, &res);
#endif

	/* Update the statics report */
	if (pool->nthreads_peak < REAL(pool)) 
		pool->nthreads_peak = REAL(pool);
	
	return n;
}

static void 
tpool_increase_threads(struct tpool_t *pool, struct tpool_thread_t *self) {	
	int ntasks_pending = pool->paused ? XLIST_SIZE(&pool->dispatch_q) : pool->npendings;
	
	/* Check the pool status */
	if (!ntasks_pending) {
		pool->launcher = 0;
		return;
	} 
		
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
		pool->nthreads_waiters <= XLIST_SIZE(&pool->ths_waitq));
	if (!XLIST_EMPTY(&pool->ths_waitq)) {
		/* Caculate the number of threads who should be woke up to provide services */
		ntasks_pending -= (REAL(pool) + pool->nthreads_going_rescheduling 
			
			/* We decrease the number of threads who is running or sleeping */
			+ pool->nthreads_dying_run - RUNNING(pool) - pool->nthreads_real_sleeping 

			/* We should decrease the number of threads that has been woke up by us */
			+ XLIST_SIZE(&pool->ths_waitq) - pool->nthreads_waiters);
		
		if (ntasks_pending > 0 && pool->nthreads_waiters >= XLIST_SIZE(&pool->ths_waitq)) {
			struct xlink *link;	
			int nwake = min(ntasks_pending, 3);
			
			/* NOTE:
			 * 		We do not care about which thread will be woke up, we just 
			 * wake up no more than 3 threads to provide services. so the threads
			 * should check the pool status again if they are going to exit after 
			 * its having been woke up.
			 */
			if (pool->nthreads_waiters  > nwake) {
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
	
#if 0
	if (!(POOL_F_CREATED & pool->status)) {
		/* If there are dispatching tasks, we try to create
		 * service threads to schedule them even if the pool
		 * is being destroyed.
		 *  <see @tpool_release_ex/@tpool_remove_pending_task2 for more details>
		 */
		if (XLIST_EMPTY(&pool->dispatch_q) ||
			((0 == pool->ref) && !pool->release_cleaning))
			return 0;
	}
#endif
	assert(RUNNING(pool) >= pool->nthreads_going_rescheduling &&
		   REAL(pool) <= XLIST_SIZE(&pool->ths));
	
	/* Verify the @maxthreads */
	if (pool->maxthreads > REAL(pool)) {
		int curthreads_pool_free = REAL(pool) + pool->nthreads_going_rescheduling 
			+ pool->nthreads_dying_run - RUNNING(pool);
		assert(curthreads_pool_free >= 0);
		
		/* Compute the number of threads who should be
		 * created to provide services
	 	*/
		if (curthreads_pool_free < pool->limit_threads_free) {	
			int n = pool->maxthreads - REAL(pool);
			/* Compute the number of threads who is should be created
			 * according to the @limit_threads_free.
			 */
			int nthreads = pool->limit_threads_free - curthreads_pool_free;	
			assert(n >= 0);
			if (n < nthreads)
				nthreads = n;

			/* Acquire the number of pending tasks */
			assert(pool->npendings >= XLIST_SIZE(&pool->dispatch_q));	
			ntasks_pending = pool->npendings - XLIST_SIZE(&pool->dispatch_q);
			if (!XLIST_EMPTY(&pool->dispatch_q)) 
				ntasks_pending += 1;
			
			/* Create the service threads propriately according to the 
			 * threads' status.
			 */
			n = ntasks_pending - curthreads_pool_free;
			if (n < nthreads) 
				nthreads = n;

			if (nthreads > 0) {
				if (nthreads > pool->limit_threads_create_per_time)
					nthreads = pool->limit_threads_create_per_time;
				nthreads = tpool_add_threads(pool, nthreads, 0);		
				pool->launcher = self ? self->thread_id : OSPX_pthread_id();
			}
		}
	}	
}

/* It seems that OSPX_random() can not work well on windows, so
 * we use @tpool_random to instead.
 */
#define tpool_random(pool, self) \
	(time(NULL) ^ (unsigned)self * 1927 * (unsigned)OSPX_random())

#define tpool_get_restto(pool, self, to) \
	do {\
		int extra = REAL(pool) - (pool)->minthreads - RUNNING(pool); \
		if ((THREAD_STAT_RM & (self)->status) ||\
			!(POOL_F_CREATED & pool->status)) \
			*to = 0;\
		else if (extra > (pool)->threads_wait_throttle) \
			*to = 0; \
		else if (extra <= 0) \
			*to = -1; \
		else if ((self)->ncont_rest_counters > 10) \
			/* We take the @ncont_rest_counters into consideration,
			 * think about situations like that.
			 *    All threads go to sleep for none tasks existing in 
			 * the pool, and then users deliver few tasks into the pool
			 * continuously, and as a result, the threads will be woke
			 * up, But if the tasks number is less than the threads number,
			 * the bad situation that some threads can not get any tasks
			 * all the way may happen.
			 */\
			*to = 0;\
		else {\
			/* Initialize the random sed */\
			OSPX_srandom(time(NULL));\
			if (extra <= 5) \
				*to = (pool)->acttimeo + tpool_random(pool, self) % pool->randtimeo;\
			else\
				*to = tpool_random(pool, self) % 35000;\
			if ((*to) < 0) \
				*to = (*to) & 0x0000ffff;\
		} \
	} while (0)

static void 
tpool_schedule(struct tpool_t *pool, struct tpool_thread_t *self) {	
	long to;

	for (;self->run;) {
		OSPX_pthread_mutex_lock(&pool->mut);
		
		/* Get a task to execute */
		if (!tpool_gettask(pool, self)) {
			/* Check whether we should exit now before our's waiting for tasks. */
			if (!self->run) {
				OSPX_pthread_mutex_unlock(&pool->mut);
				break;
			}
			tpool_get_restto(pool, self, &to);
			if (!to) 
				tpool_thread_setstatus_l(self, THREAD_STAT_FORCE_QUIT);
			else {
				tpool_thread_setstatus_l(self, THREAD_STAT_WAIT);		
				/* Queue ourself if we have not gotten a task */
				if (to > 0) 	
					errno = OSPX_pthread_cond_timedwait(&pool->cond, &pool->mut, &to);
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
		
		/* Record the task's status */
		self->is_dispatched_task = (__curtask->f_vmflags & TASK_VMARK_REMOVE);
		
		/* Run and dispatch the task */
		tpool_task_complete(pool, self, __curtask, 
						self->is_dispatched_task ? POOL_TASK_ERR_REMOVED :
						__curtask->tsk->task_run(__curtask->tsk));
	}
}

static int
tpool_gettask(struct tpool_t *pool, struct tpool_thread_t *self) {
	struct xlink *link = NULL;
	int ntasks_ready = pool->paused ? 0 : XLIST_SIZE(&pool->ready_q);
	
	/* Scan the dispaching queue firstly */
	if (!XLIST_EMPTY(&pool->dispatch_q)) {
		/* We give a chance to the ready tasks */
		if (!ntasks_ready || pool->ncont_completions < pool->limit_cont_completions)
			XLIST_POPFRONT(&pool->dispatch_q, link);
	} else if (!ntasks_ready)
		return 0;
	
	/* Scan the ready queue */
	if (!link) {
		struct tpool_priq_t *priq;
		assert(pool->npendings > 0 && !XLIST_EMPTY(&pool->ready_q));

		/* Scan the priority queue */
		link = XLIST_FRONT(&pool->ready_q);
		priq = XCOBJ(link, struct tpool_priq_t);

		/* Pop up a task from the task queue */
		XLIST_POPFRONT(&priq->task_q, link);
		if (XLIST_EMPTY(&priq->task_q)) 
			XLIST_REMOVE(&pool->ready_q, &priq->link);
#ifndef NDEBUG
		else {
			struct xlink *nlink;
			struct task_ex_t *cur, *next;
			
			/* Verify the priority */
			cur   = POOL_READYQ_task(link);
			nlink = XLIST_FRONT(&priq->task_q);
			next  = POOL_READYQ_task(nlink);
			assert(cur->pri >= next->pri);
		}
#endif
	}
	-- pool->npendings;

	__curtask = POOL_READYQ_task(link);
	__curtask->f_stat |= TASK_F_SCHEDULING;
	/* Push the task into the running queue and
	 * mark our thread status with THREAD_STAT_RUN
	 */
	tpool_thread_setstatus_l(self, THREAD_STAT_RUN);		

#ifndef NDEBUG
	fprintf(stderr, "THREAD:%p running task(%s/%p). <ntasks_done:%u ntasks_pending:"PRI64"d\n", 
			self, __curtask->tsk->task_name, __curtask->tsk,
			self->ntasks_done, pool->npendings);
#endif	
	return 1;
}


void 
tpool_thread_status_change(struct tpool_t *pool, struct tpool_thread_t *self, long status, int synchronized) {	
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
		XLIST_PUSHBACK(&pool->ths_waitq, &self->run_link);
		++ pool->nthreads_waiters;
		
		/* Reset the thread launcher */
		if (pool->launcher && pool->launcher == OSPX_pthread_id())
			pool->launcher = 0;
		break;
	case THREAD_STAT_TIMEDOUT: {	
		int nthreads_pool = XLIST_SIZE(&pool->ths) - pool->nthreads_dying;
		
		/* We try to remove the thread from the servering sets 
		 * if the threads should be stopped providing service.
		 */	
		if (!(THREAD_STAT_RM & self->status)) {
			if ((nthreads_pool > pool->minthreads) && ((!pool->npendings) || pool->paused)) { 
				status = THREAD_STAT_RM;
				++ pool->nthreads_dying;
				-- pool->nthreads_real_pool;
				self->run = 0;
				break;
			}
		}
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
		if (!XLIST_EMPTY(&pool->dispatch_q) || (!pool->paused && pool->npendings))
			tpool_increase_threads(pool, self);
		break;
	}
	case THREAD_STAT_RUN: 
		assert(__curtask);		
#if !defined(NDEBUG) && !defined(_WIN32)
		if (__curtask->tsk->task_name && strlen(__curtask->tsk->task_name))
			prctl(PR_SET_NAME, __curtask->tsk->task_name);
#endif
		++ pool->nthreads_running;
		if (THREAD_STAT_RM & self->status) {
			assert(!self->run);
			++ pool->nthreads_dying_run;
		}
		
		if (TASK_VMARK_REMOVE & __curtask->f_vmflags) 
			++ pool->ncont_completions;
		else if (pool->ncont_completions)
			pool->ncont_completions = 0;
		
		/* Reset the @ncont_rest_counters */
		if (self->ncont_rest_counters)
			self->ncont_rest_counters = 0;
		/* Try to create more threads to provide services 
		 * before our's executing the task. 
		 */
		if (pool->npendings && pool->maxthreads > REAL(pool))
			tpool_increase_threads(pool, self);
		
		/* Try to wake up the filter */
		if (pool->ev_task_waiters)
			TRY_wakeup_filter(pool);
		break;
	case THREAD_STAT_COMPLETE:
		-- pool->nthreads_running;
		status = THREAD_STAT_FREE;	
		
		if (THREAD_STAT_RM & self->status) {
			assert(!self->run);
			-- pool->nthreads_dying_run;
		}

		/* Has @task_run been executed ? */
		if (self->is_dispatched_task)
			++ pool->ntasks_dispatched;
		else
			++ pool->ntasks_done;
#ifndef NDEBUG
		++ self->ntasks_done;
#endif	
		/* Try to wake up the filter */
		if (pool->ev_thread_waiters)
			TRY_wakeup_filter(pool);
		
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
		XLIST_REMOVE(&pool->ths, &self->link);

		/* Remove current thread from the RM queue */
		if (THREAD_STAT_RM & self->status) {
			assert(pool->nthreads_dying > 0 && !self->run);
			
			/* Give @tpool_adjust_wait a notification */
			if (!-- pool->nthreads_dying)
				OSPX_pthread_cond_broadcast(&pool->cond_ths);
		} else if (XLIST_EMPTY(&pool->ths))
			/* Give @tpool_release_ex a notification */
			OSPX_pthread_cond_broadcast(&pool->cond_ths);
		
		/* Reset the thread launcher */
		if (pool->launcher && XLIST_SIZE(&pool->ths) == 1) 
			pool->launcher = 0;		
#ifndef NDEBUG
		fprintf(stderr, "THREAD:%p exits. <ntasks_done:%d status:0x%lx> (@threads_in_pool:%d(%d) @tasks_in_pool:%d)\n", 
			self, self->ntasks_done, (long)self->status, XLIST_SIZE(&pool->ths), REAL(pool),
			XLIST_SIZE(&pool->trace_q));
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
		XLIST_REMOVE(&pool->ths_waitq, &self->run_link);	
		
		/* 1. @OSPX_pthread_cond_signal may wake up more than one sleeping threads 
		 * 2. Thread is changing status from WAIT to WAIT_TIMEDOUT.
		 */
		if (pool->nthreads_waiters > XLIST_SIZE(&pool->ths_waitq))
			-- pool->nthreads_waiters;
	}	
	self->status = status | (self->status & THREAD_STAT_INNER);
	
	if (!synchronized)
		OSPX_pthread_mutex_unlock(&pool->mut);
}


static struct task_ex_t *
tpool_new_task(struct tpool_t *pool, struct task_t *tsk) {
	struct task_ex_t *tskex;
	
	/* NOTE: We can create a memory pool to improve our
	 * 		 perfermence !
	 */
	if (tsk) {
#ifdef _USE_MPOOL	
		if (pool->mp1)
			tskex = (struct task_ex_t *)mpool_new(pool->mp1);
		else
#endif
			tskex = (struct task_ex_t *)malloc(sizeof(struct task_ex_t));
	} else {
#ifdef _USE_MPOOL	
		if (pool->mp2)
			tskex = (struct task_ex_t *)mpool_new(pool->mp2);
		else
#endif
			tskex = (struct task_ex_t *)malloc(sizeof(struct task_ex_t) + sizeof(struct tpool_task_t));
	}

	if (tskex) {
		if (!tsk) 
			tskex->tsk = (struct task_t *)(tskex + 1);
		else
			tskex->tsk = tsk;

		/* Reset the f_flags */
		tskex->f_flags = 0;
	}
	
	return tskex;
}

static void
tpool_delete_task(struct tpool_t *pool, struct task_ex_t *tskex) {
#ifdef _USE_MPOOL	
	if ((size_t)tskex->tsk == (size_t)(tskex + 1)) {
		if (pool->mp2) {
			mpool_delete(pool->mp2, tskex);
			return;
		}
	} else if (pool->mp1) {
		mpool_delete(pool->mp1, tskex);
		return;
	}
#endif

	free(tskex);
}

static void
tpool_delete_tasks(struct tpool_t *pool, XLIST *deleteq) {
	int ele = XLIST_SIZE(deleteq);
	struct xlink *link;

	for (;ele; --ele) {
		XLIST_POPFRONT(deleteq, link);
		tpool_delete_task(pool, POOL_READYQ_task(link));	
	}	
}

#ifdef _CLEAN_RUBBISH_INBACKGROUND
static int
tpool_rubbish_clean(void *arg) {
	XLIST tmpq;
	int  error;
	long ms = 1000 * 60 * 3;
	struct tpool_t *pool = (struct tpool_t *)arg;

#if (!defined(NDEBUG)) && !defined(_WIN32)
	prctl(PR_SET_NAME, "rubbish_clean");
#endif
	while (1) {
		error = 0;
		XLIST_INIT(&tmpq);

		OSPX_pthread_mutex_lock(&pool->mut_garbage);
		if (XLIST_EMPTY(&pool->clq)) 
			error = OSPX_pthread_cond_timedwait(&pool->cond_garbage, &pool->mut_garbage, &ms);	

		if (((ETIMEDOUT == error) && XLIST_EMPTY(&pool->clq)) || !pool->ref) 
			break;
		
		XLIST_SWAP(&tmpq, &pool->clq);	
		OSPX_pthread_mutex_unlock(&pool->mut_garbage);
		
#if defined(_USE_MPOOL) && !defined(NDEBUG)
		tpool_verifyq(pool, &tmpq);
#endif
		/* Clean the tasks node */
		tpool_delete_tasks(pool, &tmpq);	
	}
	
#ifndef	NDEBUG
	fprintf(stderr, "@%s is exitting ...\n",
		__FUNCTION__);
#endif
	/* Give @tpool_free a notification */
	pool->rubbish_run = 0;
	if (!(POOL_F_CREATED & pool->status))
		OSPX_pthread_cond_signal(&pool->cond_ths);
	OSPX_pthread_mutex_unlock(&pool->mut_garbage);
	
	return 0;
}
#endif

#ifdef _OPTIMIZE_PTHREAD_CREATE
static int
tpool_thread_launcher(void *arg) {
	int  error, quit = 0;
	long ms = 1000 * 60 * 5, release = 0;
	struct tpool_t *pool = (struct tpool_t *)arg;
	struct xlink *link;
	XLIST tmpq, rmq;
	OSPX_pthread_t id;
	struct tpool_thread_t *self; 

#if !defined(NDEBUG) && !defined(_WIN32)
	prctl(PR_SET_NAME, "thread_launcher");
#endif
	while (1) {
		error = 0;
		XLIST_INIT(&tmpq);
		XLIST_INIT(&rmq);

		OSPX_pthread_mutex_lock(&pool->mut_launcher);
		if (quit)
			break;

		if (XLIST_EMPTY(&pool->launcherq)) 
			error = OSPX_pthread_cond_timedwait(&pool->cond_launcher, &pool->mut_launcher, &ms);	

		if (!pool->ref) 
			quit = error = 1;
		else if (!XLIST_EMPTY(&pool->launcherq)) 
			error = 0;
		XLIST_SWAP(&tmpq, &pool->launcherq);	
		OSPX_pthread_mutex_unlock(&pool->mut_launcher);	
		
		XLIST_FOREACH(&tmpq, &link) {	
			self = XCOBJEX(link, struct tpool_thread_t, link_launcher);
			if (!error && (error = OSPX_pthread_create(&id, 0, tpool_thread_entry, self))) 
				__SHOW_ERR__("pthread_create error");
			
			if (error) {
				/* Lock the pool */
				OSPX_pthread_mutex_lock(&pool->mut);
				XLIST_REMOVE(&pool->ths, &self->link);
				
				/* Remove current thread from the RM queue */
				if (THREAD_STAT_RM & self->status) {
					assert(pool->nthreads_dying > 0 && !self->run);
					
					/* Give @tpool_adjust_wait a notification */
					if (!-- pool->nthreads_dying)
						OSPX_pthread_cond_broadcast(&pool->cond_ths);
				} else {
					assert(pool->nthreads_real_pool > 0);
					-- pool->nthreads_real_pool;
					assert(THREAD_STAT_INIT == (self->status & ~THREAD_STAT_INNER));
				}
				/* Give @tpool_release_ex a notification */
				if (XLIST_EMPTY(&pool->ths))
					OSPX_pthread_cond_broadcast(&pool->cond_ths);

				/* Pay back the structure into the pool */
				if (!self->structure_release) 
					XLIST_PUSHBACK(&pool->freelst, &self->link_free);	
				OSPX_pthread_mutex_unlock(&pool->mut);
				
				if (self->structure_release)
					XLIST_PUSHBACK(&rmq, &self->link);
				++ release;
			} 
#ifndef NDEBUG
			if (!error)
				fprintf(stderr, "LAUNCHER THREAD:0x%x\n", (int)id);
#endif
		}
	
		/* Free the threads' structures */
		while (!XLIST_EMPTY(&rmq)) {
			XLIST_POPFRONT(&rmq, link);
			self = XCOBJEX(link, struct tpool_thread_t, link_launcher);
			free(self);
		}
		
		/* Decrease the references of the pool */
		for (;release; --release) 
			tpool_release_ex(pool, 0, 0);
	}	
#ifndef	NDEBUG
	fprintf(stderr, "@%s is exitting ...\n",
		__FUNCTION__);
#endif
	/* Give @tpool_free a notification */
	pool->launcher_run = 0;
	if (!(POOL_F_CREATED & pool->status))
		OSPX_pthread_cond_signal(&pool->cond_ths);
	OSPX_pthread_mutex_unlock(&pool->mut_launcher);
	
	return 0;
}
#endif

/* DEBUG interfaces */
#if defined(_USE_MPOOL) && !defined(NDEBUG)
static void 
tpool_verify(struct tpool_t *pool, struct task_ex_t *tskex) {
	if ((size_t)tskex->tsk == (size_t)(tskex + 1)) {
		if (pool->mp2) 
			mpool_assert(pool->mp2, tskex);
	} else if (pool->mp1) 
		mpool_assert(pool->mp1, tskex);
}

static void 
tpool_verifyq(struct tpool_t *pool, XLIST *assertq) {
	struct xlink *link;

	XLIST_FOREACH(assertq, &link) {
		tpool_verify(pool, POOL_READYQ_task(link));
	}
}
#endif

