#ifndef __CPOOL_RT_METHOD_H__
#define __CPOOL_RT_METHOD_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_method.h"
#include "cpool_rt_struct.h"

/** Method sets for the Core */
int  cpool_rt_core_ctor(void *priv);
void cpool_rt_core_notifyl(void *priv, eEvent_t event);
void cpool_rt_core_dtor(void *priv);
long cpool_rt_core_err_reasons(basic_task_t *ptask);
int  cpool_rt_core_gettask(void *priv, thread_t *self);
void cpool_rt_core_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons);
int  cpool_rt_core_pri_gettask(void *priv, thread_t *self);
void cpool_rt_core_pri_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons);
int  cpool_rt_core_dynamic_gettask(void *priv, thread_t *self);
void cpool_rt_core_dynamic_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons);
int  cpool_rt_core_dynamic_pri_gettask(void *priv, thread_t *self);
void cpool_rt_core_dynamic_pri_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons);

/** Method sets for the BASIC interfaces */
int    cpool_rt_remove_all(void * ins, int dispatched_by_pool);
int    cpool_rt_suspend(void * ins, long ms);
int    cpool_rt_mark_all(void * ins, long lflags);
int    cpool_rt_mark_cb(void * ins, Visit_cb cb, void *cb_arg);
int    cpool_rt_wait_all(void * ins, long ms);
struct cpool_stat *cpool_rt_stat(void * ins, struct cpool_stat *stat);
char * cpool_rt_scheduler_map_dump(void * ins, char *buff, size_t bufflen);
void   cpool_rt_set_oaattr(void * ins, struct cpool_oaattr *attr);
void   cpool_rt_get_oaattr(void * ins, struct cpool_oaattr *attr);
	
int    cpool_rt_task_init(void * ins, ctask_t *ptask);
int    cpool_rt_task_queue(void * ins, ctask_t *ptask);
int    cpool_rt_task_remove(void * ins, ctask_t *ptask, int dispatched_by_pool);
void   cpool_rt_task_mark(void * ins, ctask_t *ptask, long lflags);
long   cpool_rt_task_stat(void * ins, ctask_t *ptask, long *vm);
void   cpool_rt_throttle_ctl(void * ins, int on);
int    cpool_rt_throttle_wait(void * ins, long ms);

/** Create a routine pool instance */
int  cpool_rt_create_instance(cpool_rt_t **p_rtp, const char *desc, int max, int min, int pri_q_num, int suspend, long lflags);
void cpool_rt_free_instance(cpool_rt_t *rtp);

#endif
