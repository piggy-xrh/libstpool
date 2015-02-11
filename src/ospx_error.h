#ifndef  __OSPX_ERROR_H__
#define  __OSPX_ERROR_H__

/* <piggy_xrh@163.com>
 */
#include "ospx_type.h"
#include "ospx_errno.h"

#ifdef _WIN32
#undef EXPORT

#ifdef _DLL_IMPORT
#define EXPORT
#else
#define EXPORT __declspec(dllexport)
#endif

#else
#define EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t OSPX_error_t;

/****************error code**************
* |8 (module)|24 (reserved)|32 (code)|
***************************************/
#define OSPX_MAKERROR(m,code) \
	((OSPX_error_t)((((((uint64_t)(uint8_t)m)) << 56)) | ((uint32_t)-1 & (uint32_t)code)))

#define OSPX_emodule(error) (uint8_t)(error >> 56)
#define OSPX_ecode(error)   ((uint32_t)(error & (uint32_t)-1))

/* default module 
 */
#define OSPX_M_SYS 0x1  /*system module*/


typedef const char *(*OSPX_ansi_error)(uint32_t code);
/* NOTE:
 * 	 The OSPX_M_SYS error module has been registered in the OSPX_load like that below.
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

/* Extention */
EXPORT void OSPX_errprefix_append(const char *fmt, ...);
EXPORT const char *OSPX_errprefix();
EXPORT void OSPX_errprefix_clr();
EXPORT void OSPX_setlasterror_ex(OSPX_error_t error, const char *fmt, ...);
#ifdef __cplusplus
}
#endif

#endif
