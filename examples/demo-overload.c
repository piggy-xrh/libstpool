/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */

#include <stdio.h>
#include <stdlib.h>

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

void task_run(struct sttask *ptask)
{
	printf("'%s(%p/%d)' is running ... \n", ptask->task_name, ptask, (int)ptask->task_arg);
}

void task_err_handler(struct sttask *ptask, long reasons)
{
	fprintf(stderr, "'%s(%p/%d)' handler error:0x%lx\n", ptask->task_name, ptask, (int)ptask->task_arg, reasons);
}

int main()
{
	int res;
	int itimes;
	long eCAPs;
	struct oaattr attr;
	stpool_t *pool;
	
	/**
	 * We want a pool that supports the overload policy
	 */
	eCAPs = eCAP_F_ROUTINE|eCAP_F_SUSPEND|eCAP_F_WAIT_ALL|eCAP_F_OVERLOAD;
	pool  = stpool_create("mypool", eCAPs, 1, 0, 1, 0);
	if (!pool) {
		fprintf(stderr, "Library does not support the capiblities(0x%lx) currently.\n", eCAPs);
		abort();
	}
	
	/**
	 *  Set the task threshold to be 3
	 */
	attr.task_threshold = 3;
	attr.eoa = eOA_drain;
	stpool_set_overload_attr(pool, &attr);
	
	/**
	 * Add a few tasks into the pool
	 */
	for (itimes=0; itimes<= 20; itimes++) {
		if ((res = stpool_add_routine(pool, "task", task_run, task_err_handler, (void *)itimes, NULL))) {
			fprintf(stderr, "Error: %s\n", stpool_strerror(res));
		}
	}
	
	/**
	 * Resume the pool to schedule the pending tasks
	 */
	puts("press any key to resume the pool ...\n");
	getchar();
	stpool_resume(pool);
	stpool_wait_all(pool, -1);
	
	/**
	 * Release the pool after its have done all of the tasks
	 */
	puts("press any key to release the pool ...\n");
	getchar();
	stpool_release(pool);

	puts("press any key to exit ...\n");
	getchar();
	return 0;
}
