/** @mainpage COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the pool, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <iostream>
#include <stpool/stpool_caps.h>
#include <stpool/stpool.h>
#include <stpool/stpoolc++.h>

using stpool::CTask;
using stpool::CTaskPool;

using std::cout;
using std::cerr;
using std::hex;
using std::endl;

class CDemoTask : public CTask
{
	public:
		CDemoTask(const string &name, CTaskPool *pTaskPool = NULL):
			CTask(name, pTaskPool) {
			cout << __FUNCTION__ << ":" << this << endl;
		}

		~CDemoTask() {
			cout << __FUNCTION__ << ":" << this << endl;
		}

		virtual void onRun() {
			cout << "task('"<< taskName() << "') is running ..."  << endl;
		}

		virtual void onErrHandler(long reasons) {
			cerr << "task('"<< taskName() << "') has not been executed for reasons(" << hex << reasons << ")" << endl;
		}
};

int main()
{	
	// Set the necessary capabilities masks
	long eCaps = eCAP_F_DYNAMIC|eCAP_F_CUSTOM_TASK|eCAP_F_TASK_WAIT;

	CTaskPool *taskPool = CTaskPool::create("mypool", eCaps, 1, 0, 0, false);
	
	// Create a task and set its destination pool to our created pool
	CDemoTask *task  = new CDemoTask("mytask");
	task->setPool(taskPool);
	
	// Schedule the task
	for (int itimes = 0; itimes < 5; itimes ++) {
		task->queue();
		task->wait();
	}
	delete task;
	
	// Release the pool
	taskPool->release();

	return 0;
}
