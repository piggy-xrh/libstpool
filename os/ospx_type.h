#ifndef __OSPX_TYPE_H__
#define __OSPX_TYPE_H__
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

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64)
#define _WIN
#define WIN32_LEAN_AND_MEAN   /* Exclude rarely-used stuff from Windows headers */
#define WIN64_LEAN_AND_MEAN   

#include <sys/timeb.h>
#include <windows.h>
#include <winsock2.h>
#include <process.h>

#if defined(_HAS_STDINT_H) || defined(HAS_STDINT_H)
#include <stdint.h>   /* For dev-c++ */
#else 
typedef __int8  int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int8  uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
typedef int ssize_t;
#endif

/* Dev-cpp */
#ifndef _TIMEZONE_DEFINED
#define _TIMEZONE_DEFINED
struct timezone {
	int tz_minuteswest;     /* minutes west of Greenwich */
	int tz_dsttime;         /* type of DST correction */
};
#endif

/* NOTE: The theory is from libevent */
typedef struct {
	ssize_t n_waiting, n_to_wake, generation;
	CRITICAL_SECTION section;
	HANDLE  hEvent;
} OSPX_pthread_cond_t;

typedef struct {
	int magic;
	CRITICAL_SECTION section;
	int waiters;
	int cakes;
	HANDLE  hSem;
} OSPX_sem_t;

typedef struct {
	HANDLE   h;
	uint64_t error;	
	char *errprefix;
	uint16_t errprefix_bufflen;
	uint16_t f_ltls:1;
	uint16_t f_resv:15;
} OSPX_tls_t;

typedef DWORD  OSPX_pthread_key_t;
typedef HANDLE OSPX_pthread_t;
typedef CRITICAL_SECTION OSPX_pthread_mutex_t;

typedef struct {
	int  boolean;
	long ref;
} OSPX_pthread_once_t;

#define  OSPX_PTHREAD_ONCE_INIT {0, 0}

#else
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>        
#include <fcntl.h>

typedef struct {
	uint64_t error;
	char *errprefix;
	uint16_t errprefix_bufflen;
	uint16_t f_ltls:1;
	uint16_t f_resv:15;
} OSPX_tls_t;

typedef pthread_key_t OSPX_pthread_key_t;
typedef pthread_t OSPX_pthread_t;
typedef pthread_mutex_t OSPX_pthread_mutex_t;
typedef pthread_cond_t  OSPX_pthread_cond_t;
typedef pthread_once_t OSPX_pthread_once_t;
typedef sem_t OSPX_sem_t;
#define OSPX_PTHREAD_ONCE_INIT PTHREAD_ONCE_INIT

#endif

#ifdef HAS_PTHREAD_RWLOCK
#include <pthread.h>  // minGw + minsys
typedef pthread_rwlock_t OSPX_pthread_rwlock_t;
#else
/* NOTE: The theory is from UNIX Network Programming (version2) */
typedef struct {
	OSPX_pthread_mutex_t rw_mut;
	OSPX_pthread_cond_t rw_condreaders;
	OSPX_pthread_cond_t rw_condwriters;
	int  rw_magic, rw_ref;
	int  rw_nwaitreaders, rw_nwaitwriters;
} OSPX_pthread_rwlock_t;
#endif

#endif
