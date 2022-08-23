/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * Miscellaneous stuff
 * 
 */
#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "frommel.h"
#include "misc.h"

int debuglevel = 10;
int debugmodules = LOG_ALL;
int frommel_options = 0;
FILE* logfile = NULL;

/*
 * DPRINTF (int level, int module, char* format, ...)
 *
 * This will printf (format, ...) if the current debugging level >= level and
 * the logging options include module [module].
 *
 */
void
DPRINTF (int level, int module, char* format, ...) {
    va_list ap;

    // if our verbosity level isn't high enough, leave
    if (debuglevel < level) return;

    // if we don't log this module, leave
    if (!(debugmodules & module)) return;

    va_start (ap, format);
    vfprintf (stderr, format, ap);
    va_end (ap);

    // if there's no logfile, don't log it to the file
    if (logfile == NULL) return;

    va_start (ap, format);
    vfprintf (logfile, format, ap);
    va_end (ap);

    fflush (logfile);
}

/*
 * time2nw (time_t t)
 *
 * This will convert time [t] to NetWare time format.
 *
 */
uint16
time2nw (time_t t) {
    struct tm* lt;

    lt = localtime (&t);
    return (lt->tm_hour << 11) | (lt->tm_min << 5) | (lt->tm_sec / 2);
}

/*
 * date2nw (time_t t)
 *
 * This will convert time [t] to NetWare date format.
 *
 */
uint16
date2nw (time_t t) {
    struct tm* lt;

    lt = localtime (&t);
    return (((lt->tm_year - 80) << 9) | ((lt->tm_mon + 1) << 5) | (lt->tm_mday))
;
}

/*
 * match_fn (char* source,char* dest)
 *
 * This will return 0 if the filenames [source] and [dest] do not match,
 * otherwise 1. Wildcards are supported.
 *
 */
int
match_fn (char* source, char* dest) {
    uint8* s = (uint8*)source;
    uint8* d = (uint8*)dest;

    while (*d) {
        switch (*d) {
            case 0xbf:
             case '?': // match single char
                       if (*s && *s != '.') s++;
                       d++;
                       break;
           case 0xaa:
            case '*': // match all to end of string
                      ++d;
                      if (*d) {
                          while (*s && *s != '.') s++;
                      } else {
                          // is the last char is a real '*'?
                          if (*(d - 1) == '*') {
                              // yes. it's ok
                              return 1;
                          }
                          while (*s && *s != '.') s++;
                      }
                      break;
           case 0xae:
            case '.': // dot
                      if (*s)
                        if (*s != '.') return 0;
                        else s++;
                      d++;
                      break;
	   case 0xff: // we must skip 0xff chars
		      d++;
		      break;
             default: // ordinary charachter
                      if (tolower (*s) != tolower (*d)) {
                          // no match, sorry
                          return 0;
                      }
                      s++; d++;
                      break;
        }
    }

    return (*s ? 0 : 1);
}

/*
 * match_bind (char* source, char* dest, int len)
 *
 * This will return -1 if the bindery names [source] and [dest] do not match,
 * otherwise 0. [len] is the length of the names. Wildcards are supported.
 *
 */
int
match_bind (char* source, char* dest, int len) {
    // XXX: should stuff like 'A*' and 'A*B' also work? I hope not...

    // if we encounter wildcards, it's OK
    if (!strcmp (source, "*")) return 0;
    if (!strcmp (dest, "*")) return 0;

    // otherwise, it's a string match
    return ((!strncmp (source, dest, len)) ? 0 : -1);
}

/*
 * dump_hex (uint8* buf, int size)
 *
 * This will dump [size] bytes of buffer [buf] as hex digits.
 *
 */
void
dump_hex (uint8* buf, int size) {
    int i;
    uint8 ch;

    for (i = 0; i < size; i++) {
	ch = (uint8)(*(buf + i));
	fprintf (stderr, "%02x ", ch);
	if (((i % 16) == 0) && (i)) fprintf (stderr, "\n");
    }
}

/*
 * convert_blocks (uint32 blocks, int from, int to)
 *
 * This will convert block count [blocks] from count [from] to count [to].
 *
 * This is copied from MARS_NWE, which in turn copied it from GNU fileutils.
 *
 */
uint32
convert_blocks (uint32 blocks, int from, int to) {
    // if the count is the same, return the original number
    if (from == to)
	return blocks;

    // from something bigger to something smaller?
    if (from > to)
	// yes, do it
	return blocks * (from / to);

    // no, from a small thing to a big thing
    return (blocks + (blocks < 0 ? -1 : 1)) / (to / from);
}

#ifdef LINUX
char*
ipx_ntoa (struct sockaddr_ipx* addr) {
    // TODO
    return NULL;
}
#endif
