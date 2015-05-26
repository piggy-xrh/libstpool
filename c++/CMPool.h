#ifndef __CMPOOL_H__
#define __CMPOOL_H__

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

#include <string>
#include <map>

class CAllocator
{
	public:
		CAllocator(const char *desc, size_t bytes): 
			m_desc(desc), m_bytes(bytes) {}
		virtual ~CAllocator() {}

		size_t size() const {return m_bytes;}
		const std::string &desc() const {return m_desc;}
		
		/* Reference interfaces */
		virtual long addRef() = 0;
		virtual long release() = 0;

		/* Clone protocol */
		virtual CAllocator *clone(const char *desc) throw (std::bad_alloc) = 0;

		/* Function interfaces */
		virtual void *alloc() throw() = 0;
		virtual void  dealloc(void *ptr) throw() = 0;
		virtual void  flush() = 0;
	private:
		std::string m_desc;
		size_t m_bytes;
};

class CMPool
{
	public:
		static CAllocator *createAllocator(const char *desc, size_t bytes) throw(std::bad_alloc);
		static CAllocator *addAllocatorIfNoExist(size_t bytes) throw(std::bad_alloc);

		static void add(CAllocator *allocator) throw(std::bad_alloc);
		static void remove(CAllocator *allocator);
		static void flush();

		static CAllocator *get(size_t bytes);
		static CAllocator *get(const char *desc);
	private:
		typedef std::multimap<size_t, CAllocator *> T;
		static T sm_ap;
		
		static struct CMPoolLockInitializer
		{
			CMPoolLockInitializer();	
			~CMPoolLockInitializer();
		} dummy;

		static void *sm_lock;
};

#endif
