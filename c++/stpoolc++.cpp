/** @mainpage COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the pool, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <string.h>
#include <cassert>
#include <exception>

#include "stpoolc++.h"

namespace stpool
{

struct CTask::CName
{
	CName(const string &str, bool bStore = false): 
		m_bRelease(false), m_buffer(NULL)
	{
		setName(str.c_str(), bStore);
	}

	CName() : m_bRelease(false), m_buffer(NULL)
	{
	}

	~CName()
	{
		reset();
	}

	void setName(const char *name, bool bStore = false)
	{
		reset();
		
		try {
			m_buffer = new char[strlen(name) + 1];
		} catch (...) {
		}

		if (m_buffer) {
			m_bRelease = true; 
			strcpy(m_buffer, name);
		}
	}

	void reset()
	{	
		if (m_bRelease && m_buffer) {
			delete [] m_buffer;
		}
		m_bRelease = false;
		m_buffer = NULL;
	}
	
	inline operator char *() {return m_buffer;}
	
	bool m_bRelease;
	char *m_buffer;
};

bool CTask::CNameComp::operator()(CName *const &n1, CName * const &n2)
{
	return n1->m_buffer == n2->m_buffer || !n1 || strcmp(n1->m_buffer, n2->m_buffer);
}

std::set<CTask::CName *, CTask::CNameComp> CTask::sm_taskName;

// Helper tools
static void Helper_task_run(sttask *ptask)
{
	CTask *pTask = reinterpret_cast<CTask *>(ptask->task_arg);
	
	assert (pTask);
	pTask->onRun();
}

static void Helper_task_err_handler(sttask *ptask, long reasons)
{
	CTask *pTask = reinterpret_cast<CTask *>(ptask->task_arg);
	
	assert (pTask);
	pTask->onErrHandler(reasons);
}

CTask::CTask(const string &name, CTaskPool *pTaskPool)
{
	CName nameObj(name), *pNameObj = NULL;
	
	std::set<CTask::CName *>::iterator it = sm_taskName.find(&nameObj);
	if (it == sm_taskName.end()) {
		try {
			pNameObj = new CName(name, true);
			sm_taskName.insert(pNameObj);
		
		} catch (std::exception &e) {
			if (pNameObj) {
				delete pNameObj;
			}
			throw e;
		}
	
	} else {
		pNameObj = *it;
	}
	
	m_pTask = stpool_task_new(NULL, *pNameObj, Helper_task_run, Helper_task_err_handler, this);
	if (!m_pTask) {
		throw std::bad_alloc();
	} 
	setPool(pTaskPool);
}

CTask::~CTask() 
{	
	if (m_pTask) {
		stpool_task_delete(m_pTask);
	}
}
		
int CTask::setPool(CTaskPool *pool)
{
	int err;
	
	assert (m_pTask);

	if (!pool) {
		err = stpool_task_set_p(m_pTask, NULL);
	} else {
		err = stpool_task_set_p(m_pTask, *pool);
	}

	if (!err) {
		m_pTaskPool = pool;
	}

	return err;
} 


CTaskPool::CTaskPool()
{
}

CTaskPool::~CTaskPool()
{
	if (m_pPool) {
		stpool_release(m_pPool);
	}
}

CTaskPool *CTaskPool::create(const string &poolName, long eCaps, int max, int min, int priQNum, bool bSuspend)
{
	CTaskPool *taskPool = NULL;
	
	try {
		taskPool = new CTaskPool();

		// Create the pool instance with the user specified parameters
		taskPool->m_pPool = stpool_create(poolName.c_str(), eCaps, max, min, bSuspend ? 1 : 0, priQNum);
	
	} catch (std::exception &e) {
		
	}

	return taskPool;
}

int CTaskPool::addTask(CTask *pTask)
{
	assert (pTask && task(pTask));
	
	CTaskPool *p = pTask->getPool();

	if (!p || p != this) {
		int err = pTask->setPool(this);
	
		if (err != 0) {
			return err;
		}
	}
	
	return pTask->queue();
}


}
