/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */
#include <iostream>
using namespace std;

#include "CTaskPool.h"

class myTask:public CPoolTask<16 /*sizeof(myTask)*/>
{
	public:
		myTask(CTaskPool *p, bool autoFree = false): 
			CPoolTask<16>(p, "mytask"), m_autoFree(autoFree) {}
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
			 * task from the pool before doing its completion routine
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
	for (int i=0; i<100; i++) {
		task = new myTask(pool, true);
		task->queue();		
	}

	/* Wait for all tasks' being done */
	pool->wait(NULL, -1);

	cin.get();
	/* Shutdown the pool */
	pool->release();
	
	cin.get();
	return 0;
}
