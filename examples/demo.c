/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */

#include <stdio.h>
#include <stdlib.h>
#include "stpool.h"

#if defined(WIN32) || defined(_WIN32) || defined(_WIN64)
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

void task_run(struct sttask *ptsk) {
	/** TO DO */
	printf("%s ...:%d\n", ptsk->task_name, *(int *)ptsk->task_arg);	

	++ *(int *)ptsk->task_arg;
}

void task_err_handler(struct sttask *ptsk, long reasons) {		
	
	printf("***Err: %s has not been executed: 0x%lx\n",
			ptsk->task_name, reasons);
}

void task_reschedule(struct sttask *ptsk) {
	int e;

	/** TO DO */
	printf("%s ...:%d\n", ptsk->task_name, *(int *)ptsk->task_arg);	

	/** We sleep for a while to slow down the test */
	if (stpool_task_get_userflags(ptsk)) {
		msleep(1500);
		
		/** Reschedule the task */
		if ((e = stpool_task_queue(ptsk)))
			printf("***Err: queue(%d)\n", e);
	}
}

int main() 
{
	stpool_t *pool;
	int i, error, c = 0;
	struct schattr attr = {0, 1, ep_SCHE_TOP};	
	struct sttask *ptsk;
	
	long eCAPs = eCAP_F_DYNAMIC|eCAP_F_SUSPEND|eCAP_F_THROTTLE|eCAP_F_ROUTINE|
			eCAP_F_DISABLEQ|eCAP_F_PRIORITY|eCAP_F_WAIT_ALL;
	
	/** NO buffer */
	setbuf(stdout, 0);
	
	/** Create a pool */
	pool = stpool_create("mypool", /** pool name */
						 eCAPs,    /** neccessary capabilites */
	                     20,	   /** limited threads number*/
				          0,	   /** number of threads reserved to waiting for tasks*/
				          0,	   /** do not suspend the pool */
				          1		   /** priority queue num */
					   );
	if (!pool)
		abort();
	
	/** Print the status of the pool */
	printf("@stpool_create(20, 0, 0, 10)\n%s\n", stpool_stat_print(pool));
		
	/*------------------------------------------------------------/
	/--------------------Test @stpool_adjust(_abs)----------------/
	/------------------------------------------------------------*/
	printf("\nPress any key to test the @stpool_adjust(300, 4) ....\n");
	getchar();
	stpool_adjust_abs(pool, 300, 4);
	printf("@stpool_adjust_abs(pool, 300, 4)\n%s\n", stpool_stat_print(pool));
	
	/** We call @stpool_adjust to recover the pool env */
	printf("\nPress any key to test the @stpool_adjust(-280, -4) ....\n");
	getchar();
	stpool_adjust(pool, -280, -4);
	printf("@stpool_adjust(pool, -280, -4)\n%s\n", stpool_stat_print(pool));
	
	/*------------------------------------------------------------------/
	/----------------Test rescheduling task----------------------------/
	/------------------------------------------------------------------*/
	printf("\nPress any key to test rescheduling task. <then press key to stop testing.>\n");
	getchar();
	
	/** We creat a customed task to do the test */
	ptsk = stpool_task_new(NULL, "test-reschedule", task_reschedule, NULL, &c);
	
	/** Attach the destinational pool */
	error = stpool_task_set_p(ptsk, pool);
	if (error)
		printf("***Err: %d(%s). (try eCAP_F_CUSTOM_TASK)\n", error, stpool_strerror(error));
	else {
		stpool_task_set_userflags(ptsk, 0x1);
		stpool_task_queue(ptsk);
		
		getchar();
		/** Set the flag to notify the task to exit the rescheduling test */
		stpool_task_set_userflags(ptsk, 0);
		stpool_task_wait(ptsk, -1);
	}
	stpool_task_delete(ptsk);
	
	/*-------------------------------------------------------------------/
	/--------------------Test the throttle------------------------------/
	/-------------------------------------------------------------------*/
	printf("\nPress any key to test the throttle ....\n");
	getchar();
	
	/** Turn the throttle on */
	stpool_throttle_enable(pool, 1);
	error = stpool_add_routine(pool, "test-throttle", task_run, task_err_handler, &c, NULL);
	if (error)
		printf("***Err: @stpool_add_task: %d\n", error);
	/** Turn the throttle off */
	stpool_throttle_enable(pool, 0);
	
	/*-------------------------------------------------------------------/
	/------------------Test the priority--------------------------------/
	/-------------------------------------------------------------------*/
	printf("\nPress any key to test the priority ....\n");
	getchar();
	
	stpool_suspend(pool, 0);
	/**
	 * Add a task with zero priority, and the task will be pushed into the 
	 * lowest priority queue. 
	 */
	stpool_add_routine(pool, "zero-priority", task_run, task_err_handler, &c, NULL);
		
	/**
	 * task("non-zero-priority") will be scheduled prior to the task("zero-priority") 
	 * since it has a higher priority.
	 */
	stpool_add_routine(pool, "non-zero-priority", task_run, task_err_handler, &c, &attr); 
	
	/** Wake up the pool to schedule the tasks */
	stpool_resume(pool);

	/** Wait for all tasks' being done completely */
	stpool_wait_all(pool, -1);
	
	/*------------------------------------------------------------------/
	/---------------Test running amount of tasks-----------------------/
	/------------------------------------------------------------------*/
	printf("\nPress any key to add tasks ... <then can press any key to remove them.>\n");
	getchar();
	
	/**
	 * We can suspend the pool firstly, and then resume the pool after delivering our
	 * tasks into the pool, It'll be more effecient to do it like that if there are 
	 * a large amount of tasks that will be added into the pool.
	 */
	for (i=0; i<8000; i++) 
		stpool_add_routine(pool, "task_run", task_run, task_err_handler, &c, NULL);
	
	/*----------------------------------------------------------------/
	/-------------Test stoping all tasks fastly----------------------/
	/----------------------------------------------------------------*/
	printf("\nPress any key to test stoping all tasks fastly.\n");
	getchar();

	stpool_throttle_enable(pool, 1);
	stpool_remove_all(pool, 1);
	
	/** Wait for all tasks' being done */
	stpool_wait_all(pool, -1);
	printf("---------------------------tasks have been finished.\n");
	
	
	/*---------------------------------------------------------------/
	/-------------Release the pool-----------------------------------/
	/---------------------------------------------------------------*/
	printf("Press any key to release the pool...\n");
	getchar();
	
	/** Release the pool */
	printf("%s\n", stpool_stat_print(pool));
	stpool_release(pool);
	
	printf("Press any key to exit ...\n");
	getchar();
	
	return 0;
}


