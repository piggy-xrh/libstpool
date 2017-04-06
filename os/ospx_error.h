#ifndef  __OSPX_ERROR_H__
#define  __OSPX_ERROR_H__
/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is a portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include "ospx_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t OSPX_error_t;

/*----------------error code--------------
   |8 (module)|24 (reserved)|32 (code)|
 ---------------------------------------*/
#define OSPX_MAKERROR(m,code) \
	((OSPX_error_t)((((((uint64_t)(uint8_t)m)) << 56)) | ((uint32_t)-1 & (uint32_t)code)))

#define OSPX_emodule(error) (uint8_t)(error >> 56)
#define OSPX_ecode(error)   ((uint32_t)(error & (uint32_t)-1))

/**
 * the default module 
 */
#define OSPX_M_SYS 0x1  


typedef const char *(*OSPX_ansi_error)(uint32_t code);
/**
 * NOTE:
 * 	 The OSPX_M_SYS error module has been registered in the OSPX_library_init like that below.
 *
 * 	 uint8_t sysm = OSPX_M_SYS;
 * 	 OSPX_error_register(&sysm, "Sys", OSPX_sys_strerror);
 */
EXPORT int  OSPX_error_register(uint8_t *m, const char *desc, OSPX_ansi_error efunc);
EXPORT void OSPX_error_unregister(uint8_t m);
EXPORT void OSPX_error_unregister_all();

EXPORT const char  *OSPX_edesc(OSPX_error_t error);
EXPORT OSPX_ansi_error OSPX_efunc(uint8_t m);

EXPORT void OSPX_setlasterror(OSPX_error_t error);
EXPORT OSPX_error_t OSPX_getlasterror();
EXPORT const char *OSPX_strerror(OSPX_error_t error);

/** Extention */
EXPORT void OSPX_errprefix_append(const char *fmt, ...);
EXPORT const char *OSPX_errprefix();
EXPORT void OSPX_errprefix_clr();
EXPORT void OSPX_setlasterror2(OSPX_error_t error);

#define PUSH_CALL_RESET(error) \
	do {\
		OSPX_setlasterror(error); \
		PUSH_CALL2(ecode); \
	} while (0)

#define PUSH_CALL_SET(error)  \
	do {\
		OSPX_setlasterror2(error); \
		PUSH_CALL2(ecode); \
	} while (0)

#define PUSH_CALL2(ecode)     OSPX_errprefix_append("%s(%u)<", __FUNCTION__, (uint32_t)ecode)
#define PUSH_CALL             OSPX_errperfix_append("%s<", __FUNCTION__);
#ifdef __cplusplus
}
#endif

#endif
