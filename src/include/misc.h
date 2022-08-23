/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#ifndef __MISC_INCLUDED__
#define __MISC_INCLUDED__
#include <stdio.h>
#include <sys/time.h>

typedef unsigned char       uint8;
typedef unsigned short int uint16;
typedef unsigned long  int uint32;

extern int debuglevel;
extern int frommel_options;
extern int down_flag;
extern FILE* logfile;

#define LOG_BINDERY		1		/* bindery logging */
#define LOG_FS			2		/* filesystem logging */
#define LOG_CONN		4		/* connection logging */
#define LOG_NCP			8		/* NCP logging */
#define LOG_SAP			16		/* SAP logging */
#define LOG_WDOG		32		/* Watchdog logging */
#define LOG_WEB			64		/* Web logging */
#define LOG_TRUSTEE		128		/* Trustee logging */
#define LOG_BINDERYIO		256		/* Bindery I/O logging */
#define LOG_GASP		512		/* GASP logging */
#define LOG_LINK		1024		/* Link logging */
#define LOG_ALL			LOG_BINDERY | LOG_FS | LOG_CONN | LOG_NCP | \
				LOG_SAP | LOG_WDOG | LOG_WEB | LOG_TRUSTEE | \
				LOG_BINDERYIO | LOG_GASP | LOG_LINK

#define PACKED __attribute__ ((packed))
#define max(a,b) ((a)<(b)?(b):(a))

#define GET_BE8(b)  (     (uint8) *(((uint8*)(b)))   )

#define GET_BE16(b)  (     (int) *(((uint8*)(b))+1)  \
                     | ( ( (int) *( (uint8*)(b)   )  << 8) ) )

#define GET_BE32(b)  (   (uint32)   *(((uint8*)(b))+3)  \
                   | (  ((uint32)   *(((uint8*)(b))+2) ) << 8)  \
                   | (  ((uint32)   *(((uint8*)(b))+1) ) << 16) \
                   | (  ((uint32)   *( (uint8*)(b)   ) ) << 24) )

#define GET_LE16(b)  (     (int) *(((uint8*)(b))  )  \
                     | ( ( (int) *(((uint8*)(b))+1)  << 8) ) )

#define GET_LE32(b)  (   (uint32)   *(((uint8*)(b)))  \
                   | (  ((uint32)   *(((uint8*)(b))+1) ) << 8)  \
                   | (  ((uint32)   *(((uint8*)(b))+2) ) << 16) \
                   | (  ((uint32)   *(((uint8*)(b))+3) ) << 24) )



void DPRINTF (int, int, char*, ...);
uint16 time2nw (time_t);
uint16 date2nw (time_t);
int match_fn (char*, char*);
int match_bind (char*, char*, int);
void dump_hex (uint8*, int);
uint32 convert_blocks (uint32, int, int);
void down_server ();
void down_event (int);

#ifdef LINUX
char* ipx_ntoa (struct sockaddr_ipx*);
#endif

typedef unsigned char buf32[32];
typedef unsigned char buf16[16];
typedef unsigned char buf8[8];
typedef unsigned char buf4[4];
typedef unsigned char u8;


// I know these functions below this should be in crypt.h, but that *might*
// conflict with some known heading files, so we'll just kick it in here...
void nw_encrypt (unsigned char*, unsigned char*, unsigned char*);
static void shuffle1 (buf32, unsigned char*);
void shuffle (unsigned char*, const unsigned char*, int, unsigned char*);
void nw_decrypt_newpass (char*, char*, char*);

#endif
