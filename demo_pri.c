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

int  task_run(struct sttask_t *ptsk)	{
	printf("Run %s\n", ptsk->task_name);
	
	return 0;
}

void task_complete(struct sttask_t *ptsk, long vmflags, int task_code) {
	struct schattr_t attr;
	
	/* Acquire the scheduling attribute */
	stpool_task_getschattr(ptsk, &attr);
	
	printf("vmflags:%ld task_code:%p [%s-%d]\n\n\n", 
		vmflags, (void *)task_code, ptsk->task_name, attr.sche_pri);
	
	msleep(1000);
	
	/* Reschedule the task if the task has been done successfully */
	if (STTASK_VMARK_DONE & vmflags) 
		stpool_add_task(ptsk->hp_last_attached, ptsk);	
}

int main()
{
	HPOOL hp;
	struct schattr_t attr[] = {
		{0, 90, STPOLICY_PRI_SORT_INSERTBEFORE},
		{0, 40, STPOLICY_PRI_SORT_INSERTAFTER},
		{0, 10, STPOLICY_PRI_SORT_INSERTAFTER},
		{0, 0,  STPOLICY_PRI_SORT_INSERTAFTER},
	};

	/* Creat a pool with 1 servering threads */
	hp = stpool_create(1, 0, 1, 1);
	printf("%s\n", stpool_status_print(hp, NULL, 0));
		
	/* Add a task with zero priority */
	stpool_add_routine(hp, "hight_task", task_run, task_complete, NULL, &attr[0]);
	stpool_add_routine(hp, "middle_task", task_run, task_complete, NULL, &attr[1]);
	stpool_add_routine(hp, "low_task", task_run, task_complete, NULL, &attr[2]);
	stpool_add_routine(hp, "zero_task", task_run, task_complete, NULL, &attr[3]);
	
	/* Wake up the pool to run the tasks */
	stpool_resume(hp);
	
	getchar();
	
	/* Remove all tasks */
	stpool_remove_pending_task(hp, NULL, 0);

	/* Turn the throttle on */
	stpool_throttle_enable(hp, 1);
			
	/* Wait for all tasks' completions */
	stpool_task_wait(hp, NULL, -1);
	
	/* Release the pool */
	stpool_release(hp);
	
	printf("print any key to exit.\n");
	getchar();
	return 0;
}
