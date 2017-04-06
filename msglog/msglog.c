/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  msglog is a simple log library.
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "msglog.h"

#ifdef _WIN  
#include <windows.h>
#define inline __inline  
// MSVC++ 14.0 (Visual Studio 2015) 
#if(_MSC_VER < 1900) 
#define snprintf _snprintf
#endif 
#else
#include <pthread.h>
#endif

#define C_NONE    "\033[m"   
#define C_RED     "\033[0;32;31m"   
#define C_LRED    "\033[1;31m"   
#define C_GREEN   "\033[0;32;32m"   
#define C_LGREEN  "\033[1;32m"  
#define C_BLUE    "\033[0;32;34m"  
#define C_LBLUE   "\033[1;34m"  
#define C_GRAY    "\033[1;30m"  
#define C_LGRAY   "\033[0;37m"  
#define C_CYAN    "\033[0;36m"  
#define C_LCYAN   "\033[1;36m"  
#define C_PURPLE  "\033[0;35m"  
#define C_LPURPLE "\033[1;35m"  
#define C_BROWN   "\033[0;33m"  
#define C_LBROWN  "\033[1;33m"  
#define C_YELLOW  "\033[1;33m"  
#define C_WHITE   "\033[1;37m"  

typedef struct log_oattr {
	const char *ldesc_start;
	const char *ldesc;
	const char *ldesc_end;
	const char *textc_start;
	const char *textc_end;
} log_oattr_t;

static log_oattr_t ___log_oattr[] = {
	{"", "", "", "", ""},
	{C_BROWN"["C_GRAY   , "TRACE" ,  C_BROWN"]", C_GRAY   , C_NONE},
	{C_BROWN"["C_LPURPLE, "DEBUG" ,  C_BROWN"]", C_LPURPLE, C_NONE},   
	{C_BROWN"["C_GREEN  , "INFO " ,  C_BROWN"]", C_GREEN  , C_NONE},  
	{C_BROWN"["C_YELLOW , "WARN " ,  C_BROWN"]", C_YELLOW , C_NONE},
	{C_BROWN"["C_RED    , "ERROR" ,  C_BROWN"]", C_RED    , C_NONE}   
};

static int ___log_color_enabled = 
/** Windows can not translate the color attributes well */
#ifdef _WIN  
						0;
#else
						1;
#endif

static int ___log_level = 
#ifndef NDEBUG  
				LOG_TRACE;
#else
				LOG_WARN;
#endif

static msg_log_handler_t *___log_msgh = NULL;

#define MAX_LOG_FILTER_ENTRY 200   
static const char *___log_filter_mentry[MAX_LOG_FILTER_ENTRY];
static int ___log_filter_lentry[MAX_LOG_FILTER_ENTRY];
static int ___log_entry_idx = 0;
static enum eFilterType ___log_et = eFT_discard;

#define snprintf_ejump(label, ptr, len, ...) \
	do { \
		int fmtlen; \
		if ((len) <= 0 || 0 >= (fmtlen = snprintf((ptr), (len), ##__VA_ARGS__))) \
			goto label; \
		(ptr) += fmtlen; \
		(len) -= fmtlen; \
	} while (0)


static inline const char *
MSG_log_buffer2(char *buff, int bufflen, msg_log_brief_t *mlm, const char *omsg)
{
	time_t current;
	struct tm *ptm;

	if (!mlm->m)
		mlm->m = " ***** ";

#ifndef _WIN
#define get_id()  (unsigned)pthread_self()
#else
#define get_id()  (unsigned)GetCurrentThreadId()
#endif


	/**
	 * Insert the timestamp
	 */
	current = time(NULL);
	ptm     = localtime(&current);
	if (___log_color_enabled)
		snprintf_ejump(RETURN, buff, bufflen,
			"%s%04d-%02d-%02d %02d:%02d:%02d%s ",
			___log_oattr[mlm->level].ldesc_start,
			ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
			ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
			___log_oattr[mlm->level].ldesc_end);
	else
		snprintf_ejump(RETURN, buff, bufflen,
			"%04d-%02d-%02d %02d:%02d:%02d ",
			ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
			ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
	/**
	 * Try to color the message
	 */
	if (___log_color_enabled) 
		snprintf_ejump(RETURN, buff, bufflen, 
			"%s%-9s%s  %s%s%s  %s%0x|%s%s", 
			___log_oattr[mlm->level].ldesc_start,
			mlm->m,
			___log_oattr[mlm->level].ldesc_end,
			___log_oattr[mlm->level].ldesc_start,
			___log_oattr[mlm->level].ldesc,
			___log_oattr[mlm->level].ldesc_end,
			___log_oattr[mlm->level].textc_start, 
			get_id(),
			omsg, 
			___log_oattr[mlm->level].textc_end);
	else 
		snprintf_ejump(RETURN, buff, bufflen,
			"[%-9s]  [%s]  %0x|%s",
			mlm->m,
			___log_oattr[mlm->level].ldesc,
			get_id(), 
			omsg);

RETURN:
	return buff;
}

EXPORT const char *
MSG_log_version()
{
	return "2015/10/15-1.3-libmsglog-mfilter";
}

EXPORT void 
MSG_log_set_level(int level)
{
	___log_level = level;
}

EXPORT int  
MSG_log_get_level()
{
	return ___log_level;
}

EXPORT void
MSG_log_enable_color(int enable)
{
	___log_color_enabled = enable;
}

EXPORT void 
MSG_log_set_handler(msg_log_handler_t *msgh)
{
	___log_msgh = msgh;
}

EXPORT void 
MSG_log_mfilter_set_type(enum eFilterType et)
{
	___log_et = et;
}

EXPORT void 
MSG_log_mfilter_add(const char *m, int level)
{
	int idx;
	
	if (!m || ___log_entry_idx == MAX_LOG_FILTER_ENTRY)
		return;

	for (idx=0; idx<___log_entry_idx; idx++) 
		if (!strcmp(___log_filter_mentry[idx], m)) {
			___log_filter_lentry[idx] = level;
			return;
		}
	___log_filter_mentry[___log_entry_idx] = m;
	___log_filter_lentry[___log_entry_idx] = level;
	++ ___log_entry_idx;
}

EXPORT void 
MSG_log_mfilter_add_entry(const char **mentry, int *lentry)
{
	if (mentry) {
		do {
			MSG_log_mfilter_add(*mentry, lentry ? (*lentry ++) : ___log_level);
		} while (*(++ mentry));
	}
}

EXPORT 
void MSG_log_mfilter_set_entry(const char **mentry, int *lentry) 
{
	___log_entry_idx = 0;

	if (mentry)
		MSG_log_mfilter_add_entry(mentry, lentry);
}

EXPORT void 
MSG_log_mfilter_remove(const char *m)
{
	int idx;

	if (!m || !___log_entry_idx)
		return;

	for (idx=0; idx<___log_entry_idx; idx++) {
		if (!strcmp(___log_filter_mentry[idx], m)) {
			if (idx != ___log_entry_idx - 1) {
				memmove((void *)(___log_filter_mentry + idx), (const void *)(___log_filter_mentry + idx + 1),
					   ___log_entry_idx - idx -1);
				
				memmove(___log_filter_lentry + idx, ___log_filter_lentry + idx + 1,
					   ___log_entry_idx - idx -1);
			}

			-- ___log_entry_idx;
		}
	}
}

EXPORT void 
MSG_log_mfilter_remove_entry(const char **mentry)
{
	if (mentry) {
		do {
			MSG_log_mfilter_remove(*mentry);
		} while (*(++ mentry));
	}
}

static inline int  
___log_discard(const char *m, int level)
{
	int idx;

	if (!___log_entry_idx || !m) 
		return ___log_et == eFT_allow;
	
	for (idx=0; idx<___log_entry_idx; idx++)
		if (!strcmp(___log_filter_mentry[idx], m)) {
			if (level < ___log_filter_lentry[idx])
				return 1;

			if (level == ___log_filter_lentry[idx])
				return ___log_et == eFT_discard;
			
			return 0;
		}

	return ___log_et == eFT_allow;
}

EXPORT int  
MSG_log_should_be_discarded(msg_log_brief_t *const mlm)
{
	return mlm->level < ___log_level || ___log_discard(mlm->m, mlm->level);
}

EXPORT const char *
MSG_log_buffer(char *buff, int bufflen, msg_log_brief_t *mlm, const char *omsg)
{
	return MSG_log_buffer2(buff, bufflen, mlm, omsg);
}

EXPORT void 
MSG_log(const char *m, int level, const char *fmt, ...)
{
	int n;
	char msg[1000], omsg[1100];
	va_list ap;
	
	assert (m);
	assert (level >= LOG_TRACE && level <= LOG_ERR);
	
	/**
	 * Before formating the message , we give a chance to 
	 * the message handler to filt the log messages 
	 */
	if (___log_msgh && ___log_msgh->log_filter) {
		msg_log_brief_t mlm = {m, level};
		
		if ((*___log_msgh->log_filter)(___log_msgh, &mlm))
			return;
	
	/**
	 * If there is none message filter, then we check the 
	 * global configuration to determine whether we should
	 * discard this message or not 
	 */
	} else if (level < ___log_level || ___log_discard(m, level))
		return;
		
	/**
	 * Format the message 
	 */
	va_start(ap, fmt);
	n = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if(!(n > -1 && n < sizeof(msg))) 
		return;
		
	/**
	 * If there is a message handler, we just deliver the message 
	 * to the handler directly 
	 */
	if (___log_msgh && ___log_msgh->log_handler) {
		msg_log_brief_t mlm = {m, level};
		
		(*___log_msgh->log_handler)(___log_msgh, &mlm, msg);
		return;
	}
	
	/**
	 * Format the message with the global configurations 
	 */
	{
		msg_log_brief_t mlm = {m, level};
		
		MSG_log_buffer2(omsg, sizeof(omsg), &mlm, msg);
	}

	/**
	 * If it goes here, we just output the message to the console 
	 */
	fputs( 
			omsg,
			level < LOG_WARN ? stdout : stderr
		);
}
