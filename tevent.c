/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  tevent is a portable and efficient timer library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <assert.h>
#include <stdarg.h>

#include "ospx.h"
#include "ospx_errno.h"
#include "msglog.h"
#include "list.h"
#ifdef CONFIG_POOL_TIMER
#include "stpool.h"
#endif
#include "objpool.h"
#include "tevent.h"
#define M_TEVENT "tevent"

#define EV_STAT_WAITING          0x01
#define EV_STAT_DISPATCHING      0x02
#define EV_STAT_DISPATCHING_WAIT 0x04
#define EV_STAT_READD            0x08

typedef struct tevent_priv {
	int32_t  evstat;
	int32_t  us_fire_config;
	uint64_t clock_end;
	int32_t  us_fire;
	int32_t  min_heap_idx;
} tevent_priv_t;
const int g_const_TEVENT_SIZE = sizeof(tevent_t) + sizeof(tevent_priv_t);
#include "minheap-internal.h"

#define HTMR_STAT_WAITING 0x1
#define HTMR_STAT_RUN     0x1000
#define HTMR_STAT_SYS     HTMR_STAT_RUN

struct timer_base {
	long stat;
	void *hp;
	int  n_max_parallel_cbs;
	ext_event_t *extev;
	min_heap_t heap;
	int  waked;
	long us_wait;
	int  need_repair_clock;
	uint64_t clock_base, clock_end;
	uint64_t clock_last_ok;
	uint64_t clock_exception;
	uint64_t clock_wait;
	OSPX_pthread_t evid;
	OSPX_pthread_mutex_t lock;
	OSPX_pthread_cond_t  cond;
};
static inline int timer_try_repair_clock(timer_base_t *base, uint64_t clock_now);

static smcache_t *___smc = NULL;
/*----------------------------APIs about timer event----------------------*/
EXPORT void  
tevent_init(tevent_t *ev, timer_base_t *base, 
	void (*tmrfire)(struct tevent *), void *opaque, long us_fire)
{
	tevent_priv_t *priv = (tevent_priv_t *)ev->stack;
	
	//bzero(ev, tevent_size());
	memset(ev, 0, tevent_size());
	ev->base = base;
	ev->tmrfire = tmrfire;
	ev->opaque = opaque;
	priv->us_fire_config = us_fire;
	priv->us_fire = -1;
	priv->min_heap_idx = -1;
}

EXPORT tevent_t *
tevent_new(timer_base_t *base, 
	void (*tmrfire)(struct tevent *), void *opaque, long us_fire) 
{
	void *ev = NULL;
	static int ___dummy_boolean = 0;
	static long volatile ___dummy_ref = 0;

	/* Create a global object pool. */
	if (!___dummy_boolean) {
		static objpool_t ___dummy_objp;
		
		if (!___dummy_ref) {
			/* Only one thread can get the chance to initialize
			 * the object pool. */
			if (1 == OSPX_interlocked_increase(&___dummy_ref)) {
				if (!objpool_ctor(&___dummy_objp, "FObjp-C-Global-tevent", tevent_size(), 0))
					___smc = objpool_get_cache(&___dummy_objp);	
				___dummy_boolean = 1;
			}
		}
		
		/* Set a barrier here to synchronize the env */
		while (!___dummy_boolean) ;
	}
	
	/* Create a task object and initialzie it */
	if (___smc && (ev = smcache_get(___smc, 1))) 	
		tevent_init(ev, base, tmrfire, opaque, us_fire);
	
	return ev;
}

EXPORT void  
tevent_delete(tevent_t *ev) 
{
	assert (ev && ___smc);
	smcache_add_limit(___smc, ev, -1);
}

EXPORT long  
tevent_timeo(tevent_t *ev)
{	
	return ((tevent_priv_t *)ev->stack)->us_fire_config;	
}

EXPORT void  
tevent_set_timeo(tevent_t *ev, long us_fire)
{
	((tevent_priv_t *)ev->stack)->us_fire_config = us_fire;	
}

static inline int 
tevent_addl(tevent_t *ev, tevent_priv_t *priv, long us_fire, uint64_t clock_now)
{
	int clock_repaired = 0;
	assert (!(priv->evstat & EV_STAT_WAITING));
	assert (-1 == priv->min_heap_idx && us_fire >= 0);

	/* Add the event into the min heap */
	priv->evstat |= EV_STAT_WAITING;
	priv->evstat &= ~EV_STAT_READD;
	priv->us_fire = us_fire; 
	priv->clock_end = clock_now + us_fire;
	
	/* We try to repair the clock if it is neccessary*/
	if (ev->base->need_repair_clock) 
		clock_repaired = timer_try_repair_clock(ev->base, clock_now);
	min_heap_push(&ev->base->heap, ev);

#ifndef NDEBUG
	MSG_log(M_TEVENT, LOG_TRACE,
		"add event(%p-%d): us_fire:%ld ms (%d/%d)\n", 
		ev, priv->evstat, us_fire / 1000, ev->base->stat, ev->base->waked);
#endif	
	/* Check whehter we should wake up the @event_pool_wait 
	 * according to the event timer */
	if (ev->base->stat & HTMR_STAT_WAITING && !ev->base->waked &&
		(clock_repaired || priv->clock_end <= ev->base->clock_end)) {
		ev->base->waked = 1;
		return 1;
	}
	
	return 0;
}

static inline uint64_t
get_us_clock()
{
	struct timeval tv;
	
	/* Maybe we should use GetTickCount64 to avoid trying 
	 * to repair the clock time all the way. (If the system
	 * time has been re-configured by user, we should fix
	 * our clock time)
	 */
	OSPX_gettimeofday(&tv, NULL);

	return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

EXPORT int    
tevent_add(tevent_t *ev)
{
	int skipped = 0, notify = 0;
	tevent_priv_t *priv = (tevent_priv_t *)ev->stack;
	timer_base_t *base = ev->base;
	uint64_t clock_now;
	long us_fire = priv->us_fire_config;

	if (!base) {
		MSG_log(M_TEVENT, LOG_WARN,
			"ev->base = NULL (ev:%p)",
			ev);
		return -1;
	}

	if (us_fire < 0) {
		MSG_log(M_TEVENT, LOG_WARN,
			"Invalid event timer: %p|us_fire(%ld)\n",
			ev, us_fire);
		return -1;
	}
	clock_now = get_us_clock(); 
	
	OSPX_pthread_mutex_lock(&base->lock);
	if (priv->evstat & EV_STAT_DISPATCHING_WAIT) 
		skipped = 1;	
	else if (!(priv->evstat & (EV_STAT_WAITING|EV_STAT_READD))) {
		if (!base->hp || !(priv->evstat & EV_STAT_DISPATCHING)) 
			notify = tevent_addl(ev, priv, us_fire, clock_now);
		else	
			priv->evstat |= EV_STAT_READD;
	}
	OSPX_pthread_mutex_unlock(&base->lock);
	
	if (skipped)
		MSG_log(M_TEVENT, LOG_WARN,
			"We skip the request since @tevent_del_wait requests" 
			"to remove this event(%p)\n",
			ev);
	else if (notify) 
		(*base->extev->event_wakeup)(base->extev->opaque);
	
	return skipped;
}

EXPORT int 
tevent_set_add(tevent_t *ev, long us_fire) 
{
	int notify = 0;
	tevent_priv_t *priv = (tevent_priv_t *)ev->stack;
	timer_base_t *base = ev->base;
	uint64_t clock_now;
	
	priv->us_fire_config = us_fire;
	if (!base) {
		MSG_log(M_TEVENT, LOG_WARN,
			"ev->base = NULL (ev:%p)",
			ev);
		return -1;
	}

	if (us_fire < 0) {
		MSG_log(M_TEVENT, LOG_WARN,
			"Invalid event timer: %p|us_fire(%ld)\n",
			ev, us_fire);
		return -1;
	}
	clock_now = get_us_clock(); 
	
	OSPX_pthread_mutex_lock(&base->lock);
	/* If the event exists in the scheduler queue, we 
	 * try to remove it and add it into the scheduler
	 * again */
	if (EV_STAT_WAITING & priv->evstat) {
		tevent_t *etop = min_heap_top(&base->heap);
		
		assert (!min_heap_empty(&base->heap) && 
			!(EV_STAT_DISPATCHING & priv->evstat));
		
		/* If the event is the top element, we just 
		 * reset its fire time and wake the scheduler
		 * up to reschedule it again */
		if (etop == ev) {
			priv->us_fire = us_fire;
			notify = 1;
			
		} else {
			priv->evstat &= ~EV_STAT_WAITING;
			min_heap_erase(&base->heap, ev);
			tevent_addl(ev, priv, us_fire, clock_now);
		}
		
	} else if (!(priv->evstat & EV_STAT_READD)) {
		if (!base->hp || !(priv->evstat & EV_STAT_DISPATCHING)) 
			notify = tevent_addl(ev, priv, us_fire, clock_now);
		else	
			priv->evstat |= EV_STAT_READD;
	}
	OSPX_pthread_mutex_unlock(&base->lock);
	
	if (notify) 
		(*base->extev->event_wakeup)(base->extev->opaque);
	return 0;
}

EXPORT void
tevent_del(tevent_t *ev)
{
	tevent_priv_t *priv = (tevent_priv_t *)ev->stack;

#ifndef NDEBUG
	MSG_log(M_TEVENT, LOG_TRACE,
		"del event(%p-%d): us_fire:%ld ms (%d/%d)\n", 
		ev, priv->evstat, priv->us_fire_config / 1000, ev->base->stat, 
		ev->base->waked);
#endif

	if (ev->base && -1 != priv->us_fire) {
		OSPX_pthread_mutex_lock(&ev->base->lock);
		priv->evstat &= ~EV_STAT_READD;
		if (priv->evstat & EV_STAT_WAITING) {
			min_heap_erase(&ev->base->heap, ev);
			priv->evstat &= ~EV_STAT_WAITING;
			priv->us_fire = -1;
		}
		OSPX_pthread_mutex_unlock(&ev->base->lock);
	}
}

EXPORT void
tevent_del_wait(tevent_t *ev)
{
	tevent_priv_t *priv = (tevent_priv_t *)ev->stack;

#ifndef NDEBUG
	MSG_log(M_TEVENT, LOG_TRACE,
		"del_wait event(%p-%d): us_fire:%ld ms (%d/%d)\n", 
		ev, priv->evstat, priv->us_fire_config / 1000, ev->base->stat, 
		ev->base->waked);
#endif

	if (ev->base && -1 != priv->us_fire) {
		OSPX_pthread_mutex_lock(&ev->base->lock);
		if (!(priv->evstat & EV_STAT_WAITING)) {
			priv->evstat |= EV_STAT_DISPATCHING_WAIT;
			for (;priv->evstat & EV_STAT_DISPATCHING;) {
				priv->evstat &= ~EV_STAT_READD;
				OSPX_pthread_cond_wait(&ev->base->cond, &ev->base->lock);
			}
			priv->evstat &= ~EV_STAT_DISPATCHING_WAIT;
		}
		
		/* We check the event status again */
		priv->evstat &= ~EV_STAT_READD;
		if (priv->evstat & EV_STAT_WAITING) {
			min_heap_erase(&ev->base->heap, ev);
			priv->evstat &= ~EV_STAT_WAITING;
		}
		priv->us_fire = -1;
		OSPX_pthread_mutex_unlock(&ev->base->lock);
	}
}


/*----------------------------APIS about timer container-----------------*/
#ifdef CONFIG_POOL_TIMER
static int timer_dispatch_run(struct sttask_t *ptsk);
#endif
static ext_event_t *default_extev();

static int 
timer_event_scheduler(void *arg) 
{
	timer_base_t *base = arg;
	ext_event_t *extev = base->extev;

	for (;HTMR_STAT_RUN & base->stat;) {	
		(*extev->event_dispatch)
		(
			base,
			(*extev->event_pool_wait)(base, extev->opaque),
			extev->opaque
		);
	}

	return 0;
}

EXPORT const char *
timer_version() 
{
	return "2015/09/11-1.0-libtevent-minheap";
}

EXPORT timer_base_t *
timer_ctor(int n_max_parallel_cbs, ext_event_t *extev) 
{
	int err;
	timer_base_t *base = calloc(1, sizeof(timer_base_t));

	if (!extev && !(extev = default_extev())) 
		goto err3;
	
	if (extev->event_init && 
		(err=(*extev->event_init)(extev->opaque))) {
		MSG_log(M_TEVENT, LOG_ERR,
			"event_init return %d\n",
			err);
		goto err3;
	}
	
	base->extev = extev;
	base->stat = HTMR_STAT_RUN;
	base->clock_base = base->clock_last_ok = 
	base->clock_wait = get_us_clock();
	base->clock_end  = -1;
	base->us_wait = -1;
	base->need_repair_clock = 1;
	base->n_max_parallel_cbs = n_max_parallel_cbs;
	min_heap_ctor(&base->heap);
	
	if ((err = OSPX_pthread_mutex_init(&base->lock, 0))) {
		MSG_log2(M_TEVENT, LOG_ERR,
			"mutex_init:%s", OSPX_sys_strerror(err));
		goto err2;
	}
	
	if ((err = OSPX_pthread_cond_init(&base->cond))) {
		MSG_log2(M_TEVENT, LOG_ERR,
			"cond_init:%s", OSPX_sys_strerror(err));
		goto err1;
	}

	if ((err = OSPX_pthread_create(&base->evid, NULL, 
					timer_event_scheduler, (void *)base))) {
		MSG_log2(M_TEVENT, LOG_ERR,
			"pthread_create:%s", OSPX_sys_strerror(err));
		goto err0;
	}
	
	/* Create a task pool to schedule the timer event */
#ifdef CONFIG_POOL_TIMER
	if (base->n_max_parallel_cbs > 0) {
		base->hp = stpool_create2("timer-Scheduler", base->n_max_parallel_cbs, 0, 0, 1);
		if (base->hp)
			stpool_set_activetimeo(base->hp, 20, 60);
	}
#endif	
	return base;

err0:
	OSPX_pthread_mutex_destroy(&base->lock);
err1:
	OSPX_pthread_cond_destroy(&base->cond);
err2:
	if (extev->event_destroy)
		(*extev->event_destroy)(extev->opaque);
err3:
	free(base);
	return NULL;
}


EXPORT void *
timer_get_pool(timer_base_t *base)
{
	return base->hp;
}

static inline long   
timer_earliest_usl(timer_base_t *base, uint64_t clock_now)
{
	uint64_t clock_end;
	tevent_t *ev;

	if (min_heap_empty(&base->heap)) 
		return -1;
	
	if (base->need_repair_clock)
		timer_try_repair_clock(base, clock_now);
	
	ev = min_heap_top(&base->heap);
	clock_end = ((tevent_priv_t *)ev->stack)->clock_end;
	
	return clock_end <= clock_now ? 0 : (long)(clock_end - clock_now);	
}

EXPORT long
timer_epw_begin(timer_base_t *base, long *us)
{
	long us_earliest;
	uint64_t clock_now = get_us_clock();
	
	assert (us && *us != 0);
	OSPX_pthread_mutex_lock(&base->lock);
	us_earliest = timer_earliest_usl(base, clock_now);
	if (us_earliest != 0) {
		base->waked = 0;
		base->stat  = HTMR_STAT_WAITING | (base->stat & HTMR_STAT_SYS);
		base->clock_wait = clock_now;
	}

	if (us_earliest >= 0 && (*us < 0 || *us > us_earliest))
		*us = us_earliest;
	
	if (*us > 0) {
		base->us_wait = *us;
		base->clock_end = *us > 0 ? *us + clock_now : -1;
	}
	OSPX_pthread_mutex_unlock(&base->lock);	

#ifndef NDEBUG
	MSG_log(M_TEVENT, LOG_DEBUG,
		"epw_waiting %ld ms (elements:%d) ...\n", 
		*us > 0 ? *us / 1000 : *us, min_heap_size(&base->heap));
#endif
	return *us;
}

EXPORT void
timer_epw_end(timer_base_t *base)
{
	base->waked = 1;
	base->stat &= ~HTMR_STAT_WAITING;
}

EXPORT void
timer_dispatch(timer_base_t * base)
{
	int n = 0;
	tevent_t *ev;
	tevent_priv_t *priv;
	uint64_t clock_now; 
	
	for (ev=NULL; HTMR_STAT_RUN & base->stat; ++n, ev=NULL) {
		if (!n % 15)
			clock_now = get_us_clock();

		OSPX_pthread_mutex_lock(&base->lock);
		/* Visit the top element */
		if (timer_earliest_usl(base, clock_now)) {
			OSPX_pthread_mutex_unlock(&base->lock);
			break;
		}
		
		/* We remove the top element from the min heap if it 
		 * is timeout now, and then fire its timer callback */
		assert (!min_heap_empty(&base->heap)); 
		ev = min_heap_pop(&base->heap);
		priv = (tevent_priv_t *)ev->stack;
			
		priv->evstat &= ~EV_STAT_WAITING;
		priv->evstat |= EV_STAT_DISPATCHING;
		OSPX_pthread_mutex_unlock(&base->lock);
		
#ifdef CONFIG_POOL_TIMER
		if (base->hp) 
			stpool_add_routine(base->hp, "timer_event",
				timer_dispatch_run, NULL, ev, NULL);
		
		else 
#endif
		{
			(*ev->tmrfire)(ev); 
			
			priv->evstat &= ~EV_STAT_DISPATCHING;
			if (priv->evstat & EV_STAT_DISPATCHING_WAIT) {
				OSPX_pthread_mutex_lock(&base->lock);
				OSPX_pthread_cond_broadcast(&base->cond);
				OSPX_pthread_mutex_unlock(&base->lock);
			}
		}
	}
}


EXPORT void   
timer_dtor(timer_base_t * base)
{
	ext_event_t *extev = base->extev;
	
	/* Stop the pool */
#ifdef CONFIG_POOL_TIMER
	if (base->hp) 
		stpool_suspend(base->hp, 0);
#endif
	/* We wake up the @event_pool_wait and tell the 
	 * scheduler that it is time to exit */
	base->stat &= ~HTMR_STAT_RUN;
	(*extev->event_wakeup)(extev->opaque);
		
	/* Free the resources */
	OSPX_pthread_join(base->evid, NULL);
#ifdef CONFIG_POOL_TIMER
	if (base->hp) {
		stpool_remove_pending_task(base->hp, NULL, 0);
		stpool_task_wait(base->hp, NULL, -1);
		stpool_release(base->hp);
	}
#endif
	if (extev->event_destroy)
		(*extev->event_destroy)(extev->opaque);	
	OSPX_pthread_mutex_destroy(&base->lock);
	OSPX_pthread_cond_destroy(&base->cond);
	
	/* Destroy the min heap */
	min_heap_dtor(&base->heap);
	free(base);
}

#ifdef CONFIG_POOL_TIMER
int
timer_dispatch_run(struct sttask_t *ptsk)
{
	int notify = 0, skipped = 0;
	tevent_t *ev = ptsk->task_arg;
	tevent_priv_t *priv = (tevent_priv_t *)ev->stack;
	timer_base_t *base  = ev->base;

	(*ev->tmrfire)(ev);
	
	/* We ensure that the event will not be delived into the pool
	 * again until the privous timer callback has been executed
	 * completely */
	priv->evstat &= ~EV_STAT_DISPATCHING;
	if (priv->evstat & (EV_STAT_READD|EV_STAT_DISPATCHING_WAIT)) {
		long us_fire = priv->us_fire_config;
		uint64_t clock_now = get_us_clock();

		OSPX_pthread_mutex_lock(&base->lock);	
		if (priv->evstat & EV_STAT_DISPATCHING_WAIT) {
			OSPX_pthread_cond_broadcast(&base->cond);
			
			if (EV_STAT_READD & priv->evstat) {
				priv->evstat &= ~EV_STAT_READD;
				skipped = 1;
			}	
		} else if (priv->evstat & EV_STAT_READD) 
			notify = tevent_addl(ev, priv, us_fire, clock_now);	
		OSPX_pthread_mutex_unlock(&base->lock);
	}
	
	if (skipped)
		MSG_log(M_TEVENT, LOG_WARN,
				"We skip the request since @tevent_del_wait requests"
				"to remove this event. event(%p)\n",
				ev);
	else if (notify) 
		(*base->extev->event_wakeup)(base->extev->opaque);
	return 0;
}
#endif

static int 
timer_heap_walk_repair(tevent_t *e, void *arg)
{
	tevent_priv_t *priv = (tevent_priv_t *)e->stack;
	int64_t clock_left = priv->clock_end - e->base->clock_exception;
	
	assert (clock_left <= priv->us_fire);
	priv->clock_end = clock_left > 0 ? e->base->clock_base + clock_left : 
		e->base->clock_base;

	return 0;
}

#ifndef NDEBUG
static int
timer_heap_left(tevent_t *e, void *arg)
{
	tevent_priv_t *priv = (tevent_priv_t *)e->stack;
	
	uint64_t clock_now = *(uint64_t *)arg;
	int ms_left = clock_now >= priv->clock_end ? 0 : (priv->clock_end - clock_now) / 1000;
	
	MSG_log(M_TEVENT, LOG_DEBUG,
		"event(%p) us_fire:%ld ms  left:%d ms\n",
		e, priv->us_fire / 1000, ms_left);
	return 0;
}
#endif

void
timer_fix_clock(timer_base_t *base, uint64_t clock_now)
{
	assert (base->clock_wait <= base->clock_last_ok);	
	
	MSG_log(M_TEVENT, LOG_WARN,
		"It seems that the time does not present good, we try to fix it. \n");		
	
	/* Reset the base time */
	base->clock_exception = base->clock_last_ok;
	base->clock_last_ok = clock_now;
	base->clock_base = clock_now;
	base->clock_wait = base->clock_base;
	base->clock_end  = base->clock_wait + base->us_wait;
	
	/* Repair the timestamp of the elements in heap */
	if (!min_heap_empty(&base->heap))
		min_heap_visit(&base->heap, timer_heap_walk_repair, (void *)base);

#ifndef NDEBUG
	min_heap_visit(&base->heap, timer_heap_left, (void *)&clock_now);
#endif
}

int 
timer_try_repair_clock(timer_base_t *base, uint64_t clock_now)
{
	int repair = 0;
	
	assert (base->need_repair_clock);
	if (base->clock_base == clock_now) 
		return 0;
	
	if (base->clock_base > clock_now) 
		repair = 1;	

	if (HTMR_STAT_WAITING & base->stat && !repair) 
		/* We assume that the timer scheduler will wake up from the sleep in 
		 * 5 seconds when it has been overtime */
		repair = (base->us_wait != -1 && 
				clock_now >= (base->clock_end + 5000000));
	
	else if (!min_heap_empty(&base->heap) && !repair) {
		tevent_priv_t *priv = (tevent_priv_t *)min_heap_top(&base->heap)->stack;
		
		/* We assume that the timer scheduler loop will finished in 6 seconds */
		if (priv->clock_end < clock_now) 
			repair = (clock_now - priv->clock_end >= 6000000);
		else 
			repair = (priv->clock_end - clock_now > priv->us_fire + 6000000);
	}

	if (repair) 
		timer_fix_clock(base, clock_now);
	
	/* Ensure that it will be set properly in the multiple threads env */
	if (clock_now > base->clock_last_ok)
		base->clock_last_ok = clock_now;

	return repair;
}

/*--------------------------------default event interface (IO-Select ?) ------------------------*/
typedef struct default_ext_event_opaque_t {
	ext_event_t extev;
	OSPX_pthread_mutex_t lock;
	OSPX_pthread_cond_t  cond;
} default_ext_event_opaque_t;

static int
default_event_pool_wait(timer_base_t *base, void *opaque) 
{
	long ums = -1;
	default_ext_event_opaque_t *evop = opaque;
	
	OSPX_pthread_mutex_lock(&evop->lock);
	if (timer_epw_begin(base, &ums)) {
		if (ums > 0) {
			ums = (ums + 999) / 1000;
			OSPX_pthread_cond_timedwait(&evop->cond, &evop->lock, &ums);
		} else
			OSPX_pthread_cond_wait(&evop->cond, &evop->lock);
		timer_epw_end(base);
	}
	OSPX_pthread_mutex_unlock(&evop->lock);

	return 0;
}

static void
default_event_dispatch(timer_base_t *base, int epw_code, void *opaque)
{
	timer_dispatch(base);
}

static void
default_event_wakeup(void *opaque)
{
	default_ext_event_opaque_t *evop = opaque;
	
	OSPX_pthread_mutex_lock(&evop->lock);
	OSPX_pthread_cond_signal(&evop->cond);
	OSPX_pthread_mutex_unlock(&evop->lock);
}

static void 
default_event_destroy(void *opaque)
{
	default_ext_event_opaque_t *evop = opaque;
	
	MSG_log(M_TEVENT, LOG_INFO,
		"@%s: extev(%p)\n",
		__FUNCTION__, &evop->extev);
	
	OSPX_pthread_mutex_destroy(&evop->lock); 
	OSPX_pthread_cond_destroy(&evop->cond); 
	free(opaque);
	
	/* Flush the global cache */
	if (___smc) 
		smcache_flush(___smc, 0);
}

static ext_event_t *
default_extev()
{
	default_ext_event_opaque_t *op;

	static struct ext_event_t __ev = {
		NULL,
		default_event_pool_wait,
		default_event_dispatch,
		default_event_wakeup,
		default_event_destroy,
		NULL,	
	};
	op = malloc(sizeof(*op));
	if (!op) {
		MSG_log2(M_TEVENT, LOG_ERR,
			"malloc: system is out of memory");
		return NULL;
	}
	
	if ((errno=OSPX_pthread_mutex_init(&op->lock, 0))) {
		MSG_log2(M_TEVENT, LOG_ERR,
			"mutex_init:%s", OSPX_sys_strerror(errno));
		free(op);
		return NULL;
	}
	
	if ((errno=OSPX_pthread_cond_init(&op->cond))) {
		MSG_log2(M_TEVENT, LOG_ERR,
			"cond_init:%s", OSPX_sys_strerror(errno));
		OSPX_pthread_mutex_destroy(&op->lock); 
		free(op);
		return NULL;
	}

	MSG_log(M_TEVENT, LOG_INFO,
		"@%s returns extev(%p)\n",
		 __FUNCTION__, &op->extev);
	
	op->extev = __ev;
	op->extev.opaque = op;
	return &op->extev;
}

