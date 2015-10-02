#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "msglog.h"
#include "extev-demo-linux.h"

#define M_TEVENT "evdemo"

static int
demo_event_pool_wait(timer_base_t *base, void *opaque) 
{
	int  ret = 0;
	long ums = -1;
	app_env_t *env = opaque;
	
	/* Get the appropriate time for what we'll sleep */
	if (timer_epw_begin(base, &ums)) {
		struct timeval tv, *ptv = NULL;
		
		/* If @ums is equal to -1, it indicates that we
		 * should sleep all the time */
		if (ums != -1) {
			tv.tv_sec = ums / 1000000;
			tv.tv_usec = ums % 1000000;
			ptv = &tv;
		}

		/* Call @select to detect the app events */
		ret = select(env->maxfd + 1, &env->rset, &env->wset, &env->eset, ptv);
		
		/* Notify the timer scheduler that we wake up */
		timer_epw_end(base);
	}

	return ret;
}

static void
demo_event_dispatch(timer_base_t *base, int epw_code, void *opaque)
{
	app_env_t *env = opaque;
	
	/* Check the app's env */
	if (epw_code > 0) {
		/* Does @demo_event_wakeup wake us up ? */
		if (FD_ISSET(env->wakefd[0], &env->rset)) {
			char buff[20];

			-- epw_code;
			
			/* Flush the reader cache */
			for (;;) {
				if (0 > read(env->wakefd[0], buff, sizeof(buff)) &&
					(EAGAIN == errno || EWOULDBLOCK == errno))
					break;
			}
		}

		/* Scan the app event sets and dispatch them */
		for (;--epw_code > 0;) {
			/* TODO */
		}
	}

	/* We call @timer_dispatch to dispatch the timer event 
	 * who has been timeover now */
	timer_dispatch(base);
}

static void
demo_event_wakeup(void *opaque)
{
	int err;

	app_env_t *env = opaque;
	
	/* Write a charactor to wake up the @select 
	 * called by @demo_event_pool_wait */
	err = write(env->wakefd[1], "a", 1);	
}

static void 
demo_event_destroy(void *opaque)
{
	app_env_t *env = opaque;
	
	MSG_log(M_TEVENT, LOG_INFO,
		"@%s: extev(%p)\n",
		__FUNCTION__, &env->extev);
	
	/* Close the event fds */
	FD_CLR(env->wakefd[0], &env->rset);
	close(env->wakefd[0]);
	close(env->wakefd[1]);
}

ext_event_t *
get_demo_extev(app_env_t *env)
{
	static struct ext_event_t __ev = {
		NULL,
		demo_event_pool_wait,
		demo_event_dispatch,
		demo_event_wakeup,
		demo_event_destroy,
		NULL,	
	};
	
	/* Create a pipe */
	if (pipe2(env->wakefd, O_NONBLOCK)) {
		MSG_log2(M_TEVENT, LOG_ERR,
			"@pipe: %s", strerror(errno));
		return NULL;
	}
	
	/* The @rset must have been initialized !*/
	FD_ZERO(&env->rset); FD_ZERO(&env->wset); FD_ZERO(&env->eset);
	FD_SET(env->wakefd[0], &env->rset);
	env->maxfd = env->wakefd[0];

	MSG_log(M_TEVENT, LOG_INFO,
		"@%s returns extev(%p)\n",
		 __FUNCTION__, &env->extev);
	
	env->extev = __ev;
	env->extev.opaque = env;
	return &env->extev;
}

