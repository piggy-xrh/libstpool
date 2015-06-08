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

#ifdef _WIN32
#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#else
#define EXPORT
#endif
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
struct threadAttr {
	/* Thread stack size (0:default) */
	int stackSize;

	/* Thread schedule policy */
	enum 
	{
		ep_SCHE_NONE,
		ep_SCHE_RR,
		ep_SCHE_FIFO,
		ep_SCHE_OTHER
	};
	int ep;

	/* Thread schedule priority ([1-100] 0:default) */
	int priority;
};

class EXPORT CTaskPool
{
		CTaskPool()  {}
		~CTaskPool() {}
	public:			
		/* Create a task pool instance, you should call @release to free it after 
		 * having done your business */
		static CTaskPool *createInstance(int maxThreads = 1, int minThreads = 0, bool bSuspend = false, int priQNum = 1);
		
		/* Set/Get the schedule attribute for the working threads */
		void setThreadAttr(const struct threadAttr &attr);
		struct threadAttr &getThreadAttr(struct threadAttr &attr);

		/* Reference interfaces */
		long addRef();
		long release();
		
		void setActiveTimeo(long actTimeo, long randTimeo);
		void adjustAbs(int maxThreads, int minThreads);
		void adjust(int maxThreads, int minThreads);
		
		/* Get the pool status */
		TaskPoolStat stat();
		const std::string& stat(std::string &st);
		
		/* Get the task status. If the task does not exist in the pool now, it'll
		 * return 0 */
		long taskStat(CTask *task, long &sm);
		
		/* If @suspend is called by user, the pool will not schedule any tasks existing in
		 * the pending queue until he calls @resume to notify pool */
		void suspend(bool bWaitSchedulingTasks = false);
		void resume();

		/* If @enableQueue is called by user, anyone who calls @queue will gets a error
		 * code (ep_ENJECT). */
		void enableQueue(bool enable = true);
		int  enableQueueOnTask(CTask *task = NULL, bool enable = true);

		/* Deliver the task into the pool's pending queue 
		 *   On success, ep_OK will be returned, or the error code will be returned. */
		int  queue(CTask *task);
		
		/* @remove makes sure that the task will be removed from the pending queue.
		 *   On success, ep_OK will be returned, or the error code will be returned. */
		int  remove(CTask *task);
		
		/* Return the number for tasks who has been marked with @REMOVED */
		int  removeAll(bool dispatchedByPool = true);
		
		/* @detach is only be allowed to call in the task's
		 * working or completion routine. (see libstpool for
		 * more details) */
		void detach(CTask *task);
		
		/* If you want to wake up the WAIT functions such as @wait, @waitAll, @waitAny,
		 * @waitStat and @waitQueueEnabled, you can save the wakeID by calling @getThreadID,
		 * before your calling these WAIT functions, and then you can call @wakeup with wakeID 
		 * to force the wait functions return with error code ep_WOKEUP.
		 *
		 *   model:
		 *          thread1
		 *      wakeID = getThreadID();
		 *      wait();
		 *                                 <------ wakeup(wakeID)
		 */
		long getThreadID();	
		void wakeup(long threadID);
		
		/* Wait for tasks' completion in @ms milliseconds.
		 *   On success, ep_OK will be returned, or the error code will be returned.
		 */
		int  wait(CTask *task = NULL, long ms = -1);
		int  waitAll(const std::list<CTask *> &sets, long ms = -1);
		int  waitAny(const std::list<CTask *> &sets, long ms = -1);
		
		/* Watch the number of the pending tasks in @ms milliseconds.
		 *	On success, ep_OK will be returned, or the error code will be returned.
		 */
		int  waitStat(long nMaxPendingTasks, long ms = -1);

		/* If the user has called @enableQueue(false) to disable queueing the task,
		 * ep_ENJECT will be returned by @queue if users call it to schedule the tasks.
		 * 
		 *    @waitQueueEnabled will not be return in @ms milliseconds until user
		 * calls @enableQueue to allow users queue the tasks again.
		 *
		 *	On success, ep_OK will be returned, or the error code will be returned.
		 */
		int  waitQueueEnabled(long ms = -1);
	private:
		int  extractErr(int libErr);
		int  extractErr1(int waitErr);
	private:	
		void *m_proxyHandle;
};

class EXPORT CTask
{		
	public:	
		CTask(void *cproxy, const char *taskName = "dummy", CTaskPool *p = NULL);
		virtual ~CTask(); 

		/* Acquire the proxy object length.
		 * 	(All of the services are provided by the proxy object) */
		static size_t getProxySize();
		
		/* Set the owner of the task. 
         *   NOTE: @setParent should be called firstly to set the task's parent 
		 * before the task's delivering into the task pool if the task's parent
		 * is not the destination task pool.
		 */
		inline int setParent(CTaskPool *p) {
			if (p != m_parent) { 
				long _sm = 0;

				/* We do not reset the parent if the task
				 * has not been finished */
				if (m_parent && stat(_sm))
					return ep_BUSY;

				m_parent = p;
				
				/* We remove the sm_DISABLE_QUEUE if the task's parent has 
				 * been changed */
				if (p && (_sm & CTask::sm_DISABLE_QUEUE))
					return p->enableQueueOnTask(this, true);
			} else
				m_parent = p;
			return ep_OK;
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
			/* The task is in the pool's pending queue */
			st_PENDING = 0x01,

			/* The task is being scheduled */
			st_SCHEDULING = 0x02,
			
			/* The task has been swapped since the pool is suspended */
			st_SWAPED = 0x04,

			/* The task has been removed from the pending queue */
			st_DISPATCHING = 0x08,

			/* The task is in progress, and it has been requested to be scheduled again */
			st_WPENDING = 0x10
		};
		inline long stat() {long dummy; return stat(dummy);}
		
		/* The mark flags */
		enum 
		{
			/* sm_DONE will only be passed to @onTaskComplete by the pool */
			sm_DONE  = 0x01,
			
			/* The task has been removed from the pending queue, and the
			 * task's completion routine will be called as soon as possible. */
			sm_REMOVED_BYPOOL = 0x04,
			sm_REMOVED = 0x08,
			
			/* The pool is being destroyed */
			sm_POOL_DESTROYING = 0x10,
			
			/* The task will be queued again automatically after its done */
			sm_ONCE_AGAIN = 0x20,
			
			/* The task is not allowed to dilived into the pool */
			sm_DISABLE_QUEUE = 0x40
		};
		/* Note: 	All of the task's marks except sm_DISABLE_QUEUE will be cleared
		 *      after it having been delived into the parent's pending queue by @queue
		 */
		inline long sm()   {long sm0; stat(sm0); return sm0;}
		
		inline int  queue()  {return m_parent ? m_parent->queue(this) : ep_PARENT;}
		inline int  remove() {return m_parent ? m_parent->remove(this) : ep_OK;}
		inline int  wait(long ms = -1) {return m_parent ? m_parent->wait(this, ms) : ep_OK;}
		inline int  enableQueue(bool enable = true) {return m_parent ? m_parent->enableQueueOnTask(this, enable) : ep_OK;}

		/* The working routine and completion routine of the task.
		 * the subclass will implement them to do his customed work. */
		virtual int   onTask() = 0;
		virtual void  onTaskComplete(long sm, int errCode) = 0;
	protected:
		friend class CTaskPool;
		
		/* Get the proxy object */
		inline struct sttask_t *getProxy() const {return m_proxy;}
	private:
		struct sttask_t *m_proxy;
		CTaskPool *m_parent;
};


/* In order to make the pool works effeciently, we allocate all tasks 
 * object in the memory pool .*/
template <typename T>
class EXPORT CPoolTask:public CTask, public CMObj<T>
{
	public:	
		CPoolTask(const char *taskName = "poolDummy", CTaskPool *p = NULL): 
			CTask(reinterpret_cast<char *>(this) + sizeof(T), taskName, p) {}
		virtual ~CPoolTask() {}
		
		/* We implement our own operator new to expand the spaces for the proxy object */
		inline void *operator new(size_t bytes) throw(std::bad_alloc)
		{
			/* We use the dumy to initialize the @sm_allocator */
			static typename CMObj<T>::allocatorInitializer dummy("poolTask", bytes + getProxySize());
			assert(CMObj<T>::sm_allocator && bytes + getProxySize() == CMObj<T>::sm_allocator->size());

			/* Allocate a object from the pool */
			void *ptr = CMObj<T>::sm_allocator->alloc();
			
			if (!ptr)
				throw std::bad_alloc();

			return ptr;
		}
};

#endif

