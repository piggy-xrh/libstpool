/* 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "ospx.h"
#include "msglog.h"
#include "cpool_factory.h"

#define M_DICTIONARY "Glb-dic"
#define MAX_FAC_ENTRY 15

int __fac_idx = 0, __fac_enum_idx = 0;
struct cpool_factory_entry {
	const char *desc;
	const cpool_factory_t *fac;
} __fac[MAX_FAC_ENTRY];

int 
add_factory(const char *fac_desc, const cpool_factory_t *const fac)
{
	int idx;
	
	MSG_log(M_DICTIONARY, LOG_INFO,
		"add Factory(\"%s\") ...\n",
		fac_desc);

	if (__fac_idx == MAX_FAC_ENTRY) {
		MSG_log2(M_DICTIONARY, LOG_ERR,
			"Factory entry is full.");
		
		return -1;
	}
	
	/**
	 * We try to cover the factory according to the description 
	 */
	for (idx=0; idx<__fac_idx; idx++) {
		if (!strcmp(__fac[idx].desc, fac_desc)) {
			if (!memcmp(fac, __fac[idx].fac, sizeof(*fac)))
				return 0;

			__fac[idx].fac = fac;
			return 0;
		}
	}
	
	__fac[__fac_idx].desc = fac_desc;
	__fac[__fac_idx ++].fac = fac;
	return 0;
}

#include "gp/cpool_gp_factory.h"
#include "rt/cpool_rt_factory.h"

static OSPX_pthread_once_t __octl = OSPX_PTHREAD_ONCE_INIT;

static void 
__default_factory_loading()
{	
	add_factory("dynamic_group",  get_gp_dynamic_factory());
	add_factory("dynamic_rt",     get_rt_dynamic_factory());
	add_factory("dynamic_rt_pri", get_rt_dynamic_pri_factory());
	add_factory("fixed_rt",       get_rt_fixed_factory());
	add_factory("fixed_rt_pri",   get_rt_fixed_pri_factory());
}

const 
cpool_factory_t *get_factory(const char *fac_desc)
{
	int idx;

	OSPX_pthread_once(&__octl, __default_factory_loading);
	
	for (idx=0; idx<__fac_idx; idx++) 
		if (!strcmp(__fac[idx].desc, fac_desc))
			return __fac[idx].fac;

	return NULL;
}

const cpool_factory_t *
first_factory(const char **p_fac_desc)
{
	OSPX_pthread_once(&__octl, __default_factory_loading);
	
	__fac_enum_idx = 1;	
	*p_fac_desc = __fac[0].desc;
	
	return __fac_idx ? __fac[0].fac : NULL;
}

const cpool_factory_t *
next_factory(const char **p_fac_desc)
{
	if (__fac_enum_idx >= __fac_idx)
		return NULL;
	
	*p_fac_desc = __fac[__fac_enum_idx].desc;

	return __fac[__fac_enum_idx ++].fac;
}
