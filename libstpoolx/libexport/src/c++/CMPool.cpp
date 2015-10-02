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
#include <stdio.h>
#include <stdexcept>

#include "CMAllocator.h"
#include "ospx.h"

CMPool::CMPoolLockInitializer::CMPoolLockInitializer()
{
	static OSPX_pthread_mutex_t slock;
	
	OSPX_pthread_mutex_init(&slock, 0);
	CMPool::sm_lock = reinterpret_cast<void *>(&slock);
}

CMPool::CMPoolLockInitializer::~CMPoolLockInitializer()
{
}

class CLocker
{
	public:
		CLocker(void *lock) : m_lock(lock) 
		{
			OSPX_pthread_mutex_lock(
					reinterpret_cast<OSPX_pthread_mutex_t *>(m_lock)
				);
		}
		~CLocker() 
		{
			OSPX_pthread_mutex_unlock(
					reinterpret_cast<OSPX_pthread_mutex_t *>(m_lock)
				);
		}
	private:
		void *m_lock;
};


void *CMPool::sm_lock = NULL;
CMPool::T CMPool::sm_ap;
CMPool::CMPoolLockInitializer CMPool::dummy;

CAllocator *CMPool::create(const char *desc, size_t bytes) throw (std::bad_alloc)
{
	CAllocator *alc = new(std::nothrow) CMAllocator(desc, bytes);

	if (!alc)
		throw std::bad_alloc();
	return alc;
}

CAllocator *CMPool::addIfNoExist(size_t bytes) throw (std::bad_alloc)
{
	return addIfNoExist(NULL, bytes);
}

CAllocator *CMPool::addIfNoExist(const char *desc, size_t bytes) throw (std::bad_alloc)
{
	CLocker locker(sm_lock);
	
	T::iterator it;

	if (!desc) {
		/* Find a allocator according to the bytes */
		if (sm_ap.end() != (it = sm_ap.find(bytes))) {
			it->second->addRef();
			return it->second;
		}
		desc = "dummy";
	} else {
		for (std::pair<T::iterator, T::iterator> r = sm_ap.equal_range(bytes);
			r.first != r.second; ++ r.first) {
			if (!r.first->second->desc().compare(desc)) {
				r.first->second->addRef();
				return r.first->second;
			}
		}
	}
	
	/* We try to create a new default allocator if there
	 * are none allocators exisiting in the map */
	CAllocator *alc = create(desc, bytes);

	try {
		T::value_type ele = std::make_pair<size_t, CAllocator *>(bytes, alc);
		sm_ap.insert(ele);

	} catch (std::exception &e) {
		alc->release();
		throw e;
	}

	return alc;
}

void CMPool::add(CAllocator *alc) throw (std::bad_alloc)
{
	CLocker locker(sm_lock);
	T::value_type ele = std::make_pair<size_t, CAllocator *>(alc->size(), alc);
	
	sm_ap.insert(ele);
	alc->addRef();
}

void CMPool::remove(CAllocator *alc)
{
	CLocker locker(sm_lock);

	for (std::pair<T::iterator, T::iterator> r = sm_ap.equal_range(alc->size());
		r.first != r.second; ++ r.first) {
		if (r.first->second == alc) {
			alc->release();
			break;
		}
	}
}

void CMPool::flush()
{
	CLocker locker(sm_lock);
	
	/* Flush all allocators */
	for (T::iterator it = sm_ap.begin(), end = sm_ap.end();
		it != end; it ++)
		it->second->flush();
}

CAllocator *CMPool::get(size_t bytes)
{
	CLocker locker(sm_lock);
	
	/* Find a allocator by the object length */
	T::iterator it = sm_ap.find(bytes);
	
	if (sm_ap.end() != it) {
		it->second->addRef();
		return it->second;
	}

	return NULL;
}

CAllocator *CMPool::get(const char *desc)
{
	CLocker locker(sm_lock);

	for (T::iterator it = sm_ap.begin(), end = sm_ap.end();
		it != end; it ++) {
		
		/* Find a allocator by the description */
		if (!it->second->desc().compare(desc)) {
			it->second->addRef();
			return it->second;
		}
	}
	
	return NULL;
}

std::string &CMPool::report(std::string &s)
{
	char r[300];
	CAllocator::Stat st;

	CLocker locker(sm_lock);
	for (T::iterator it = sm_ap.begin(), end = sm_ap.end();
		it != end; it ++) {
		CAllocator *alc = it->second;
		CAllocator::Attr attr;
		
		alc->getAttr(attr);
		alc->stat(st);
		
		/* Format the report */
		sprintf(r, "=====================\n"
		           "alc: \"%s\" /%p\n"
				   "objSize: %u\n"
				   "memHold: %u bytes\n"
				   "nCached: %u\n"
				   "nAllocated: %u\n"
				   "nAcquired: %u\n"
				   "nBlks: %u\n"
				   "readOnly: %s\n"
				   "nMinCache: %u\n"
				   "nMaxAlloc: %d\n"
				   "nBlkSize: %u bytes\n",
				   alc->desc().c_str(),
				   alc,
				   alc->size(),
				   st.memHold,
				   st.nCached,
				   st.nAllocated,
				   st.nAcquired,
				   st.nBlks,
				   attr.bReadOnly ? "Yes" : "No",
				   attr.nMinCache,
				   attr.nMaxAlloc,
				   attr.nBlkSize);

		s.append(r);
	}

	return s;
}
