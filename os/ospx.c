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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "ospx.h"
#include "ospx_error.h"
#include "timer.h"

OSPX_pthread_key_t g_ospx_key  = 0;
static int g_installer = 0;

/**************************************OSPX**************************/
EXPORT int 
OSPX_library_init(long lflags) 
{
	int installer = 0;

	/* Have we loaded the library ? */
	if (!g_ospx_key) {
		/* The codes below should be executed only once. so
		 * we'd better call OSPX_library_init in the main thread. 
		 */
		if ((errno = OSPX_pthread_key_create(&g_ospx_key))) {
			fprintf(stderr, "@%s-OSPX_TLS_create: %s\n",
				__FUNCTION__, strerror(errno));

			return -1;
		}
		installer = 1;
		g_installer = (int)OSPX_pthread_id();
	}

	/* @OSPX_library_init should be called in every threads 
	 * who is not created by our library, and the @OSPX_library_end
	 * should be called if the threads are going to exit.
	 */
	if (!OSPX_pthread_getspecific(g_ospx_key)) {
		OSPX_tls_t *tls;
		
		/* Verify the TLS datas */
		if (!(tls = (OSPX_tls_t *)calloc(1, sizeof(OSPX_tls_t)))) {
			/* Are we responsible for freeing the TLS key ? 
			 * To make sure that the library works well, the
			 * caller should call the @OSPX_library_init in 
			 * the main routine of the APP.
			 */
			if (g_installer == (int)OSPX_pthread_id()) {
				OSPX_pthread_key_delete(g_ospx_key);
				g_ospx_key = 0;	
			}
			errno = ENOMEM;
			return -1;
		}
	
		/* @DuplicateHandle 
		 *
		 * NOTE:
		 *     We set the fake handle here. we always assume 
		 *  that the user won't call @OSPX_pthread_detach in 
		 *  the thread routine if the thread is not created 
		 *  by @OSPX_pthread_create.
		 */
	#ifdef _WIN	
		tls->h = GetCurrentThread();
	#endif
		/* Attach the TLS datas */
		OSPX_pthread_setspecific(g_ospx_key, tls);
	}
		
	if (installer) {
		static uint8_t sl_sys_em = 0;
		
		/* Register the default error function */ 
		if (!sl_sys_em) {
			sl_sys_em = OSPX_M_SYS;
			if (OSPX_error_register(&sl_sys_em, "Sys", OSPX_sys_strerror)) {
				fprintf(stderr, "@%s-OSPX_error_register error: %s\n",
					__FUNCTION__, strerror(errno));
			
				/* We just ignore the error */
			}	
		}
	}
	
	return 0;
}

EXPORT void 
OSPX_library_end() 
{
	OSPX_tls_t *tls;

	/* Free the TLS datas */
	if (!g_ospx_key && 
		(tls = OSPX_pthread_getspecific(g_ospx_key))) {
		/* We call @OSPX_setlasterror to free the error prefix strings */
		OSPX_setlasterror(OSPX_MAKERROR(OSPX_M_SYS, 0));
		if (!tls->f_ltls)
			free((void *)tls);
		
		/* Check whether we are the owner */
		if (g_installer == (int)OSPX_pthread_id()) {	
			/* We do the clean job here. */
			OSPX_pthread_key_delete(g_ospx_key);
			g_ospx_key = 0;
		}
	}
}
/**************************************OSPX_tls**********************/

/**************************************OSPX_com**********************/
#ifdef _WIN
EXPORT int 
OSPX_gettimeofday(struct timeval *tv, struct timezone *tz) 
{
	struct _timeb tb;

	_ftime(&tb);
	if (tv) {
		tv->tv_sec  = (long)tb.time;
		tv->tv_usec = tb.millitm * 1000;
	}

	if (tz) {
		tz->tz_minuteswest = tb.timezone;
		tz->tz_dsttime = tb.dstflag;
	}

	return 0;
}
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

EXPORT long  
OSPX_interlocked_increase(long volatile *target) 
{
	long lorg;
	
	pthread_mutex_lock(&g_lock);
	lorg =  ++ *target;
	pthread_mutex_unlock(&g_lock);

	return lorg;
}

EXPORT long  
OSPX_interlocked_decrease(long volatile *target) 
{
	long lorg;
	
	pthread_mutex_lock(&g_lock);
	lorg = -- *target;
	pthread_mutex_unlock(&g_lock);

	return lorg;
}

EXPORT long 
OSPX_interlocked_add(long volatile *target, long value)
{
	long lorg;

	pthread_mutex_lock(&g_lock);
	lorg = *target;
	*target += value;
	pthread_mutex_unlock(&g_lock);
	
	return lorg;
}

#endif

/**************************************OSPX_thread*******************/
typedef struct {
#ifdef _WIN
	/* We use the h to record the real thread handle,
	 * the handle returned by GetCurrentThread() is
	 * not the effective thread handle.
	 */
	HANDLE h;
#endif
	int  (*routine)(void *);
	void *arglst;
} OSPX_param_t;

#ifdef _WIN
unsigned  __stdcall 
OSPX_thread_entry(void *arglst) 
{
	OSPX_tls_t ltls  = {0};
	OSPX_param_t *p  = arglst;
	int (*thread_routine)(void *) = p->routine;
	void *arg = p->arglst;
	
	/* Record the handle */
	ltls.h = p->h;
	free(p);
	
	/* Attach the TLS datas */
	if (g_ospx_key) {
		ltls.f_ltls = 1;
		OSPX_pthread_setspecific(g_ospx_key, &ltls);
	}

	return (*thread_routine)(arg);		
}
#else
void *
OSPX_thread_entry(void *arglst) 
{
	OSPX_tls_t ltls = {0};
	OSPX_param_t *p = arglst;
	int (*thread_routine)(void *) = p->routine;
	void *arg = p->arglst;
	
	free(p);
	
	/* Attach the TLS datas */
	if (g_ospx_key) {
		ltls.f_ltls = 1;
		OSPX_pthread_setspecific(g_ospx_key, &ltls);
	}

	pthread_exit(
			(void *)(long)(*thread_routine)(arg)
		);
}
#endif

EXPORT int 
OSPX_pthread_create(OSPX_pthread_t *handle, OSPX_pthread_attr_t *attr, int (*routine)(void *arg), void *arg) 
{
	OSPX_param_t *p;
	
	if (!(p = (OSPX_param_t *)calloc(1, sizeof(OSPX_param_t))))
		return ENOMEM;
	
	errno = 0;
	p->routine = routine;	
	p->arglst  = arg;
#ifdef _WIN
	{
		static int PRIORITY[] = {
			THREAD_PRIORITY_IDLE,
			THREAD_PRIORITY_LOWEST,	 THREAD_PRIORITY_BELOW_NORMAL, 
			THREAD_PRIORITY_NORMAL,  THREAD_PRIORITY_ABOVE_NORMAL,
			THREAD_PRIORITY_HIGHEST, THREAD_PRIORITY_TIME_CRITICAL, 
		};
		HANDLE h;
		
		/* In order to get the thread handle before the thread's running,
		 * we suspend the thread and then resume it
		 */
		h = (HANDLE)_beginthreadex(NULL, attr ? attr->stack_size : 0, OSPX_thread_entry, p, CREATE_SUSPENDED, NULL);
		if (errno) 
			free(p);
		else {
			/* Record the real handle */
			p->h = h;
			*handle = h;

			if (attr) {
				int sche = NORMAL_PRIORITY_CLASS;
				
				/* Set the schedule policy */
				if (ep_NONE != attr->sche_policy) {	
					switch (attr->sche_policy) {
					case ep_RR:
						sche = HIGH_PRIORITY_CLASS;
						break;
					case ep_FIFO:
						sche = REALTIME_PRIORITY_CLASS;
						break;
					default:
						break;
					}
					SetPriorityClass(h, sche);
				}

				/* Set the schedule priority */
				if (attr->sche_priority > 0) {
					int index = 0;

					if (attr->sche_priority >= 100)
						attr->sche_priority = 100;

					index = attr->sche_priority * 7 / 100;
					if (index >= 7)
						-- index;
					SetThreadPriority(h, PRIORITY[index]);
				}
			}
			ResumeThread(h);

			if (attr && !attr->joinable)
				CloseHandle(h);
		}
	}
	return errno;
#else
	{
		int error, sche = SCHED_OTHER;

		pthread_attr_t *pattr = NULL, att;
		if (attr && !pthread_attr_init(&att)) {
			pattr = &att;
			
			/* Set the stack size */
			if (attr->stack_size)
				pthread_attr_setstacksize(pattr, attr->stack_size);
			
			/* Set the scope attributes */
			if (!attr->joinable) {
				pthread_attr_setscope(pattr, PTHREAD_SCOPE_SYSTEM);
				pthread_attr_setdetachstate(pattr, PTHREAD_CREATE_DETACHED);	
			}
			
			/* Set the schedule policy */
			if (ep_NONE != attr->sche_policy) {	
				/* android-NDK does not support @pthread_attr_get_inheritsched */
#ifdef HAS_PTHREAD_ATTR_GETINHERITSCHED
				int inh;
				
				pthread_attr_getinheritsched(pattr, &inh);
				if (PTHREAD_EXPLICIT_SCHED != inh)
					pthread_attr_setinheritsched(pattr, PTHREAD_EXPLICIT_SCHED);
#endif
				switch (attr->sche_policy) {
				case ep_RR:
					sche = SCHED_RR;
					break;
				case ep_FIFO:
					sche = SCHED_FIFO;
					break;
				default:
					;
				}
				pthread_attr_setschedpolicy(pattr, sche);
			}
	
			if (attr->sche_priority > 0) {
				int min = sched_get_priority_min(sche), max = sched_get_priority_max(sche);
				struct sched_param param = {0};
				
				if (attr->sche_priority >= 100)
					attr->sche_priority = 100;
					
				param.sched_priority = min + attr->sche_priority  * (max - min + 1) / 100;
				pthread_attr_setschedparam(pattr, &param);
			}
		}
		error = pthread_create(handle, pattr, OSPX_thread_entry, p);	
		if (error)
			free(p);

		if (pattr)
			pthread_attr_destroy(pattr);
		
		return error;
	}
#endif
}

EXPORT int 
OSPX_pthread_join(OSPX_pthread_t handle, int *ret) 
{
#ifdef _WIN
	DWORD code;

	if (!handle || WAIT_OBJECT_0 != WaitForSingleObject(handle, INFINITE))
		return EINVAL;
	
	GetExitCodeThread(handle, &code);
	CloseHandle(handle);

	if (ret)
		*ret = code;
	return 0;
#else
	return pthread_join(handle, (void **)&ret);
#endif
}

EXPORT int 
OSPX_pthread_detach(OSPX_pthread_t handle) 
{
#ifdef _WIN
	return CloseHandle(handle) ? 0 : EINVAL;	
#else
	return pthread_detach(handle);
#endif
}

EXPORT
OSPX_pthread_t OSPX_pthread_self() 
{
#ifdef _WIN
	OSPX_tls_t *tls;	
	/* We should not call GetCurrentThread(Id)() to
	 * get the thread handle (If we do this, Calling 
	 * OSPX_pthread_detach( OSPX_pthread_self()) will
	 * have no effect)
	 */
	tls = (OSPX_tls_t *)OSPX_pthread_getspecific(g_ospx_key);
	if (!tls)
		return 0;
	return tls->h;
#else
	return pthread_self();
#endif
}

/**************************************OSPX_once_var**********************/
#ifdef _WIN
EXPORT int 
OSPX_pthread_once(OSPX_pthread_once_t *once_control, void (*init_routine)())
{
	if (!once_control->boolean) {
		if (!once_control->ref) {
			/**
			 * Only one thread can get the chance to call @init_routine 
			 */
			if (1 == OSPX_interlocked_increase(&once_control->ref)) {
				(*init_routine)();
				once_control->boolean = 1;
			}
		}
		
		/**
		 * Set a barrier here to synchronize the env 
		 */
		while (!once_control->boolean) ;
	}

	return 0;
}
#endif

/**************************************OSPX_ipc**********************/
#ifndef _WIN
EXPORT  int 
OSPX_pthread_mutex_init(OSPX_pthread_mutex_t *mut, int recursive)  
{
	pthread_mutexattr_t *attr = NULL, xattr;

	if (recursive) {
		if ((errno = pthread_mutexattr_init(&xattr)))
			return errno;
	
		if ((errno = pthread_mutexattr_settype(&xattr, PTHREAD_MUTEX_RECURSIVE))) {
			pthread_mutexattr_destroy(&xattr);
			return errno;
		}
		attr = &xattr;
	} 
	
	errno = pthread_mutex_init(mut, attr);
	if (errno && attr)
		pthread_mutexattr_destroy(attr);
	return errno;
}
#endif

#ifdef _WIN
EXPORT int 
OSPX_pthread_cond_init(OSPX_pthread_cond_t *cond) 
{
	memset(cond, 0, sizeof(OSPX_pthread_cond_t));
	
	InitializeCriticalSectionAndSpinCount(&cond->section, 1000);
	if (!(cond->hEvent = CreateEvent(NULL,TRUE,FALSE,NULL))) {
		DeleteCriticalSection(&cond->section);
		return ENOMEM;
	}
	cond->n_waiting = cond->n_to_wake = cond->generation = 0;

	return 0;
}

EXPORT int 
OSPX_pthread_cond_signal(OSPX_pthread_cond_t *cond) 
{
	EnterCriticalSection(&cond->section);
	++cond->n_to_wake;
	cond->generation++;
	SetEvent(cond->hEvent);
	LeaveCriticalSection(&cond->section);
	
	return 0;
}

EXPORT int 
OSPX_pthread_cond_broadcast(OSPX_pthread_cond_t *cond) 
{
	EnterCriticalSection(&cond->section);
	cond->n_to_wake = cond->n_waiting;
	cond->generation++;
	SetEvent(cond->hEvent);
	LeaveCriticalSection(&cond->section);
	
	return 0;
}

EXPORT int 
OSPX_pthread_cond_destroy(OSPX_pthread_cond_t *cond) 
{
	DeleteCriticalSection(&cond->section);
	CloseHandle(cond->hEvent);
	memset(cond, 0, sizeof(OSPX_pthread_cond_t));
	
	return 0;
}
#else
EXPORT int 
OSPX_pthread_cond_init(OSPX_pthread_cond_t *cond)
{
	int e;

	pthread_condattr_t *attr = NULL;
#if defined(HAS_CLOCK_GETTIME) && defined(HAS_CLOCK_MONOTONIC) && defined(HAS_PTHREAD_CONDATTR_SETCLOCK)
	/* We use the mononic clock if the OS supports it */
	{
		pthread_condattr_t attr0;
		
		if (!pthread_condattr_init(&attr0)) {
			pthread_condattr_setclock(&attr0, CLOCK_MONOTONIC);
			attr = &attr0;
		}
	}
#endif
	e = pthread_cond_init(cond, attr);
	if (attr)
		pthread_condattr_destroy(attr);
	
	return e;
}
#endif

EXPORT int 
OSPX_pthread_cond_timedwait(OSPX_pthread_cond_t *cond, OSPX_pthread_mutex_t *mut, long to) 
{
	int error = 0;

#ifdef _WIN
	int generation_at_start;
	int waiting = 1;
	DWORD ms = INFINITE, to_orig = INFINITE, startTime, endTime;
	if (to >= 0) 
		to_orig = ms = to;	
	
	EnterCriticalSection(&cond->section);
	++cond->n_waiting;
	generation_at_start = cond->generation;
	LeaveCriticalSection(&cond->section);

	LeaveCriticalSection(mut);	
	startTime = GetTickCount();
	do {
		DWORD res;
		res = WaitForSingleObject(cond->hEvent, ms);
		EnterCriticalSection(&cond->section);
		if (cond->n_to_wake &&
		    cond->generation != generation_at_start) {
			--cond->n_to_wake;
			--cond->n_waiting;
			error = 0;
			waiting = 0;
			goto out;
		} else if (res != WAIT_OBJECT_0) {
			error = (res==WAIT_TIMEOUT) ? ETIMEDOUT : -1;
			--cond->n_waiting;
			waiting = 0;
			goto out;
		} else if (ms != INFINITE) {
			endTime = GetTickCount();
			if (startTime + to_orig <= endTime) {
				error = ETIMEDOUT; /* to */
				--cond->n_waiting;
				waiting = 0;
				goto out;
			} else {
				ms = startTime + to_orig - endTime;
			}
		}
		/* If we make it here, we are still waiting. */
		if (cond->n_to_wake == 0) {
			/* There is nobody else who should wake up; reset
			 * the event. */
			ResetEvent(cond->hEvent);
		}
	out:
		LeaveCriticalSection(&cond->section);
	} while (waiting);
	EnterCriticalSection(mut);

	EnterCriticalSection(&cond->section);
	if (!cond->n_waiting)
		ResetEvent(cond->hEvent);
	LeaveCriticalSection(&cond->section);
#else
	struct timespec abstime = {0};
	
	if (0 > to)
		error = pthread_cond_wait(cond, mut);

#ifdef HAS_CLOCK_GETTIME
#ifdef HAS_CLOCK_MONOTONIC
	clock_gettime(CLOCK_MONOTONIC, &abstime);
#else	
	clock_gettime(CLOCK_REALTIME, &abstime);
#endif	
	abstime.tv_nsec += (to % 1000) * 1000000;
	abstime.tv_sec  +=  to / 1000;
#else
	{
		struct timeval tv;
		
		OSPX_gettimeofday(&tv, NULL);
		abstime.tv_sec = tv.tv_sec + (to / 1000);
		abstime.tv_nsec = (tv.tv_usec + (to % 1000) * 1000) * 1000;
	}
#endif
	
	if (abstime.tv_nsec > 1000000000) {
		abstime.tv_sec  += 1;
		abstime.tv_nsec -= 1000000000;
	}
	
	error = pthread_cond_timedwait(cond, mut, &abstime);
#endif	

	return error;
}


/**************************************OSPX_sem**********************/
#ifdef _WIN
static const int SEMMAGIC = 0x45df00e1;

EXPORT int 
OSPX_sem_init(OSPX_sem_t *sem, unsigned int value) 
{
	if (value > (LONG)(((ULONG)-1)/2 -1))
		value = (LONG)(((ULONG)-1)/2 -1);
	
	sem->hSem  = CreateSemaphore(NULL, value, (LONG)(((ULONG)-1)/2 -1), NULL);
	if (!sem->hSem) {
		errno = ENOMEM;
		return -1;
	}
	sem->magic = SEMMAGIC;
	InitializeCriticalSectionAndSpinCount(&sem->section, 1000);		
	sem->waiters = sem->cakes = 0;

	return 0;
}

EXPORT int 
OSPX_sem_post(OSPX_sem_t *sem) 
{
	if (SEMMAGIC != sem->magic) {
		errno = EINVAL;
		return -1;
	}

	EnterCriticalSection(&sem->section);
	++ sem->cakes;
	LeaveCriticalSection(&sem->section);
	
	ReleaseSemaphore(sem->hSem, 1, NULL);
	return 0;
}

EXPORT int 
OSPX_sem_wait(OSPX_sem_t *sem) 
{
	return OSPX_sem_timedwait(sem, -1);
}

EXPORT int 
OSPX_sem_getvalue(OSPX_sem_t *sem, int *val) 
{
	if (SEMMAGIC != sem->magic) {
		errno = EINVAL;
		return -1;
	}
	
	EnterCriticalSection(&sem->section);
	*val = sem->cakes > sem->waiters ? sem->cakes - sem->waiters : 0;
	LeaveCriticalSection(&sem->section);
	return 0;
}

EXPORT int 
OSPX_sem_destroy(OSPX_sem_t *sem) 
{
	if (SEMMAGIC != sem->magic) {
		errno = EINVAL;
		return -1;
	}
	sem->magic = 0;
	DeleteCriticalSection(&sem->section);
	CloseHandle(sem->hSem);
	return 0;
}
#endif

EXPORT int 
OSPX_sem_timedwait(OSPX_sem_t *sem, long ms) 
{
	int error;
#ifdef _WIN
	int ret;
	
	EnterCriticalSection(&sem->section);
	++ sem->waiters;
	LeaveCriticalSection(&sem->section);

	if (ms >= 0)
		ret = WaitForSingleObject(sem->hSem, ms);
	else
		ret = WaitForSingleObject(sem->hSem, INFINITE);
	switch (ret) {
	case WAIT_ABANDONED:
		error = EINVAL;
		break;
	case WAIT_OBJECT_0:
		error = 0;
		break;
	case WAIT_TIMEOUT:
		error = ETIMEDOUT;
		break;
	}

	EnterCriticalSection(&sem->section);
	-- sem->waiters;
	if (!error)
		-- sem->cakes;

	assert(sem->waiters >= 0 && sem->cakes >= 0);
	LeaveCriticalSection(&sem->section);
#elif defined(HAS_SEM_TIMEDWAIT)	
	if (0 > ms)
		error = sem_wait(sem);
	else {
		struct timespec abstime;

		clock_gettime(CLOCK_REALTIME, &abstime);
		abstime.tv_nsec += (ms % 1000) * 1000000;
		abstime.tv_sec  += ms / 1000;
		if (abstime.tv_nsec > 1000000000) {
			abstime.tv_sec  += 1;
			abstime.tv_nsec -= 1000000000;
		}
		error = sem_timedwait(sem, &abstime);
	}
#else 
	/* Fix me !!! (OSX) */
	error = sem_wait(sem);
#endif
	return error;
}


#ifndef HAS_PTHREAD_RWLOCK
static const int RWMAGIC = 0xf2349e20;

EXPORT int 
OSPX_pthread_rwlock_init(OSPX_pthread_rwlock_t *rwlock) 
{
	int error;
	
	memset(rwlock, 0, sizeof(OSPX_pthread_rwlock_t));
	rwlock->rw_magic = RWMAGIC;

	if ((error = OSPX_pthread_mutex_init(&rwlock->rw_mut, 0)))
		return error;
	
	if ((error = OSPX_pthread_cond_init(&rwlock->rw_condreaders)))
		goto err0;
	
	if ((error = OSPX_pthread_cond_init(&rwlock->rw_condwriters)))
		goto err1;

	return 0;
err1:
	OSPX_pthread_cond_destroy(&rwlock->rw_condreaders);
err0:
	OSPX_pthread_mutex_destroy(&rwlock->rw_mut);
	
	return error;
}

EXPORT int 
OSPX_pthread_rwlock_rdlock(OSPX_pthread_rwlock_t *rwlock) 
{
	int error;

	if (RWMAGIC != rwlock->rw_magic)
		return EINVAL;
	if ((error = OSPX_pthread_mutex_lock(&rwlock->rw_mut)))
		return error;
	
	/* Give preference to waiting writers */
	while (rwlock->rw_ref < 0 || rwlock->rw_nwaitwriters > 0) {
		++ rwlock->rw_nwaitreaders; 
		error = OSPX_pthread_cond_wait(&rwlock->rw_condreaders, &rwlock->rw_mut);
		-- rwlock->rw_nwaitreaders; 
		if (error)
			break;
	}

	if (!error)
		/* Another reader has a read lock */
		++ rwlock->rw_ref; 
	OSPX_pthread_mutex_unlock(&rwlock->rw_mut);
	
	return error;
}

EXPORT int 
OSPX_pthread_rwlock_tryrdlock(OSPX_pthread_rwlock_t *rwlock) 
{
	int error;

	if (RWMAGIC != rwlock->rw_magic)
		return EINVAL;
	if ((error = OSPX_pthread_mutex_lock(&rwlock->rw_mut)))
		return error;
	
	if (rwlock->rw_ref < 0 || rwlock->rw_nwaitwriters > 0)
		/* The lock is held by a writers or waiting writers */
		error = EBUSY;
	else
		++ rwlock->rw_ref;
	OSPX_pthread_mutex_unlock(&rwlock->rw_mut);
	
	return error;
}

EXPORT int 
OSPX_pthread_rwlock_wrlock(OSPX_pthread_rwlock_t *rwlock) 
{
	int error;

	if (RWMAGIC != rwlock->rw_magic)
		return EINVAL;
	if ((error = OSPX_pthread_mutex_lock(&rwlock->rw_mut)))
		return error;
	
	while (rwlock->rw_ref) {
		++ rwlock->rw_nwaitwriters;
		error = OSPX_pthread_cond_wait(&rwlock->rw_condwriters, &rwlock->rw_mut);
		-- rwlock->rw_nwaitwriters;

		if (error)
			break;
	}

	if (!error)
		rwlock->rw_ref = -1;

	OSPX_pthread_mutex_unlock(&rwlock->rw_mut);
	return error;
}

EXPORT int 
OSPX_pthread_rwlock_trywrlock(OSPX_pthread_rwlock_t *rwlock) 
{
	int error;
	
	if (RWMAGIC != rwlock->rw_magic)
		return EINVAL;
	if ((error = OSPX_pthread_mutex_lock(&rwlock->rw_mut)))
		return error;
	
	if (rwlock->rw_ref)
		/* The lock is held by either writer or readers */
		error = EBUSY;
	else
		/* available, indicate a writer has it */
		rwlock->rw_ref = -1;
	OSPX_pthread_mutex_unlock(&rwlock->rw_mut);
	
	return error;
}

EXPORT int 
OSPX_pthread_rwlock_unlock(OSPX_pthread_rwlock_t *rwlock) 
{
	int error;

	if (RWMAGIC != rwlock->rw_magic)
		return EINVAL;
	if ((error = OSPX_pthread_mutex_lock(&rwlock->rw_mut)))
		return error;
	
	if (rwlock->rw_ref > 0)
		-- rwlock->rw_ref;
	else if (rwlock->rw_ref == -1)
		rwlock->rw_ref = 0;
	else {
		fprintf(stderr, "@%s error:<rw_ref:%d>\n",
			__FUNCTION__, rwlock->rw_ref);
		abort();
	}

	/* Give preference to waiting writers writers over waitting readers */
	if (rwlock->rw_nwaitwriters > 0) {
		if (rwlock->rw_ref == 0)
			error = OSPX_pthread_cond_signal(&rwlock->rw_condwriters);
	} else if (rwlock->rw_nwaitreaders > 0) 
		error = OSPX_pthread_cond_broadcast(&rwlock->rw_condreaders);
	OSPX_pthread_mutex_unlock(&rwlock->rw_mut);
	
	return error;
}

EXPORT int 
OSPX_pthread_rwlock_destroy(OSPX_pthread_rwlock_t *rwlock) 
{
	int error;

	if (RWMAGIC != rwlock->rw_magic)
		return EINVAL;
	
	if ((error = OSPX_pthread_mutex_destroy(&rwlock->rw_mut)) ||
		(error = OSPX_pthread_cond_destroy(&rwlock->rw_condreaders)) ||
		(error = OSPX_pthread_cond_destroy(&rwlock->rw_condwriters))) 
		return error;
	
	return 0;
}
#endif


