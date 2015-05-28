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

#include <assert.h>

#include "mpool.h"
#include "stpool.h"
#include "tpool.h"

const size_t g_const_TASK_SIZE = sizeof(struct task_t);

/* stpool is just a simple Wrapper of the tpool */

static struct mpool_t *gs_mp = NULL;

const char *
stpool_version() {
	return "2015/04/10-2.6-libstpool-GC";
}

static void
tpool_hook_atexit(struct tpool_t *pool, void *arg) {
	free(pool);
}

void   
stpool_task_init(struct sttask_t *ptsk, 
				const char *name, int (*run)(struct sttask_t *ptsk),
				void (*complete)(struct sttask_t *ptsk, long vmflags, int code),
				void *arg) {
	struct task_t *nptsk = (struct task_t *)ptsk;
	
	memset(nptsk, 0, sizeof(struct task_t));
	tpool_task_init(nptsk, name, (int (*)(struct task_t *))run, 
			(void (*)(struct task_t *, long , int))complete, arg);	
	nptsk->pri_policy = P_SCHE_BACK;
	nptsk->f_mask |= TASK_F_PUSH;
}

struct sttask_t *
stpool_task_new(const char *name,
				int (*run)(struct sttask_t *ptsk),
				void (*complete)(struct sttask_t *ptsk, long vmflags, int code),
				void *arg) {
	struct task_t  *ptsk;
	static int volatile sl_mp_initialized = 0;
	
	/* Create a global object pool */
	if (!sl_mp_initialized) {
		struct mpool_t *mp;
		
		if (1 == ++ sl_mp_initialized) {
			mp = malloc(sizeof(struct mpool_t));
			if (mp) {
				if (!mpool_init(mp, sizeof(struct task_t)))
					gs_mp = mp;
				else
					free(mp);
			}
		}
	}
	
	if (gs_mp) 
		ptsk = mpool_new(gs_mp);
	else
		ptsk = calloc(sizeof(struct task_t), 1);
	
	/* Initialzie the task object */
	if (ptsk) {
		if (gs_mp) {
			memset(ptsk, 0, sizeof(struct task_t));
			ptsk->f_mask = TASK_F_MPOOL;
		}

		tpool_task_init(ptsk, name, (int (*)(struct task_t *))run, 
			(void (*)(struct task_t *, long , int))complete, arg);
		
		/* Reset the task with the default priority */
		ptsk->pri_policy = P_SCHE_BACK;
		ptsk->f_mask |= TASK_F_PUSH;
	}

	return (struct sttask_t *)ptsk;
}

struct sttask_t *
stpool_task_clone(struct sttask_t *ptsk, int clone_schattr) {
	struct sttask_t *nptsk;
	struct schattr_t attr;

	/* Construct the object */
	nptsk = stpool_task_new(ptsk->task_name,
		ptsk->task_run, ptsk->task_complete, ptsk->task_arg);
	
	/* Copy the schedule attribute */
	if (nptsk && clone_schattr) {
		stpool_task_getschattr(ptsk, &attr);
		stpool_task_setschattr(nptsk, &attr);
	}

	return nptsk;
}

void stpool_task_delete(struct sttask_t *ptsk) {
	struct task_t *rptsk = (struct task_t *)ptsk;
	
	if (rptsk->f_mask & TASK_F_MPOOL) {
		assert(gs_mp);
		mpool_delete(gs_mp, rptsk);
	} else
		free(rptsk);
}

void 
stpool_task_setschattr(struct sttask_t *ptsk, struct schattr_t *attr) {
	tpool_task_setschattr((struct task_t *)ptsk, 
						(struct xschattr_t *)attr);
}

void 
stpool_task_getschattr(struct sttask_t *ptsk, struct schattr_t *attr) {
	tpool_task_getschattr((struct task_t *)ptsk, 
						(struct xschattr_t *)attr);
}

HPOOL 
stpool_create(int maxthreads, int minthreads, int suspend, int pri_q_num) {
	struct tpool_t *pool;
	
	/* It does not need to load the ospx library since 
     * we do not call any APIs who must use the TLS datas.
	 */
	pool = (struct tpool_t *)malloc(sizeof(struct tpool_t));
	if (pool) {
		if (tpool_create(pool, pri_q_num, maxthreads, minthreads, suspend)) {
			free(pool);
			pool = NULL;
		} else {
			tpool_atexit(pool, tpool_hook_atexit, NULL);	
			
			/* Enable caching the task objects for @tpool_add_routine */
		#ifndef _DISABLE_MPOOL	
			tpool_use_mpool(pool);
		#endif
		}
	}

	return (HPOOL)pool;
}

long 
stpool_addref(HPOOL hp) {	
	return tpool_addref((struct tpool_t *)hp);
}

long 
stpool_release(HPOOL hp) {		
	return tpool_release((struct tpool_t *)hp, 0);	
}

/* We do not export the interface at present. */
void 
stpool_load_env(HPOOL hp) {
	tpool_load_env((struct tpool_t *)hp);	
}


void 
stpool_set_activetimeo(HPOOL hp, long acttimeo) {
	tpool_set_activetimeo((struct tpool_t *)hp, acttimeo); 
}

void 
stpool_adjust_abs(HPOOL hp, int maxthreads, int minthreads) {
	tpool_adjust_abs((struct tpool_t *)hp, maxthreads, minthreads);
}

void 
stpool_adjust(HPOOL hp, int maxthreads, int minthreads) {
	tpool_adjust((struct tpool_t *)hp, maxthreads, minthreads);
}

int
stpool_flush(HPOOL hp) {
	return tpool_flush((struct tpool_t *)hp);
}

void 
stpool_adjust_wait(HPOOL hp) {
	tpool_adjust_wait((struct tpool_t *)hp);
}

struct stpool_stat_t *
stpool_getstat(HPOOL hp, struct stpool_stat_t *stat) {
	return (struct stpool_stat_t *)tpool_getstat((struct tpool_t *)hp, 
					 		 (struct tpool_stat_t *)stat);	
}

const char *
stpool_status_print(HPOOL hp, char *buffer, size_t bufferlen) {
	return tpool_status_print((struct tpool_t *)hp, 
							buffer, 
							bufferlen);
}

long
stpool_gettskstat(HPOOL hp, struct stpool_tskstat_t *stat) {
	return tpool_gettskstat((struct tpool_t *)hp, 
	                        (struct tpool_tskstat_t *)stat);
}

static long 
mark_walk(struct stpool_tskstat_t *st, void *arg) {
	return (long)arg;
}

long
stpool_mark_task(HPOOL hp, struct sttask_t *ptsk, long lflags) {
	if (ptsk)
		return tpool_mark_task((struct tpool_t *)hp,
						  	 (struct task_t *)ptsk,
						   	lflags);
	else
		return stpool_mark_task_ex(hp, mark_walk, (void *)lflags);
}

int  
stpool_mark_task_ex(HPOOL hp, 
				long (*tskstat_walk)(struct stpool_tskstat_t *, void *),
				void *arg) {
	return tpool_mark_task_ex((struct tpool_t *)hp, 
							(long (*)(struct tpool_tskstat_t *, void *))tskstat_walk,
							arg);
}

void 
stpool_throttle_enable(HPOOL hp, int enable) {
	tpool_throttle_enable((struct tpool_t *)hp, enable);
}

int  
stpool_throttle_wait(HPOOL hp, long ms) {
	return tpool_throttle_wait((struct tpool_t *)hp, ms);
}

void 
stpool_suspend(HPOOL hp, int wait) {
	tpool_suspend((struct tpool_t *)hp, wait);
}

void 
stpool_resume(HPOOL hp) {
	tpool_resume((struct tpool_t *)hp);
}

int  
stpool_add_task(HPOOL hp, struct sttask_t *tsk) {
	return tpool_add_task((struct tpool_t *)hp,
	                      (struct task_t *)tsk);
}

int 
stpool_add_routine(HPOOL hp, 
		const char *name, int (*run)(struct sttask_t *), 
		void (*complete)(struct sttask_t *, long, int),
		void *arg, struct schattr_t *attr) {	
	return tpool_add_routine((struct tpool_t *)hp,
			name, (int (*)(struct task_t*))run,
			(void (*)(struct task_t *, long, int))complete,
			arg,  (struct xschattr_t *)attr);
}

int  
stpool_remove_pending_task(HPOOL hp, struct sttask_t *ptsk, int dispatched_by_pool) {
	int ele = 0;

	if (ptsk) {
		long lflags = dispatched_by_pool ? TASK_VMARK_REMOVE_BYPOOL
			: TASK_VMARK_REMOVE_DIRECTLY;
			
		lflags = tpool_mark_task((struct tpool_t *)hp, (struct task_t *)ptsk, lflags);	
		if (lflags & TASK_VMARK_REMOVE)
			++ ele;
	} else 
		ele = tpool_remove_pending_task((struct tpool_t *)hp, dispatched_by_pool);

	return ele;
}

void 
stpool_detach_task(HPOOL hp, struct sttask_t *tsk) {
	tpool_detach_task((struct tpool_t *)hp,
	                  (struct task_t *)tsk);
}

long 
stpool_wkid() {
	return tpool_wkid();
}

int  
stpool_task_wait(HPOOL hp, struct sttask_t *ptsk, long ms) {
	return stpool_task_any_wait(hp, ptsk ? &ptsk : NULL, ptsk ? 1 : 0, NULL, ms);
}

int  
stpool_task_any_wait(HPOOL hp, struct sttask_t *entry[], int n, int *npre, long ms) {
	return tpool_task_any_wait((struct tpool_t *)hp,
	                  (struct task_t **)entry,
					  n, npre,
					  ms);
}

int  
stpool_task_waitex(HPOOL hp, int (*sttask_match)(struct stpool_tskstat_t *, void *), void *arg, long ms) {
	return tpool_task_waitex((struct tpool_t *)hp,
	                  (int (*)(struct tpool_tskstat_t *, void *))sttask_match,
					  arg,
					  ms);
}

int  
stpool_pending_leq_wait(HPOOL hp, int n_max_pendings, long ms) {
	return tpool_pending_leq_wait((struct tpool_t *)hp, n_max_pendings, ms);
}


void 
stpool_wakeup(HPOOL hp, long wkid) {
	tpool_wakeup((struct tpool_t *)hp, wkid);
}
