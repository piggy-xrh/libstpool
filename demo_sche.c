#include <stdio.h>
#include "stpool.h"

#ifdef _WIN32
#include <Windows.h>
#pragma comment(lib, "stpool.lib")
#define msleep Sleep
#define random rand
#else
#define msleep(x) usleep(x * 1000)
#endif

static void do_work(int *val) {
	*val += 150;
	*val *= 0.2;
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

int main()
{
	time_t now;
	int i, c, times;
	int sum, *arg;
	HPOOL hp;
	
	/* We can set the global env here */
#ifndef _WIN32
	setenv("LIMIT_THREADS_FREE", "1", 1);
	setenv("LIMIT_THREADS_CREATE_PER_TIME", "1", 1);
#else
	_putenv("LIMIT_THREADS_FREE=1");
	_putenv("LIMIT_THREADS_CREATE_PER_TIME=1");
#endif

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
		/* It'll take a long time if the program is linked with the debug library */
		if (i % 4000 == 0 || (i + 1) ==times) {
			printf("\rLoading tasks ... %.2f%%  ", (float)i * 100/ times);
			fflush(stdout);
		}
		arg[i] = i;
		stpool_add_routine(hp, "sche", task_run, NULL, (void *)&arg[i], NULL);	
	}
	printf("\nAfter having executed @stpool_add_routine for %d times:\n"
		   "--------------------------------------------------------\n%s\n", 
		   times, stpool_status_print(hp, NULL, 0));
	
	printf("Press any key to resume the pool.\n");
	getchar();
	
	/* Wake up the pool to schedule tasks */
	stpool_resume(hp);

	/* Wait for all tasks' being done. */
	stpool_task_wait(hp, NULL, -1);
	
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
		usleep(20);
	}

	/* Clear the stdio cache */
	while ((c=getchar()) && c != '\n' && c != EOF)
		;
#endif
	
	/* Release the pool */
	printf("Shut down the pool now.\n");
	stpool_release(hp);
	getchar();
		
	return 0;
}
