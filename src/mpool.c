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
#include "ospx.h"
#include "list.h"
#include "mpool.h"

struct mpool_blk_t {
	struct  hlist_node link;
	struct  hlist_node base_link;
	uint8_t  slot;
	uint16_t u16_num;
	void    *base;
	void    *end;
	size_t  length;
	uint8_t *bitmap;
	size_t   num;
	size_t   left;
	size_t   freeslot;
	mbuffer_free mfree;
};

struct mpool_init_data_t {
	struct hlist_head mq;
	struct hlist_head *mbase;
	size_t *mqnarray;
	size_t mbnum;
	size_t nacquires;
	uint64_t left;
	OSPX_pthread_mutex_t lock;
};

struct mpool_obj_ptr_t {
	uint8_t f_slot:7;
	uint8_t f_resv:1;
};

#define PAGE_SIZE (1024 * 4)
#define MPOOL_data(mp) ((struct mpool_init_data_t *)mp->init_data)

#define ACQUIRE_LOCK(mp) OSPX_pthread_mutex_lock(&MPOOL_data(mp)->lock)
#define RELEASE_LOCK(mp) OSPX_pthread_mutex_unlock(&MPOOL_data(mp)->lock)
int   
mpool_init(struct mpool_t *mp, size_t objlen) {
	size_t i;
	struct mpool_init_data_t *initd;

	assert(objlen > 0);
	mp->align  = 4 - objlen % 4;
	mp->objlen = mp->align + objlen;
	initd = (struct mpool_init_data_t *)calloc(1, sizeof(*initd) + 
		6 * (sizeof(struct hlist_head) + sizeof(size_t)));
	if (!initd) {
		fprintf(stderr, "@%s error:Out of memeory.\n",
			__FUNCTION__);
		return -1;
	}
	if ((errno = OSPX_pthread_mutex_init(&initd->lock, 0))) {
		fprintf(stderr, "@%s error: %d.\n",
			__FUNCTION__, errno);
		free(initd);
		return -1;
	}
	initd->left = 0;
	initd->nacquires = 0;
	initd->mbnum = 6;
	initd->mbase = (struct hlist_head *)(initd + 1);
	initd->mqnarray = (size_t *)(initd->mbase + initd->mbnum);
	INIT_HLIST_HEAD(&initd->mq);
	for (i=0; i<initd->mbnum; i++) 
		INIT_HLIST_HEAD(&initd->mbase[i]);
	mp->init_data = (void *)initd;
	return 0;
}

static struct mpool_blk_t *
mpool_new_blk(struct mpool_t *mp, void *buffer, size_t size, mbuffer_free mfree) {
	size_t nobjs, mblen, i, elements;
	struct hlist_node *pos, *last;
	struct hlist_head *hlst;
	struct mpool_blk_t *nblk, *iter;
	struct mpool_obj_ptr_t *optr;
	struct mpool_init_data_t *initd = MPOOL_data(mp);
	
	if ((mfree && !buffer) || (size < mp->objlen))
		return NULL;

	nobjs = size / mp->objlen;
	mblen = sizeof(struct mpool_blk_t) + (nobjs + 7) / 8;

	nblk = (struct mpool_blk_t *)calloc(1, mblen);
	if (!nblk) {
		fprintf(stderr, "@%s error:Out of memeory.\n",
			__FUNCTION__);
		return NULL;
	}
	
	if (!buffer) {
		/* Reset the size */
		size   = nobjs * mp->objlen;
#ifndef NDEBUG	
		buffer = calloc(1, size);
#else
		buffer = malloc(size);
#endif
		if (!buffer) {
			fprintf(stderr, "@%s error:Out of memeory.\n",
				__FUNCTION__);
			free(nblk);
			return NULL;
		}
		mfree = free;
	}
	nblk->base   = buffer;
	nblk->end    = (uint8_t *)buffer + (nobjs - 1) * mp->objlen;
	nblk->length = size;
	nblk->mfree  = mfree;

	/* Construct the bitmap */
	nblk->u16_num = nobjs / 16;
	nblk->num  = nobjs;
	nblk->left = nobjs;
	nblk->freeslot = 1;
	nblk->bitmap = (uint8_t *)(nblk + 1);
	
	ACQUIRE_LOCK(mp);
	elements = initd->mqnarray[0];
	nblk->slot = 0;
	for (i=1; i<initd->mbnum; i++){
		if (elements > initd->mqnarray[i]) {
			elements = initd->mqnarray[i];
			nblk->slot = i;
		}
	}
	for (i=0; i<nobjs; i++) {
		optr = (struct mpool_obj_ptr_t *)((uint8_t *)nblk->base + i * mp->objlen);
		optr->f_slot = nblk->slot;
		optr->f_resv = 0;
	}
	hlst = &initd->mbase[nblk->slot];
	
	/* Insert the block into the slot */
	if (hlist_empty(hlst))
		hlist_add_head(&nblk->base_link, hlst);
	else {	
		hlist_for_each_entry(iter, pos, hlst, struct mpool_blk_t, base_link) {
			if (nblk->end > iter->end) {
				hlist_add_before(&nblk->base_link, &iter->base_link);
				break;
			}

			/* Record the last ptr */
			last = pos;
		}

		if (!pos) 
			hlist_add_after(last, &nblk->base_link);
	}	
	++ initd->mqnarray[nblk->slot];
	
	/* Put the block into the free buffer queue */
	hlist_add_head(&nblk->link, &initd->mq);
	initd->left += nblk->left;

	return nblk;
}

int
mpool_add_buffer(struct mpool_t *mp, void *buffer, size_t size, mbuffer_free mfree) {
	struct mpool_blk_t *nblk;

	nblk = mpool_new_blk(mp, buffer, size, mfree);	
	if (!nblk) {
		if (mfree)
			mfree(buffer);
		return -1;
	}
	RELEASE_LOCK(mp);

	return 0;
}

#ifndef NDEBUG
#define PASSERT(expr) \
	do {\
		if (!(expr)) {\
			fprintf(stderr, "assert error:%s:%d:assert_num:%d\n",\
				func, line, ++ assert_num); \
			assert(0); \
		} \
	} while(0)

static void 
assert_ptr(struct mpool_t *mp, struct mpool_obj_ptr_t *ptr, int allocated, const char *func, int line) {
	int assert_num = 0;
	struct hlist_node *pos;
	struct mpool_blk_t *blk;
	struct mpool_init_data_t *initd = MPOOL_data(mp);
	
	PASSERT((!ptr->f_resv) && (ptr->f_slot < initd->mbnum));
	
	hlist_for_each_entry(blk, pos, &initd->mbase[ptr->f_slot], 
			struct mpool_blk_t, base_link) {
		if (((size_t)blk->base <= (size_t)ptr) &&
			((size_t)blk->end  >= (size_t)ptr)) {
			size_t offset = (size_t)ptr - (size_t)blk->base;
				
			PASSERT(offset % mp->objlen == 0);
			if (allocated) {
				PASSERT(OSPX_bitget(blk->bitmap, (offset / mp->objlen + 1)));
				PASSERT((blk->left >= 0) && (blk->left < blk->num));	
			} else {
				PASSERT(!OSPX_bitget(blk->bitmap, (offset / mp->objlen + 1)));
				PASSERT((blk->left > 0) && (blk->left <= blk->num));
			}
			break;
		}
	}
}

void 
mpool_assert(struct mpool_t *mp, void *ptr) {
	ACQUIRE_LOCK(mp);
	assert_ptr(mp, (struct mpool_obj_ptr_t *)((uint8_t *)ptr - mp->align), 1, __FUNCTION__, __LINE__);
	RELEASE_LOCK(mp);
}
#else
#define assert_ptr(mp, initd, ptr, func, line)
#endif

void *
mpool_new(struct mpool_t *mp) {
	struct mpool_obj_ptr_t *ptr = NULL;
	struct mpool_blk_t *blk = NULL;
	struct mpool_init_data_t *initd = MPOOL_data(mp);

	ACQUIRE_LOCK(mp);
	if (!hlist_empty(&initd->mq)) 
		blk = hlist_entry(initd->mq.first, struct mpool_blk_t, link);
	
	++ initd->nacquires;
	
	/* We try to create a new blok if there are none
	 * enough spaces.
	 */
	if (!blk) {
		RELEASE_LOCK(mp);
		blk = mpool_new_blk(mp, NULL,
				(mp->objlen * 15  + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE, NULL);
		if (!blk)
			return NULL;
	}
	/* Get a obj from the block */
	assert(blk->left > 0 && (blk->freeslot >= 1 && blk->freeslot <= blk->num));
	assert(!OSPX_bitget(blk->bitmap, blk->freeslot));

	ptr = (struct mpool_obj_ptr_t *)((uint8_t *)blk->base + (blk->freeslot-1) * mp->objlen);
	
	/* Uncomment the code below if you want to check the ptr, we comment it since
	 * it'll waste our so much time to load a large mount of tasks into the pool.
	 */
	//assert_ptr(mp, ptr, 0, __FUNCTION__, __LINE__);
	
	OSPX_bitset(blk->bitmap, blk->freeslot);
	
	if (!-- blk->left) 
		hlist_del(&blk->link);
	else {
		++ blk->freeslot;
		
		if ((blk->freeslot > blk->num) || OSPX_bitget(blk->bitmap, blk->freeslot)) { 
			int index = 0, nth_base = -1, num;
			uint8_t  *u8;
			uint16_t *u16 = (uint16_t *)blk->bitmap;
			
			for (;index<blk->u16_num; ++ index) {
				if ((uint16_t)-1 != u16[index]) {
					u8 = (uint8_t *)(u16 + index);
					nth_base = 16 * index;
					index = 16;
					break;
				}
			}
			if (-1 == nth_base) {
				u8 = (uint8_t *)(u16 + blk->u16_num);
				nth_base = 16 * blk->u16_num;
				index = blk->num % 16;
			}

			/* Get the free slot */
			for (num=1; num <= index; num++) {
				if (!OSPX_bitget(u8, num)) {
					blk->freeslot = nth_base + num;
					break;
				}
			}
			assert(num <= index);
		}
	}		
	-- initd->left;
	//assert_ptr(mp, ptr, 1, __FUNCTION__, __LINE__);
	RELEASE_LOCK(mp);
	
	return ((uint8_t *)ptr + mp->align);
}

void 
mpool_delete(struct mpool_t *mp, void *ptr) {	
	int release = 0;
	struct mpool_obj_ptr_t *optr = (struct mpool_obj_ptr_t *)((uint8_t *)ptr - mp->align);
	struct mpool_init_data_t *initd = MPOOL_data(mp);
	struct hlist_node *pos;
	struct mpool_blk_t *blk;
	
	if ((!ptr) || optr->f_resv) {
		assert(0);
		return;
	}	
	
	ACQUIRE_LOCK(mp);
	assert_ptr(mp, optr, 1, __FUNCTION__, __LINE__);
	hlist_for_each_entry(blk, pos, &initd->mbase[optr->f_slot],
			struct mpool_blk_t, base_link) {
		/* Verify the address again */
		if (((size_t)optr > (size_t)blk->end)) {
			assert(0);
			break;
		}

		if ((size_t)optr >= (size_t)blk->base) {
			/* Set the bitmap */
			blk->freeslot = ((size_t)optr - (size_t)blk->base) / mp->objlen + 1;
			OSPX_bitclr(blk->bitmap, blk->freeslot);
			
			++ blk->left;
			if (1 == blk->left) 
				hlist_add_head(&blk->link, &initd->mq);

			/* Should we release the block ? */
			else if ((blk->left == blk->num) && (initd->left >= (blk->left + 5))) {	
				hlist_del(&blk->link);
				hlist_del(&blk->base_link);
				release = 1;
				initd->left -= blk->left;
				-- initd->mqnarray[blk->slot];
			}
			++ initd->left;
			assert_ptr(mp, optr, 0, __FUNCTION__, __LINE__);
			break;
		}
	}
	RELEASE_LOCK(mp);
	
	if (release) {
		if (blk->mfree)
			blk->mfree(blk->base);
		free(blk);
	}
}

int
mpool_blkstat_walk(struct mpool_t *mp, int (*walkstat)(struct mpool_blkstat_t *, void *), void *arg) {
	size_t cnt = 0, index = 0;
	struct hlist_node *pos;
	struct mpool_blk_t *blk;
	struct mpool_blkstat_t st;
	struct mpool_init_data_t *initd = MPOOL_data(mp);

	ACQUIRE_LOCK(mp);
	for (;index<initd->mbnum; index++) {
		hlist_for_each_entry(blk, pos, &initd->mbase[index], 
				struct mpool_blk_t, base_link) {	
			st.base = blk->base;
			st.length = blk->length;
			st.nobjs_resved = blk->left;
			st.nobjs_allocated = blk->num - blk->left;
			
			++ cnt;
			if (walkstat(&st, arg))
				break;
		}
	}
	RELEASE_LOCK(mp);
	
	return cnt;
}

struct mpool_stat_t *
mpool_stat(struct mpool_t *mp, struct mpool_stat_t *stat) {
	size_t index = 0;
	static struct mpool_stat_t slstat;
	struct hlist_node *pos;
	struct mpool_blk_t *blk;
	struct mpool_init_data_t *initd = MPOOL_data(mp);

	if (!stat)
		stat = &slstat;
	
	stat->mem_hold_all = 0;
	stat->objs_size = mp->objlen;
	stat->nobjs_resved = 0;
	stat->nobjs_allocated = 0;
	stat->nblks = 0;

	ACQUIRE_LOCK(mp);
	for (;index<initd->mbnum; index++) {
		hlist_for_each_entry(blk, pos, &initd->mbase[index], 
				struct mpool_blk_t, base_link) {		
			stat->nobjs_resved += blk->left;
			stat->nobjs_allocated  += blk->num - blk->left;
			stat->mem_hold_all += blk->length;
			++ stat->nblks;
		}
	}
	stat->nobjs_acquired = initd->nacquires;
	RELEASE_LOCK(mp);
	
	return stat;
}

const char *
mpool_stat_print(struct mpool_t *mp, char *buffer, size_t len) {
	static char slbuffer[200];
	struct mpool_stat_t st;

	if (!buffer) {
		buffer = slbuffer;
		len    = sizeof(slbuffer);
	}
	mpool_stat(mp, &st);

#ifdef _WIN32
	#define snprintf _snprintf
#endif
	snprintf(buffer, len, 
			"mem_hold_all: %u bytes\n"
			"objs_size: %u bytes\n"
			"nobjs_resved: %u\n"
			"nobjs_allocated: %u\n"
			"nobjs_acquired: %u\n"
			"nblks: %u\n",
			st.mem_hold_all,
			st.objs_size,
			st.nobjs_resved,
			st.nobjs_allocated,
			st.nobjs_acquired,
			st.nblks);

	return buffer;
}

void
mpool_destroy(struct mpool_t *mp, int force) {
	int release = 0;
	struct hlist_node *pos, *n;
	struct mpool_blk_t *blk;
	struct mpool_stat_t st;
	struct mpool_init_data_t *initd = MPOOL_data(mp);

	assert(mp);	
	mpool_stat(mp, &st);
	if (st.nobjs_allocated) {
		fprintf(stderr, "MPOOL is busy now:\n%s\n",
			mpool_stat_print(mp, NULL, 0));
		release = force;
	} else
		release = 1;

	if (release) {
		size_t index;

		for (index=0;index<initd->mbnum; index++) {
			struct hlist_head *hlst = &initd->mbase[index];
			
			hlist_for_each_entry_safe(blk, pos, n, hlst,
					struct mpool_blk_t, base_link) {	
				if (blk->mfree) {
					blk->mfree(blk->base);
					free(blk);
				}
			}
		}
		OSPX_pthread_mutex_destroy(&initd->lock);
		free(initd);
	}
}

