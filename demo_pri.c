#include <stdio.h>

#include "stpool.h"

#ifdef _WIN32
#include <Windows.h>
#pragma comment(lib, "../lib/stpool.lib")
#define msleep Sleep
#define random rand
#else
#define msleep(x) usleep(x * 1000)
#endif

int  task_run(struct sttask_t *tsk)	{
	printf("Run %s\n", tsk->task_name);

	return 0;
}

int  task_complete(struct sttask_t *tsk, long vmflags, int task_code, struct stpriority_t *pri) {
	printf("vmflags:%d task_code:%p\n", vmflags, task_code);
	if (pri && pri->pri)
		printf("pri:%d policy:%d\n", pri->pri, pri->pri_policy);
		
	msleep(1000);

	return 1;
}

int main()
{
	HPOOL hp;
	struct sttask_t  ltask = {
		"low_pri_task", task_run, task_complete, NULL
	};
	
	/* Creat a pool with 1 servering threads */
	hp = stpool_create(20, 20, 1, 10);
	printf("%s\n", stpool_status_print(hp, NULL, 0));
	
	/* Add a task with zero priority */
	stpool_add_task(hp, &ltask);
	
	//stpool_remove_pending_task(hp, &ltask, 0);
	/* Wake up the pool to run the tasks */
	stpool_resume(hp);
	
	getchar();
	/* Disable rescheduling tasks */
	stpool_disable_rescheduling(hp, &ltask);
	
	/* Wait for all tasks' completions */
	stpool_wait(hp, NULL, -1);
	
	/* Release the pool */
	stpool_release(hp);
	
	printf("print any key to exit.\n");
	getchar();
	return 0;
}
