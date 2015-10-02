/* COPYRIGHT (C) 2014 - 2020, piggy_xrh */

#include <stdio.h>
#include <stdlib.h>
#include "stpool.h"

#ifdef _WIN

#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libstpool.lib")
#else
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libstpool.lib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "../../../lib/Release/x86_64_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Release/x86_64_win/libstpool.lib")
#else
#pragma comment(lib, "../../../lib/Release/x86_32_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Release/x86_32_win/libstpool.lib")
#endif
#endif

#else
#include <stdint.h>
#include <sys/time.h>
#endif

/* (log library)    depends  (task pool library) 
 * libmsglog.lib <-------------libstpool.lib 
*/
static void do_work(int *val) {
	*val += 100;
	*val *= 0.371;
}

int  task_run(struct sttask_t *ptsk) {
	size_t i, j, sed = 20;
	
	for (i=0; i<sed; i++)
		for (j=0; j<sed; j++)
			do_work((int *)ptsk->task_arg);
	
	/* Do not call @printf in the test since it will waste our 
	 * so much time on competing the IO.
	 */
	return 0;
}

void task_complete(struct sttask_t *ptsk, long vmflags, int code) {
}

int main()
{
	time_t now;
	int i, c, times, j=0;
	int sum, *arg;
	HPOOL hp;
		
	/* Creat a task pool */
	hp = stpool_create(50,  /* max servering threads */
			           0,   /* 0 servering threads that reserved for waiting for tasks */
			           1,   /* suspend the pool */
			           0);  /* default number of priority queue */
	printf("%s\n", stpool_status_print(hp, NULL, 0));
	
	/* Add tasks */
	times = 90000;
	arg = (int *)malloc(times * sizeof(int));
	for (i=0; i<times; i++) {
		/* It may take a long time to load a large amount of tasks 
		 * if the program is linked with the debug library */
		if (i % 4000 == 0 || (i + 1) ==times) {
			printf("\rLoading ... %.2f%%  ", (float)i * 100/ times);
			fflush(stdout);
		}
		arg[i] = i;
		stpool_add_routine(hp, "sche", task_run, task_complete, (void *)&arg[i], NULL);	
	}
	printf("\nAfter having executed @stpool_add_routine for %d times:\n"
		   "--------------------------------------------------------\n%s\n", 
		   times, stpool_status_print(hp, NULL, 0));
	
	printf("Press any key to resume the pool.\n");
	getchar();
	
	/* Wait for all tasks' being done. */
	{
	#ifndef _WIN
		int64_t t0, t1;
		struct timeval tv0, tv1;
		
		gettimeofday(&tv0, 0);
	#endif
		
		/* Wake up the pool to schedule tasks */
		stpool_resume(hp);		
		stpool_task_wait(hp, NULL, -1);
		
	#ifndef _WIN
		gettimeofday(&tv1, 0);
		
		t0 = tv0.tv_sec * 1000 + tv0.tv_usec / 1000;
		t1 = tv1.tv_sec * 1000 + tv1.tv_usec / 1000;
		
		printf("Test costs %lld ms.\n", t1 - t0);
	#endif
	}
	/* Get the sum */
	for (i=0, sum=0; i<times; i++)
		sum += arg[i];
	free(arg);
	
	now = time(NULL);
	printf("--OK. finished. <arg: %d> %s\n%s\n", 
		sum, ctime(&now), stpool_status_print(hp, NULL, 0));
#if 0
	/* You can use debug library to watch the status of the pool */
	while ('q' != getchar()) {
		for (i=0; i<40; i++)
			stpool_add_routine(hp, "debug", task_run, NULL, &sum, NULL);	
	}

	/* Clear the stdio cache */
	while ((c=getchar()) && c != '\n' && c != EOF)
		;
#endif
	getchar();
	/* Release the pool */
	printf("Shut down the pool now.\n");
	stpool_release(hp);
	getchar();
		
	return 0;
}
