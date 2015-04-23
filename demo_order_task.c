#include <stdio.h>
#include "stpool.h"


int  taskA_run(struct sttask_t *ptsk) {
	printf("\n\nRun %s\n", ptsk->task_name);
	
	return 0;
}

int taskB_run(struct sttask_t *ptsk) {
	printf("\n\nRun %s\n", ptsk->task_name);
	
	return 0;
}

int taskC_run(struct sttask_t *ptsk) {
	printf("\n\nRun %s\n", ptsk->task_name);
	
	return 0;
}

int  other_task_run(struct sttask_t *ptsk) {
	printf("\n\nRun %s\n", ptsk->task_name);
}

#define offset(type, member) (char *)(&((type *)0)->member)
#define	container_of(ptr, type, member) ((type *)((char *)ptr - offset(type, member)))
struct my_wrapper_task {
	/* Tasks entry */
	int entry_num;
	struct sttask_t **entry;

	int last_task_index;
	int last_task_code;
	
	/* The wrapper task object */
	struct sttask_t self[0];
};

int task_wrapper_run(struct sttask_t *ptsk) {
	struct my_wrapper_task *wrapper = 
		container_of(ptsk, struct my_wrapper_task, self);
	struct sttask_t *current = wrapper->entry[wrapper->last_task_index];
	
	/* Run current task */
	return current->task_run(current);	
}

void task_wrapper_complete(struct sttask_t *ptsk, long vmflags, int task_code) {
	struct my_wrapper_task *wrapper = 
		container_of(ptsk, struct my_wrapper_task, self);
	struct sttask_t *current = wrapper->entry[wrapper->last_task_index];
		
	/* Run current task's completion routine */
	if (current->task_complete)
		current->task_complete(current, vmflags, task_code);

	/* Record the error code */
	wrapper->last_task_code = task_code;
	
	if (STTASK_VMARK_DONE & vmflags) {
		struct schattr_t attr;
		struct sttask_t *next;
		
		/* Obtain the next scheduling task */
		next = wrapper->entry[wrapper->last_task_index];
		
		/* Clone the scheduling attribute */
		stpool_task_getschattr(next, &attr);
		stpool_task_setschattr(ptsk, &attr);

		/* Reschedule the wrapper task */
		if (wrapper->entry_num != ++ wrapper->last_task_index)
			stpool_add_task(ptsk->hp_last_attached, ptsk);
	}
}

int main()
{
	HPOOL hp;
	struct schattr_t attr0, attr[] = {
		{0, 0, STP_SCHE_TOP},
		{0, 0, STP_SCHE_BACK}
	};

	struct sttask_t *taskA, *taskB, *taskC;
	struct my_wrapper_task *wrapper;
	
	/* Prepare for the tasks */
	taskA = stpool_task_new("taskA", taskA_run, NULL, NULL);
	taskB = stpool_task_new("taskB", taskA_run, NULL, NULL);
	taskC = stpool_task_new("taskC", taskA_run, NULL, NULL);
	wrapper = calloc(sizeof(struct my_wrapper_task) + stpool_task_size(), 1);

	/* Initialize the wrapper task */
	wrapper->entry_num = 3;
	wrapper->entry = malloc(sizeof(struct sttask_t *) * wrapper->entry_num);
	wrapper->entry[0] = taskA;
	wrapper->entry[1] = taskB;
	wrapper->entry[2] = taskC;
	
	/* Clone the schedulint attribute */
	stpool_task_getschattr(wrapper->entry[0], &attr0);
	stpool_task_init(wrapper->self, "wrapper", task_wrapper_run, task_wrapper_complete, NULL);
	stpool_task_setschattr(wrapper->self, &attr0);

	/* Create a task pool */
	hp = stpool_create(1, 1, 1, 1);

	/* Add tasks into the pool 
	 *
	 * other_task_top
	 * wrapper
	 * other_task_back
	 */
	stpool_add_routine(hp, "other_task_top", other_task_run, NULL, NULL, &attr[0]);
	stpool_add_task(hp, wrapper->self);
	stpool_add_routine(hp, "other_task_back", other_task_run, NULL, NULL, &attr[1]);

	/* Wake up pool to schedule task */
	stpool_resume(hp);

	/* Wait for taskA->taskB->taskC */
	stpool_task_wait(hp, wrapper->self, -1);
	puts("taskA->taskB->taskC Done !\n");

	/* Wait for all tasks' being done */
	stpool_task_wait(hp, NULL, -1);
	stpool_release(hp);
	
	/* Free the objects */
	stpool_task_delete(taskA);
	stpool_task_delete(taskB);
	stpool_task_delete(taskC);
	free(wrapper->entry);
	free(wrapper);
	
	return 0;
}
