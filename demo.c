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

/* Email: piggy_xrh@163.com */

static int g_test_reschedule = 0;

int task_run(struct sttask_t *task) {
	/* TO DO */
	printf("@task_run ...:%d\n", *(int *)task->task_arg);	

	++ *(int *)task->task_arg;
	msleep(20);
	
	return 0;
}

int task_complete(struct sttask_t *task, long vmflags , int task_code, struct stpriority_t *pri) {	
	int reschedule = 0;
	
	/* NOTE:
	 * 	  .If vmflags has been marked with STTASK_VMARK_DONE, it indicates that 
	 * 	 the @task_run has been excuted by the pool.
	 *
	 * 	  .If the task is added by @stpool_add_pri_task(2), @pri will be filled
	 * 	 with the current priority informations of the task.
	 */
	if (!(STTASK_VMARK_DONE & vmflags)) {
		printf("@task_run is not executed: 0x%lx-code:%d\n",
			   vmflags, task_code /* STPOOL_XX */);	
		
		/* NOTE:
		 * 		We can return a non-zero value to reschedule the task again even if
		 * the task is not executed in this time. we just return 0 here.
		 */
		return 0;
	}
	
	/* The task can be delivered into the pool again if user has not called 
	 * @stpool_mark_task/@stpool_disable_rescheduling to mark the task with 
	 * STTASK_VMARK_DISABLE_RESCHEDULE flag.
	 */
	if (!(STTASK_VMARK_DISABLE_RESCHEDULE & vmflags)) {
		if (g_test_reschedule) {
			/* We can adjust the task's priority if the task is added
			 * by @stpool_pri_add_task 
			 */
			if (pri) {
				/* Modify the task's priority attribute here */
				pri->pri = random() % 99;
				pri->pri_policy = pri->pri_policy;
				
				/* Notify the pool that we have modified the schedule policy */
				reschedule = 2;
			} else
				reschedule = 1;	
			
			/* We sleep for a while to slow down the test */
			//msleep(1500);
		}
	}
	
	return reschedule;
}

static int counter = 0;
struct sttask_t task = {
	"test", task_run, task_complete, (void *)&counter,
};


int task_run2(void *arg) {
	static int i=0;

	printf("@task_run2: %d\n", ++i);
	msleep(20);
	return 0;
}

int task_complete2(long vmflags, int task_code, void *arg, struct stpriority_t *pri) {	
	if (!(STTASK_VMARK_DONE & vmflags)) 
		printf("@task_run2 is not executed: 0x%lx-code:%d\n",
			vmflags, task_code);
	
	return g_test_reschedule;
}

int mark_walk(struct stpool_tskstat_t *stat, void *arg) {
	/* If you want to stop walking the task, you should return -1 */
	//return -1;
	
	/* If you just want to walk the tasks, you should return 0 */
	//return 0;

	/* Return the marks */
	return STTASK_VMARK_REMOVE_BYPOOL /* Remove the task */
			|
		   STTASK_VMARK_DISABLE_RESCHEDULE /* Disable rescheduling the task */
		   ;
}

int main() 
{
	int i, error;
	HPOOL hp;
	
	/* NO buffer */
	setbuf(stdout, 0);

	/* Create a pool */
	hp = stpool_create(20, /*limited threads number*/
				       0,  /*number of threads reserved to waiting for tasks*/
				       0,  /*do not suspend the pool */
				       10  /*priority queue num */
					   );
	
	/* Set the sleep time for the threads (10s + random() % 25s)*/
	stpool_set_activetimeo(hp, 10);
	
	/* Print the status of the pool */
	printf("@tpool_create(20, 0, 0, 10)\n%s\n", stpool_status_print(hp, NULL, 0));

	/************************************************************/
	/********************Test @stpool_adjust(_abs)****************/
	/************************************************************/
	printf("\nPress any key to test the @tpool_adjust(abs) ....\n");
	getchar();
	stpool_adjust_abs(hp, 400, 400);
	printf("@tpool_adjust_abs(pool, 300, 1)\n%s\n", stpool_status_print(hp, NULL, 0));
	
	/* We call @stpool_adjust to recover the pool env */
	stpool_adjust(hp, -280, -1);
	stpool_adjust_wait(hp);
	printf("@tpool_adjust(pool, -280, -1)\n%s\n", stpool_status_print(hp, NULL, 0));
	
	/*******************************************************************/
	/********************Test the throttle******************************/
	/*******************************************************************/
	printf("\nPress any key to test the throttle ....\n");
	getchar();
	stpool_throttle_enable(hp, 1);
	error = stpool_add_task(hp, &task);
	if (error)
		printf("***@stpool_add_task error:%d\n", error);
	stpool_throttle_enable(hp, 0);
	
	/*******************************************************************/
	/******************Test the priority********************************/
	/*******************************************************************/
	printf("\nPress any key to test the priority ....\n");
	getchar();
	
	stpool_suspend(hp, 0);
	/* Tasks added by @stpool_add_task have a zero priority, and they will be pushed 
	 * into the lowest priority queue. 
	 */
	stpool_add_task(hp, &task);			
	
	/* @task_run2 will be scheduled prior to the @task_run since the @task_run2 has
	 * a higher priority.
	 */
	stpool_add_pri_routine(hp, task_run2, task_complete2, NULL, 1, STPOLICY_PRI_SORT_INSERTBEFORE);	
	
	/* Wake up the pool to schedule the tasks */
	stpool_resume(hp);

	/* Wait for all tasks' being done completely */
	stpool_wait(hp, NULL, -1);
	
	/******************************************************************/
	/****************Test rescheduling task****************************/
	/******************************************************************/
	printf("\nPress any key to test reschedule task. <then press key to stop testing.>\n");
	getchar();
	g_test_reschedule = 1;
	stpool_add_task(hp, &task);
	stpool_add_routine(hp, task_run2, task_complete2, NULL);
	
	getchar();
	g_test_reschedule = 0;
	stpool_wait(hp, NULL, -1);
	
	/******************************************************************/
	/***************Test running amount of tasks***********************/
	/******************************************************************/
	printf("\nPress any key to add tasks ... <then can press any key to remove them.>\n");
	getchar();
	
	/* We can suspend the pool firstly, and then resume the pool after delivering our
	 * tasks into the pool, It'll be more effecient to do it like that if there are 
	 * a large amount of tasks that will be added into the pool.
	 */
	/* NOTE: We can add the same task into the pool one more times */
	for (i=0; i<1000; i++) {
		stpool_add_task(hp, &task);	
		stpool_add_routine(hp, task_run2, task_complete2, NULL);		
	}
	
	/****************************************************************/
	/*************Test stoping all tasks fastly**********************/
	/****************************************************************/
	printf("\nPress any key to test stoping all tasks fastly.\n");
	getchar();

	/* Remove all pending tasks by calling @stpool_mark_task,
	 * We can also call @stpool_remove_pending_task(2) to reach
	 * our goal, But we call @stpool_mark_task here for showing 
	 * how to use @stpool_mark_task to do the customed works.
	 */
	stpool_mark_task(hp, NULL, mark_walk, hp);

	/* Wait for all tasks' being done */
	stpool_wait(hp, NULL, -1);
	printf("---------------------------tasks have been finished.\n");
	
	/* Release the pool */
	printf("%s\n", stpool_status_print(hp, NULL, 0));
	stpool_release(hp);
	printf("Press any key to exit ...\n");
	getchar();
	
	return 0;
}
