#ifndef __OSPX_H__
#define __OSPX_H__
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

#include "ospx_config.h"
#include "ospx_type.h"

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64)
#define _WIN
#ifdef HAS_SDKDDKVER_H
#include <SDKDDKVer.h> // InitializeCriticalSectionAndSpinCount
#endif
#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#else
#define EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * If you want to operate the error APIs which is defined
 * in the ospx_error.h in the thread,  you should call
 * OSPX_library_init with LB_F_ERRLIB flag before your calling
 * any error APIs. 
 */
#define LB_F_ERRLIB       0x1

EXPORT int  OSPX_library_init(long lflags);
EXPORT void OSPX_library_end();

/** Thread */
enum ep_POLICY
{
	ep_NONE,
	ep_RR,
	ep_FIFO,
	ep_OTHER
};

typedef struct {
	/** The thread is joinable */
	int joinable;

	/** Stack size (0:default) */
	int stack_size;    
	
	/** Schedule policy */
	enum ep_POLICY sche_policy;
	
	/** Schedule priority ([1-100] 0:default) */
	int sche_priority; 
} OSPX_pthread_attr_t;

EXPORT int OSPX_pthread_create(OSPX_pthread_t *handle, OSPX_pthread_attr_t *attr, int (*routine)(void *arg), void *arg);
EXPORT int OSPX_pthread_join(OSPX_pthread_t handle, int *ret);
EXPORT int OSPX_pthread_detach(OSPX_pthread_t handle);
EXPORT OSPX_pthread_t OSPX_pthread_self();

/** Thread ID */
#ifndef _WIN
#define OSPX_pthread_id() (OSPX_pthread_t)pthread_self()
#else
#define OSPX_pthread_id() (OSPX_pthread_t)GetCurrentThreadId()
#endif

#ifndef _WIN

/*================================================LINUX========================================*/
#define OSPX_gettimeofday gettimeofday
EXPORT  long OSPX_interlocked_increase(long volatile *target);
EXPORT  long OSPX_interlocked_decrease(long volatile *target);
EXPORT  long OSPX_interlocked_add(long volatile *target, long value);

/**
 * NOTE: The return value of the interfaces below is 
 * the same as the POSIX interfaces.
 */
#define OSPX_pthread_key_create(key)        pthread_key_create(key, NULL)
#define OSPX_pthread_key_delete(key)        pthread_key_delete(key)
#define OSPX_pthread_getspecific(key)       pthread_getspecific(key)
#define OSPX_pthread_setspecific(key, val)  pthread_setspecific(key, val);

/** Once var */
#define OSPX_pthread_once(once_ctl, cb) pthread_once(once_ctl, cb)

/** Mutex */
EXPORT  int OSPX_pthread_mutex_init(OSPX_pthread_mutex_t *, int recursive);    
#define OSPX_pthread_mutex_lock(mut)    pthread_mutex_lock(mut)
#define OSPX_pthread_mutex_trylock(mut) pthread_mutex_trylock(mut)
#define OSPX_pthread_mutex_unlock(mut)  pthread_mutex_unlock(mut)
#define OSPX_pthread_mutex_destroy(mut) pthread_mutex_destroy(mut)

/** Condition */
EXPORT int OSPX_pthread_cond_init(OSPX_pthread_cond_t *cond);
#define OSPX_pthread_cond_wait(cond, mut) pthread_cond_wait(cond, mut)
#define OSPX_pthread_cond_signal(cond)    pthread_cond_signal(cond)
#define OSPX_pthread_cond_broadcast(cond) pthread_cond_broadcast(cond)
#define OSPX_pthread_cond_destroy(cond)   pthread_cond_destroy(cond)
EXPORT int OSPX_pthread_cond_timedwait(OSPX_pthread_cond_t *, OSPX_pthread_mutex_t *, long ms);

/* Semaphore */
#define OSPX_sem_init(sem, value) sem_init(sem, 0, value)
#define OSPX_sem_post(sem)        sem_post(sem)
#define OSPX_sem_wait(sem)        sem_wait(sem)
#define OSPX_sem_destroy(sem)     sem_destroy(sem)
#define OSPX_sem_getvalue(sem, val) sem_getvalue(sem, val)
EXPORT int OSPX_sem_timedwait(OSPX_sem_t *sem, long ms);

/** Share memory */
#define OSPX_shm_open(name, oflag, mode)                 shm_open(name, oflag, mode)
#define OSPX_shm_unlink(name)                            shm_unlink(name)
#define OSPX_mmap(addr, length, prot, flags, fd, offset) mmap(addr, length, prot, flags, fd, offset)
#define OSPX_munmap(addr, length)                        munmap(addr, length)
/*=============================================================================================*/
#else
/*===============================================WIN===========================================*/
EXPORT int OSPX_gettimeofday(struct timeval *, struct timezone *);
#define OSPX_interlocked_increase(target) InterlockedIncrement(target)
#define OSPX_interlocked_decrease(target) InterlockedDecrement(target)
#define OSPX_interlocked_add(target, inc) InterlockedExchangeAdd(target, inc)

#define OSPX_pthread_key_create(key)       (*(key) = TlsAlloc(), -1 == *(key) ? ENOMEM : 0)
#define OSPX_pthread_key_delete(key)       (TlsFree(key) ? 0 : EINVAL)
#define OSPX_pthread_getspecific(key)       TlsGetValue(key)
#define OSPX_pthread_setspecific(key, val)  (TlsSetValue(key, val) ? 0 : EINVAL)

EXPORT int OSPX_pthread_once(OSPX_pthread_once_t *once_control, void (*init_routine)());

#define OSPX_pthread_mutex_init(mut, recursive)  (InitializeCriticalSection(mut), 0)
#define OSPX_pthread_mutex_lock(mut)    (EnterCriticalSection(mut), 0)
#define OSPX_pthread_mutex_trylock(mut) (TryEnterCriticalSection(mut) ? 0 : EBUSY)
#define OSPX_pthread_mutex_unlock(mut)  (LeaveCriticalSection(mut), 0)
#define OSPX_pthread_mutex_destroy(mut) (DeleteCriticalSection(mut), 0)

EXPORT int OSPX_pthread_cond_init(OSPX_pthread_cond_t *);
#define OSPX_pthread_cond_wait(cond, mut) OSPX_pthread_cond_timedwait(cond, mut, -1)
EXPORT int OSPX_pthread_cond_timedwait(OSPX_pthread_cond_t *, OSPX_pthread_mutex_t *, long);
EXPORT int OSPX_pthread_cond_signal(OSPX_pthread_cond_t *);
EXPORT int OSPX_pthread_cond_broadcast(OSPX_pthread_cond_t *);
EXPORT int OSPX_pthread_cond_destroy(OSPX_pthread_cond_t *);

EXPORT int OSPX_sem_init(OSPX_sem_t *, unsigned int);
EXPORT int OSPX_sem_timedwait(OSPX_sem_t *, long);
EXPORT int OSPX_sem_post(OSPX_sem_t *);
EXPORT int OSPX_sem_wait(OSPX_sem_t *);   
EXPORT int OSPX_sem_getvalue(OSPX_sem_t *, int *);
EXPORT int OSPX_sem_destroy(OSPX_sem_t *);

/**
 * We do not support the posix share memory since it
 * is not neccessary for us at present. We can use APIs
 * (CreateFileMapping/OpenFileMapping/...) to reach
 * our goal.
 */
/*=============================================================================================*/
#endif

/** RWlock */
#ifdef HAS_PTHREAD_RWLOCK
#define OSPX_pthread_rwlock_init(rwlock)      pthread_rwlock_init(rwlock, NULL)
#define OSPX_pthread_rwlock_rdlock(rwlock)    pthread_rwlock_rdlock(rwlock)
#define OSPX_pthread_rwlock_tryrdlock(rwlock) pthread_rwlock_tryrdlock(rwlock)
#define OSPX_pthread_rwlock_wrlock(rwlock)    pthread_rwlock_wrlock(rwlock)
#define OSPX_pthread_rwlock_trywrlock(rwlock) pthread_rwlock_trywrlock(rwlock)
#define OSPX_pthread_rwlock_unlock(rwlock)    pthread_rwlock_unlock(rwlock)
#define OSPX_pthread_rwlock_destroy(rwlock)   pthread_rwlock_destroy(rwlock)
#else
EXPORT int OSPX_pthread_rwlock_init(OSPX_pthread_rwlock_t *);
EXPORT int OSPX_pthread_rwlock_rdlock(OSPX_pthread_rwlock_t *);
EXPORT int OSPX_pthread_rwlock_tryrdlock(OSPX_pthread_rwlock_t *);
EXPORT int OSPX_pthread_rwlock_wrlock(OSPX_pthread_rwlock_t *);
EXPORT int OSPX_pthread_rwlock_trywrlock(OSPX_pthread_rwlock_t *);
EXPORT int OSPX_pthread_rwlock_unlock(OSPX_pthread_rwlock_t *);
EXPORT int OSPX_pthread_rwlock_destroy(OSPX_pthread_rwlock_t *);
#endif

#ifdef __cplusplus
}
#endif


#endif
