#include <stdio.h>
#include "stpool.h"

int  test_run(struct sttask_t *ptsk)
{
	return 0;
}


void test_complete(struct sttask_t *ptsk, long vmflags, int task_code)
{
}

int main()
{
	int i;
	HPOOL hp;

	hp = stpool_create(1, 0, 0, 1);
	
	stpool_set_activetimeo(hp, 50, 40);
	stpool_adjust_abs(hp, 5, 3);
	printf("%s\n", stpool_status_print(hp, NULL, 0));
	
	stpool_adjust(hp, 5, -3);
	printf("\n\n%s\n", stpool_status_print(hp, NULL, 0));
	
	stpool_add_routine(hp, "test", test_run, test_complete, NULL, NULL);
	while ('q' != getchar())
		for (i=0; i<4; i++)
			stpool_add_routine(hp, "test", test_run, test_complete, NULL, NULL);
	
	printf("%s\n", stpool_status_print(hp, NULL, 0));
	stpool_release(hp);
	return 0;
}
