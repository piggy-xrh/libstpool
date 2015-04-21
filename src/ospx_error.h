/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *	  Stpool is portable and efficient tasks pool library, it can works on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 * 	  blog: http://www.oschina.net/code/snippet_1987090_44422
 */

#ifndef  __OSPX_ERROR_H__
#define  __OSPX_ERROR_H__

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

/* Extention */
EXPORT void OSPX_errprefix_append(const char *fmt, ...);
EXPORT const char *OSPX_errprefix();
EXPORT void OSPX_errprefix_clr();
EXPORT void OSPX_setlasterror_ex(OSPX_error_t error, const char *fmt, ...);
#ifdef __cplusplus
}
#endif

#endif
