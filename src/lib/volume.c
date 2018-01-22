/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * Volume handling code.
 * 
 */
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include "config.h"
#include "conn.h"
#include "fs.h"
#include "ncp.h"
#include "misc.h"
#include "trustee.h"

VOLUME volume[VOL_MAX_VOLS];

/*
 * vol_init()
 *
 * This will initialize the volume management system.
 *
 */
void
vol_init() {
    // reset the volume table
    bzero (&volume, sizeof (VOLUME) * VOL_MAX_VOLS);
}

/*
 * vol_add_volume (char* name, char* path, int flags)
 *
 * This will add volume [name] using path [path] and flags [flags]. On success,
 * the new volume number will be returned, or -1 if we ran out of space.
 *
 */
int
vol_add_volume (char* name, char* path, int flags) {
    int i;

    // scan for a free volume number
    for (i = 0; i < VOL_MAX_VOLS; i++) {
	// if this volume is unused, quit the loop
	if (volume[i].flags == 0) break;
    }

    // did the loop end?
    if (i == VOL_MAX_VOLS) {
	// yes. complain
	DPRINTF (5, LOG_FS, "[INFO] vol_add_volume(): out of volumes while adding volume '%s'\n", name);
	return -1;

    }

    // we have a free handle. copy the path and such
    bzero (&volume[i], sizeof (VOLUME));
    volume[i].flags = flags | VOL_FLAG_USED;
    strncpy (volume[i].name, name, VOL_MAX_VOL_NAME_LEN);
    strncpy (volume[i].path, path, VOL_MAX_VOL_PATH_LEN);

    // return the handle
    return i;
}

/*
 * vol_find_volume (char* name)
 *
 * This will scan for volume [name]. If it is found, it will return the volume
 * ID, otherwise -1.
 *
 */
int
vol_find_volume (char* name) {
    int i;

    // scan them all
    for (i = 0; i < VOL_MAX_VOLS; i++) {
	// is this an used volume and the volume we seek?
	if ((volume[i].flags != 0) && (!strcmp (name, volume[i].name))) {
	    // this matches. return the id
	    return i;
	}
    }

    // the volume was not found
    return -1;
}

/*
 * vol_count_vols()
 *
 * This will return the number of used volumes.
 *
 */
int
vol_count_vols() {
    int i, j = 0;

    // scan them all
    for (i = 0; i < VOL_MAX_VOLS; i++)
	// is this volume nused?
	if (volume[i].flags)
	    // yes. increment the count
	    j++;

    // return the count
    return j;
}

/*
 * fs_delete_volume (int no)
 *
 * This will get rid of volume [no].
 *
 */
void
fs_delete_volume (int no) {
    // bye!
    bzero (&volume[no], sizeof (VOLUME));
}
