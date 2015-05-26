#ifndef __CMALLOCATOR_H__
#define __CMALLOCATOR_H__

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

#include "mpool.h"
#include "CMPool.h"

class CMAllocator:public CAllocator
{
	public:
		CMAllocator(const char *desc, size_t bytes);
		~CMAllocator();

		virtual long addRef();
		virtual long release();

		virtual CAllocator *clone(const char *desc) throw(std::bad_alloc);

		virtual void *alloc() throw();
		virtual void  dealloc(void *ptr) throw();
		virtual void  flush();
	private:	
		long m_ref;
		struct mpool_t m_mp;

};

#endif
