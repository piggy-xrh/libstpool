#ifndef __OSPX_CONFIG_H__
#define __OSPX_CONFIG_H__

/*
 *  COPYRIGHT (C) 2014 - 2020, piggy_xrh
 * 
 *	  Stpool is portable and efficient tasks pool library, it can work on diferent 
 * platforms such as Windows, linux, unix and ARM.  
 *
 *    If you have any troubles or questions on using the library, contact me.
 *
 * 	  (Email: piggy_xrh@163.com  QQ: 1169732280)
 */

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64)
#define _WIN
#if defined(_WIN64)
#define _OS_WIDTH_TYPE_64 
#else
#define _OS_WIDTH_TYPE_32
#endif

#ifdef _USRDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

#else
#define EXPORT
#endif

#if !defined(_OS_WIDTH_TYPE_64) && !defined(_OS_WIDTH_TYPE_32)
#error "_OS_WIDTH_TYPE is undefined !"
#endif

#endif
