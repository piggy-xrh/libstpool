
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

#include "ospx.h"
#include "CMAllocator.h"

CMAllocator::CMAllocator(const char *desc, size_t bytes):
	CAllocator(desc, bytes), m_ref(1) 
{
	objpool_ctor(&m_objp, desc, bytes, 0);
}

CMAllocator::~CMAllocator() 
{
	objpool_dtor(&m_objp);
}

long CMAllocator::addRef()
{
	return OSPX_interlocked_increase(&m_ref);
}

long CMAllocator::release()
{
	long ref = OSPX_interlocked_decrease(&m_ref);

	if (!ref)
		delete this;

	return ref;
}

CAllocator *CMAllocator::clone(const char *desc) throw(std::bad_alloc)
{
	CAllocator *alc = new(std::nothrow) CMAllocator(desc, size());
	
	if (!alc)
		throw std::bad_alloc();
	return alc;
}

void *CMAllocator::alloc() throw()
{
	return smcache_get(
				objpool_get_cache(&m_objp),
				1);
}

void  CMAllocator::dealloc(void *ptr) throw()
{
	smcache_add_limit(
				objpool_get_cache(&m_objp),
				ptr,
				-1);
}

void  CMAllocator::flush()
{
	smcache_flush(
			objpool_get_cache(&m_objp),
			0);
}

CAllocator::Attr &CMAllocator::setAttr(Attr &attr) 
{
	/* The fast objpool will not allowed to configure
	 * its attribute if it has been initialized */
	return getAttr(attr);
}

CAllocator::Attr &CMAllocator::getAttr(Attr &attr)
{	
	/* Fix me: 
	 * 	 we should not operate the pool object directly */
	attr.bReadOnly = true;
	attr.nBlkSize  = m_objp.block_size;
	attr.nMinCache = m_objp.block_nobjs;
	attr.nMaxAlloc = -1;
	
	return attr;
}
		
CAllocator::Stat &CMAllocator::stat(CAllocator::Stat &s)
{	
	int nTotal = m_objp.ntotal;
	
	/* Fix me: 
	 * 	 we should not operate the pool object directly */
	s.memHold = m_objp.iblocks * m_objp.block_size;
	s.nCached = m_objp.nput + smcache_n(objpool_get_cache(&m_objp));
	if (s.nCached >= nTotal) /* For thread safe */
		nTotal = s.nCached;
	s.nAllocated = nTotal - s.nCached; 
	s.nAcquired = s.nAllocated;
	s.nBlks = m_objp.iblocks;
	
	return s;
}
