/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#ifndef __FROMMEL_INCLUDED__
#define __FROMMEL_INCLUDED__

#include "misc.h"

#define PACKED __attribute__ ((packed))
#define max(a,b) ((a)<(b)?(b):(a))

#define PORT_ECHO	0x4002

#define FROMMEL_RECREATE_BINDERY 1

#define FROMMEL_MAX_CONFIG_LINE		1024

extern FILE* logfile;
extern int frommel_options;
extern int down_flag;

#endif
