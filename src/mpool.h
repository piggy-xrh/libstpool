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
