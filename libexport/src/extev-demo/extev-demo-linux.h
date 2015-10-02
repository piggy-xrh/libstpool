#ifndef __DEMO_EXTEV_LINX_H__
#define __DEMO_EXTEV_LINX_H__

#include "tevent.h"

#ifdef __cplusplus
extern "C"
#endif

/* A demo module to implment the extev module to 
 * intergrate the timer with the user's app.
 */
typedef struct app_env {
	ext_event_t extev;
	int wakefd[2];

	/* As a demo, we assume that the user is using 
	 * the @select to detect the IO events */
	int maxfd;
	fd_set rset, wset, eset;
} app_env_t ;

ext_event_t *get_demo_extev(app_env_t *env);

#ifdef __cplusplus
}
#endif

#endif
