#ifndef __MPOOL_H__
#define __MPOOL_H__

#include <stdlib.h>

typedef void (*mbuffer_free)(void *buffer);

struct mpool_stat_t {
	size_t mem_hold_all;
	size_t objs_size;
	size_t nobjs_resved;
	size_t nobjs_allocated;
	size_t nobjs_acquired;
	size_t nblks;
};

struct mpool_blkstat_t {
	void   *base;
	size_t length;
	size_t nobjs_resved;
	size_t nobjs_allocated;
};

struct mpool_t {
	size_t objlen;	
	size_t align;
	void *init_data;
};

int   mpool_init(struct mpool_t *mp, size_t objlen);
int   mpool_add_buffer(struct mpool_t *mp, void *buffer, size_t size, mbuffer_free mfree);
void *mpool_new(struct mpool_t *mp);
void  mpool_delete(struct mpool_t *mp, void *ptr);
struct mpool_stat_t *mpool_stat(struct mpool_t *mp, struct mpool_stat_t *stat);
const char *mpool_stat_print(struct mpool_t *mp, char *buffer, size_t len);
int   mpool_blkstat_walk(struct mpool_t *mp, int (*walkstat)(struct mpool_blkstat_t *stat, void *arg), void *arg);
void  mpool_destroy(struct mpool_t *mp, int force);

#ifndef NDEBUG
/* Verify whether the object ptr belongs to the mpool */
void  mpool_assert(struct mpool_t *mp, void *ptr);
#endif
#endif
