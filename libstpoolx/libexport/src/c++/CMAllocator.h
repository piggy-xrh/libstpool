#ifndef __CMALLOCATOR_H__
#define __CMALLOCATOR_H__
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

#include "objpool.h"
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
		virtual Attr &setAttr(Attr &attr);
		virtual Attr &getAttr(Attr &attr);
		virtual Stat &stat(Stat &s);
	private:	
		long m_ref;
		objpool_t m_objp;

};

#endif
