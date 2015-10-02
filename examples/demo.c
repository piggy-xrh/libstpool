/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */

#include <stdio.h>
#include "stpool.h"

#ifdef _WIN

#include <Windows.h>
#define msleep Sleep
#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "../x64/Debug/libmsglog.lib")
#pragma comment(lib, "../x64/Debug/libstpool.lib")
#else
#pragma comment(lib, "../Debug/libmsglog.lib")
#pragma comment(lib, "../Debug/libstpool.lib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "../x64/Release/libmsglog.lib")
#pragma comment(lib, "../x64/Release/libstpool.lib")
#else
#pragma comment(lib, "../Release/libmsglog.lib")
#pragma comment(lib, "../Release/libstpool.lib")
#endif
#endif

#else
#include <stdint.h>
#include <unistd.h>
#define msleep(x) usleep(x * 1000)
#endif

/* (log library)    depends  (task pool library) 
 * libmsglog.lib <-------------libstpool.lib 
*/

int task_run(struct sttask_t *ptsk) {
	/* TO DO */
	printf("@task_run ...:%d\n", *(int *)ptsk->task_arg);	

	++ *(int *)ptsk->task_arg;
	
	/* We sleep for a while to slow down the test */
	if (stpool_task_get_userflags(ptsk)) 
		msleep(1500);
	
	return 0;
}

void task_complete(struct sttask_t *ptsk, long vmflags , int task_code) {		
	/* NOTE:
	 * 	   If vmflags has been marked with STTASK_VMARK_DONE, it indicates that 
	 * 	 the @task_run has been excuted by the pool.
	 */
	if (!(STTASK_VMARK_DONE & vmflags)) {
		
		printf("@task_run is not executed: 0x%lx-code:%d\n",
			   vmflags, task_code /* STPOOL_XX */);	
		return;
	}
		
	if (stpool_task_get_userflags(ptsk)) {
		struct schattr_t attr;
		
		/* We adjust the task's priority */
		stpool_task_getschattr(ptsk, &attr);
		if (!attr.sche_pri) {
			attr.permanent = 1;
			attr.sche_pri  = 80;
			stpool_task_setschattr(ptsk, &attr);
		} 
			
		/* Reschedule the task */
		stpool_add_task(ptsk->hp, ptsk);
	}	
}

int task_run2(struct sttask_t *ptsk) {
	static int i=0;

	printf("@task_run2: %d\n", ++i);
	return 0;
}

void task_complete2(struct sttask_t *ptsk, long vmflags, int task_code) {	
	/* NOTE:
	 * 	   If vmflags has been marked with STTASK_VMARK_DONE, it indicates that 
	 * 	 the @task_run has been excuted by the pool.
	 */
	if (!(STTASK_VMARK_DONE & vmflags)) {
		
		printf("@task_run2 is not executed: 0x%lx-code:%d\n",
			vmflags, task_code /* STPOOL_XX */);
		return;
	}
}

long mark_walk(struct stpool_tskstat_t *stat, void *arg) {
	struct sttask_t *ptsk = stat->task;
	
	(void)ptsk;

	/* If you want to stop walking the task, you should return -1 */
	//return -1;
	
	/* If you just want to walk the tasks, you should return 0 */
	//return 0;

	/* Return the marks */
	return STTASK_VMARK_DISABLE_QUEUE |  /* Disable rescheduling */
		   STTASK_VMARK_REMOVE;          /* Remove the task */

}

int main() 
{
	HPOOL hp;
	int i, error, c = 0;
	struct schattr_t attr = {0, 1, STP_SCHE_TOP};	
	struct sttask_t *ptsk;
	
	/* NO buffer */
	setbuf(stdout, 0);
	
	/* Create a pool */
	hp = stpool_create(20, /*limited threads number*/
				       0,  /*number of threads reserved to waiting for tasks*/
				       0,  /*do not suspend the pool */
				       1   /*priority queue num */
					   );
	
	/* Set the sleep time for the threads (10s + random() % 20s)*/
	stpool_set_activetimeo(hp, 10, 20);
	
	/* Print the status of the pool */
	printf("@tpool_create(20, 0, 0, 10)\n%s\n", stpool_status_print(hp, NULL, 0));
		
	/************************************************************/
	/********************Test @stpool_adjust(_abs)****************/
	/************************************************************/
	printf("\nPress any key to test the @tpool_adjust(300, 4) ....\n");
	getchar();
	stpool_adjust_abs(hp, 300, 4);
	printf("@tpool_adjust_abs(pool, 300, 4)\n%s\n", stpool_status_print(hp, NULL, 0));
	
	/* We call @stpool_adjust to recover the pool env */
	printf("\nPress any key to test the @tpool_adjust(-280, -4) ....\n");
	getchar();
	stpool_adjust(hp, -280, -4);
	stpool_adjust_wait(hp);
	printf("@tpool_adjust(pool, -280, -4)\n%s\n", stpool_status_print(hp, NULL, 0));
	
	printf("\n\nPress any key to test the @tpool_flush ....\n");
	getchar();
	stpool_flush(hp);
	stpool_adjust_wait(hp);
	printf("@tpool_flush\n%s\n", stpool_status_print(hp, NULL, 0));

	/*******************************************************************/
	/********************Test the throttle******************************/
	/*******************************************************************/
	printf("\nPress any key to test the throttle ....\n");
	getchar();
	/* Turn the throttle on */
	stpool_throttle_enable(hp, 1);
	ptsk = stpool_task_new("test", task_run, task_complete, &c);
	error = stpool_add_task(hp, ptsk);
	if (error)
		printf("***@stpool_add_task error:%d\n", error);
	/* Turn the throttle off */
	stpool_throttle_enable(hp, 0);

	/*******************************************************************/
	/******************Test the priority********************************/
	/*******************************************************************/
	printf("\nPress any key to test the priority ....\n");
	getchar();
	
	stpool_suspend(hp, 0);
	/* Add a task with zero priority, and the task will be pushed into the 
	 * lowest priority queue. 
	 */
	stpool_add_routine(hp, "test", task_run, task_complete, &c, NULL);
		
	/* @task_run2 will be scheduled prior to the @task_run since the @task_run2 has
	 * a higher priority.
	 */
	stpool_add_routine(hp, "routine", task_run2, task_complete2, NULL, &attr); 
	
	/* Wake up the pool to schedule the tasks */
	stpool_resume(hp);

	/* Wait for all tasks' being done completely */
	stpool_task_wait(hp, NULL, -1);
	
	/******************************************************************/
	/****************Test rescheduling task****************************/
	/******************************************************************/
	printf("\nPress any key to test rescheduling task. <then press key to stop testing.>\n");
	getchar();
	stpool_task_set_userflags(ptsk, 0x1);
	stpool_add_task(hp, ptsk);
	
	getchar();
	stpool_task_set_userflags(ptsk, 0);
	stpool_task_wait(hp, NULL, -1);

	/******************************************************************/
	/***************Test running amount of tasks***********************/
	/******************************************************************/
	printf("\nPress any key to add tasks ... <then can press any key to remove them.>\n");
	getchar();
	
	/* We can suspend the pool firstly, and then resume the pool after delivering our
	 * tasks into the pool, It'll be more effecient to do it like that if there are 
	 * a large amount of tasks that will be added into the pool.
	 */
	for (i=0; i<4000; i++) {
		stpool_add_routine(hp, "test", task_run, task_complete, &c, NULL);
		stpool_add_routine(hp, "routine", task_run2, task_complete2, NULL, NULL);
	}
	
	/****************************************************************/
	/*************Test stoping all tasks fastly**********************/
	/****************************************************************/
	printf("\nPress any key to test stoping all tasks fastly.\n");
	getchar();

	/* Remove all pending tasks by calling @stpool_mark_task_cb,
	 * We can also call @stpool_remove_pending_task to reach our 
	 * goal, But we call @stpool_mark_task_cb here for showing 
	 * how to use @stpool_mark_task_cb to do the customed works.
	 */
#if 1
	stpool_mark_task_cb(hp, mark_walk, NULL);
#else
	stpool_mark_task(hp, NULL, STTASK_VMARK_DISABLE_QUEUE|STTASK_VMARK_REMOVE);
#endif
	/* Wait for all tasks' being done */
	stpool_task_wait(hp, NULL, -1);
	printf("---------------------------tasks have been finished.\n");
			
	/* Release the pool */
	printf("%s\n", stpool_status_print(hp, NULL, 0));
	stpool_release(hp);
	printf("Press any key to exit ...\n");
	getchar();
	
	return 0;
}


