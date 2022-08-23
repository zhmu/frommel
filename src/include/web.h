/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <sys/types.h>
#include <netipx/ipx.h>
#include "config.h"

#ifndef __WEB_H__
#define __WEB_H__

#define    RFC1123FMT	"%a, %d %b %Y %H:%M:%S GMT"

// WEB_REALM is the realm we say we are
#define	   WEB_REALM			"FART"

// WEB_TEMPLATE_xxx are the template names
#define    WEB_TEMPLATE_HEADER		"header.html"
#define    WEB_TEMPLATE_FOOTER		"footer.html"
#define	   WEB_TEMPLATE_CONN_LIST	"conn_entry.html"
#define	   WEB_TEMPLATE_VOL_LIST	"vol_entry.html"

// WEB_MAX_TEMPLATE_NAME is the maximum length of a template, including .html
#define	   WEB_MAX_TEMPLATE_NAME	64

// WEB_VALUE_xxx are the web template values
#define	   WEB_VALUE_UNKNOWN		-1
#define	   WEB_VALUE_SERVER_NAME	0
#define	   WEB_VALUE_SERVER_VERSION	1
#define    WEB_VALUE_USED_CONNS		2
#define    WEB_VALUE_TOTAL_CONNS	3
#define    WEB_VALUE_MOUNTED_VOLS	4
#define    WEB_VALUE_VOL_NAMES		5
#define    WEB_VALUE_CONN_LIST		6

#define    WEB_VALUE_CONN_NO		7
#define    WEB_VALUE_CONN_ADDR		8
#define    WEB_VALUE_CONN_OBJECT	9
#define    WEB_VALUE_CONN_LOGINTIME	10

#define    WEB_VALUE_CONN_CLEAR		11
#define    WEB_VALUE_CONN_SENDMSG	12

#define	   WEB_VALUE_VOL_LIST		13
#define	   WEB_VALUE_VOL_NO		14
#define	   WEB_VALUE_VOL_NAME		15
#define	   WEB_VALUE_VOL_PATH		16
#define	   WEB_VALUE_VOL_DISMOUNT	17

// WEB_PARM_xxx are the web parameters
#define	   WEB_PARM_UNKNOWN		-1
#define	   WEB_PARM_CONN		0
#define	   WEB_PARM_MESSAGE		1
#define	   WEB_PARM_VOL			2

extern int fd_web;
extern int fd_web_conn[WEB_MAX_CONNS];

void	   web_init();
void	   web_handle_conn ();
void	   web_handle_data (int);

void  	   web_show_template (int, char*);
#endif
