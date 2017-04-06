#ifndef __SM_CACHE_H__
#define __SM_CACHE_H__

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
#include <assert.h>
#include "ospx.h"
#include "ospx_compatible.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FUNC_ALWAYS_NEED_DESTROY ((int (*)(void *, void *))-1)

struct smlink 
{
	struct smlink *next;
};

typedef struct smlink_q {
	int n;
	struct smlink *qfirst, *qlast;
} smlink_q_t;

#define SMLINK_Q_HEAD(name) \
	smlink_q_t name = {0, NULL, NULL}

static inline void INIT_SMLINK_Q(smlink_q_t *q) {
	q->n = 0;
	q->qfirst = q->qlast = 0;
}

/**
 * Interfaces (@smlink_q_x) are just for user to operate the objects sets
 */
#define smlink_q_empty(q) !(q)->qfirst
#define smlink_q_size(q)  (q)->n
#define smlink_q_first(q) (q)->qfirst
#define smlink_q_last(q)  (q)->qlast
#define smlink_q_next(obj) *(void **)(obj)

static inline void smlink_q_push(smlink_q_t *q, void *obj) {
	*(void **)obj = q->qfirst;
	q->qfirst = (struct smlink *)obj;
	if (!q->n)
		q->qlast = (struct smlink *)obj;
	++ q->n;
}

static inline void *smlink_q_pop(smlink_q_t *q) {
	void *obj = NULL;

	if (q->n) {
		obj = q->qfirst;
		q->qfirst = q->qfirst->next;
		if (!-- q->n)
			q->qlast = NULL;
	}
	return obj;
}

#define smlink_q_foreach(q, obj) \
	for (obj=q->qfirst; obj; obj=*(void **)obj)

#define smlink_q_foreach_safe(q, n, obj) \
	for (obj=(q)->qfirst, n=obj ? *(void **)obj : NULL; \
		obj; obj=n, n=obj ? *(void **)obj : NULL)


/* ------------------------------a simple object cache---------------------*/
typedef struct smcache {
	const char *desc;
	smlink_q_t xq;
	int  nlimit_cache;
	long lflags;
	void *opaque;
	void *(*creater)(void *opaque);
	void (*destroy)(void *obj, void *opaque);
	int  (*need_destroy)(void *obj, void *opaque);
	OSPX_pthread_mutex_t *lock, stack_lock;
} smcache_t; 

#define CACHE_F_LOCK_CREATER 0x1
int smcache_init2(smcache_t *smc, const char *name, int nlimit_cache, OSPX_pthread_mutex_t *external_lock, 
	long lflags, void *opaque, void *(*creater)(void *opaque), void (*destroy)(void *obj, void *opaque),
	int (*need_destroy)(void *obj, void *opaque));

static inline int smcache_init(smcache_t *smc, int nlimit_cache, OSPX_pthread_mutex_t *external_lock, 
	long lflags, void *opaque, void *(*creater)(void *opaque), void (*destroy)(void *obj, void *opaque),
	int (*need_destroy)(void *obj, void *opaque)) {
	return smcache_init2(smc, "dummy", nlimit_cache, external_lock,
						lflags, opaque, creater, destroy, need_destroy);
}


#define smcache_name(smc)   (smc)->desc
#define smcache_lock(smc)   OSPX_pthread_mutex_lock((smc)->lock) 
#define smcache_unlock(smc) OSPX_pthread_mutex_unlock((smc)->lock)
#define smcache_limited_cache(smc) (smc)->nlimit_cache
#define smcache_nl(smc) (smc)->xq.n
#define smcache_auto_lock(smc) ((smc)->lock == &(smc)->stack_lock)

static inline int smcache_n(smcache_t *smc) {
	int n;
	
	smcache_lock(smc);
	n = smcache_nl(smc);
	smcache_unlock(smc);

	return n;
}

static inline int smcache_need_destroy(smcache_t *smc, void *obj) {
	return FUNC_ALWAYS_NEED_DESTROY == smc->need_destroy || (smc->need_destroy &&
			(*smc->need_destroy)(obj, smc->opaque));
}

static inline int smcache_need_destroy2(smcache_t *smc) {
	return smc->xq.n > 0 && smcache_need_destroy(smc, smc->xq.qfirst);
}

/**
 * The object who needn't to be destroyed is always allowed to
 * be add into the cache 
 */
static inline int smcache_accessl(smcache_t *smc, void *obj, int ncached_limited) {
	return  ncached_limited < 0 || !smcache_need_destroy(smc, obj) ||
				ncached_limited > smcache_nl(smc);
}


static inline int smcache_flushablel(smcache_t *smc, int ncached_limit) {
	return ncached_limit >= 0 && ncached_limit < smcache_nl(smc) && 
				smcache_need_destroy(smc, smc->xq.qfirst);
}

/**
 * Remove all of objects that does not need to be destroyed 
 */
static inline int smcache_remove_unflushable_objectsl(smcache_t *smc) {
	if (smcache_nl(smc) > 0) {
		while (smc->xq.n > 0 && !smcache_need_destroy(smc, smc->xq.qfirst)) {
			smc->xq.qfirst = smc->xq.qfirst->next;
			-- smc->xq.n;
		}

		if (smc->xq.n <= 0)
			smc->xq.qlast = smc->xq.qfirst;
	}

	return smcache_nl(smc); 
}

static inline int smcache_addl(smcache_t *smc, void *obj) {
	/**
	 * If the object is need to be destroyed, we push it 
	 * into the front of the cache 
	 */
	if (smcache_need_destroy(smc, obj) && smc->xq.n) {
		*(void **)obj = smc->xq.qfirst; 
		smc->xq.qfirst = (struct smlink *)obj; 
	} else { 
		if (smc->xq.n) 
			smc->xq.qlast->next = (struct smlink *)obj; 
		else 
			smc->xq.qfirst = (struct smlink *)obj; 
		smc->xq.qlast = (struct smlink *)obj; 
		smc->xq.qlast->next = NULL; 
	} 
	return ++ smc->xq.n; 
} 

/**
 * Note: It is not recommented to call this interface except that you 
 * really kown what you are doing.
 */
static inline void smcache_addl_dir(smcache_t *smc, void *obj) {
	if (smc->xq.n) {
		*(void **)obj = smc->xq.qfirst; 
		smc->xq.qfirst = (struct smlink *)obj; 
	
	} else { 
		if (smc->xq.n) 
			smc->xq.qlast->next = (struct smlink *)obj; 
		else 
			smc->xq.qfirst = (struct smlink *)obj; 
		smc->xq.qlast = (struct smlink *)obj; 
		smc->xq.qlast->next = NULL; 
	} 
	++ smc->xq.n; 
}


static inline int smcache_add(smcache_t *smc, void *obj) {
	int n;
	
	smcache_lock(smc);
	n = smcache_addl(smc, obj);
	smcache_unlock(smc);

	return n;
}

/**
 * Note: It is not recommented to call this interface except that you 
 * really kown what you are doing.
 */
static inline void smcache_add_dir(smcache_t *smc, void *obj) {
	smcache_lock(smc);
	smcache_addl_dir(smc, obj);
	smcache_unlock(smc);
}

int smcache_add_limit(smcache_t *smc, void *obj, int ncached_limit);

static inline int smcache_add_ql(smcache_t *smc, smlink_q_t *q) {
	assert (q && q->n > 0);

	if (!smc->need_destroy) {
		smc->xq.n += q->n; 
		q->qlast->next = smc->xq.qfirst; 
		smc->xq.qfirst = q->qfirst; 
		if (!smc->xq.qlast) 
			smc->xq.qlast = q->qlast;
	
	} else {
		struct smlink *c, *n; 

		for (c=q->qfirst; c; c=n) {
			n = c->next;
			smcache_addl(smc, c);
		}
	}

	return smc->xq.n;
}

static inline void smcache_add_ql_dir(smcache_t *smc, smlink_q_t *q) {
	smc->xq.n += q->n; 
	q->qlast->next = smc->xq.qfirst; 
	smc->xq.qfirst = q->qfirst; 
	if (!smc->xq.qlast) 
		smc->xq.qlast = q->qlast;
}

static inline int smcache_add_q(smcache_t *smc, smlink_q_t *q) {
	int n;
	
	smcache_lock(smc);
	n = smcache_add_ql(smc, q);
	smcache_unlock(smc);

	INIT_SMLINK_Q(q);
	return n;
}

static inline void smcache_add_q_dir(smcache_t *smc, smlink_q_t *q) {
	smcache_lock(smc);
	smcache_add_ql_dir(smc, q);
	smcache_unlock(smc);

	INIT_SMLINK_Q(q);
}

int smcache_add_q_limit(smcache_t *smc, smlink_q_t *q, int ncached_limit);

static inline void *smcache_getl(smcache_t *smc, int create) {
	void *obj = smc->xq.qfirst;	

	if (obj) {
		smc->xq.qfirst = (struct smlink *)*(void **)obj;
		if (!-- smc->xq.n)
			smc->xq.qlast = NULL;
	
	} else if (create && smc->creater) 
		obj = (*smc->creater)(smc->opaque);
	
	return obj;
}

static inline void *smcache_get(smcache_t *smc, int create) {
	void *obj;
	int   create0 = create && CACHE_F_LOCK_CREATER & smc->lflags;	

	smcache_lock(smc); 
	obj = smcache_getl(smc, create0);
	smcache_unlock(smc);
	
	if (!obj && create && smc->creater &&
		!(CACHE_F_LOCK_CREATER & smc->lflags)) 
		return (*smc->creater)(smc->opaque);	

	return obj;
}

int smcache_get_flush_ql(smcache_t *smc, int ncached_limit, smlink_q_t *q);

int smcache_flush(smcache_t *smc, int ncached_limit); 

static inline void smcache_destroy(smcache_t *smc, void *obj) {
	if (obj && smcache_need_destroy(smc, obj)) {
		/**
		 * Should we hold the lock while we are
		 * destroying the task objects ?
		 */
		if (CACHE_F_LOCK_CREATER & smc->lflags) {
			smcache_lock(smc);
			(*smc->destroy)(obj, smc->opaque);
			smcache_unlock(smc);
		
		} else
			(*smc->destroy)(obj, smc->opaque);
	}
}

static inline void smcache_destroy_q(smcache_t *smc, smlink_q_t *q) {
	struct smlink *c, *n;
	
	if (!smc->destroy || !q  || q->n <= 0)
		return;
	
	if (CACHE_F_LOCK_CREATER & smc->lflags) 
		smcache_lock(smc);
	
	for (c=q->qfirst; c; c=n) {
		n = c->next;
		
		if (smcache_need_destroy(smc, c))
			(*smc->destroy)(c, smc->opaque);
	}
	
	if (CACHE_F_LOCK_CREATER & smc->lflags) 
		smcache_unlock(smc);
}

/**
 * Reserve n objects
 */
void smcache_reserve(smcache_t *smc, int n); 

/**
 * Reset the cache
 */
void smcache_reset(smcache_t *smc, smlink_q_t *oq);

/**
 * Destroy the cache
 */
void smcache_deinit(smcache_t *smc);

#ifdef __cplusplus
}
#endif
#endif

