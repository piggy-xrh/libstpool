#ifndef __LOG_MSG_H__  
#define __LOG_MSG_H__  

/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  msglog is a simple log library.
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

/** The debug level */
enum {
	LOG_TRACE = 1,
	LOG_DEBUG,
	LOG_INFO = 3,
	LOG_WARN,
	LOG_ERR = 5,
};

#ifdef __cplusplus  
extern "C" {
#endif

/** a breif about the log message */
typedef struct msg_log_brief {
	
	/**
	 * The module from what this log message comes
	 */
	const char *m;

	/**
	 * The debug level of this log message 
	 */
	int level;
} msg_log_brief_t;

/** the log message handler */
typedef struct msg_log_hander {
	/** 
	 * log message filter
	 *
	 * If @log_filter is not NULL and it returns a non-zero value,
	 * @ref MSG_log will discard this debug message. Or Global::\@log_level
	 * will be token into consideration by @ref MSG_log to determine whether
	 * the message should be discarded or not
	 */
	int  (*log_filter)(struct msg_log_hander *mlh, msg_log_brief_t *mlm);
	
	/**
	 * log message handler
	 *
	 * If \@log_handler is not NULL, it will be responsible for handling
	 * the debug messages, Or the messages will be formated with the 
	 * global configurations, and then it will be flushed into the console 
	 */
	void (*log_handler)(struct msg_log_hander *mlh, msg_log_brief_t *mlm, const char *omsg);
	
	/** 
	 * Opaque datas reserved for the handler 
	 */
	void *opaque;

} msg_log_handler_t;

/**
 * A const string to describle version of the libmsglog
 * y/m/d-version-desc
 *
 * @param None
 *
 * @return A const string to describle the current library version.
 */
EXPORT const char *MSG_log_version();

/**
 * Set the global log message handler (Gloal::\@msgh)
 *    
 * the message handler will take the ownership of the right to handle 
 * the debug messages. it means that all of the other global configurations
 * will be ignored if the global log message handler is not NULL.  
 *
 * @param [in] msgh
 *
 * @return None
 *
 * @note If you want to process the debug messages in your own way, 
 * @ref MSG_log_set_handler must be called before your first calling
 * @ref MSG_log or @ref MSG_log2
 */
EXPORT void MSG_log_set_handler(msg_log_handler_t *msgh);

/**
 * Set the global debug level(Global::\@log_level)
 *
 * If Global::\@msgh::log_filter has not been set, all of the debug 
 * messages passed to @ref MSG_log or @ref MSG_log2 will be discarded 
 * if their debug level is smaller than Global::\@log_level.
 *
 * @param [in] level the global debug level that the user wants to set.
 *
 * @return None
 */
EXPORT void MSG_log_set_level(int level);

/**     
 * Get the global debug level (Global::\@log_level)
 */
EXPORT int  MSG_log_get_level();

/** the working model of the global filter entry */
enum eFilterType
{
	/**
	 * Set the global filter entry to be a discarded entry
	 *
	 * If level of the log message is not greater than the level 
	 * that has been configured for the module, the message will
	 * be discarded.
	 */
	eFT_discard,

	/**
	 * Set the global filter entry to be a allowed entry
	 *
	 * If level of the log message is not less than the level 
	 * that has been configured for the module, the message will
	 * be allowed to be processed by the log system.
	 */
	eFT_allow
};

/**
 * Set the working model for the global filter entry
 */
EXPORT void MSG_log_mfilter_set_type(enum eFilterType et);

/**
 *	Add a module into the global filter entry
 */
EXPORT void MSG_log_mfilter_add(const char *m, int level);

/**
 *  Add a module entry into the global filter entry
 *  
 *  @param [in] mentry  the module array
 *  @param [in] lentry  the level array, if it is NULL, the level value 
 *                      of the modules will be set to Gloal::\@log_level
 *
 *  @note the entry is a const string array terminated with NULL
 */
EXPORT void MSG_log_mfilter_add_entry(const char **mentry, int *lentry);

/**
 *  Reset the global filter entry
 *
 *  This interface will clear the global filter entry firstly, and then
 *  call @ref MSG_log_mfilter_add_entry (mentry, lentry)
 */
EXPORT void MSG_log_mfilter_set_entry(const char **mentry, int *lentry);

/**
 *  Remove the module from the global filter entry
 */
EXPORT void MSG_log_mfilter_remove(const char *m);

/**
 *  Remove the module whose description exists in \@mentry from the global filter entry
 */
EXPORT void MSG_log_mfilter_remove_entry(const char **mentry);

/**
 * Check whether the log message should be discarded according to the current
 * global configurations
 */
EXPORT int  MSG_log_should_be_discarded(msg_log_brief_t *const mlm);

/** 
 * Enable or Disable the global clolor attributes (Global::\@color_enable) 
 *
 * If it is enabled, the log messages will be formated with the default color attributes.
 */
EXPORT void MSG_log_enable_color(int enable);

/** 
 * Format the message according to the global configurations and output it to the buffer.
 *
 * @param [out] buff    The received buffer of the formated message
 * @param [in]  bufflen the length of the \@buff
 * @param [in]  mlm     the details about the message
 * @param [in]  omsg    the message who is waiting for being formated
 * 	                
 * @return \@buff
 */
EXPORT const char *MSG_log_buffer(char *buff, int bufflen, msg_log_brief_t *mlm, const char *omsg);

/**
 * Write a debug message to the log system 
 *
 * @ref MSG_log knows how to process the messages according to the global configurations
 *
 * @param [in]  m    a const string to describe the module
 * @param [in] level the debug level. (LOG_XX)
 * @param [in] fmt   the format of the user's arguments
 * @param [in] ...   user's variable arguments
 *
 * @return None
 *
 * @code exp:
 *     #define M_TEVENT "event"
 *
 *     MSG_log(M_TEVENT, LOG_WARN,
 *     	      "Failed to initialize the module: %s\n", strerror(errno));
 *
 *     By default, this log message will be flushed into the console like that.
 *
 *        [event  ]  [WARN ]  Failed to initialize the module: system is out of memory.
 *  @endcode
 */

EXPORT void MSG_log(const char *m, int level, const char *fmt, ...); 

#define MSG_log2(m, l, fmt, ...) \
	MSG_log(m, l, fmt " [%s:@%s:%d]\n",##__VA_ARGS__, __FILE__, __FUNCTION__, __LINE__)   

#ifdef __cplusplus  
}
#endif

#endif
