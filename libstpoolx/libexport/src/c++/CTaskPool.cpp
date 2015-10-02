
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


#include <string.h>
#include "CTaskPool.h"
#include "stpool.h"

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
	if (!cproxy) {
		/* Create a proxy object from the default heap */
		m_proxy = stpool_task_new(taskName, doTaskWrapper,
						onTaskCompleteWrapper, this);
		if (!m_proxy)
			throw std::bad_alloc();
		
		/* Mark the proxy object */
		stpool_task_set_userflags(getProxy(), 0x40);
	} else
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

void  CTask::freeProxy(struct sttask_t *proxy) {
	stpool_task_delete(proxy);
}

const char *CTask::taskName() const
{
	return getProxy()->task_name;
}

bool CTask::isProxyCreatedBySystem()
{
	return 0x40 & stpool_task_get_userflags(getProxy());
}

long CTask::getUserFlags()
{
	return stpool_task_get_userflags(getProxy()) & 0x3f;
}

long CTask::setUserFlags(long uflags)
{
	long lflags = stpool_task_get_userflags(getProxy());
	
	uflags |= (lflags & 0x40);
	return stpool_task_set_userflags(getProxy(), uflags);
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

bool CTask::isFreeNow() 
{
	return stpool_task_is_free(getProxy());
}

const char *CTaskPool::version() {
	return stpool_version();
}

CTaskPool *CTaskPool::createInstance2(const char *desc, int maxThreads, 
	int minThreads, bool bSuspend, int priQNum)
{
	CTaskPool *p = new(std::nothrow) CTaskPool;
	
	if (p) {
		p->m_proxyHandle = stpool_create2(desc, maxThreads, minThreads, bSuspend, priQNum);
		if (!p->m_proxyHandle) {
			delete p;
			p = NULL;
		}
	}

	return p;
}

const char *CTaskPool::desc()
{
	return stpool_desc(m_proxyHandle);
}

void CTaskPool::setThreadAttr(const struct threadAttr &attr)
{
	assert(sizeof(attr) == sizeof(stpool_thattr_t));

	stpool_thread_setscheattr(m_proxyHandle, 
		reinterpret_cast<struct stpool_thattr_t *>
		(
			const_cast<struct threadAttr *>(&attr)
		));	
}

struct threadAttr &CTaskPool::getThreadAttr(struct threadAttr &attr)
{
	assert(sizeof(attr) == sizeof(stpool_thattr_t));
	
	stpool_thread_setscheattr(m_proxyHandle, 
		reinterpret_cast<struct stpool_thattr_t *>(&attr));

	return attr;
}

long CTaskPool::addRef()
{
	return stpool_addref(m_proxyHandle);
}

long CTaskPool::release()
{
	return stpool_release(m_proxyHandle);
}

void CTaskPool::setActiveTimeo(long actTimeo, long randTimeo)
{
	stpool_set_activetimeo(m_proxyHandle, actTimeo, randTimeo);
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
	  deliver it into the task pool */
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

long CTaskPool::mark(CTask *task, long smFlags)
{
	if (task && this != task->getParent())
		return ep_PARENT;

	return stpool_mark_task(m_proxyHandle, task ? task->getProxy() : NULL, smFlags);
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
				stpool_status_wait(m_proxyHandle, nMaxPendingTasks, ms)
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

std::string& CTaskPool::stat(std::string &s)
{
	char statBuffer[1024 * 2] = {0};
	
	s.append(stpool_status_print(m_proxyHandle, statBuffer, sizeof(statBuffer) -1));
	return s;
}

long CTaskPool::taskStat(CTask *task, long &sm)
{
	struct stpool_tskstat_t stat = {0};
	
	if (task->getParent() == this) {
		stat.task = task->getProxy();
		stpool_gettskstat(m_proxyHandle, &stat);
	}
	sm = stat.vmflags;
		
	return stat.stat;
}

void CTaskPool::suspend(bool bWaitBeingScheduledTasks)
{
	stpool_suspend(m_proxyHandle, bWaitBeingScheduledTasks ? 1 : 0);
}

void CTaskPool::resume()
{
	stpool_resume(m_proxyHandle);
}

void CTaskPool::enableQueue(bool enable)
{
	stpool_throttle_enable(m_proxyHandle, enable ? 0 : 1);
}

int CTaskPool::enableQueueOnTask(CTask *task, bool enable)
{
	long lflags = enable ? STTASK_VMARK_ENABLE_QUEUE : STTASK_VMARK_DISABLE_QUEUE;
	
	if (task && task->getParent() != this)
		return ep_PARENT;

	stpool_mark_task(m_proxyHandle, task ? task->getProxy() : NULL, lflags);
	return ep_OK;
}

int  CTaskPool::extractErr(int libErr)
{
	static struct {
		int libErr;
		int epErr;
	} epErrTable [] = {
		{0, ep_OK},
		{STPOOL_ERR_NOMEM, ep_NOMEM},
		{STPOOL_ERR_DESTROYING, ep_DESTROYING},
		{STPOOL_ERR_THROTTLE, ep_ENJECT},
		{STPOOL_TASK_ERR_DISABLE_QUEUE, ep_ENJECT},
		{STPOOL_TASK_ERR_REMOVED, ep_REMOVED},
		{STPOOL_TASK_ERR_BUSY, ep_PARENT},
	};
	
	for (unsigned i=0; i<sizeof(epErrTable)/sizeof(*epErrTable); i++)
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
