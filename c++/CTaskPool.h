#ifndef __TASK_POOL_H__
#define __TASK_POOL_H__
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

#include <cassert>
#include <stdexcept>
#include <list>
#include "CMPool.h"

class  CTask;

/* Status of the pool.
 *   (See the definition of struct stpool_stat_t for more details (libstpool/stpool.h))
 */
struct TaskPoolStat
{
	long references;                    
	time_t creatTime;              
	int priQNum;               
	int queueEnabled;        
	int suspended;               
	int maxThreads;              
	int minThreads;              
	int curThreads;              
	int curThreadsActive;       
	int curThreadsDying;        
	long actTimeo;               
	size_t tasksPeak;           
	size_t threadsPeak;         
	size_t tasksAdded;          
	size_t tasksDone;           
	size_t tasksDispatched;      
	size_t curTasks;            
	size_t curTasksPending;    
	size_t curTasksScheduling; 
	size_t curTasksRemoving;   
};

enum
{
	/* Everything is ok */
	ep_OK = 0,

	/* Unkown error */
	ep_OTHER = -1,

	/* System is out of memory */
	ep_NOMEM = -2,

	/* Timeout */
	ep_TIMEOUT = -3,

	/* @enableQueue(false) has been called by user */
	ep_ENJECT = -4,

	/* The task pool is not the parent of the task */
	ep_PARENT = -5,

	/* Pool is being destroyed */
	ep_DESTROYING = -6,
	
	/* The function is wokeup by @wakeup */
	ep_WOKEUP = -7,

	/* The task has been removed for some reasons */
	ep_REMOVED = -8,

	/* The task is in progress */
	ep_BUSY = -9,
};

/* Note:
 * 	  CTaskPool is just a simple wrapper of libstpool for c++ 
 */
class CTaskPool
{
		CTaskPool()  {}
		~CTaskPool() {}
	public:			
		static CTaskPool *createInstance(int maxThreads, int minThreads, bool bSuspend = false, int priQNum = 1);
		long addRef();
		long release();
		
		void setActiveTimeo(long actTimeo);
		void adjustAbs(int maxThreads, int minThreads);
		void adjust(int maxThreads, int minThreads);
		
		TaskPoolStat stat();
		const std::string& stat(std::string &st);
		long taskStat(CTask *task, long &sm);
		
		void suspend(bool bWaitSchedulingTasks = false);
		void resume();
		void enableQueue(bool enable = true);

		/* Deliver the task into the pending queue 
		 *   On success, ep_OK will be returned, or the error code will be returned. */
		int  queue(CTask *task);
		
		/* Remove the task from the pending queue 
		 *   On success, ep_OK will be returned, or the error code will be returned. */
		int  remove(CTask *task);
		
		/* Return the number for tasks who has been marked with @REMOVED */
		int  removeAll(bool dispatchedByPool = true);
		
		/* @detach is only be allowed to call in the task's
		 * working or completion routine. (see libstpool for
		 * more details) */
		void detach(CTask *task);
		
		long getThreadID();	
		void wakeup(long threadID);
		
		/* Wait for tasks' completion in @ms milliseconds.
		 *   On success, ep_OK will be returned, or the error code will be returned.
		 */
		int  wait(CTask *task, long ms);
		int  waitAll(const std::list<CTask *> &sets, long ms);
		int  waitAny(const std::list<CTask *> &sets, long ms);
		
		/* Watch the number of the pending tasks in @ms milliseconds.
		 *	On success, ep_OK will be returned, or the error code will be returned.
		 */
		int  waitStat(long nMaxPendingTasks, long ms);

		/* If the user has called @enableQueue(false) to disable queueing the task,
		 * ep_ENJECT will be returned by @queue if users call it to schedule the tasks.
		 * 
		 *    @waitQueueEnabled will not be return in @ms milliseconds until user
		 * calls @enableQueue to allow users queue the tasks again.
		 *
		 *	On success, ep_OK will be returned, or the error code will be returned.
		 */
		int  waitQueueEnabled(long ms);
	private:
		int  extractErr(int libErr);
		int  extractErr1(int waitErr);
	private:	
		void *m_proxyHandle;
};

class CTask
{		
	public:	
		CTask(void *cproxy, const char *taskName = "dummy", CTaskPool *p = NULL);
		virtual ~CTask(); 

		/* Acquire the proxy object length.
		 * 	(All of the services are provided by the proxy object) */
		static size_t getProxySize();
		
		/* Set the owner of the task. 
         *   NOTE: @setParent should be called firstly to set the task's parent 
		 * before the task's delivering into the task pool.
		 */
		int setParent(CTaskPool *p) {
			if (p != m_parent) { 
				/* We do not reset the parent if the task
				 * has not been finished */
				if (m_parent && stat())
					return ep_BUSY;
				m_parent = p;	
			}
			return 0;
		}
		inline CTaskPool *getParent() const {return m_parent;}
		
		struct attr 
		{
			/* If permanent is not true, the task will be reset 
			 * to the lowerest priority after its done */
			bool permanent;

			/* The scheduling priority. [0~99] */
			int  schePriority;
			
			/* The scheduling policy (see libstpool for more details) */
			enum 
			{
				scheP_TOP = 1,
				scheP_BACK
			};
			int  schePolicy;
		};
		void setAttr(const attr& at);
		attr& getAttr(attr &at);

		/* Get the task's description. */
		const char *taskName() const;
		
		/* Get the status of the task */
		inline long stat(long &sm) {sm = 0; return m_parent ? m_parent->taskStat(this, sm) : 0;}
	
		/* The task status */
		enum 
		{
			st_PENDING = 0x01,
			st_SCHEDULING = 0x02,
			st_SWAPED = 0x04,
			st_DISPATCHING = 0x08,
			st_WPENDING = 0x10
		};
		inline long stat() {long dummy; return stat(dummy);}
		
		/* The mark flags */
		enum 
		{
			/* sm_DONE will only be passed to @onTaskComplete by the pool */
			sm_DONE  = 0x01,
			
			sm_REMOVED_BYPOOL = 0x04,
			sm_REMOVED = 0x08,
			sm_DESTROYING = 0x10,
		};
		inline long sm()   {long sm0; stat(sm0); return sm0;}
		
		inline int queue()  {return m_parent ? m_parent->queue(this) : ep_PARENT;}
		inline int remove() {return m_parent ? m_parent->remove(this) : ep_OK;}
		inline int wait(long ms = -1) {return m_parent ? m_parent->wait(this, ms) : ep_OK;}

		/* The working routine and completion routine of the task.
		 * the subclass will implement them to do his customed work. */
		virtual int   onTask() = 0;
		virtual void  onTaskComplete(long sm, int errCode) = 0;
	private:
		friend class CTaskPool;
		
		inline struct sttask_t *getProxy() const {return m_proxy;}
	private:
		struct sttask_t *m_proxy;
		CTaskPool *m_parent;
};

/* In order to make the pool works effeciently, we allocate all tasks 
 * object in the memory pool */

template<size_t spaces>
class CPoolTask:public CTask
{
	public:	
		CPoolTask(CTaskPool *p = NULL, const char *taskName = "poolDummy"): 
			CTask(reinterpret_cast<char *>(this) + spaces, taskName, p) {}
		virtual ~CPoolTask() {}
		
		inline void *operator new(size_t bytes) throw(std::bad_alloc)
		{
			static allocatorInitializer dummy;
			assert(sm_allocator && bytes <= spaces);
			
			/* Allocate a object from the pool */
			void *ptr = sm_allocator->alloc();
			
			if (!ptr)
				throw std::bad_alloc();

			return ptr;
		}

		inline void operator delete(void *ptr) throw()
		{
			if (ptr) 
				sm_allocator->dealloc(ptr);
		}
	private:
		struct allocatorInitializer
		{
			allocatorInitializer() {sm_allocator = CMPool::addAllocatorIfNoExist(spaces + getProxySize());}
			~allocatorInitializer() {}
		};
		static CAllocator *sm_allocator;
};

template<size_t spaces>
CAllocator *CPoolTask<spaces>::sm_allocator = NULL;

#endif

