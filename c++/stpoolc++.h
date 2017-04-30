#ifndef __STPOOL_CPP_H__
#define __STPOOL_CPP_H__

/** @mainpage COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the pool, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */
#include <set>
#include <string>
#include <stpool/stpool.h>

using std::string;
using std::set;
namespace stpool 
{

class  CTaskPool;

class CTask
{
	public:
		CTask(const string &name, CTaskPool *pTaskPool = NULL);
		virtual ~CTask(); 
		
		int setPool(CTaskPool *pool);
		inline CTaskPool *getPool() const {
			return m_pTaskPool;
		}
		
		inline string taskName() { 
			return m_pTask->task_name; 
		}

		inline void setCode(unsigned int code) { 
			m_pTask->task_code = code; 
		}

		inline int  getCode() { 
			return m_pTask->task_code; 
		}
		
		inline void setFlags(unsigned short flags) { 
			stpool_task_set_userflags(m_pTask, flags); 
		}
		
		inline unsigned short getFlags() { 
			return stpool_task_get_userflags(m_pTask); 
		}
		
		inline int  queue() { 
			return stpool_task_queue(m_pTask); 
		}
		
		inline bool remove(bool dispatchedByPool = true) { 
			return 0 == stpool_task_remove(m_pTask, dispatchedByPool ? 1 : 0); 
		}
	
		inline long stat() { 
			return stpool_task_stat(m_pTask); 
		}
		
		inline int  wait(long ms = -1) { 
			return stpool_task_wait(m_pTask, ms); 
		}

		inline void detach() {
			stpool_task_detach(m_pTask);
		}

		void setScheAttr(const schattr &attr) {
			stpool_task_setschattr(m_pTask, const_cast<schattr *>(&attr));
		}

		schattr getSchAttr() {
			schattr attr;

			stpool_task_getschattr(m_pTask, &attr);
			return attr;
		}

		
		// The subclass must implement the interface @onRun 

		virtual void onRun() = 0; 
		virtual void onErrHandler(long reasons) {};	
	
	protected:
		friend class CTaskPool;
		inline sttask *task() {return m_pTask;}
		
		struct CName;
		struct CNameComp {
			bool operator()(CName * const &n1, CName * const &n2); 
		};

		static set<CName *, CNameComp> sm_taskName;

		sttask *m_pTask;
		CTaskPool *m_pTaskPool;
};

class CTaskPool
{
		CTaskPool();
		~CTaskPool();
	public:
		static CTaskPool *create(const string &poolName, long eCaps, int max = 1, int min = 0, int priQNum = 0, bool bSuspend = false);

		inline const string poolName() const {
			return stpool_desc(m_pPool);
		}
		
		long addRef() {
			return stpool_addref(m_pPool);
		}

		long release() {
			return stpool_release(m_pPool);
		}

		inline long caps() const {
			return stpool_caps(m_pPool);
		}
		
		void suspend() {
			stpool_suspend(m_pPool, 0);
		}

		void resume()  {
			stpool_resume(m_pPool);
		}
		
		void adjustThreads(int max, int min) {
			stpool_adjust(m_pPool, max, min);
		}

		void adjustThreadsAbs(int min, int max) {
			stpool_adjust_abs(m_pPool, max, min);
		};

		void setThreadAttr(const stpool_thattr &attr) {
			stpool_thread_setscheattr(m_pPool, const_cast<stpool_thattr *>(&attr));
		}

		stpool_thattr getThreadAttr() {
			stpool_thattr attr;

			return *stpool_thread_getscheattr(m_pPool, &attr);
		}
		
		void setOverloadAttr(const oaattr &attr) {
			stpool_set_overload_attr(m_pPool, const_cast<oaattr *>(&attr));
		};
		
		oaattr getOverloadAttr() {
			oaattr attr;

			stpool_get_overload_attr(m_pPool, &attr);
			return attr;
		};
		
		int  addTask(CTask *pTask);
		
		int  removeAll(bool dispatchedByPool = true) {
			return stpool_remove_all(m_pPool, dispatchedByPool ? 1 : 0);
		}
		
		void enableThrottle(bool enable = true) {
			stpool_throttle_enable(m_pPool, enable ? 1 : 0);
		}

		int  waitAll(long ms = -1) {
			return stpool_wait_all(m_pPool, ms);
		};
		
		int  waitAny(long ms = -1) {
			return stpool_wait_any(m_pPool, ms);
		}

		pool_stat stat() {
			pool_stat st;

			return *stpool_stat(m_pPool, &st);
		}

		string statString() {
			return stpool_stat_print(m_pPool);
		}

		static string statString(const pool_stat &stat) {
			return stpool_stat_print2(const_cast<pool_stat *>(&stat), NULL, 0);
		}

		string scheString() {
			return stpool_scheduler_map_dump(m_pPool);
		}

	protected:
		inline sttask *task(CTask *pTask) {return pTask->task();}

		friend int CTask::setPool(CTaskPool *pool);
		inline operator stpool_t*() {
			return m_pPool;
		}
	protected:
		stpool_t *m_pPool;
};

}
#endif
