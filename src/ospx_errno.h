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
