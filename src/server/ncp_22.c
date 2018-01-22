/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * NCP subfunction 22 code
 * 
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#ifdef LINUX
#include <sys/vfs.h>
#endif
#include <unistd.h>
#include "config.h"
#include "conn.h"
#include "fs.h"
#include "ncp.h"
#include "misc.h"
#include "trustee.h"

/*
 * ncp_get_volinfo_handle (int c, char* data, int size)
 *
 * This will handle the NCP 22 21 (Get volume info with handle) call.
 *
 */
void
ncp_get_volinfo_handle (int c, char* data, int size) {
    uint8	 dh;
    char  	 path[VOL_MAX_VOL_PATH_LEN];
    struct	 statfs st;
    int		 i;

    NCP_REQUIRE_LENGTH (2);
    dh = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 21: Get volume info with handle (handle=%u)\n", c, dh);

    // grab the real path
    i = fs_build_path (c, dh, NULL, path, VOL_MAX_VOL_PATH_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. inform the user
	ncp_send_compcode (c, i);
	return;
    }

    // get the filesystem statistics
    if (statfs (path, &st) < 0) {
	// this failed. complain
	DPRINTF (5, LOG_NCP, "[WARN] [%u] NCP 22 21: I/O error\n", c);
	ncp_send_compcode (c, 0xff);
	return;
    }

    // send the reply
    ncp_send_volinfo (c, conn[c]->dh[dh].volno, &st);
}

/*
 * ncp_alloc_temp_dirhandle (int c, char* data, int size)
 *
 * This will handle the NCP 22 19 (allocate a temponary directory handle) call.
 *
 */
void
ncp_alloc_temp_dirhandle (int c, char* data, int size) {
    uint8 	   source_dh, path_len, name;
    char  	   path[NCP_MAX_FIELD_LEN];
    int   	   i;
    NCP_REPLY 	   r;
    NCP_DIRHANDLE* ndi;
    TRUST_RECORD   rec;

    bzero (&path, NCP_MAX_FIELD_LEN);
    NCP_REQUIRE_LENGTH_MIN (4);

    source_dh = GET_BE8 (data); data++;
    name = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (path_len + 4);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 19: Allocate temporary directory handle (source=%u,name=0x%x,path='%s')\n", c, source_dh, name, path);

    // convert the path to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // allocate the directory handle (XXX: volume number!)
    i = fs_con_alloc_dh (c, FS_DH_TEMP, 0, path);
    if (i  < 0) {
	// this failed (XXX: error code?)
	DPRINTF (6, LOG_NCP, "[WARN] [%u] NCP 22 19: out of directory handles\n", c);
	ncp_send_compcode (c, 0xff);
	return;
    }

    // it worked. send the reply
    ndi = (NCP_DIRHANDLE*)r.data;
    bzero (ndi, sizeof (NCP_DIRHANDLE));

    // fetch the directory rights
    if (trust_get_rights (c, i, NULL, &rec) != TRUST_ERROR_OK) {
	// can't get the rights, zero them out
	bzero (&rec, sizeof (TRUST_RECORD));
    }

    ndi->dh = (uint8)i;
    ndi->rights = (rec.rights & 0xff);

    // send this packet
    ncp_send_reply (c, &r, sizeof (NCP_DIRHANDLE), 0);
}

/*
 * ncp_dealloc_dirhandle (int c, char* data, int size)
 *
 * This will handle NCP 22 20 (deallocate directory handle) calls.
 *
 */
void
ncp_dealloc_dirhandle (int c, char* data, int size) {
    uint8    dh;
    int      i;

    NCP_REQUIRE_LENGTH_MIN (2);

    dh = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 20: Deallocate directory handle (handle=%u)\n", c, dh);

    // get rid of the handle
    ncp_send_compcode (c, (fs_con_dealloc_dh (c, dh) < 0) ? FS_ERROR_BAD_DH : 0);
}

/*
 * ncp_handle_effrights (int c, char* data, int size)
 *
 * This handle NCP 22 3 (Get effective directory rights)
 *
 */
void
ncp_handle_effrights (int c, char* data, int size) {
    uint8 	   dh, path_len;
    char  	   path[NCP_MAX_FIELD_LEN];
    char  	   tmpath[NCP_MAX_FIELD_LEN];
    int		   i;
    struct stat	   fs;
    NCP_REPLY	   r;
    TRUST_RECORD   tr;

    NCP_REQUIRE_LENGTH_MIN (3);
    dh = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (3 + path_len);
    bzero (path, NCP_MAX_FIELD_LEN);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 3: Get effective directory rights (dh=%u,path='%s')\n", c, dh, path);

    // change the path name to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // build the real path
    i = fs_build_path (c, dh, path, tmpath, NCP_MAX_FIELD_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. complain
	DPRINTF (10, LOG_NCP, "[DEBUG] Error: fs_build_path() failed, error 0x%x\n", i);
	ncp_send_compcode (c, i);
	return;
    }

    // we got the path. fstat() it
    i = 1;
    if (stat (tmpath, &fs) < 0) i = 0;
    if ((i) & ((fs.st_mode & S_IFDIR) == 0)) i = 0;

    // was the thing found and a directory?
    if (!i) {
	// no. return an error code
	ncp_send_compcode (c, FS_ERROR_INVALIDPATH);
	return;
    }

    // get the effective rights
    if ((i = trust_get_rights (c, dh, path, &tr)) != TRUST_ERROR_OK) {
	// this failed. we have no rights, then
	tr.rights = 0;
    }

    // do we have any rights here (but it's not the root?)
    if ((!tr.rights) && (strcmp (tmpath, volume[conn[c]->dh[dh].volno].path))) {
	// no. just claim there's no such directory
	ncp_send_compcode (c, FS_ERROR_INVALIDPATH);
	return;
    }

    // send the packet
    r.data[0] = (tr.rights & 0xff);
    DPRINTF (10, LOG_NCP, "[DEBUG] Rights = 0x%x\n", tr.rights);
    ncp_send_reply (c, &r, 1, 0);
}

/*
 * ncp_alloc_perm_dirhandle (int c, char* data, int size)
 *
 * This will handle the NCP 22 18 (allocate a permanent directory handle) call.
 *
 */
void
ncp_alloc_perm_dirhandle (int c, char* data, int size) {
    uint8 	   source_dh, path_len, name;
    char  	   path[NCP_MAX_FIELD_LEN];
    int   	   i, j;
    NCP_REPLY 	   r;
    NCP_DIRHANDLE* ndi;
    TRUST_RECORD   rec;

    bzero (&path, NCP_MAX_FIELD_LEN);
    NCP_REQUIRE_LENGTH_MIN (4);

    source_dh = GET_BE8 (data); data++;
    name = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (path_len + 4);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 18: Allocate permanent directory handle (source=%u,name=0x%x,path='%s')\n", c, source_dh, name, path);

    // convert the path to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // allocate the directory handle (XXX: volume number!)
    i = fs_con_alloc_dh (c, FS_DH_PERM, 0, path);
    if (i < 0) {
	// this failed (XXX: error code?)
	DPRINTF (6, LOG_NCP, "[WARN] [%u] NCP 22 18: out of directory handles\n", c);
	ncp_send_compcode (c, 0xff);
	return;
    }

    DPRINTF (9, LOG_NCP, "[DEBUG] [%u] Directory handle %u allocated\n", c, i);

    // fetch the directory rights
    if (trust_get_rights (c, i, NULL, &rec) != TRUST_ERROR_OK) {
	// can't get the rights, zero them out
	bzero (&rec, sizeof (TRUST_RECORD));
    }

    // it worked. send the reply
    ndi = (NCP_DIRHANDLE*)r.data;
    bzero (ndi, sizeof (NCP_DIRHANDLE));

    ndi->dh = (uint8)i;
    ndi->rights = (rec.rights & 0xff);

    // send this packet
    ncp_send_reply (c, &r, sizeof (NCP_DIRHANDLE), 0);
}

/*
 * ncp_handle_getdirpath (int c, char* data, int size)
 *
 * This will handle NCP 22 1 (Get directory path) calls.
 *
 */
void
ncp_handle_getdirpath (int c, char* data, int size) {
    uint8	dh;
    int		i;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (2);

    dh = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 1: Get directory path (dh=%u)\n", c, dh);

    bzero (r.data, NCP_DATASIZE);
    snprintf (r.data + 1, NCP_DATASIZE, "%s:%s", volume[conn[c]->dh[dh].volno].name, conn[c]->dh[dh].path);
    r.data[0] = (uint8)strlen (r.data + 1);

    // uppercase the result
    for (i = 0; i < strlen (r.data + 1); i++)
	r.data[1 + i] = toupper (r.data[1 + i]);

    // send the reply
    ncp_send_reply (c, &r, strlen (r.data + 1) + 1, 0);
}

/*
 * ncp_handle_setdirpath (int c, char* data, int size)
 *
 * This will handle NCP 22 0 (Set directory path) calls.
 *
 */
void
ncp_handle_setdirpath (int c, char* data, int size) {
    uint8	source_dh, dest_dh, path_len;
    char  	path[NCP_MAX_FIELD_LEN];
    char  	tmpath[NCP_MAX_FIELD_LEN];
    int		i;
    char*	ptr;
    DIRHANDLE	dest_tmp;

    bzero (&path, NCP_MAX_FIELD_LEN);
    NCP_REQUIRE_LENGTH_MIN (4);

    dest_dh = GET_BE8 (data); data++;
    source_dh = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (path_len + 4);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 0: Set Directory Handle (source=%u,dest=%u,path='%s')\n", c, source_dh, dest_dh, path);

    // convert the path to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // store the old handle
    bcopy (&conn[c]->dh[dest_dh].path, &dest_tmp, sizeof (DIRHANDLE));

    // copy the path to a temp var and change all backslashes to slashes
    strcpy (tmpath, path);
    while ((ptr = strchr (tmpath, '\\')) != NULL)
	*ptr = '/';

    // is the source the same as the destination?
    if (source_dh != dest_dh)
	// no. copy the entire handle
        bcopy (&conn[c]->dh[source_dh], &conn[c]->dh[dest_dh], sizeof (DIRHANDLE));

    // do we need to strip the path down?
    if (!strcmp (tmpath, "..")) {
	// yes. scan for the last '/' and zap everything after it
	ptr = strrchr (conn[c]->dh[dest_dh].path, '/');
	if (ptr != NULL) {
	   // we got it. now, kill it
	   *ptr = 0;
	}
    } else {
	// append it
	snprintf (conn[c]->dh[dest_dh].path, CONN_DH_MAX_PATH_LEN, "%s/%s", conn[c]->dh[dest_dh].path, tmpath);
    }

    DPRINTF (10, LOG_NCP, "[DEBUG] NEW PATH = '%s'\n", conn[c]->dh[dest_dh].path);

    /*// verify the rights
    DPRINTF (10, "[DEBUG] check_rights(): dh=%u, path='%s'\n", source_dh, tmpath);
    if (trust_check_rights (c, source_dh, tmpath, TRUST_RIGHT_FILESCAN) < 0) {
	// access is denied
	DPRINTF (9, "[ACCESS DENIED]\n");
	ncp_send_compcode (c, FS_ERROR_REMAPERROR);
	return;
    }*/

    // XXX: check dh range!

    ncp_send_compcode (c, 0);
}

/* 
 * ncp_handle_getvolno (int c, char* data, int size)
 *
 * This will handle NCP 22 5 (Get Volume Number) calls.
 *
 */
void
ncp_handle_getvolno (int c, char* data, int size) {
    uint8 	   vol_len;
    char  	   vol[NCP_MAX_FIELD_LEN];
    NCP_REPLY 	   r;
    int		   i;

    NCP_REQUIRE_LENGTH_MIN (2);

    vol_len = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (vol_len + 2);
    bzero (vol, NCP_MAX_FIELD_LEN);
    bcopy (data, vol, vol_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 5: Get volume number (vol='%s')\n", c, vol);

    // convert the name to uppercase
    for (i = 0; i < vol_len; i++) vol[i] = toupper (vol[i]);

    // scan for the volume name
    i = vol_find_volume (vol);
    if (i < 0) {
	ncp_send_compcode (c, 0xff);
	return;
    }

    // it worked. send the reply
    r.data[0] = (uint8)i;
    ncp_send_reply (c, &r, 1, 0);
}

/*
 * ncp_handle_getvolname (int c, char* data, int size)
 *
 * This will handle NCP 22 6 (Get Volume Name) calls.
 *
 */
void
ncp_handle_getvolname (int c, char* data, int size) {
    uint8	vol_no;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (2);
    vol_no = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 6: Get volume name (volid=%u)\n", c, vol_no);

    // is this volume within range?
    if (vol_no >= VOL_MAX_VOLS) {
	// no. complain
	ncp_send_compcode (c, FS_ERROR_NOSUCHVOLUME);
	return;
    }

    // send the result
    r.data[0] = strlen (volume[vol_no].name);
    bcopy (volume[vol_no].name, (char*)(r.data + 1), r.data[0]);

    ncp_send_reply (c, &r, r.data[0] + 1, 0);
}

/*
 * ncp_handle_scandiskrestrictions (int c, char* data, int size)
 *
 * This will handle NCP 22 32 (Scan volume's user disk restrictions) calls.
 *
 */
void
ncp_handle_scandiskrestrictions (int c, char* data, int size) {
    uint8	vol_no;
    uint32	seq_no;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (6);

    vol_no = GET_BE8 (data); data++;
    seq_no = GET_BE32 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 32: Scan volume's user disk restrictions (volno=%u,seqno=%u)\n", c, vol_no, seq_no);

    // XXX: not implemented... why would we care anyway?
    r.data[0] = 0;
    ncp_send_reply (c, &r, 1, 0);
}

/*
 * ncp_handle_createdir (int c, char* data, int size)
 *
 * This will handle NCP 22 10 (Create Directory) calls.
 *
 */
void
ncp_handle_createdir (int c, char* data, int size) {
    uint8	dh, mask, path_len;
    char	path[NCP_MAX_FIELD_LEN];
    char	realpath[VOL_MAX_VOL_PATH_LEN];
    int		i;

    NCP_REQUIRE_LENGTH_MIN (4);
    bzero (path, NCP_MAX_FIELD_LEN);

    dh = GET_BE8 (data); data++;
    mask = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 10: Create directory (dh=%u,mask=%u,path='%s')\n", c, dh, mask, path);

    // lowercase the directory name
    for (i = 0; i < strlen (path); i++)
	path[i] = tolower (path[i]);

    // grab the real path
    i = fs_build_path (c, dh, path, realpath, VOL_MAX_VOL_PATH_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. inform the user
	ncp_send_compcode (c, i);
	return;
    }

    DPRINTF (10, LOG_NCP, "[DEBUG] Create directory '%s'\n", realpath);

    // create the directory. an error is the same as the error code
    ncp_send_compcode (c, (mkdir (realpath, conf_dirmode) < 0) ? 0xff : 0);
}

/*
 * ncp_handle_addtrusteetodir (int c, char* data, int size)
 *
 * This will handle NCP 22 13 (Add trustee to directory) calls.
 *
 */
void
ncp_handle_addtrusteetodir (int c, char* data, int size) {
    uint8	dh, path_len, mask;
    uint32	trustee_id;
    char	path[NCP_MAX_FIELD_LEN];
    int		i;

    NCP_REQUIRE_LENGTH_MIN (9);
    bzero (path, NCP_MAX_FIELD_LEN);

    dh		= GET_BE8 (data); data++;
    trustee_id  = GET_BE32 (data); data += 4;
    mask	= GET_BE8  (data); data++;
    path_len	= GET_BE8 (data); data++;
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 13: Add trustee to directory (dh=%u,trusteeid=0x%x,mask=0x%x,path='%s')\n", c, dh, trustee_id, mask, path);

    // lowercase the path
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // it's all up to trust_grant_right(), now
    ncp_send_compcode (c, trust_grant_right (c, dh, path, trustee_id, mask));
}

/*
 * ncp_handle_addexttrustee (int c, char* data, int size)
 *
 * This will handle NCP 22 39 (Add extended trustee to directory or file) calls.
 *
 */
void
ncp_handle_addexttrustee (int c, char* data, int size) {
    uint8	dh, path_len;
    uint16	mask;
    uint32	trustee_id;
    char	path[NCP_MAX_FIELD_LEN];
    int		i;

    NCP_REQUIRE_LENGTH_MIN (9);
    bzero (path, NCP_MAX_FIELD_LEN);

    dh = GET_BE8 (data); data++;
    trustee_id  = GET_BE32 (data); data += 4;
    mask	= GET_BE16  (data); data += 2;
    path_len	= GET_BE8 (data); data++;
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 39: Add trustee to directory or file (dh=%u,trusteeid=0x%x,mask=0x%x,path='%s')\n", c, dh, trustee_id, mask, path);

    // lowercase the path
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // it's all up to trust_grant_right(), now
    ncp_send_compcode (c, trust_grant_right (c, dh, path, trustee_id, mask));
}

/*
 * ncp_handle_deletedir (int c, char* data, int size)
 *
 * This will handle NCP 22 11 (Delete directory) calls.
 *
 */
void
ncp_handle_deletedir (int c, char* data, int size) {
    uint8		dh, mask, name_len;
    char		name[NCP_MAX_FIELD_LEN];
    char		path[VOL_MAX_VOL_PATH_LEN];
    int			i;

    NCP_REQUIRE_LENGTH_MIN (5);

    bzero (path, VOL_MAX_VOL_PATH_LEN);
    dh = GET_BE8 (data); data++;
    mask = GET_BE8 (data); data++;
    name_len = GET_BE8 (data); data++;
    bcopy (data, name, name_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 11: Delete directory (dh=%u,mask=%u,path='%s')\n", c, dh, mask, name);

    // lowercase the directory name
    for (i = 0; i < name_len; i++)
	name[i] = tolower (name[i]);

    // grab the real path
    i = fs_build_path (c, dh, name, path, VOL_MAX_VOL_PATH_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. inform the user
	ncp_send_compcode (c, i);
	return;
    }

    // zap the directory (XXX: should mind more specific error codes)
    ncp_send_compcode (c, (rmdir (path) == 0) ? 0 : 0xff);
}

/*
 * ncp_handle_spacerest (int c, char* data, int size)
 *
 * This will handle NCP 22 41 (Get object disk usuage and restrictions) calls.
 *
 */
void
ncp_handle_spacerest (int c, char* data, int size) {
    uint8		volno;
    uint32		obj_id;
    NCP_REPLY		r;
    NCP_SPACEREST*	sr;

    NCP_REQUIRE_LENGTH (6);

    volno = GET_BE8 (data); data++;
    obj_id  = GET_BE32 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 41: Get object disk usuage and restrictions (vol=%u,objid=%u)\n", c, volno, obj_id);

    sr = (NCP_SPACEREST*)r.data;
    bzero (sr, sizeof (NCP_SPACEREST));
    sr->restriction[3] =  0x40;		// no restrictions

    // XXX: we should fill in the space used...
    ncp_send_reply (c, &r, sizeof (NCP_SPACEREST), 0);
}

/*
 * ncp_handle_geteffrights (int c, char* data, int size)
 *
 * This will handle NCP 22 42 (Get effective rights for directory entry) calls.
 *
 */
void
ncp_handle_geteffrights (int c, char* data, int size) {
    uint8		dh, pathlen;
    char		path[VOL_MAX_VOL_PATH_LEN];
    char  	 	realpath[VOL_MAX_VOL_PATH_LEN];
    NCP_REPLY		r;
    TRUST_RECORD	tr;
    int			i;

    NCP_REQUIRE_LENGTH_MIN (3);

    bzero (path, VOL_MAX_VOL_PATH_LEN);
    dh = GET_BE8 (data); data++;
    pathlen = GET_BE8 (data); data++;
    bcopy (data, path, pathlen);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 42: Get effective rights for directory entry (dh=%u,path='%s')\n", c, dh, path);

    // lowercase the path
    for (i = 0; i < pathlen; i++) path[i] = tolower (path[i]);

    // get the effective rights
    if ((i = trust_get_rights (c, dh, path, &tr)) != TRUST_ERROR_OK) {
	// this failed. return the error code
	ncp_send_compcode (c, i);
	return;
    }

    // send the packet
    r.data[0] = (tr.rights >> 8); r.data[1] = (tr.rights & 0xff);
    ncp_send_reply (c, &r, 2, 0);
}

/*
 * ncp_handle_scanextrust (int c, char* data, int size)
 *
 * This will handle NCP 22 38 (Scan file or directory for extended trustees)
 * calls.
 *
 */
void
ncp_handle_scanextrust (int c, char* data, int size) {
    uint8		dh, seq, pathlen;
    char		path[VOL_MAX_VOL_PATH_LEN];
    char  		realpath[VOL_MAX_VOL_PATH_LEN];
    TRUST_DSKRECORD	td;
    TRUST_RECORD	tr;
    FILE*		f;
    int			i;
    NCP_REPLY		r;
    NCP_SCANTRUST*	st;

    NCP_REQUIRE_LENGTH_MIN (4);
    bzero (path, VOL_MAX_VOL_PATH_LEN);
    dh = GET_BE8 (data); data++;
    seq = GET_BE8 (data); data++;
    pathlen = GET_BE8 (data); data++;
    bcopy (data, path, pathlen);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 38: Scan file or directory for extended trustees (dh=%u,seq=%u,path='%s')\n", c, dh, seq, path);

    // lowercase the path name
    for (i = 0; i < pathlen; i++) path[i] = tolower (path[i]);

    // build the trustee filename
    if ((i = trust_build_trustfile (c, dh, path, realpath, VOL_MAX_VOL_PATH_LEN)) != FS_ERROR_OK) {
	// this failed. return the error code
	ncp_send_compcode (c, i);
	return;
    }

    // build the result buffer
    st = (NCP_SCANTRUST*)r.data;
    bzero (st, sizeof (NCP_SCANTRUST));

    // open the file
    if ((f = fopen (realpath, "rb")) == NULL) {
	// this failed. return just zero trustees
	st->count = 0;
    } else {
	// it worked. grab all trustees
	DPRINTF (10, LOG_NCP, "[DEBUG] trustee file '%s' opened\n", realpath);
	st->count = 0;
	fseek (f, (sizeof (TRUST_DSKRECORD) * 20) * seq, SEEK_SET);
	while ((fread (&td, sizeof (TRUST_DSKRECORD), 1, f)) && (st->count < 20)) {
	    // convert the structure
	    trust_conv_rec (&td, &tr);

	    DPRINTF (10, LOG_NCP, "[DEBUG] Record: OBJID 0x%x, right 0x%x\n", tr.obj_id, tr.rights);

	    // add it to the buffer
	    st->object_id[st->count * 4    ] = (tr.obj_id >> 24);
	    st->object_id[st->count * 4 + 1] = (tr.obj_id >>  16);
	    st->object_id[st->count * 4 + 2] = (tr.obj_id >>   8);
	    st->object_id[st->count * 4 + 3] = (tr.obj_id & 0xff);
	    st->rights[st->count * 2       ] = (tr.rights >> 8);
	    st->rights[st->count * 2 + 1   ] = (tr.rights & 0xff);

	    // increment the count
	    st->count++;
	}

	fclose (f);
    }

    // send the reply
    ncp_send_reply (c, &r, sizeof (NCP_SCANTRUST), (st->count == 0) ? 0x9c : 0);
}

/*
 * ncp_handle_removetrusteefromdir (int c, char* data, int size)
 *
 * This will handle NCP 22 43 (Remove extended trustee from dir or file) calls.
 *
 */
void
ncp_handle_removetrusteefromdir (int c, char* data, int size) {
    uint8		dh, pathlen;
    uint32		objid;
    char		path[VOL_MAX_VOL_PATH_LEN];
    int			i;

    NCP_REQUIRE_LENGTH_MIN (5);
    bzero (path, VOL_MAX_VOL_PATH_LEN);
    dh = GET_BE8 (data); data++;
    objid = GET_BE32 (data); data += 5; // skip a byte
    pathlen = GET_BE8 (data); data++;
    bcopy (data, path, pathlen);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 43: Remove extended trustee from dir or file (dh=%u,objid=0x%x,path='%s')\n", c, dh, objid, path);

    // convert the path to lowercase
    for (i = 0; i < pathlen; i++)
	path[i] = tolower (path[i]);

    // it's all up to trust_remove_right(), now
    ncp_send_compcode (c, trust_remove_right (c, dh, path, objid));
}

/*
 * ncp_getvolpurgeinfo (int c, char* data, int size)
 *
 * This will handle NCP 22 44 (Get volume and purge information) requests.
 *
 */
void
ncp_getvolpurgeinfo (int c, char* data, int size) {
    uint8		volno;
    uint32		i;
    struct statfs	st;
    NCP_VOLPURGEINFO*	vpi;
    NCP_REPLY		r;

    // NOTE: Novell's documentation claims the volume number is only one byte,
    // but appearantly, the VLM.EXE sends 4 bytes. Lets just make sure we have
    // one byte or more (and discard everything else)
    NCP_REQUIRE_LENGTH_MIN (1);
    volno = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 44: Get volume and purge information (volno=%u)\n", c, volno);

    // XXX: volume range

    // do we have such a volume?
    if ((volume[volno].flags & VOL_FLAG_USED) == 0) {
	// no. return an error code
	ncp_send_compcode (c, FS_ERROR_NOSUCHVOLUME);
	return;
    }

    // query the volume information
    if (statfs (volume[volno].path, &st) < 0) {
	// this failed. complain
	DPRINTF (6, LOG_NCP, "[WARN] Cannot stat() path '%s'\n", volume[volno].path);
	ncp_send_compcode (c, FS_ERROR_IOERROR);
	return;
    }

    // build a pointer to the reply
    vpi = (NCP_VOLPURGEINFO*)r.data; bzero (vpi, sizeof (NCP_VOLPURGEINFO));

    // convert the data
    vpi->sectorsperblock = 8;	// XXX?
    i = (convert_blocks (st.f_blocks, st.f_bsize, 512) / 8);
    vpi->totalblocks[3] = (i >>  24); vpi->totalblocks[2] = (i >>  16);
    vpi->totalblocks[1] = (i >>   8); vpi->totalblocks[0] = (i & 0xff);
    i = (convert_blocks (st.f_bavail, st.f_bsize, 512) / 8);
    vpi->freeblocks[3] = (i >>  24); vpi->freeblocks[2] = (i >>  16);
    vpi->freeblocks[1] = (i >>   8); vpi->freeblocks[0] = (i & 0xff);
    i = st.f_files;
    vpi->totaldirentries[3] = (i >>  24); vpi->totaldirentries[2] = (i >>  16);
    vpi->totaldirentries[1] = (i >>   8); vpi->totaldirentries[0] = (i & 0xff);
    i = st.f_ffree;
    vpi->availdirentries[3] = (i >>  24); vpi->availdirentries[2] = (i >>  16);
    vpi->availdirentries[1] = (i >>   8); vpi->availdirentries[0] = (i & 0xff);
    vpi->namelen = strlen (volume[volno].name);
    bcopy (volume[volno].name, (char*)(r.data + 30), vpi->namelen);

    // send the reply
    ncp_send_reply (c, &r, sizeof (NCP_VOLPURGEINFO) + vpi->namelen, 0);
}

/*
 * ncp_handle_scandir (int c, char* data, int size)
 *
 * This will handle NCP 22 30 (Scan a Directory) calls.
 *
 */
void
ncp_handle_scandir (int c, char* data, int size) {
    uint8	   dh, attr, pathlen;
    uint32	   seqno, curseq;
    char	   path[VOL_MAX_VOL_PATH_LEN];
    char	   realpath[VOL_MAX_VOL_PATH_LEN];
    char*	   ptr;
    char*	   pattern;
    DIR*	   dir;
    struct dirent* dent;
    NCP_REPLY	   r;
    NCP_SCANFILE*  sf;   
    NCP_SCANDIR*   sd;
    int		   i;
    struct stat	   fs;
    
    NCP_REQUIRE_LENGTH_MIN (7);

    bzero (path, VOL_MAX_VOL_PATH_LEN);
    dh = GET_BE8 (data); data++;
    attr = GET_BE8 (data); data++;
    seqno = GET_BE32 (data); data += 4;
    pathlen = GET_BE8 (data); data++;
    bcopy (data, path, pathlen);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 22 30: Scan a directory (dh=%u,attr=0x%x,seqno=0x%x,path='%s')\n", c, dh, attr, seqno, path);

    // build the real path
    if (fs_build_path (c, dh, path, realpath, VOL_MAX_VOL_PATH_LEN, 0) != FS_ERROR_OK) {
	// this failed. return an error (XXX)
	ncp_send_compcode (c, FS_ERROR_IOERROR);
	return;
    }

    // okay, now isolate everything after the final '/' (that is our pattern)
    pattern = strrchr (realpath, '/');
    *pattern = 0; pattern++;

    // debugging :)
    DPRINTF (10, LOG_NCP, "[DEBUG] scan_dir path is '%s', pattern is '%s'\n", realpath, pattern);

    // open the directory
    dir = opendir (realpath);
    if (dir == NULL) {
	// we couldn't open the dir. complain
	DPRINTF (10, LOG_NCP, "[DEBUG] opendir('%s') failed\n", realpath);
        ncp_send_compcode (c, FS_ERROR_INVALIDPATH);
	return;
    }

    // go go go!
    curseq = 0xfffffffe;

    // keep looking
    while (1) {
	// grab the entry
	dent = readdir (dir);
	if (dent == NULL) break;

	// do we have a match?
        if ((match_fn (dent->d_name, pattern) || (!strcmp (dent->d_name, pattern))) && (strcmp (dent->d_name, TRUST_SYSFILE) != 0)) {
	    // yes. is it a directory?
            if ((dent->d_type & DT_DIR) != 0) {
                // yes. do we need to look for those?
                if ((attr & 0x10) != 0) {
                    // yes. are these not the '.', '..'?
                    if ((strcmp (dent->d_name, ".") != 0) && (strcmp (dent->d_name, "..") != 0)) {
			// no. increment the sequence number
			curseq++;
		    }
		}
	    } else {
		// we got a file match. is that what we were looking for?
                if ((attr & 0x10) == 0) {
		    // yes. increment the sequence number
		    curseq++;
		}
	    }
	}

	DPRINTF (10, LOG_NCP, "[DEBUG] scandir(): seqno=0x%x vs curseq=0x%x\n", seqno, curseq);

	// if it's correct, leave
	if (seqno == curseq) break;
    }

    // close the directory
    closedir (dir);

    // set up the result buffers
    sf = (NCP_SCANFILE*)r.data;
    sd = (NCP_SCANDIR*)r.data;
    bzero (sf, sizeof (NCP_SCANFILE));

    // was the outcome positive?
    if (dent != NULL) {
	// yes. build the reply
	DPRINTF (10, LOG_NCP, "[DEBUG] DENT != NULL (file '%s')!!!\n", dent->d_name);
	sf->namelen = strlen (dent->d_name);
	for (i = 0; i < 12; i++) sf->name[i] = toupper (dent->d_name[i]);

	// stat() the file
	snprintf (realpath, VOL_MAX_VOL_PATH_LEN, "%s/%s", realpath, dent->d_name);
	if (stat (realpath, &fs) < 0) {
	    DPRINTF (4, LOG_NCP, "[WARN] stat('%s') failed\n", realpath);
	}

	// insert the new sequence number
	seqno++;
	sf->seqno[0] = (seqno >> 24); sf->seqno[1] = (seqno >>  16);
	sf->seqno[2] = (seqno >>  8); sf->seqno[3] = (seqno & 0xff);

	// XXX: creator ID (it's always SUPERVISOR now)
	sf->create_id[3] = 1;

	// fill out the time and date fields
        i = date2nw (fs.st_mtime);
        sf->create_date[1] = (i >> 8); sf->create_date[0] = (i & 0xff);
        i = time2nw (fs.st_mtime);
        sf->create_time[1] = (i >> 8); sf->create_time[0] = (i & 0xff);
        i = date2nw (fs.st_mtime);
        sf->update_date[1] = (i >> 8); sf->update_date[0] = (i & 0xff);
        i = time2nw (fs.st_mtime);
        sf->update_time[1] = (i >> 8); sf->update_time[0] = (i & 0xff);

 	// is this a directory?
        if ((dent->d_type & DT_DIR) != 0) {
	    // yes. set the attribute
	    sd->attr[0] = 0x10;
	} else {
	    // no. set the file size to the correct value
	    sd->attr[0] = 0x20;
	    sf->size[3] = (fs.st_size >> 24); sf->size[2] = (fs.st_size >> 16);
	    sf->size[1] = (fs.st_size >>  8); sf->size[0] = (fs.st_size & 0xff);
	}

	// send the reply
	ncp_send_reply (c, &r, sizeof (NCP_SCANFILE), 0);
	return;
    }

    DPRINTF (10, LOG_NCP, "[DEBUG] SCAN_FILE: FAILURE\n");

    // this failed.
    ncp_send_compcode (c, 0xff);
}

/*
 * ncp_handle_r22 (int c, char* data, int size)
 *
 * This will handle [size] bytes of NCP 22 subfunction packet [data] for
 * connection [c].
 * 
 */
void
ncp_handle_r22 (int c, char* data, int size) {
    uint8 sub_func = *(data + 2);
    uint16 sub_size;

    // get the substructure size
    sub_size = (uint8)((*(data)) << 8)| (uint8)(*(data + 1));

    // does this size match up?
    if ((sub_size + 2) != size) {
	// no. complain
	DPRINTF (6, LOG_NCP, "[WARN] [%u] Got NCP 22 %u packet with size %u versus %u expected, adjusting\n", c, sub_func, sub_size + 2, size);
	sub_size = (size - 2);
    }

    // fix up the buffer offset
    data += 3;

    // handle the subfunction
    switch (sub_func) {
	 case 0: // set directory path
		 ncp_handle_setdirpath (c, data, sub_size);
		 break;
	 case 1: // get directory path
		 ncp_handle_getdirpath (c, data, sub_size);
		 break;
 	 case 3: // get effective directory rights
		 ncp_handle_effrights (c, data, sub_size);
		 break;
	 case 5: // get volume number
		 ncp_handle_getvolno (c, data, sub_size);
		 break;
	 case 6: // get volume name
		 ncp_handle_getvolname (c, data, sub_size);
		 break;
	case 10: // create directory
		 ncp_handle_createdir (c, data, sub_size);
		 break;
	case 11: // delete directory
		 ncp_handle_deletedir (c, data, sub_size);
		 break;
	case 13: // add trustee to directory
		 ncp_handle_addtrusteetodir (c, data, sub_size);
		 break;
	case 18: // allocate permanent dir handle
		 ncp_alloc_perm_dirhandle (c, data, sub_size);
		 break;
	case 19: // allocate temponary dir handle
		 ncp_alloc_temp_dirhandle (c, data, sub_size);
		 break;
	case 20: // deallocate directory handle
		 ncp_dealloc_dirhandle (c, data, sub_size);
	   	 break;
	case 21: // get volume info with handle
		 ncp_get_volinfo_handle (c, data, sub_size);
		 break;
	case 30: // scan a directory
		 ncp_handle_scandir (c, data, sub_size);
		 break;
	case 32: // scan volume's user disk restrictions
		 ncp_handle_scandiskrestrictions (c, data, sub_size);
		 break;
	case 38: // scan file or directory for extended trustees
		 ncp_handle_scanextrust (c, data, sub_size);
		 break;
	case 39: // add trustee to directory
		 ncp_handle_addexttrustee (c, data, sub_size);
		 break;
	case 41: // get object disk usage and space restrictions
		 ncp_handle_spacerest (c, data, sub_size);
		 break;
	case 42: // get effective rights for directory entry
		 ncp_handle_geteffrights (c, data, sub_size);
		 break;
	case 43: // remove extended trustee from dir or file
		 ncp_handle_removetrusteefromdir (c, data, sub_size);
		 break;
	case 44: // get volume and purge information
		 ncp_getvolpurgeinfo (c, data, sub_size);
		 break;
	case 55: // get volume and purge information
		 ncp_getvolpurgeinfo (c, data, sub_size);
		 break;
	default: // what's this?
		 DPRINTF (6, LOG_NCP, "[INFO] [%u] Unsupported NCP function 22 %u, ignored\n", c, sub_func);
		 ncp_send_compcode (c, 0xff);
	 	 break;
    }
}
