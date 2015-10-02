#ifndef __LOG_MSG_H__
#define __LOG_MSG_H__

/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  msglog is simple log library.
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64)
#define _WIN
#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#else
#define EXPORT
#endif

/* The debug level */
enum {
	LOG_TRACE,
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERR,
};

#ifdef __cplusplus
extern "C"
#endif

typedef struct msg_log_module {
	const char *m;
	int level;
} msg_log_module_t;

typedef struct msg_log_hander {
	/* If @log_filter is not NULL and it returns a non-zero value,
	 * @MSG_log will discard this debug message. Or Global::@log_level
	 * will be token into consideration by @MSG_log to determine whether
	 * the message should be discarded or not*/
	int  (*log_filter)(struct msg_log_hander *mlh, msg_log_module_t *mlm);
	
	/* If @log_handler is not NULL, it will be responsible for handling
	 * the debug messages, Or the messages will be formated with the 
	 * global configurations, and then it will be flushed into the console */
	void (*log_handler)(struct msg_log_hander *mlh, msg_log_module_t *mlm, const char *omsg);
	
	/* Opaque datas reserved for the handler */
	void *opaque;

} msg_log_handler_t;

/*@MSG_log_version
 *    y/m/d-version-desc
 *
 * Arguments:
 * 		None
 *
 * Return:
 *    A const string to describle the current library version.
 */
EXPORT const char *MSG_log_version();

/*@MSG_log_set_handler
 *    Set the global debug message handler(Global::@msgh), the 
 * message handler will take the ownership of the right to handle
 * the debug messages. it means that all of the other global 
 * configurations will be ignored if the message handler want
 * to process the debug messages. see the definition of the 
 * structure msg_log_handler_t for more details.
 *	
 *	  If you want to process the debug messages in your own way, 
 * @MSG_log_set_handler must be called before your first calling
 * @MSG_log(2).
 *
 * Arguments:
 * 		msgh [in]  the messsage handler
 *
 * Return:
 *    None
 */
EXPORT void MSG_log_set_handler(msg_log_handler_t *msgh);

/* @MSG_log_set_level
 *     Set the global debug level(Global::@log_level), If 
 * Global::@msgh::log_filter has not been set, all of the 
 * debug messages passed to @MSG_log(2) will be discarded 
 * if their debug level is smaller than Global::@log_level.
 *
 * Arguments:
 * 	    level [in]  the global debug level that the user 
 * 	                want to set.
 *
 * Return:
 * 	    None
 */
EXPORT void MSG_log_set_level(int level);

/* @MSG_log_get_level
 *     Get the global debug level
 *
 * Arguments:
 * 	   None
 *
 * Return:
 * 	   Global::@log_level
 */
EXPORT int  MSG_log_get_level();

/* @MSG_log_enable_color
 *    Enable or Disable the global clolor attributes
 * (Global::@color_enable), If it is a non-zero value, 
 * the debug messages will be formated with the default 
 * color attributes.
 *
 * Arguments:
 * 	    enable [in] 1(enable), 0(disable) 
 * 	                
 * Return:
 * 	    None
 */

EXPORT void MSG_log_enable_color(int enable);

/* @MSG_log_buffer
 *    Format the message according to the global configurations
 * and output it to the buffer.
 *
 * Arguments:
 * 	    buff    [out] The received buffer of the formated message
 * 	    bufflen [in]  the length of the @buff
 * 	    mlm     [in]  the details about the message
 * 	    omsg    [in]  the message who is waiting for being formated
 * 	                
 * Return:
 * 	    @buff
 */
EXPORT const char *MSG_log_buffer(char *buff, size_t bufflen, msg_log_module_t *mlm, const char *omsg);

/* @MSG_log
 *     Write a debug message to the log system, @MSG_log knows how 
 * to process the messages according to the global configurations
 *
 * Arguments:
 * 	    m    [in] a const string to describe the module
 * 	   level [in] the debug level. (LOG_XX)
 * 	   fmt   [in] the format of the user's arguments
 * 	   ...   [in] user's variable arguments
 *
 * Return:
 * 	    None
 *
 * exp:
 *     #define M_TEVENT "event"
 *
 *     MSG_log(M_TEVENT, LOG_WARN,
 *     	      "Failed to initialize the module: %s\n", strerror(errno));
 *
 *     By default, this log message will be flushed into the console like that.
 *
 *        [event  ]  [WARN ]  Failed to initialize the module: system is out of memory.
 */

EXPORT void MSG_log(const char *m, int level, const char *fmt, ...); 

#define MSG_log2(m, l, fmt, ...) \
	MSG_log(m, l, fmt " [%s:@%s:%d]\n",##__VA_ARGS__, __FILE__, __FUNCTION__, __LINE__ );

#ifdef __cplusplus
}
#endif

#endif
