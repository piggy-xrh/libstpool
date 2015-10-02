#ifndef __CMPOOL_H__
#define __CMPOOL_H__
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

#include <cassert>
#include <string>
#include <map>

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64)
#define _WIN
#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#else
#define EXPORT
#endif

class EXPORT CAllocator
{
	public:
		CAllocator(const char *desc, size_t bytes): 
			m_desc(desc), m_bytes(bytes) {}
		virtual ~CAllocator() {}

		/* The object size */
		size_t size() const {return m_bytes;}
		const std::string &desc() const {return m_desc;}
		
		/* Reference interfaces */
		virtual long addRef() = 0;
		virtual long release() = 0;

		/* Clone protocol */
		virtual CAllocator *clone(const char *desc) throw (std::bad_alloc) = 0;

		/* Basic interfaces */
		virtual void *alloc() throw() = 0;
		virtual void  dealloc(void *ptr) throw() = 0;
		virtual void  flush() = 0;

		/* Attribute */
		struct Attr
		{
			/* If @bReadOnly is true, then we can call @setAttr
			 * to configure the allocator dynamically */
			bool bReadOnly;

			/* The min number of objects that should be cached for perfermance */
			size_t nMinCache;

			/* The max number of objects that can be received from the allocators */
			int nMaxAlloc;

			/* Block size(4K/8K/16K/..) of the allocator.
			 * block is the base unit for the allocator to expand the memories */
			size_t nBlkSize;
		};
		virtual Attr &setAttr(Attr &attr) = 0;
		virtual Attr &getAttr(Attr &attr) = 0;
		
		/* Status */
		struct Stat
		{
			/* The memory size that the allocator holds now */
			size_t memHold;

			/* The number of free objects existing in the cache */
			size_t nCached;

			/* The number of objects owned by users */
			size_t nAllocated;

			/* The requesting number of users */
			size_t nAcquired;

			/* The memory block number of the allocator holds now */
			size_t nBlks;
			
			/* @nMinCache, @nMaxAlloc, @BlkSize */
			size_t nMinCache;
			int nMaxAlloc;
			size_t  nBlkSize;
		};
		virtual Stat &stat(Stat &s) = 0;
	private:
		std::string m_desc;
		size_t m_bytes;
};

/* CMPool is created to manager the object allocators */
class EXPORT CMPool
{
	public:
		/* Create a default allocator */
		static CAllocator *create(const char *desc, size_t bytes) throw(std::bad_alloc);
		
		/* Get a allocator from the memory pool according to the @bytes (or both @desc 
		 * and @bytes), If none any allocator matches our request, then it'll add a 
		 * default allocator, you should call @release to release it if it is useless
		 * for you. */
		static CAllocator *addIfNoExist(size_t bytes) throw(std::bad_alloc);
		static CAllocator *addIfNoExist(const char *desc, size_t bytes) throw(std::bad_alloc);

		/* Add a allocator into the memory pool */
		static void add(CAllocator *allocator) throw(std::bad_alloc);
		
		/* Remove a allocator from the memory pool if it exists */
		static void remove(CAllocator *allocator);

		/* Flush all of the allocators existing in the memory pool */
		static void flush();

		/* Get a allocator from the memory pool according to @bytes or @desc,
		 * you should call @release to release it if it is useless for you */
		static CAllocator *get(size_t bytes);
		static CAllocator *get(const char *desc);
		
		/* Get a report of all of the allocators */
		static std::string &report(std::string &s);
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

/* We provide a common object pool template */
template<typename T>
class EXPORT CMObj
{
	public:	
		inline void *operator new(size_t bytes) throw(std::bad_alloc)
		{
			assert(sizeof(T) == bytes);

			/* We use the dumy to initialize the @sm_allocator */
			static allocatorInitializer dummy("dummy", bytes);
			assert(sm_allocator && sm_allocator->size() >= bytes);

			/* Allocate a object from the pool */
			void *ptr = sm_allocator->alloc();
			
			if (!ptr)
				throw std::bad_alloc();

			return ptr;
		}

		inline void operator delete(void *ptr) throw()
		{
			if (ptr) {
				assert(sm_allocator);
				sm_allocator->dealloc(ptr);
			}
		}
	protected:
		struct allocatorInitializer
		{
			allocatorInitializer(const char *s, size_t bytes) 
			{sm_allocator = CMPool::addIfNoExist(s, bytes);}
			
			~allocatorInitializer() {}
		};
		static CAllocator *sm_allocator;
};

template<typename T>
CAllocator *CMObj<T>::sm_allocator = NULL;
#endif
