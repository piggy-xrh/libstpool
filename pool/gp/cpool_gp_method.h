#ifndef __CPOOL_GP_METHOD_H__
#define __CPOOL_GP_METHOD_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_method.h"
#include "cpool_core_struct.h"
#include "cpool_gp_struct.h"

/** Method sets for the Core */
int  cpool_gp_core_ctor(void *priv);
void cpool_gp_core_notifyl(void *priv, eEvent_t event);
void cpool_gp_core_dtor(void *priv);
int  cpool_gp_core_gettask(void *priv, thread_t *self);
void cpool_gp_core_finished(void *priv, thread_t *self, basic_task_t *ptask, long eReasons);
long cpool_gp_core_err_reasons(basic_task_t *ptask);

/** Method sets for the BASIC interfaces */
int   cpool_gp_suspend(void *ins, long ms);
int   cpool_gp_remove_all(void *ins, int dispatched_by_pool);
int   cpool_gp_mark_all(void *ins, long lflags);
int   cpool_gp_wait_all(void *ins, long ms);
int   cpool_gp_wait_cb(void *ins, Visit_cb cb, void *cb_arg, long ms);
int   cpool_gp_mark_cb(void *ins, Visit_cb cb, void *cb_arg);

struct cpool_stat *cpool_gp_stat(void *ins, struct cpool_stat *stat);
char * cpool_gp_scheduler_map_dump(void *ins, char *buff, size_t bufflen);

int   cpool_gp_task_queue(void *ins, ctask_t *ptask);
int   cpool_gp_task_remove(void *ins, ctask_t *ptask, int dispatched_by_pool);
void  cpool_gp_task_detach(void *ins, ctask_t *ptask);
void  cpool_gp_task_mark(void *ins, ctask_t *ptask, long lflags);
long  cpool_gp_task_stat(void *ins, ctask_t *ptask, long *vm);
void  cpool_gp_throttle_ctl(void *ins, int on);
int   cpool_gp_throttle_wait(void *ins, long ms);
int   cpool_gp_wait_any(void *ins, long ms);
int   cpool_gp_task_wsync(void *ins, ctask_t *ptask);
int   cpool_gp_task_wait(void *ins, ctask_t *ptask, long ms);
int   cpool_gp_task_wait_any(void *ins, ctask_t *entry[], int n, long ms);

/** Method sets for the ADVACNED interfaces */
int   cpool_gp_entry_create(void *ins, const char *desc, int pri_q_num, int suspend);
void  cpool_gp_entry_delete(void *ins, int gid);
int   cpool_gp_entry_id(void *ins, const char *desc);
char *cpool_gp_entry_desc(void *ins, int gid, char *desc_buff, size_t len);
int   cpool_gp_entry_stat(void *ins, int gid, struct ctask_group_stat *gstat);
int   cpool_gp_entry_stat_all(void *ins, struct ctask_group_stat **gstat);
int   cpool_gp_entry_suspend(void *ins, int gid, long ms);
int   cpool_gp_entry_suspend_all(void *ins, long ms);
void  cpool_gp_entry_resume(void *ins, int gid);
void  cpool_gp_entry_resume_all(void *ins);
int   cpool_gp_entry_setattr(void *ins, int gid, struct scheduler_attr *attr);
int   cpool_gp_entry_getattr(void *ins, int gid, struct scheduler_attr *attr);
void  cpool_gp_entry_throttle_ctl(void *ins, int gid, int enable);
int   cpool_gp_entry_throttle_wait(void *ins, int gid, long ms);
int   cpool_gp_entry_remove_all(void *ins, int gid, int dispatched_by_pool);
int   cpool_gp_entry_mark_all(void *ins, int gid, long lflags);
int   cpool_gp_entry_mark_cb(void *ins, int gid, Visit_cb wcb, void *wcb_arg);
int   cpool_gp_entry_wait_all(void *ins, int gid, long ms);
int   cpool_gp_entry_wait_cb(void *ins, int gid, Visit_cb wcb, void *wcb_arg, long ms);
int   cpool_gp_entry_wait_any(void *ins, int gid, long ms);

/** Create a routine pool instance */
int  cpool_gp_create_instance(cpool_gp_t **p_gpool, const char *desc, int max, int min, int pri_q_num, int suspend, long lflags);
void cpool_gp_free_instance(cpool_gp_t *gpool);

#endif

