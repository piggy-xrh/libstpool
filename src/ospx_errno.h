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

#ifndef __OSPX_ERRNO_H__
#define __OSPX_ERRNO_H__
#include <errno.h>

#ifdef _WIN32
#undef EXPORT
#ifdef _DLL_IMPORT
#define EXPORT
#else
#define EXPORT __declspec(dllexport)
#endif

/* We make the error codes start from 40000 to
 * prevent it from confilicting with the system 
 * errno.
 */
#define EXTEND_BASE_ERR 40000
#define EOFF(x) (EXTEND_BASE_ERR + x)

#define ENETNOTINITIALIZED EOFF(0)
#define ENETDOWN     EOFF(1)
#define ENETRESET    EOFF(2)
#define EWOULDBLOCK  EOFF(3)
#define EOVERFLOW    EOFF(4)
#define EAFNOSUPPORT EOFF(5)
#define ENOTSOCK     EOFF(6)
#define EINPROGRESS  EOFF(7)
#define EPROTONOSUPPORT EOFF(8)
#define ENOTCONN     EOFF(9)
#define EOPNOTSUPP   EOFF(10)
#define EMSGSIZE     EOFF(11)
#define EHOSTUNREACH EOFF(12)
#define ECONNRESET   EOFF(13)
#define ETIMEDOUT    EOFF(14)
#define EEDESTADDRREQ EOFF(15)
#define ENETUNREACH   EOFF(16)
#define EADDRINUSE    EOFF(17)
#define EALREADY      EOFF(18)
#define ECONNREFUSED  EOFF(19)
#define EDESTADDRREQ  EOFF(20)
#define EISCONN       EOFF(21)
#define EABORTED      EOFF(22)
#define ESYSNOTREADY  EOFF(23)
#define EVER          EOFF(24)
#define EPROTOTYPE    EOFF(25)

#else
#define EXPORT
#endif

#define OSPX_ERR_END  50000
#include "ospx_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NOTE: We should call OSPX_sys_strerror rather
 * than calling strerror to get the errno description 
 */
EXPORT const char *OSPX_sys_strerror(uint32_t code);

#ifdef __cplusplus
}
#endif
#endif
