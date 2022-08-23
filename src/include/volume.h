/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#ifndef __VOL_INCLUDED__
#define __VOL_INCLUDED__

#include "misc.h"

#define VOL_FLAG_USED		1	// used
#define VOL_FLAG_REMOVABLE	2	// removable

#define VOL_MAX_VOLS		16	// maximum volumes
#define VOL_MAX_VOL_NAME_LEN	16	// maximum volume name length
#define VOL_MAX_VOL_PATH_LEN	256	// maximum volume path length

typedef struct {
    int   flags;			// flags
    char  name[VOL_MAX_VOL_NAME_LEN];	// volume name
    char  path[VOL_MAX_VOL_PATH_LEN];	// volume path
} VOLUME;

extern VOLUME volume[VOL_MAX_VOLS];

void vol_init();
int vol_add_volume (char*, char*, int);
int vol_find_volume (char*);
int vol_count_vols();
void vol_delete_volume (int);
#endif
