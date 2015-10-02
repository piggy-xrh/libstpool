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

#include "ospx_errno.h"
#include "objpool.h"
#include "msglog.h"

#define M_FOBJP "FObjpool"

/* The lock must have been held */
static void *
objpool_get(void *opaque) 
{
	int i = 0, n_inc = 0;
	objpool_t *p = opaque;
	smlink_q_t *putq;
	void *m0 = NULL, *m1;
	
	/* Scan the free queue of the blocks */
	if (p->nput) {
		assert (p->ntotal > p->nput);
		for (i=0; i<p->iblocks && p->nput && n_inc < 5; i++) {
			putq = &p->blocks[i].putq;
			
			/* If there are free objects existing in the 
			 * blocks, we add some few of them into the 
			 * cache again */
			if (!smlink_q_empty(putq)) {
				i = smlink_q_size(putq);
	
				assert (i < p->block_nobjs);
				if (!m0) 
					m0 = smlink_q_pop(putq);

				if (i > 1) {
					-- i;
					p->nput -= i;
					n_inc += i;
					smcache_add_ql(&p->smc, putq);
					INIT_SMLINK_Q(putq);
				}	
			}
		}
		return m0;
	}

	/* If we can not get any task objects from the 
	 * free queue, we try to create a new block to 
	 * get more task objects */
	if (!(m0 = calloc(1, p->block_size))) {
		MSG_log2(M_FOBJP, LOG_ERR,
				"obj_create2: no memory.");
		return NULL;
	}
		
	/* Allocate a memory to store the block */
	if (p->ialloc == p->iblocks) {
		n_inc = 3;

		if (p->iblocks)
			m1 = realloc(p->blocks, sizeof(obj_block_t) *(p->iblocks + n_inc));
		else
			m1 = malloc(sizeof(obj_block_t) * n_inc);

		if (!m1) {
			MSG_log2(M_FOBJP, LOG_ERR,
				"obj_create2: no memory.");
			free(m0);
			return NULL;
		}
		p->ialloc += n_inc;
		p->blocks = m1;
	}
		
	/* Sort the block according to its objects address */
	if (p->iblocks) {
		int l = 0, r = p->iblocks -1;
		
		for (;r>l;) {	
			if ((char *)m0 < p->blocks[(l+r)/2].begin) 
				r = (l+r)/2 - 1;	
			else 
				l = (l+r)/2 + 1;	
		}
		assert (l >= 0 && r <= p->iblocks - 1);
		
		if ((char *)m0 > p->blocks[l].begin) 
			l += 1;	
	
		if (l <= p->iblocks - 1)
			memmove(p->blocks + l + 1, p->blocks + l, 
				(p->iblocks - l) * sizeof(obj_block_t));	
		i = l;
	}
	
	/* Initialize the block */
	{
		obj_block_t *ob = &p->blocks[i];
		
		ob->begin = m0;
		ob->end = (char *)m0 + p->block_nobjs * p->objlen;
		putq = &ob->putq;
		INIT_SMLINK_Q(putq);
				
		assert (i==0 || p->blocks[i].begin >= p->blocks[i-1].begin);
		/* Add the objects into the cache */
		if (p->block_nobjs > 1) {
			for (i=1; i<p->block_nobjs; i++) 
				smlink_q_push(putq, 
							 ob->begin + i * p->objlen
							 );	
			smcache_add_ql(&p->smc, putq);
			INIT_SMLINK_Q(putq);		
		}
	}
	assert (p->blocks[p->iblocks].begin != 0 &&
	        p->blocks[p->iblocks].end > p->blocks[p->iblocks].begin);
	++ p->iblocks;
	p->ntotal += p->block_nobjs;
	
	return m0;
}

/* The lock must have been held */
static void
objpool_put(void *obj, void *opaque) 
{
	objpool_t *p = opaque;
	obj_block_t *ob;
	int l = 0, r = p->iblocks - 1;

	/* Normaly this interface will not be called except
	 * that the user is destroying the object pool */
	++ p->nput;
	assert (p->nput <= p->ntotal && p->iblocks > 0);
	
	/* Scan the object blocks */	
	for (;r>l;) {	
		ob = &p->blocks[(l+r)/2];

		if ((char *)obj < ob->begin) 
			r = (l+r) / 2 - 1;	
		else  if ((char *)obj < ob->end)
			break;
		else
			l = (l+r) / 2 + 1;	
	}
	ob = &p->blocks[(l+r)/2];
	assert ((char *)obj >= ob->begin &&
	        (char *)obj <  ob->end);

	assert (smlink_q_size(&ob->putq) < p->block_nobjs);
	
	/* If all of the objects has been put into the pool,
	 * we free the blocks */	
	if (smlink_q_size(&ob->putq) == p->block_nobjs - 1) {
		char *m = ob->begin;
		
		l = (r+l)/2;
		if (l != p->iblocks -1)
			memmove(p->blocks + l, p->blocks + l + 1, 
				(p->iblocks -l - 1) * sizeof(obj_block_t));
		-- p->iblocks;
		p->ntotal -= p->block_nobjs;
		p->nput -= p->block_nobjs;     
		free(m);

	} else
		smlink_q_push(&ob->putq, obj);
}


int  
objpool_ctor2(objpool_t *p, const char *name, int objlen, int nreserved, int nlimit_cache, OSPX_pthread_mutex_t *cache_lock)
{
	/* A block must can store at least 20 objects */
	int n = 20, page_size = 4096, dummy = 50;
	
	if (objlen >= 256) {
		n = 8;
		page_size = 8096;
	}
	
	p->objlen = objlen;
	p->blocks = NULL;
	p->iblocks = p->ialloc = 0;
	p->block_size = (n + sizeof(obj_block_t) + dummy +
		page_size - 1) / page_size * page_size;
	p->block_nobjs = (p->block_size - sizeof(obj_block_t)) / objlen;
	p->ntotal = p->nput = 0;
	
	MSG_log(M_FOBJP, LOG_INFO,
			"Initializing fast objpool(\"%s\"/%p) ...\n", 
			name, p);

	/* Initialize the cache object */
	if (smcache_init2(&p->smc, name, !nlimit_cache ? p->block_nobjs : nlimit_cache, 
			cache_lock, CACHE_F_LOCK_CREATER, p, objpool_get, 
			objpool_put, FUNC_ALWAYS_NEED_DESTROY)) {
		MSG_log2(M_FOBJP, LOG_ERR,
			"cache_init error");
		return -1;
	}
	
	/* Reserve some objects for the app if it has been 
	 * requested by user */
	if (nreserved > 0)
		smcache_reserve(&p->smc, nreserved);

	return 0;
}

void
objpool_dtor(objpool_t *p) 
{
	MSG_log(M_FOBJP, LOG_INFO,
			"Destroying fast objpool(\"%s\"/%p) ...\n", 
			objpool_name(p), p);

	smcache_deinit(&p->smc);
	assert (p->iblocks == 0);
	if (p->blocks)
		free(p->blocks);
}


