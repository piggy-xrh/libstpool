#include <stdio.h>
#include <time.h>
#include "tevent.h"

#ifdef _WIN
#include <windows.h>
#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Debug/x86_64_win/libtevent.lib")
#else
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Debug/x86_32_win/libtevent.lib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "../../../lib/Release/x86_64_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Release/x86_64_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Release/x86_64_win/libtevent.lib")
#else
#pragma comment(lib, "../../../lib/Release/x86_32_win/libmsglog.lib")
#pragma comment(lib, "../../../lib/Release/x86_32_win/libstpool.lib")
#pragma comment(lib, "../../../lib/Release/x86_32_win/libtevent.lib")
#endif
#endif
#define random rand
#pragma comment( linker, "/subsystem:console /entry:mainCRTStartup")
#endif

/* (log library)    depends  (task pool library)
 * libmsglog.lib <-------------libstpool.lib
 *            ^                ^
 *             \              / 
 *      depends \            /depends
 *               \          /
 *               libtevent.lib
 *           (timer event libarary)
 */
void tmfire(tevent_t *ev)
{
	time_t n = time(NULL);

	printf("fire:%ld %s", tevent_timeo(ev) / 1000, ctime(&n));
	
	/* Schedule the timer event again */
	tevent_add(ev);
}

int main()
{
	int i, n;
	long delay;
	timer_base_t *base;
	tevent_t *ev[8];
	
	/* Create a timer scheduler */
	base = timer_ctor(1, NULL);
	n = sizeof(ev)/sizeof(*ev);
	srand(7999);

	/* Add timer events into the scheduler */
	for (i=0; i<n; i++) {
		delay = 50000 + 900 * ((unsigned)(random()) % 8000);
		ev[i] = tevent_new(base, tmfire, NULL, delay);
		tevent_add(ev[i]);
	}
	getchar();
	
	/* Remove all timer events */
	for (i=0; i<n; i++) {
		tevent_del_wait(ev[i]);
		tevent_delete(ev[i]);
	}

	/* Destroy the scheduler */
	timer_dtor(base);
	getchar();
	return 0;
}
