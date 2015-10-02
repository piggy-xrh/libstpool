/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */
#include <iostream>
using namespace std;

#include "CTaskPool.h"

#ifdef _WIN
#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libstpoolc++.lib")
#else
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libstpoolc++.lib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "../../../lib/Release/x86_64_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Release/x86_64_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Release/x86_64_win/libstpoolc++.lib")
#else
#pragma comment(lib, "../../../lib/Release/x86_32_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Release/x86_32_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Release/x86_32_win/libstpoolc++.lib")
#endif
#endif
#endif

/* (log library)    depends  (task pool library)   depends    (task pool library for c++)
 * libmsglog.lib <-------------libstpool.lib <--------------------libstpoolc++.lib
*/
class myTask:public CTask
{
	public:
		/* We can allocate a block manually for the proxy object.
		 * and we can retreive its address by @getProxy()
		 */
		myTask(): CTask(/*new char[getProxySize()]*/NULL,  "mytask") {}
		~myTask() 
		{
			/* NOTE: We are responsible for releasing the proxy object if
			 * the parameter @cproxy passed to CTask is NULL */
			if (isProxyCreatedBySystem()) 
				freeProxy(getProxy());
			
			else
				delete [] reinterpret_cast<char *>(getProxy());
		}

	private:
		virtual int onTask()
		{
			cout << taskName() << ": onTask.\n";
			return 0;
		}

		virtual void  onTaskComplete(long sm, int errCode) 
		{
			if (CTask::sm_DONE & sm)
				cout << taskName() << " has been done with code:" << dec << errCode 
					 <<"  stat:0x" << hex << stat() << " sm:0x" << sm << endl;
				   
			else
				cerr << taskName() << " has not been done. reason:" << dec << errCode 
					 <<"  stat:0x" << hex << stat() << " sm:0x" << sm << endl;
			
			static int slTimes = 0;	
			/* 		We reschedule the task again.
			 * NOTE:
			 * 		task->wait() will not return until the task
			 *  does not exist in both the pending pool and the
			 *  scheduling queue.
			 */
			if (++ slTimes < 5) 
				queue();	

			/* The task will be marked with @sm_ONCE_AGAIN if user calls
			 * @queue to reschedule it while it is being scheduled. and
			 * @sm_ONCE_AGAIN will be removed by the pool after it having
			 * been delived into the pool. */
			cout << dec << slTimes << "  sm:0x" << hex << this->sm() << endl << endl;
		}
};

int main()
{
	/* Create a pool instance with 1 servering thread */
	CTaskPool *pool = CTaskPool::createInstance(1, 0, false);	
	
	/* Test running the task */
	myTask *task = new myTask;
	
	/* Set the task's parent before our's calling @queue */
	task->setParent(pool);
	
	/* Deliver the task into the pool */
	task->queue();	
	
	/* Wait for the task's being done */
	task->wait(); 
	
	cout << "\ntask has been done !" << endl;

	/* Free the task object */
	delete task;
	
	/* Shut down the pool */
	pool->release();
	
	cin.get();
	return 0;
}
