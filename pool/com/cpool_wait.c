/* 
 *	  Stpool is portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <assert.h>
#include "ospx.h"
#include "ospx_errno.h"
#include "msglog.h"
#include "cpool_wait.h"

static LIST_HEAD(___glb_wq);
static int ___initialized = 0;
static OSPX_pthread_mutex_t ___lock;
static OSPX_pthread_once_t  ___octl = OSPX_PTHREAD_ONCE_INIT;

static void glbwq_init()
{
	if ((errno = OSPX_pthread_mutex_init(&___lock, 0))) 
		MSG_log(M_WAIT, LOG_ERR,
				"Failed to initialize the global wait queue: %s\n",
				OSPX_sys_strerror(errno));
	else
		___initialized = 1;
}

void 
WWAKE_add(struct WWAKE_requester *requester)
{
	OSPX_pthread_once(&___octl, glbwq_init);
	
	assert (___initialized);
	if (!___initialized)
		return;

	OSPX_pthread_mutex_lock(&___lock);
	list_add_tail(&requester->link, &___glb_wq);
	OSPX_pthread_mutex_unlock(&___lock);
}

void 
WWAKE_erase(int id, struct WWAKE_requester **requester)
{
	struct WWAKE_requester *r, *n;
	
	if (!___initialized)
		return;
	
	OSPX_pthread_mutex_lock(&___lock);
	if (-1 == id)
		INIT_LIST_HEAD(&___glb_wq);
	else {
		list_for_each_entry_safe(r, n, &___glb_wq, struct WWAKE_requester, link) {
			if (r->id == id) {
				list_del(&r->link);

				if (requester)
					*requester = r;
			}
		}
	}
	OSPX_pthread_mutex_unlock(&___lock);
}

void 
WWAKE_erase_direct(struct WWAKE_requester *r)
{
	if (!___initialized)
		return;

	OSPX_pthread_mutex_lock(&___lock);
	list_del(&r->link);
	OSPX_pthread_mutex_unlock(&___lock);
}

int  
WWAKE_invoke(int id)
{
	int c = 0;
	struct WWAKE_requester *r, *n;
	
	if (!___initialized) 
		return 0;
	
	OSPX_pthread_mutex_lock(&___lock);
	list_for_each_entry_safe(r, n, &___glb_wq, struct WWAKE_requester, link) {
		if (-1 == id || r->id == id) {
			r->wakeup(r);
			++ c;
		}
	}
	OSPX_pthread_mutex_unlock(&___lock);
	
	return c;
}

void
WWAKE_cb(WWAKE_walk cb, void *cb_arg)
{
	struct WWAKE_requester *r, *n;
	
	if (!___initialized) 
		return;

	OSPX_pthread_mutex_lock(&___lock);
	list_for_each_entry_safe(r, n, &___glb_wq, struct WWAKE_requester, link) {
		if (cb(r, cb_arg))
			break;
	}
	OSPX_pthread_mutex_unlock(&___lock);
}


