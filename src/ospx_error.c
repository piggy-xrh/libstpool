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

#include <stdarg.h>
#include <assert.h>
#include "ospx.h"
#include "ospx_errno.h"
#include "ospx_error.h"

/* NOTE: g_ospx_key is definited in ospx.c
 */
extern OSPX_pthread_key_t g_ospx_key;

typedef struct OSPX_em_t {
	struct OSPX_em_t *next;
	uint8_t m;
	char   *desc;
	OSPX_ansi_error efunc;
} OSPX_em_t;

struct forcomplier {
	uint8_t ectx_bitmap[32];
	OSPX_em_t *em;
#ifndef _DISABLE_OPSX_ERR_THREAD_SAFE
	OSPX_pthread_rwlock_t lock;
#endif
} ospx_ectx;

#ifdef _DISABLE_OSPX_ERR_THREAD_SAFE
#define RWLOCK_INIT(ctx)           OSPX_pthread_rwlock_init(&(ctx)->lock)
#define RWLOCK_READER_ACQUIRE(ctx) OSPX_pthread_rwlock_rdlock(&(ctx)->lock)
#define RWLOCK_WRITER_ACQUIRE(ctx) OSPX_pthread_rwlock_wrlock(&(ctx)->lock)
#define RWLOCK_RELEASE(ctx)        OSPX_pthread_rwlock_unlock(&(ctx)->lock)
#else
#define RWLOCK_INIT(ctx)            0
#define RWLOCK_READER_ACQUIRE(ctx)  0
#define RWLOCK_WRITER_ACQUIRE(ctx)  0
#define RWLOCK_RELEASE(ctx)         0
#endif

#define OSPX_m_isfree(m) (!BIT_get(ospx_ectx.ectx_bitmap, m+1))
#define OSPX_m_used(m) BIT_set(ospx_ectx.ectx_bitmap, m+1)
#define OSPX_m_del(m)  BIT_clr(ospx_ectx.ectx_bitmap, m+1)
#define OSPX_m_new(m) \
	{ int i=0;\
		for (;i<255; i++) {\
			if (OSPX_m_isfree(i)) {\
				m = i;\
				break;\
			}\
		}\
	}

OSPX_em_t *OSPX_ctx(uint8_t m) {	
	OSPX_em_t *ctx = ospx_ectx.em;

	if (ctx && m != ctx->m)  	
		for (;ctx;ctx=ctx->next) 
			if (m == ctx->next->m) 
				break;
	return ctx;
}

EXPORT
const char *OSPX_edesc(OSPX_error_t error) {
	const char *m = "Unkown";

	RWLOCK_READER_ACQUIRE(&ospx_ectx);
	if (!OSPX_m_isfree(OSPX_emodule(error))) {
		OSPX_em_t *ctx = OSPX_ctx(OSPX_emodule(error));
		
		if (ospx_ectx.em != ctx) 
			ctx = ctx->next;	
		m = ctx->desc;
	}
	RWLOCK_RELEASE(&ospx_ectx);

	return m;
}

EXPORT
OSPX_ansi_error OSPX_efunc(uint8_t m) {
	OSPX_ansi_error efunc = NULL;

	RWLOCK_READER_ACQUIRE(&ospx_ectx);
	if (!OSPX_m_isfree(m)) {
		OSPX_em_t *ctx = OSPX_ctx(m);
		
		if (ospx_ectx.em != ctx) 
			ctx = ctx->next;	
		efunc = ctx->efunc;
	}
	RWLOCK_RELEASE(&ospx_ectx);

	return efunc;
}

EXPORT
int OSPX_error_register(uint8_t *m, const char *desc, OSPX_ansi_error efunc) {	
	static int slinitializied = 0;	
	OSPX_em_t *ectx;

	if (!slinitializied) {
		memset(&ospx_ectx, 0, sizeof(ospx_ectx));
		if ((errno = RWLOCK_INIT(&ospx_ectx)))
			return -1;
		slinitializied = 1;
	}
	
	ectx = (OSPX_em_t *)calloc(1, sizeof(OSPX_em_t) + (desc ? (strlen(desc) + 1) : 0));
	if (!ectx) {
		errno = ENOMEM;
		return -1;
	}	
	ectx->desc = (char *)(ectx + 1);
	ectx->m    = *m;
	ectx->efunc = efunc;
	if (desc)
		strcpy(ectx->desc, desc); 
	
	RWLOCK_WRITER_ACQUIRE(&ospx_ectx);
	if (!OSPX_m_isfree(ectx->m)) 
		OSPX_m_new(ectx->m);

	if (!ospx_ectx.em)
		ospx_ectx.em = ectx;
	else {
		ectx->next = ospx_ectx.em;
		ospx_ectx.em = ectx;
	}
	*m = ectx->m;
	OSPX_m_used(ectx->m);
	RWLOCK_RELEASE(&ospx_ectx);
	
	return 0;	
}

EXPORT
void OSPX_error_unregister(uint8_t m) {	
	RWLOCK_WRITER_ACQUIRE(&ospx_ectx);
	if (!OSPX_m_isfree(m)) {
		OSPX_em_t *ctx;
		
		ctx = OSPX_ctx(m);	
		if (ctx == ospx_ectx.em) {
			ospx_ectx.em = ospx_ectx.em->next;
			free(ctx);
		} else {
			OSPX_em_t *ctx0;
					
			ctx0 = ctx->next;
			ctx->next = ctx0->next;
			free(ctx0);
		}
		OSPX_m_del(m);
	}
	RWLOCK_RELEASE(&ospx_ectx);
}

EXPORT
void OSPX_setlasterror(OSPX_error_t error) {
	OSPX_tls_t *tls;
	
	tls = (OSPX_tls_t *)OSPX_pthread_getspecific(g_ospx_key);
	
	/* If the thread is not created by our library, 
	 * and @OSPX_library_init has not been called by 
	 * user, we just ignore it .
	 */	
	if (tls) {
		tls->error = error;	
		if (tls->errprefix) {
			free(tls->errprefix);
			tls->errprefix = 0;
			tls->errprefix_bufflen = 0;
		}
	}
}

EXPORT
OSPX_error_t OSPX_getlasterror() {
	OSPX_tls_t *tls;
	
	tls = (OSPX_tls_t *)OSPX_pthread_getspecific(g_ospx_key);	
	if (!tls)
		return OSPX_MAKERROR(OSPX_M_SYS, 0);	
	
	return (OSPX_error_t)tls->error;
}


EXPORT
const char *OSPX_strerror(OSPX_error_t error) {
	int handled = 0;
	const char *err = "Unkown";	
	uint8_t m = OSPX_emodule(error);

	RWLOCK_READER_ACQUIRE(&ospx_ectx);
	if (!OSPX_m_isfree(m)) {	
		OSPX_em_t *ctx = OSPX_ctx(m);
		
		assert(ctx);
		if (ospx_ectx.em != ctx) 
			ctx = ctx->next;	
			
		if (ctx->efunc) {
			handled = 1;
			err = ctx->efunc(OSPX_ecode(error));
		}
	}
	
	/* We try to process the error code by default if
	 * we haven't found the propriate handle */
	if (!handled && OSPX_ERR_END >= OSPX_ecode(error)) 
		err = OSPX_sys_strerror(OSPX_ecode(error));
	RWLOCK_RELEASE(&ospx_ectx);
	
	return err;	
}

EXPORT
const char *OSPX_sys_strerror(uint32_t code) {
#ifdef _WIN32
	static struct {
		uint32_t error;
		const char *desc;
	} winerr[] = {
		{ENETNOTINITIALIZED, "A successful WSAStartup call must occur before using this function"},
		{ENETDOWN,        "The network subsystem or the associated service provider has failed"},
		{ENETRESET,       "The connection has been broken due to keep-alive activity detecting"
						  "a failure while the operation was in progress"},
		{EWOULDBLOCK,     "Resource temporarily unavailable"},
		{EOVERFLOW,       "Value too large for defined data type"},
		{EAFNOSUPPORT,    "Address family not supported by protocol"},
		{ENOTSOCK,        "Socket operation on non-socket"},
		{EINPROGRESS,     "Operation now in progress"},
		{EPROTONOSUPPORT, "Protocol not supported"},
		{ENOTCONN,        "Transport endpoint is not connected"},
		{EOPNOTSUPP,      "Operation not supported"},
		{EMSGSIZE,        "Message too long"},
		{EHOSTUNREACH,    "No route to host"},
		{ECONNRESET,      "Connection reset by peer"},
		{ETIMEDOUT,       "Connection timed out"},
		{EDESTADDRREQ,    "Destination address required"},
		{ENETUNREACH,     "Network is unreachable"},
		{EADDRINUSE,      "Address already in use"},
		{EALREADY,        "Operation already in progress"},
		{ECONNREFUSED,    "Connection refused"},
		{EDESTADDRREQ,    "Destination address required"},
		{EISCONN,     	  "Transport endpoint is already connected"},
		{EABORTED,        "The virtual circuit was terminated due to a time-out or other failure"},
		{EPROTOTYPE,      "The specified protocol is the wrong type for this socket"},
		{EVER,            "The version of Windows Sockets support requested is not provided by"
						  "this particular Windows Sockets implementation"},
		{ESYSNOTREADY,    "The underlying network subsystem is not ready for network communication"}
	};
	static size_t wesize = sizeof(winerr)/sizeof(*winerr);
	
	if (code >= EXTEND_BASE_ERR) {	
		size_t index;
		const char *err = "Unkown";

		for (index=0; index<wesize; ++index) {
			if (winerr[index].error == code) {
				err = winerr[index].desc;
				break;
			}
		}
		return err;
	}
#endif
	return strerror(code);
}

/*********************************EXTENTION*******************************/
static char *
errprefix_dump(char *errprefix, uint16_t *bufflen, const char *append) {
	size_t left = 0;
	
	if (errprefix) {
		strcat(errprefix, "/");
		left = *bufflen - strlen(errprefix) - 1;
	}
	
	if (strlen(append) > left) {
		size_t blklen = *bufflen + strlen(append) - left + 50;
		char *newblk  = (char *)realloc(errprefix, blklen); 
		
		if (!newblk) 
			return errprefix;	
		
		/* Reset the buffer */
		if (!errprefix)
			newblk[0] = '\0';
		errprefix = newblk;
		*bufflen  = blklen;
	}
	strcat(errprefix, append);
	return errprefix;
}

EXPORT 
void OSPX_errprefix_append(const char *fmt, ...) {
	OSPX_tls_t *tls;
	
	tls = (OSPX_tls_t *)OSPX_pthread_getspecific(g_ospx_key);
	if (tls && fmt) {
		char errprefix[500];
		va_list ap;

		va_start(ap, fmt);
		vsnprintf(errprefix, sizeof(errprefix), fmt, ap);
		va_end(ap);	
		tls->errprefix = errprefix_dump(tls->errprefix, &tls->errprefix_bufflen, errprefix);	
	}
}

EXPORT 
const char *OSPX_errprefix() {
	OSPX_tls_t *tls;
	
	tls = (OSPX_tls_t *)OSPX_pthread_getspecific(g_ospx_key);
	
	return tls ? tls->errprefix : NULL;
}

EXPORT 
void OSPX_errprefix_clr() {
	OSPX_tls_t *tls;
	
	tls = (OSPX_tls_t *)OSPX_pthread_getspecific(g_ospx_key);
	if (tls && tls->errprefix) {
		free(tls->errprefix);
		tls->errprefix = 0;
		tls->errprefix_bufflen = 0;
	}
}

EXPORT 
void OSPX_setlasterror_ex(OSPX_error_t error, const char *fmt, ...) {
	OSPX_tls_t *tls;
	
	tls = (OSPX_tls_t *)OSPX_pthread_getspecific(g_ospx_key);
	if (tls) {
		tls->error = error;	
		if (fmt) {
			char errprefix[500];
			va_list ap;

			va_start(ap, fmt);
			vsnprintf(errprefix, sizeof(errprefix), fmt, ap);
			va_end(ap);
			
			/* Clear the prefix buffer */
			if (tls->errprefix)
				tls->errprefix[0] = '\0';
			tls->errprefix = errprefix_dump(tls->errprefix, &tls->errprefix_bufflen, errprefix);	
		}
	}
}
