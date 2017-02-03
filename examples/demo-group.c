#include <stdio.h>
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

/**
 * We include the header file msglog.h here to change the behavior of the log messages 
 */
#include "msglog.h"

#include "stpool_group.h"

void time_consuming_task_run(struct sttask *ptsk)
{
	msleep((long)ptsk->task_arg);
	
	/**
	 * Reschedule the task again 
	 */
	stpool_task_queue(ptsk);
}

void fast_task_run1(struct sttask *ptsk)
{
	msleep(15);

	/**
	 * Reschedule the task again 
	 */
	stpool_task_queue(ptsk);
}

void fast_task_run2(struct sttask *ptsk)
{
	msleep(30);

	/**
	 * Reschedule the task again 
	 */
	stpool_task_queue(ptsk);
}

int main()
{
	long eCAPs;
	long idx, exe_func_timeout;
	int gid_slow, gid_fast1, gid_fast2;
	struct gscheduler_attr attr;
	stpool_t *pool;
	
	/*-----------------------------------------------------------------------*/
	/*--------------------Control the ouput of the log messages--------------*/
	/*-----------------------------------------------------------------------*/
	const char *mentry[] = {"Scheduler", "thread", NULL};
	
	/**
	 * Note:
	 * 	   If our progam is linked with the DEBUG library, the console may always
	 * be filled up with the log messages from modules (Scheduler, thread), so we
	 * filt some messages to make it easier for users to watch the running behaviors
	 * of the libstpool.
	 */
	MSG_log_set_level(LOG_DEBUG);
	
	MSG_log_mfilter_add_entry(mentry, NULL);
	MSG_log_mfilter_set_type(eFT_discard);
	/*----------------------------------------------------------------------*/

	eCAPs = eCAP_F_GROUP|eCAP_F_ROUTINE|eCAP_F_GROUP_SUSPEND|eCAP_F_DISABLEQ|eCAP_F_WAIT_ALL;

	/**
	 * Create a share pool 
	 */
	pool = stpool_create("share_pool", 
						  eCAPs,         /** Capabities */
	                      10,            /** max working threads number */
						   0,            /** min working threads number */
						   0,            /** Suspended */
						   1             /** priority queue number */
						   );
	
	/**
	 * Create a time-consuming group and set its absolute paralle tasks number to 2 
	 */
	attr.limit_paralle_tasks = 2;
	attr.receive_benifits = 0;
	gid_slow = stpool_group_create(pool, "g_slow", &attr, 1, 1);
	
	/**
	 * Add 10 tasks into the group 
	 */
	for (idx=0; idx<10; idx++) {
		exe_func_timeout = 1000 * (idx % 5 + 1);
		stpool_group_add_routine(pool, gid_slow, "slow_task", time_consuming_task_run, NULL, (void *)exe_func_timeout, NULL);
	}
	puts(
		stpool_scheduler_map_dump(pool)
	);

	puts("\nprint any key to schedule the slow task ...\n");
	getchar();

	/**
	 * Resume the time-consuming group 
	 */
	stpool_group_resume(pool, gid_slow);
	
	puts("print q to go next test ...\n");
	while ('q' != getchar())
		puts(
			stpool_scheduler_map_dump(pool)
			);
	/**
	 * Create two other fast groups 
	 */
	gid_fast1 = stpool_group_create(pool, "g_fast1", NULL, 1, 1);
	gid_fast2 = stpool_group_create(pool, "g_fast2", NULL, 1, 1);
		
	/**
	 * Add a few tasks into the fast groups 
	 */
	for (idx=0; idx<10; idx++) {
		stpool_group_add_routine(pool, gid_fast1, "fast1_task", fast_task_run1, NULL, NULL, NULL);
		stpool_group_add_routine(pool, gid_fast2, "fast2_task", fast_task_run2, NULL, NULL, NULL);
	}
	
	/**
	 * print the scheduler map 
	 */
	puts( 
	     stpool_scheduler_map_dump(pool)
	    );

	/**
	 * resume the fast groups 
	 */
	getchar();
	puts("print any key to resume the fast groups ...\n");
	stpool_group_resume_all(pool);
	
	puts("print q to go next test ...\n");
	while ('q' != getchar())
		puts(
			stpool_scheduler_map_dump(pool)
			);

	/**
	 * remove all tasks 
	 */
	getchar();
	
	puts("print any key to remove all tasks ...\n");
	stpool_mark_all(pool, TASK_VMARK_REMOVE|TASK_VMARK_DISABLE_QUEUE);
	stpool_wait_all(pool, -1);

	/**
	 * Show the realtime scheduler map 
	 */
	puts(
		 stpool_scheduler_map_dump(pool)
		);
	
	/**
	 * release the pool 
	 *
	 * NOTE:
	 *    If the pool's reference is changed to zero, all groups will be
	 * deleted by the pool automatically and the pool itself will be destroyed
	 * later in the background after doing all of the active tasks existing 
	 * in the pool
	 */
	getchar();
	puts("print any key to release the pool ...\n");
	stpool_release(pool);

	puts("print any key to exit ...\n");
	getchar();

	return 0;
}
