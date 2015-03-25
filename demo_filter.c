#include <stdio.h>
#include <assert.h>
#include "stpool.h"

#ifdef _WIN32
#include <Windows.h>
#pragma comment(lib, "../lib/stpool.lib")
#define msleep Sleep
#define random rand
#else
#define msleep(x) usleep(x * 1000)
#endif

static int g_reject = 0;
int  task_run(void *arg) {
	printf("==>OK.\n");
	return stpool_add_routine((HPOOL)arg, task_run, NULL, arg);	
}

struct stevflt_res_t ev_filter(HPOOL hp, struct stevent_t *ev, struct stbrf_stat_t *brfstat, struct sttask_t *task) {
	struct stevflt_res_t res = {0};
	
	if (g_reject) {
		fprintf(stderr, "Discard task: %s-%p\n",
			task->task_name, task);

		res.ev_flttyp = EV_FILTER_DISCARD;
		return res;
	}
		
	/* The task will be delivered into the pool automatically after
	 * 1000 milliseconds if the pool is busy.
	 */
	if (brfstat->evflt_busy) {
		fprintf(stderr, "Sleep for 1000 milliseconds since the pool is busy now: %s-%p\n",
			task->task_name, task);

		res.ev_flttyp = EV_FILTER_WAIT;
		res.ev_param  = 1000;
	} else 
		res.ev_flttyp = EV_FILTER_PASS;
	
	return res;
}

int main()
{
	int error;
	HPOOL hp;
	struct stpool_stat_t state;
	struct stevent_t ev = {
		0, 0,
		EV_TRIGGLE_TASKS,
		ev_filter,
		NULL
	};

	/* Creat a task pool */
	hp = stpool_create(50,  /* max servering threads number */
			           0,   /* 0 servering threads that reserved for waiting for tasks */
			           0,   /* do not suspend the pool */
			           0);  /* using the default number of priority queue */
	printf("%s\n", stpool_status_print(hp, NULL, 0));
	
	/* Install the filter */
	stpool_event_set(hp, &ev);

	stpool_add_routine(hp, task_run, NULL, (void *)hp);	
	
	/* Add tasks */
	while ('q' != getchar())
		;
	g_reject = 1;

	/* Wait for all tasks' being done. */
	stpool_wait(hp, NULL, -1);
		
	/* Release the pool */
	printf("Shut down the pool now.\n");
	stpool_release(hp);
		
	return 0;
}
