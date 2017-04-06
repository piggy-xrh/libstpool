#ifndef __CPOOL_RT_FACTORY_H__
#define __CPOOL_RT_FACTORY_H__

/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "cpool_factory.h"

/**
 * Factory(eFUNC_F_DYNAMIC_RT)
 */
const cpool_factory_t *const
get_rt_dynamic_factory();

/**
 * Factory(eFUNC_F_DYNAMIC_RT|eFUNC_F_PRIORITY)
 */
const cpool_factory_t *const
get_rt_dynamic_pri_factory();

/**
 * Factory(eFUNC_F_FIXED_RT)
 */
const cpool_factory_t *const
get_rt_fixed_factory();

/**
 * Factory(eFUNC_F_FIXED_RT|eFUNC_F_PRIORITY)
 */
const cpool_factory_t *const
get_rt_fixed_pri_factory();

#endif

