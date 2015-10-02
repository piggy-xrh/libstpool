/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  msglog is simple log library.
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include "msglog.h"

#ifdef _WIN
#define inline __inline
#define snprintf _snprintf
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
	{C_BROWN"["C_GRAY   , "TRACE" ,  C_BROWN"]", C_GRAY   , C_NONE},
	{C_BROWN"["C_LPURPLE, "DEBUG" ,  C_BROWN"]", C_LPURPLE, C_NONE},   
	{C_BROWN"["C_GREEN  , "INFO " ,  C_BROWN"]", C_GREEN  , C_NONE},  
	{C_BROWN"["C_YELLOW , "WARN " ,  C_BROWN"]", C_YELLOW , C_NONE},
	{C_BROWN"["C_RED    , "ERROR" ,  C_BROWN"]", C_RED    , C_NONE}   
};

static int ___log_color_enabled = 
/* Windows can not translate the color attributes well */
#ifdef _WIN
						0;
#else
						1;
#endif

static int ___log_level = LOG_TRACE;
static msg_log_handler_t *___log_msgh = NULL;

static inline const char *
MSG_log_buffer2(char *buff, size_t bufflen, msg_log_module_t *mlm, const char *omsg)
{
	if (___log_color_enabled) {
		snprintf(buff, bufflen, 
			"%s%-8s%s  %s%s%s  %s%s%s", 
			___log_oattr[mlm->level].ldesc_start,
			mlm->m,
			___log_oattr[mlm->level].ldesc_end,
			___log_oattr[mlm->level].ldesc_start,
			___log_oattr[mlm->level].ldesc,
			___log_oattr[mlm->level].ldesc_end,
			___log_oattr[mlm->level].textc_start, 
			omsg, 
			___log_oattr[mlm->level].textc_end);
	} else 
		snprintf(buff, bufflen,
			"[%-8s]  [%s]  %s",
			mlm->m,
			___log_oattr[mlm->level].ldesc,
			omsg);
	
	return buff;
}

EXPORT const char *
MSG_log_version()
{
	return "2015/09/11-1.0-libmsglog-desc";
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

EXPORT const char *
MSG_log_buffer(char *buff, size_t bufflen, msg_log_module_t *mlm, const char *omsg)
{
	return MSG_log_buffer2(buff, bufflen, mlm, omsg);
}

EXPORT void 
MSG_log(const char *m, int level, const char *fmt, ...)
{
	int n;
	char msg[1000], omsg[1100];
	msg_log_module_t mlm = {m, level};
	va_list ap;
	
	assert (level >= LOG_TRACE && level <= LOG_ERR);
	
	/* Before formating the message , we give a chance to 
	 * the message handler to filter the log messages */
	if (___log_msgh && ___log_msgh->log_filter) {
		if ((*___log_msgh->log_filter)(___log_msgh, &mlm))
			return;
	
	/* If there is none message filter, then we check the 
	 * global configuration to determine whether we should
	 * discard this message or not */
	} else if (level < ___log_level)
		return;
	
	/* Format the message */
	va_start(ap, fmt);
	n = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if(!(n > -1 && n < sizeof(msg))) 
		return;
	
	if (!m)
		m = " ***** ";
	
	/* If there is a message handler, we just deliver the message 
	 * to the handler directly */
	if (___log_msgh && ___log_msgh->log_handler) {
		(*___log_msgh->log_handler)(___log_msgh, &mlm, msg);
		return;
	}
	MSG_log_buffer2(omsg, sizeof(omsg), &mlm, msg),
	
	/* If it goes here, we just output the message to the console */
	fputs( 
			omsg,
			level < LOG_WARN ? stdout : stderr
		);
}
