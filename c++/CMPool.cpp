#include <stdexcept>
#include "CMAllocator.h"
extern "C"
{
#include "ospx.h"
}

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

CAllocator *CMPool::createAllocator(const char *desc, size_t bytes) throw (std::bad_alloc)
{
	CAllocator *alc = new(std::nothrow) CMAllocator(desc, bytes);

	if (!alc)
		throw std::bad_alloc();
	return alc;
}

CAllocator *CMPool::addAllocatorIfNoExist(size_t bytes) throw (std::bad_alloc)
{
	CLocker locker(sm_lock);
	
	T::iterator it;

	/* Find a allocator */
	if (sm_ap.end() != (it = sm_ap.find(bytes))) {
		it->second->addRef();
		return it->second;
	}
	
	/* We try to create a new default allocator if there
	 * are none allocators exisiting in the map */
	CAllocator *alc = createAllocator("dummy", bytes);

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
