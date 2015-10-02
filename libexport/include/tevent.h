#ifndef __TEVENT_H__
#define __TEVENT_H__
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

#include <stdlib.h>
#if defined(_WIN32) || defined(WIN32) || defined(_WIN64)
#define _WIN
#define inline __inline
#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#else
#define EXPORT
#endif

struct timer_base;
typedef struct timer_base timer_base_t;

typedef struct tevent {
	/* The timer scheduler of the event, it must
	 * be set before user's calling @tevent_add
	 * to schedule this event */
	timer_base_t *base;

	/* The timer callback */
	void (*tmrfire)(struct tevent *);

	/* @opaque is the reserved argument for user */
	void *opaque;
	
	/* The private datas for inner implemetions, 
	 * user should not touch them */
	char stack[0];
} tevent_t;
extern const int g_const_TEVENT_SIZE;

/* The definition of the external event module, user
 * can implement these interfaces requsted by the 
 * module to intergrate the timer scheduler with his
 * apps, the scheduler is just running in the background
 * like that 
 * 
 *    for (;base->run;) {
 *    	epw_code = base->extev->event_pool_wait
 *    	           (   base, 
 *    	               base->extev->opaque
 *    	           );
 *
 *    	base->extev->event_dispatch
 *    	(   base, 
 *    	    epw_code, 
 *    	    base->extev->opaquee
 *    	); 
 *    }
 *
 *    User must implement @event_pool_wait, @event_dispatch,
 * and @event_wakeup.
 */
typedef struct ext_event_t {
	/* @event_init
	 *    It will be called by @timer_ctor if it is not NULL.
	 *
	 * Arguments:
	 *   @opaque  [in] app argument passed by the system
	 *
	 * Return:
	 *    On success, it should return 0.
	 */
	int  (*event_init)(void *opaque);
	
	/* @event_disaptch
	 *     @event_pool_wait is designed to detect the app's events, 
	 * if there are no any app events, it will go to sleep.
	 *
	 *     @timer_epw_begin(base, &us_sleep) must be called with 
	 * the sleep time before user's doing the real sleep actions, 
	 * @timer_epw_begin will return a propriate sleeping time to
	 * the caller according to the status of the timer events.
	 *     
	 *     and also when @tevent_dispatch returns from the sleep, 
	 * @timer_epw_end(base) must be called imediatelly to give a 
	 * notification to the timer scheduler.
	 * 
	 * Arguments:
	 *   @base     [in] the timer scheduler passed by the system
	 *   @epw_code [in] the code returned by @event_pool_wait
	 *   @opaque   [in] app argument passed by the system
	 *
	 * Return:
	 *    It depends on the user's implemention.
	 */
	int  (*event_pool_wait)(timer_base_t *base, void *opaque);
	
	/* @event_diaptch
	 *     Dispatch the events, user can dispatch its app events
	 * in its implemention, but also @timer_dispatch(base) must 
	 * be called in this interface to dispatch the timer events
	 * who is timeout now.
	 * 
	 * Arguments:
	 *   @base     [in] the timer scheduler passed by the system.
	 *   @epw_code [in] the code returned by @event_pool_wait
	 *   @opaque   [in] app argument passed by the system
	 *
	 * Return:
	 *    On success, it should return 0.
	 */
	void (*event_dispatch)(timer_base_t *base, int epw_code, void *opaque);
	
	/* @event_wakeup
	 *     This interface is used to wake @event_pool_wait up,
	 * the timer scheduler may call it to wake @event_pool_wait
	 * up in some situations (exp: A shorter timer event is added
	 * into the timer scheduler)
	 *
	 * Arguments:
	 *   @opaque  [in] app argument passed by the system
	 *
	 * Return:
	 *    None
	 */
	void (*event_wakeup)(void *opaque);

	/* @event_destroy
	 *    It will be called by @timer_dtor if it is not NULL. 
	 *
	 * Arguments:
	 *   @opaque  [in] app argument passed by the system
	 *
	 * Return:
	 *    None
	 */
	void (*event_destroy)(void *opaque);
	
	/* The reserved argumets for APP */
	void *opaque;
} ext_event_t;

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------APIs about the timer event-----------------------------*/
#define tevent_size()  g_const_TEVENT_SIZE
EXPORT void  tevent_init(tevent_t *ev, timer_base_t *base, 
	void (*tmrfire)(struct tevent *), void *opaque, long us_fire);

EXPORT tevent_t *tevent_new(timer_base_t *base, 
	void (*tmrfire)(struct tevent *), void *opaque, long us_fire);

EXPORT void  tevent_delete(tevent_t *ev); 

/* @tevent_base/@tevent_set_base
 *    Get/Set the timer scheduler for the timer event.
 *
 * Arguments:
 *      @base [in] the timer scheduler
 *      @ev   [in] the timer event object
 *
 * Return:
 * 		@tevent_base returns the scheduler of the event.
 */
#define tevent_base(ev) (ev)->base
#define tevent_set_base(base, ev) (ev)->base = base

/* @tevent_timeo/@tevent_set_timeo
 *    Get/Set the delay time of the timer event.
 *
 * Arguments:
 *      @tevent_t  [in] the timer event object
 *      @us_fire   [in] the delay time to triggle the
 *                      timer event.
 * Return:
 * 		@tevent_timeo returns the current delay time 
 * of the timer event.
 */
EXPORT long tevent_timeo(tevent_t *ev);
EXPORT void tevent_set_timeo(tevent_t *ev, long us_fire);

/* @tevent_add
 *    Deleiver the timer event into the timer scheduler.
 *    
 *    If the event's timer callback is being executed, it 
 * will be added into the scheduler automatically after the
 * scheduler's finishing done its previous timer callback. 
 *
 *    If the timer event is in the pending queue, @tevent_add
 * will do noting.
 *
 * Arguments:
 *      @tevent_t  [in] the timer event object
 *
 * Return:
 * 		On success, it returns 0, or -1 will be returned
 * if the parameters of the timer event is ilegal.
 */
EXPORT int  tevent_add(tevent_t *ev);

/* @tevent_set_add
 * 	    Reset the delay time and deliver the timer
 * event into the timer scheduler.
 *
 * Arguments:
 *      @tevent_t  [in] the timer event object
 *      @us_fire   [in] the delay time to tirggle the 
 *                      timer event.
 * Return:
 * 		The same as @tevent_add
 */
EXPORT int tevent_set_add(tevent_t *ev, long us_fire); 

/* @tevent_del
 * 	   If the timer event is in the pending queue.
 * @tevent_del will remove it, or it will do nothing.
 *
 * Arguments:
 *      @tevent_t  [in] the timer event object
 *
 * Return:
 * 		None
 */
EXPORT void tevent_del(tevent_t *ev);

/* @tevent_del_wait
 * 	   If the timer event is in the pending queue,
 * @tevent_del_wait will remove it, or if the timer
 * callback is being executed, @tevent_del_wait will 
 * wait for its done compeletely.
 *
 * Arguments:
 *      @tevent_t  [in] the timer event object
 *
 * Return:
 * 		None
 */
EXPORT void tevent_del_wait(tevent_t *ev);

/*---------------------------------APIs about the timer event env-----------------------------*/
/*@timer_version
 *    y/m/d-version-desc
 */
EXPORT const char *timer_version();

/* @timer_ctor
 * 	   Create a timer scheduler 
 *
 * Arguments:
 *      @n_max_paralle_cbs [in] the max paralle number of the dispatching timer event.
 *                                  if @n_max_paralle_cbs is positive, and the libtevent 
 *                              has been configured with CONFIG_POOL_TIMER, the libtevent
 *                              will load the libstpool to create a task pool with 
 *                              @n_max_paralle_cbs servering threads to dispatch the timer
 *                              event.
 *
 *      @extev             [in] the external event module, if it is NULL, the system will 
 *      		                create a default event module to schedule the timer event.
 *
 * Return:
 * 		On success, a timer scheduler will be returned.
 * 		On error, it returns NULL
 */
EXPORT timer_base_t *timer_ctor(int n_max_paralle_cbs, ext_event_t *extev);

/* @timer_get_pool
 *	   @timer_ctor may load libstpool to create a task pool to schedule
 *the timer events (see @timer_ctor for more details). so i export this
 *API to give users a chances to configure the pool's settings.
 *
 * Arguments:
 *      @base [in]     the timer scheduler. 
 *
 * Return:
 * 	   The handle of the task pool.
 */
EXPORT void *timer_get_pool(timer_base_t *base);

/* @timer_epw_begin
 * 	   User should not call this interface, it is called by the extev 
 * module, See the description of the extev::@event_pool_wait for more 
 * details.
 *
 * Arguments:
 *      @base [in]     the timer scheduler. 
 *      @us   [in/out] the us time for what the extev moudle will sleep.
 *                     (*us it must not be zero, -1 = INFINITE)
 * Return:
 * 	    The real apropriate sleeping time.
 */
EXPORT long  timer_epw_begin(timer_base_t *base, long *us);

/* @timer_epw_end
 * 	  User should not call this interface, it is called by the extev 
 * module, See the description of the extev::@event_pool_wait for more 
 * details.
 *
 * Arguments:
 *      @base [in]     the timer scheduler. 
 *
 * Return:
 * 	    None
 */
EXPORT void  timer_epw_end(timer_base_t *base);

/* @timer_epw_begin
 * 	  Dispatch the timer event, User should not call this interface, 
 * it is called by the extev module, See the description of the 
 * extev::@event_dispatch for more details.
 *
 * Arguments:
 *      @base [in]     the timer scheduler. 
 *
 * Return:
 * 	    None
 */
EXPORT void  timer_dispatch(timer_base_t *base);

/* @timer_dtor
 * 	   Destroy the timer scheduler 
 *
 * Arguments:
 *      @base [in] the timer scheduler env returned by @timer_ctor
 *
 * Return:
 * 	    NULL
 */
EXPORT void  timer_dtor(timer_base_t * base);

#endif
