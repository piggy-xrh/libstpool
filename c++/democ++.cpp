/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */
#include <iostream>
using namespace std;

#include "CTaskPool.h"

class myTask:public CPoolTask<myTask>
{
	public:
		myTask(CTaskPool *p, bool autoFree = false): 
			CPoolTask<myTask>(p, "mytask"), m_autoFree(autoFree) {}
		~myTask() {}

	private:
		virtual int onTask()
		{
			cout << "onTask.\n";
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
	
			/* If we want to free the task in the completion routine,
			 * we should call @detach to tell the pool to remove the 
			 * task from the pool before executing its completion routine
			 * completely */
			if (m_autoFree) {
				getParent()->detach(this);
				delete this;
			}
		}
	private:
		bool m_autoFree;
};

int main()
{
	/* Create a instance */
	CTaskPool *pool = CTaskPool::createInstance(5, 0, false);
		
	/* Print the status */
	std::string s;
	cout << pool->stat(s) << endl;

	/* Test running the task */
	myTask *task = new myTask(pool);
	task->queue();
	task->wait();
	delete task;
	
	/* Test running amount of tasks */
	pool->suspend();
	for (int i=0; i<1000; i++) {
		task = new myTask(pool, true);
		task->queue();		
	}
	pool->resume();

	/* Wait for all tasks' being done */
	pool->wait();

	cin.get();
	/* Shutdown the pool */
	pool->release();
	
	/* Look up the pool */
	cout << CMPool::report(s) << endl;
	cout << "\nafter FLUSH.\n" << endl;
	CMPool::flush();
	cout << CMPool::report(s) << endl;

	cin.get();
	return 0;
}
