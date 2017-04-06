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
#include "ospx_errno.h"
#include "sm_cache.h"
#include "msglog.h"

#define M_CACHE "smcache"

int 
smcache_init2(smcache_t *smc, const char *name, int nlimit_cache, 
	OSPX_pthread_mutex_t *external_lock, 
	long lflags, void *opaque, void *(*creater)(void *opaque), 
	void (*destroy)(void *obj, void *opaque),
	int (*need_destroy)(void *obj, void *opaque))
{
	smc->desc = name;
	smc->xq.n = 0;
	smc->xq.qfirst = smc->xq.qlast = NULL;
	smc->nlimit_cache = nlimit_cache ? nlimit_cache : -1;
	smc->lock = external_lock;
	smc->lflags = lflags;
	smc->opaque  = opaque;
	smc->creater = creater;
	smc->destroy = destroy;
	smc->need_destroy = need_destroy;
	
	MSG_log(M_CACHE, LOG_INFO,
		"Initializing cache (\"%s\"/%p) ...\n",
		smc->desc, smc);

	assert (!smc->need_destroy || smc->destroy);
	if (!smc->lock) {
		if ((errno=OSPX_pthread_mutex_init(&smc->stack_lock, 0))) {
			MSG_log(M_CACHE, LOG_ERR,
				"mutex_init:%s\n", OSPX_sys_strerror(errno));
			return -1;
		}
		smc->lock = &smc->stack_lock;
	}
	
	return 0;
}

void 
smcache_reserve(smcache_t *smc, int n) 
{
	if (smc->creater) {
		void *obj = (void *)0x1;
	
		n -= smcache_n(smc);
		while (-- n > 0 && obj) {
			if (CACHE_F_LOCK_CREATER & smc->lflags) {
				smcache_lock(smc);
				if ((obj = (*smc->creater)(smc->opaque)) &&
					n <= smcache_addl(smc, obj))
					n = 0;
				smcache_unlock(smc);
			
			} else if ((obj = (*smc->creater)(smc->opaque)) &&
				n <= smcache_add(smc, obj))
				n = 0;
		}
	}
}

void 
smcache_reset(smcache_t *smc, smlink_q_t *oq) 
{
	int nflushed = 0;
	smlink_q_t rmq = {0, NULL, NULL};

	MSG_log(M_CACHE, LOG_INFO,
		"Reseting cache (\"%s\"/%p) ...\n",
		smc->desc, smc);

	smcache_lock(smc);
	/* Flush all of the objects who can be destroyed */
	if (smcache_flushablel(smc, 0))
		nflushed = smcache_get_flush_ql(smc, 0, &rmq);
	
	/* Dump the queue */
	if (oq) {
		*oq = smc->xq;
		
		/* Reset the cache */
		smc->xq.n = 0;
		smc->xq.qfirst = smc->xq.qlast = NULL;
	}
	smcache_unlock(smc);

	/* Destroy the objects */
	if (nflushed)
		smcache_destroy_q(smc, &rmq);
}

int 
smcache_get_flush_ql(smcache_t *smc, int ncached_limit, smlink_q_t *q) 
{
	int nflushed;
	struct smlink *p, *n;
	
	assert (ncached_limit >= 0 && q);
	assert (smcache_flushablel(smc, ncached_limit));
	
	if (!ncached_limit && smcache_need_destroy(smc, smc->xq.qlast)) {
		*q = smc->xq;
		smc->xq.qfirst = smc->xq.qlast = NULL;
		smc->xq.n = 0;
		return q->n;
	}
	p = smc->xq.qfirst;
	nflushed = smcache_nl(smc) - ncached_limit;
	for (q->n=0;p && nflushed > 0; p=n, --nflushed) {
		n = p->next;
	
		if (!smcache_need_destroy(smc, p))
			break;
		
		if (!q->qfirst)
			q->qfirst = p;
		q->qlast = p;
		++ q->n;
	}

	if (q->n) {
		q->qlast->next  = NULL;
		smc->xq.qfirst = p;
		smc->xq.n -= q->n;
	}
	return q->n;
}	

int 
smcache_flush(smcache_t *smc, int ncached_limit)
{
	if (!ncached_limit)
		MSG_log(M_CACHE, LOG_INFO,
		 	"Flushing cache (\"%s\"/%p) ...\n",
			smc->desc, smc);
	
	return smcache_add_limit(smc, NULL, ncached_limit);
}

int 
smcache_add_limit(smcache_t *smc, void *obj, int ncached_limit) 
{
	int nflushed = 0, enjected = 0;
	smlink_q_t rmq = {0, NULL, NULL};
	
	if (-1 == ncached_limit)
		ncached_limit = smcache_limited_cache(smc);
	
	else if (ncached_limit < 0)
		ncached_limit = 1;

	smcache_lock(smc); 
	if (obj) {
		if (!smcache_accessl(smc, obj, ncached_limit))
			enjected = 1;
		else
			smcache_addl(smc, obj);
	}
	if (smcache_flushablel(smc, ncached_limit)) 
		smcache_get_flush_ql(smc, ncached_limit, &rmq); 
	smcache_unlock(smc); 	
	
	if (enjected) {
		*(void **)obj = NULL;
		
		if (nflushed) 
			rmq.qlast->next = obj;
		else
			rmq.qfirst = obj;
		rmq.qlast = obj;
		++ rmq.n;
		++ nflushed;
	}

	if (rmq.n) {
		nflushed = rmq.n;
		smcache_destroy_q(smc, &rmq);
	}

	return nflushed;
} 

int 
smcache_add_q_limit(smcache_t *smc, smlink_q_t *q, int ncached_limit) 
{
	int nflushed = 0;
	smlink_q_t rmq = {0, NULL, NULL};
	
	if (-1 == ncached_limit)
		ncached_limit = smcache_limited_cache(smc);
	
	assert (!q || q->n >= 0);

	smcache_lock(smc); 
	smcache_add_ql(smc, q);

	if (smcache_flushablel(smc, ncached_limit))
		smcache_get_flush_ql(smc, ncached_limit, &rmq);
	smcache_unlock(smc); 
	
	if (rmq.n) {
		nflushed = rmq.n;
		smcache_destroy_q(smc, &rmq);
	}

	return nflushed;
} 

void 
smcache_deinit(smcache_t *smc)
{
	SMLINK_Q_HEAD(q);
	
	MSG_log(M_CACHE, LOG_INFO,
		"Destroying cache (\"%s\"/%p) ...\n",
		smc->desc, smc);

	/* NOTE:
	 *   We do not hold the lock */
	if (smcache_flushablel(smc, 0)) {
		smcache_get_flush_ql(smc, 0, &q);
		smcache_destroy_q(smc, &q);
	}

	if (smc->lock != &smc->stack_lock)
		OSPX_pthread_mutex_destroy(smc->lock);
}

