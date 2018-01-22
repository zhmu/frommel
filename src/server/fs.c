/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * File system code
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
#include "volume.h"
#include "trustee.h"

/*
 * fs_init()
 *
 * This will initialize the file system management system.
 *
 */
void
fs_init() {
}

/*
 * fs_con_find_free_dh (int c)
 *
 * This will scan for a free directory handle for connection [c]. If one is
 * found, the number will be returned. If not, -1 will be returned.
 *
 */
int
fs_con_find_free_dh (int c) {
    int i;

    // scan all handles
    for (i = 0; i < CONN_DH_PER_CONN; i++) {
	// is this one in use?
	if (conn[c]->dh[i].flags == 0) {
	    // no. return the number
	    return i;
	}
    }

    // there are no more free directory handles. return -1
    return -1;
}

/*
 * fs_con_alloc_dh (int c, int flags, int volno, char* path)
 *
 * This will allocate a directory handle for connection [c]. It will assign
 * volume number [volno], path [path] and flags [flags] to the handle. On
 * success, the directory handle will be returned. On failure, -1 will be
 * returned.
 *
 */
int
fs_con_alloc_dh (int c, int flags, int volno, char* path) {
    int dh, i, j;
    char tmp[NCP_MAX_FIELD_LEN];
    char tempath[VOL_MAX_VOL_PATH_LEN];
    char* ptr;
    char* pathptr;
    struct stat st;

    // scan for a free directory handle
    dh = fs_con_find_free_dh (c);
    if (dh < 0) {
	// no directory handles available. complain
	DPRINTF (5, LOG_FS, "[WARN] [%u] fs_con_alloc_dh(): out of directory handles\n", c);
	return -1;
    }

    // does our path contain a : ?
    pathptr = path;
    if ((ptr = strchr (path, ':')) != NULL) {
	// yes. copy everything before it to [tmp]
	bzero (tmp, NCP_MAX_FIELD_LEN);
	strncpy (tmp, path, (ptr - path));

	// make it uppercase
	for (i = 0; i < strlen (tmp); i++)
	    tmp[i] = toupper (tmp[i]);

	// scan for this volume
	j = vol_find_volume (tmp);
	if (j != -1) {
	    // there's such a volume. update the volume number
	    volno = j;

	    // update the path
	    pathptr = ptr + 1;
	}
    }

    // we got a directory handle. set it up
    conn[c]->dh[dh].flags = flags;
    conn[c]->dh[dh].volno = volno;
    strncpy (conn[c]->dh[dh].path, pathptr, CONN_DH_MAX_PATH_LEN);

    // build the path itself
    if (fs_build_path (c, dh, NULL, tempath, VOL_MAX_VOL_PATH_LEN, 0) != FS_ERROR_OK) {
	// this is weird... complain
	DPRINTF (0, LOG_FS, "[WARN] [%u] fs_build_path() failed in fs_con_alloc_dh()!\n", c);
	bzero (&conn[c]->dh[dh], sizeof (DIRHANDLE));
	return -1;
    }

    // does this path actually exist?
    if (stat (tempath, &st) < 0) {
	// no. complain
	bzero (&conn[c]->dh[dh], sizeof (DIRHANDLE));
	DPRINTF (10, LOG_FS, "[INFO] [%u] Can't stat directory '%s'\n", c, tempath);
	return -1;
    }

    DPRINTF (10, LOG_FS, "[DEBUG] [%u] Directory handle %u allocated (flags=%u,volno=%u,path='%s')\n", c, dh, flags, volno, pathptr);

    // it worked. return the number
    return dh;
}

/*
 * fs_build_path (int c, int dh, char* path, char* dest, int destlen, int flags)
 *
 * This will construct a real path from connection [c] directory handle [dh].
 * It will append /[path] to the path, if [path] is not NULL. All results
 * will be put in [dest], with a maximum of [destlen] chars (excluding
 * terminating nul). This will return FS_ERROR_OK on success or a FS_ERROR_xxx
 * code on failure. If [flags] is FS_IGNORE_DH_PATH, the directory handle path
 * will not be copied. If [flags] is FS_IGNORE_CONN the connection will be
 * ignored.
 *
 */
int
fs_build_path (int c, int dh, char* path, char* dest, int destlen, int flags) {
    int i;
    char* ptr;
    char  tmp[NCP_MAX_FIELD_LEN];

    // need to ignore the connection?
    if (!(flags & FS_IGNORE_CONN)) {
	// no. is this directory handle valid?
	if (conn[c]->dh[dh].flags == FS_DH_UNUSED) {
	    // no. complain
	    return FS_ERROR_BAD_DH;
	}

	// zero out the destination first
	bzero (dest, destlen);

	// copy the real volume path to the destination
	strncpy (dest, volume[conn[c]->dh[dh].volno].path, destlen);
    } else {
	// if we're ignoring the connection, ignore the directory handle path,
	// too
	flags |= FS_IGNORE_DH_PATH;
    }

    // need to ignore the directory handle path?
    if (!(flags & FS_IGNORE_DH_PATH)) {
        // no. does this directory handle have a path?
	if ((conn[c]->dh[dh].path != NULL) && (conn[c]->dh[dh].path[0] != 0)) {
	    // yes. add it
	    strcat (dest, "/");		// XXX
	    strncat (dest, conn[c]->dh[dh].path, destlen - strlen (dest));
	}
    }

    // is a path given?
    if ((path != NULL) && (path[0] != 0)) {
	// yes. add it
	strcat (dest, "/");		// XXX
	strncat (dest, path, destlen - strlen (dest));
    }

    // change all \\'s to /'s
    for (i = 0; i < strlen (dest); i++)
	if (dest[i] == '\\') dest[i] = '/';

    // does our result have a colon in it?
    if ((ptr = strchr (dest, ':')) != NULL) {
	// yes. scan for a / before this
	for (i = (ptr - dest); i >= 0; i--) {
	    if (dest[i] == '/') break;
	}
	if (dest[i] == '/') i++;

	bzero (tmp, NCP_MAX_FIELD_LEN);
	strncpy (tmp, (dest + i), (ptr - dest) - i);

	// uppercase this name
	for (i = 0; i < strlen (tmp); i++)
	    tmp[i] = toupper (tmp[i]);

	// [tmp] holds the volume name. find it
	i = vol_find_volume (tmp);
	if (i < 0) {
	    // this failed. complain
	    DPRINTF (10, LOG_FS, "fs_build_path(): invalid vol '%s' (path '%s')\n", tmp, dest);
	    return FS_ERROR_INVALIDPATH;
	}

	// zero out the temp first
	bzero (tmp, NCP_MAX_FIELD_LEN);

	// copy the real volume path to the destination
	strncpy (tmp, volume[i].path, NCP_MAX_FIELD_LEN);

	// add a '/' if needed
  	ptr++;

	if ((*ptr != 0) && (tmp[strlen (tmp) - 1] != '/'))
	    strncat (tmp, "/", NCP_MAX_FIELD_LEN);

	strncat (tmp, ptr, NCP_MAX_FIELD_LEN);

	// copy the path
	strncpy (dest, tmp, destlen);
    }

    // XXX: debug!
    DPRINTF (10, LOG_FS, "[DEBUG] [%u] fs_build_path(): path = '%s'\n", c, dest);

    // it worked. return positive status
    return FS_ERROR_OK;
}

/*
 * fs_con_dealloc_dh (int c, int dh)
 *
 * This will deallocate directory handle [dh] of connection [c]. It will return
 * 0 on success or -1 on failure.
 *
 */
int
fs_con_dealloc_dh (int c, int dh) {
    // is this connection within range?
    if ((dh < 0) || (dh > CONN_DH_PER_CONN)) {
	// no. complain
	return -1;
    }

    // get rid of the handle
    bzero (&conn[c]->dh[dh], sizeof (DIRHANDLE));
    return 0;
}

/*
 * fs_con_find_free_sh (int c)
 *
 * This will scan for a free search handle for connection [c]. If one is found,
 * the number will be returned. If not, -1 will be returned.
 *
 */
int
fs_con_find_free_sh (int c) {
    int i;

    // scan all handles
    for (i = 0; i < CONN_SH_PER_CONN; i++) {
	// is this one in use?
	if (conn[c]->sh[i].path[0] == 0) {
	    // no. return the number
	    return i;
	}
    }

    // there are no more free search handles. return -1
    return -1;
}

/*
 * fs_con_alloc_sh (int c, int dh, char* path)
 *
 * This will allocate a search handle for connection [c], directory handle [dh]
 * and path [path]. It will return the search handle ID on success or -1 on
 * failure.
 *
 */
int
fs_con_alloc_sh (int c, int dh, char* path) {
    int i;

    // get a free search handle
    i = fs_con_find_free_sh (c);
    if (i < 0) {
	// this failed. return bad status
	DPRINTF (6, LOG_FS, "[INFO] [%u] fs_con_alloc_sh(): out of search handles!\n", c);
	return -1;
    }

    // we got a handle. set it up
    if (fs_build_path (c, dh, path, conn[c]->sh[i].path, CONN_SH_MAX_PATH_LEN, 0) != FS_ERROR_OK) {
	// this failed. return an error
	return -1;
    }
    if (path != NULL) {
	strcpy (conn[c]->sh[i].relpath, path);		// XXX: length
    } else {
	strcpy (conn[c]->sh[i].relpath, "");
    }
    conn[c]->sh[i].dh = dh;
    conn[c]->sh[i].scan_right = (trust_check_rights (c, dh, path, TRUST_RIGHT_FILESCAN) < 0) ? 0 : 1;
    DPRINTF (10, LOG_FS, "[DEBUG] Scan_right %u\n", conn[c]->sh[i].scan_right);

    // it worked. return the handle number
    return i;
}

/*
 * fs_con_find_free_fh (int c)
 *
 * This will scan for an available file handle for connection [c]. It will
 * return the handle number on success or -1 on failure.
 *
 */
int
fs_con_find_free_fh (int c) {
    int i;

    // scan all handles
    for (i = 0; i < CONN_FH_PER_CONN; i++) {
	// is this one used?
	if (conn[c]->fh[i].fd == -1) {
	    // no. return the number
	    return i;
	}
    }

    // all handles have been used
    return -1;
}

/*
 * fs_open_file (int c, int dh, uint8 rights, char* fname, int* handle)
 *
 * This will attempt to open file [fname] for connection [c] using directory
 * handle [dh]. On success, [*handle] will be set to the file handle and
 * FS_ERROR_OK will be returned. On failure, one of the FS_ERROR_xxx defines
 * will be returned. If [mode] has the highest bit set, it will create the
 * file.
 *
 */
int
fs_open_file (int c, int dh, uint8 rights, char* fname, int* handle) {
    int 	h, i, mode;
    char	path[VOL_MAX_VOL_PATH_LEN];
    char	tmpath[VOL_MAX_VOL_PATH_LEN];
    char*	ptr;
    char*	tmptr = tmpath;
    int		trustee;
    struct stat fs;

    // get a free handle for this connection
    h = fs_con_find_free_fh (c);
    if (h < 0) {
	// this failed. return an error
	DPRINTF (4, LOG_FS, "[WARN] [%u] Out of file handles\n", c);
	return FS_ERROR_OUTOFHANDLES;
    }

    // build the file mode
    mode = 0; trustee = -1;
    if (rights & 1) { mode = O_RDONLY; trustee = TRUST_RIGHT_READ; };
    if (rights & 2) { mode = O_WRONLY; trustee = TRUST_RIGHT_WRITE; };
    if (rights & 4) { mode = O_RDWR; trustee = TRUST_RIGHT_READ | TRUST_RIGHT_WRITE; };
    if (rights & 128) { mode |= (O_CREAT | O_TRUNC); trustee = TRUST_RIGHT_CREATE; };

    // search for the final '/'
    strncpy (tmpath, fname, VOL_MAX_VOL_PATH_LEN);

    // turn all backslashes in [tmpath] to slashes
    while ((ptr = strchr (tmpath, '\\')) != NULL)
	*ptr = '/';

    // always get rid of everything after the last slash
    ptr = strrchr (tmpath, '/');
    if (ptr) {
	// there's a last slash. kill it
	*ptr = 0;
    } else {
	// we've got no last slash. probably only a filename... make the
	// temp path blank
	tmptr = NULL;
    }

    // verify the directory rights
    DPRINTF (10, LOG_FS, "[DEBUG] fs_open_file() called check_rights(): dh=%i,path='%s',right=0x%x\n", dh, tmpath, trustee);
    if (trust_check_rights (c, dh, tmptr, trustee) < 0) {
	// we don't have enough rights. complain
	return FS_ERROR_NOFILE;
    }

    // build the real path
    i = fs_build_path (c, dh, fname, path, VOL_MAX_VOL_PATH_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. return an error
	return i;
    }

    DPRINTF (10, LOG_FS, "fs_open(): file '%s'\n", path);

    // get the last '/' in the filename so we can figure out what the filename
    // is
    if ((ptr = strrchr (path, '/')) == NULL) {
	// eek, there's no filename... use the file name itself, then
	ptr = path;
    } else {
	// we want the next position, actually...
	ptr++;
    }

    // does this filename actually resemble our trustee system file?
    if (!strcasecmp (ptr, TRUST_SYSFILE)) {
	// yes. that's access denied for sure!
	return FS_ERROR_CANTWRITE;
    }

    // turn the weird 0xae thing into the path into a dot
    while ((ptr = strchr (path, 0xae)) != NULL)
	*ptr = '.';

    // need to create the file?
    if ((rights & 128) == 0) {
	// no. stat() the file
	if (stat (path, &fs) < 0)
	    // this failed. return
	    return -1;

	// is this a directory?
	if (fs.st_mode == S_IFDIR)
	    // yes. refuse to open it
	    return -1;
    }

    // we got a handle. try to open the file
    conn[c]->fh[h].fd = open (path, mode);
    if (conn[c]->fh[h].fd < 0) {
	perror ("open()");
	return -1;
    }

    DPRINTF (10, LOG_FS, "[DEBUG] [%u] fs_open_file(): handle %u gives fd %u\n", c, h, conn[c]->fh[h].fd);

    // it worked. set the handle
    *handle = h;
    return FS_ERROR_OK;
}

/*
 * fs_close_file (int c, int handle)
 *
 * This will close handle [handle] belonging to connection [c]. This will return
 * FS_ERROR_OK on success or FS_ERROR_xxx on failure.
 *
 */
int
fs_close_file (int c, int h) {
    // is this handle opened?
    if (conn[c]->fh[h].fd < 0) {
	// no. return an error
	return FS_ERROR_BADHANDLE;
    }

    // the handle is open. close it
    close (conn[c]->fh[h].fd);

    // mark the handle as unused
    conn[c]->fh[h].fd = -1;

    // say it's ok
    return FS_ERROR_OK;
}

/*
 * fs_volume_inuse (int no)
 *
 * This will check whether volume [no] is in use. It will return 0 if it is or
 * -1 if not.
 *
 */
int
fs_volume_inuse (int no) {
    int i, j;

    // scan all connections
    for (i = 1; i <= conf_nofconns; i++)
	// is this connection in use?
	if (conn[i]->status != CONN_STATUS_UNUSED)
	    // yes. scan all directory handles
	    for (j = 0; j < CONN_DH_PER_CONN; j++)
		// handle in use and volume number equal?
		if ((conn[i]->dh[j].flags != 0) && (conn[i]->dh[j].volno == no))
		    // yes. it's in use
		    return 0;

    // the volume is not in use
    return -1;
}
