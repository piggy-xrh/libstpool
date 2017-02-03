/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */

#include <stdio.h>

#include "stpool.h"

#if defined(WIN32) || defined(_WIN32) || defined(_WIN64)
#include <windows.h>
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
#include <unistd.h>
#define msleep(ms) usleep(ms * 1000)
#endif

/**
 * (log library)    depends  (task pool library) 
 * libmsglog.lib <-------------libstpool.lib 
 */
void task_run(struct sttask *ptask)	
{
	int e;

	printf("\n\nRun %s\n", ptask->task_name);
	
	/** Sleep for a while to slow down the test */
	msleep(1000);
	
	/** Reschedule the task */
	if ((e = stpool_task_queue(ptask)))
		fprintf(stderr, "***reschedule(%s): '%s'\n",
			ptask->task_name, stpool_strerror(e));
}

void task_err_handler(struct sttask *ptask, long reasons) 
{
	fprintf(stderr, "**ERR: '%s' (%lx)\n",
			ptask->task_name, reasons);
}

int main()
{
	stpool_t *pool;
	struct schattr attr[] = {
		{0, 0,  ep_SCHE_BACK},
		{0, 10, ep_SCHE_BACK},
		{0, 40, ep_SCHE_BACK},
		{0, 90, ep_SCHE_TOP},
	};
	long eCAPs = eCAP_F_SUSPEND|eCAP_F_ROUTINE|eCAP_F_PRIORITY|eCAP_F_THROTTLE|eCAP_F_WAIT_ALL;

	/**
	 * Creat a pool with 1 servering threads 
	 */
	pool = stpool_create("mypool", eCAPs, 1, 0, 1, 1);
	
	/**
	 * Add a few tasks into the pool
	 */
	stpool_add_routine(pool, "zero_task",   task_run, task_err_handler, NULL, &attr[0]);
	stpool_add_routine(pool, "low_task",    task_run, task_err_handler, NULL, &attr[1]);
	stpool_add_routine(pool, "middle_task", task_run, task_err_handler, NULL, &attr[2]);
	stpool_add_routine(pool, "hight_task",  task_run, task_err_handler, NULL, &attr[3]);
	
	puts("Print any key to resume the pool ...\n");
	getchar();
	
	/**
	 * Wake up the pool to run the tasks 
	 */
	stpool_resume(pool);
	puts("Print any key to exit the test ...\n");
	getchar();
	
	/**
	 * Turn the throttle on 
	 */
	stpool_throttle_enable(pool, 1);
	
	/**
	 * Remove all pendings task
	 */
	stpool_remove_all(pool, 0);

	/**
	 * Wait for all tasks' completions 
	 */
	stpool_wait_all(pool, -1);
	puts("All tasks have been removed completely.\n");
	getchar();
	
	/**
	 * Release the pool 
	 */
	stpool_release(pool);
	
	printf("print any key to exit.\n");
	getchar();
	return 0;
}
