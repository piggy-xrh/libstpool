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


#include <string.h>
#include "CTaskPool.h"

extern "C"
{
#include "stpool.h"
}

char externPoolAllocatorName[] = "taskPool";

static int doTaskWrapper(struct sttask_t *task)
{
	return reinterpret_cast<CTask *>
				(task->task_arg)->onTask();
}

static void onTaskCompleteWrapper(struct sttask_t *task, long stat, int errCode)
{
	reinterpret_cast<CTask *>
			(task->task_arg)->onTaskComplete(stat, errCode);
}

CTask::CTask(void *cproxy, const char *taskName, CTaskPool *p):
	m_proxy(reinterpret_cast<struct sttask_t *>(cproxy)), m_parent(p)
{	
	/* Initialize the proxy object */
	stpool_task_init(getProxy(), taskName, doTaskWrapper,
			onTaskCompleteWrapper, this);
}

CTask::~CTask()
{
}

size_t CTask::getProxySize() 
{
	return stpool_task_size();
}

const char *CTask::taskName() const
{
	return getProxy()->task_name;
}

void CTask::setAttr(const attr& at)
{
	struct schattr_t impl = {
		at.permanent ? 1 : 0,
		at.schePriority,
		(attr::scheP_TOP == at.schePolicy) ? STP_SCHE_TOP : STP_SCHE_BACK
	};
	
	/* Set the scheduling attribute */
	stpool_task_setschattr(getProxy(), &impl);
}

CTask::attr& CTask::getAttr(CTask::attr &at)
{
	struct schattr_t impl;
	
	/* Obtain the attribute */
	stpool_task_getschattr(getProxy(), &impl);
	
	/* Convert the attribute */
	at.permanent = impl.permanent;
	at.schePriority = impl.sche_pri;
	at.schePolicy = (STP_SCHE_TOP == impl.sche_pri_policy) ?
		attr::scheP_TOP : attr::scheP_BACK;

	return at;
}

CTaskPool *CTaskPool::createInstance(int maxThreads, int minThreads, bool bSuspend, int priQNum)
{
	CTaskPool *p = new(std::nothrow) CTaskPool;
	
	if (p) {
		p->m_proxyHandle = stpool_create(maxThreads, minThreads, bSuspend, priQNum);
		if (!p->m_proxyHandle) {
			delete p;
			p = NULL;
		}
	}

	return p;
}

long CTaskPool::addRef()
{
	return stpool_addref(m_proxyHandle);
}

long CTaskPool::release()
{
	return stpool_release(m_proxyHandle);
}

void CTaskPool::setActiveTimeo(long actTimeo)
{
	stpool_set_activetimeo(m_proxyHandle, actTimeo);
}

void CTaskPool::adjustAbs(int maxThreads, int minThreads)
{
	stpool_adjust_abs(m_proxyHandle, maxThreads, minThreads);
}

void CTaskPool::adjust(int maxThreads, int minThreads)
{
	stpool_adjust(m_proxyHandle, maxThreads, minThreads);
}

int CTaskPool::queue(CTask *task) 
{	
	/* Check the owner */
	if (task->getParent() != this)
		return ep_PARENT;
	
	/* We try to extract the proxy object and
	  Deliver it the task pool */
	return extractErr(
			stpool_add_task(m_proxyHandle, task->getProxy())
			);
}

void CTaskPool::detach(CTask *task)
{
	if (task->getParent() == this)
		stpool_detach_task(m_proxyHandle, task->getProxy());
}

int CTaskPool::remove(CTask *task)
{	
	if (this != task->getParent()) 
		return ep_PARENT;
	
	stpool_remove_pending_task(m_proxyHandle, task->getProxy(), 1);
	return ep_OK;
}

int CTaskPool::removeAll(bool dispatchedByPool)
{
	return stpool_remove_pending_task(m_proxyHandle, NULL, dispatchedByPool ? 1 : 0);
}

long CTaskPool::getThreadID()
{
	return stpool_wkid();
}

void CTaskPool::wakeup(long threadID)
{
	stpool_wakeup(m_proxyHandle, threadID);
}
		

int CTaskPool::wait(CTask *task, long ms)
{
	if (task && this != task->getParent()) 
		return ep_PARENT;

	return extractErr1(
				stpool_task_wait(m_proxyHandle, task ? task->getProxy() : NULL, ms)
				);
}

int  CTaskPool::waitAll(const std::list<CTask *> &sets, long ms)
{
	struct sttask_t *stack[10] = {0}, **entry = stack;
	
	if (sets.size() > 10) 
		entry = new(std::nothrow) struct sttask_t *[sets.size()];
	if (!entry)
		return ep_NOMEM;

	std::list<CTask *>::const_iterator it = sets.begin(), end = sets.end();
	for (int i=0; it != end; ++ it, ++ i)
		entry[i] = (*it)->getProxy();
	
	int err = stpool_task_wait2(m_proxyHandle, entry, sets.size(), ms);
	if (entry != stack)
		delete [] entry;
	
	return extractErr1(err);
}

int  CTaskPool::waitAny(const std::list<CTask *> &sets, long ms)
{
	struct sttask_t *stack[10] = {0}, **entry = stack;
	
	if (sets.size() > 10) 
		entry = new(std::nothrow) struct sttask_t *[sets.size()];
	if (!entry)
		return ep_NOMEM;

	std::list<CTask *>::const_iterator it = sets.begin(), end = sets.end();
	for (int i=0; it != end; ++ it, ++ i)
		entry[i] = (*it)->getProxy();
	
	int pre = 0, err = stpool_task_any_wait(m_proxyHandle, entry, sets.size(), &pre, ms);
	if (entry != stack)
		delete [] entry;
	
	return extractErr1(err);

}

int  CTaskPool::waitStat(long nMaxPendingTasks, long ms)
{
	return extractErr1(
				stpool_pending_leq_wait(m_proxyHandle, nMaxPendingTasks, ms)
				);
}

int  CTaskPool::waitQueueEnabled(long ms)
{
	return extractErr1(
				stpool_throttle_wait(m_proxyHandle, ms)
				);
}

TaskPoolStat CTaskPool::stat()
{
	TaskPoolStat st = {0};
	
	stpool_getstat(m_proxyHandle, reinterpret_cast<struct stpool_stat_t *>(&st));
	return st;
}

const std::string& CTaskPool::stat(std::string &st)
{
	char statBuffer[1024 * 4] = {0};
	
	return st.assign(stpool_status_print(m_proxyHandle, statBuffer, sizeof(statBuffer) -1));
}

long CTaskPool::taskStat(CTask *task, long &sm)
{
	struct stpool_tskstat_t stat = {0};
	
	stat.task = task->getProxy();
	stpool_gettskstat(m_proxyHandle, &stat);
	sm = stat.vmflags;
		
	return stat.stat;
}

void CTaskPool::suspend(bool bWaitSchedulingTasks)
{
	stpool_suspend(m_proxyHandle, bWaitSchedulingTasks ? 1 : 0);
}

void CTaskPool::resume()
{
	stpool_resume(m_proxyHandle);
}

void CTaskPool::enableQueue(bool enable)
{
	stpool_throttle_enable(m_proxyHandle, enable ? 1 : 0);
}


int  CTaskPool::extractErr(int libErr)
{
	static struct {
		int libErr;
		int epErr;
	} epErrTable [] = {
		{STPOOL_ERR_NOMEM, ep_NOMEM},
		{STPOOL_ERR_DESTROYING, ep_DESTROYING},
		{STPOOL_ERR_THROTTLE, ep_ENJECT},
		{STPOOL_TASK_ERR_REMOVED, ep_REMOVED},
		{STPOOL_TASK_ERR_BUSY, ep_PARENT},
	};

	for (int i=0; i<sizeof(epErrTable)/sizeof(*epErrTable); i++)
		if (epErrTable[i].libErr == libErr)
			return epErrTable[i].epErr;

	return ep_OTHER;
}

int CTaskPool::extractErr1(int waitErr)
{
	int epErr;

	switch (waitErr) {
	case 0:
		epErr = ep_OK;
		break;
	case 1:
		epErr = ep_TIMEOUT;
		break;
	case -1:
		epErr = ep_WOKEUP;
		break;
	default:
		epErr = ep_OTHER;
	}

	return epErr;
}
