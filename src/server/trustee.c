/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "bindery.h"
#include "conn.h"
#include "config.h"
#include "fs.h"
#include "misc.h"
#include "trustee.h"

/*
 * trust_build_trustfile (int c, uint8 dh, char* path, char* dest, int destlen)
 *
 * This will fill at most [destlen] bytes of [dest] with the trustee path of
 * path [path] in directory handle [dh] of connection [c]. This will return
 * TRUST_ERROR_OK on success or xxx_ERROR_xxx on failure.
 *
 */
int
trust_build_trustfile (int c, uint8 dh, char* path, char* dest, int destlen) {
    int i;

    // first of all, build the path
    if ((i = fs_build_path (c, dh, path, dest, destlen, 0)) != FS_ERROR_OK) {
	// this failed. complain
	return i;
    }

    // now, add the trustee datafile
    strncat (dest, "/", destlen);
    strncat (dest, TRUST_SYSFILE, destlen);

    // it worked
    return TRUST_ERROR_OK;
}

/*
 * trust_conv_rec (TRUST_DSKRECORD* dr, TRUST_RECORD* rec)
 *
 * This will convert a trustee disk structure [dr] to internal structure [rec].
 *
 */
void
trust_conv_rec (TRUST_DSKRECORD* dr, TRUST_RECORD* rec) {
    rec->obj_id  = GET_BE32 (dr->obj_id);
    rec->rights  = GET_BE16 (dr->rights);
}

/* 
 * trust_grab_rights (char* fname, uint32 objid, TRUST_RECORD* rec)
 *
 * This will grab the trustee rights from file [fname] and store them in [rec].
 * It will make sure the rights relate to object [objid]. If no trustee could be
 * found, -1 will be returned, otherwise 0.
 *
 */
int
trust_grab_rights (char* fname, uint32 objid, TRUST_RECORD* rec) {
    TRUST_DSKRECORD	dr;
    FILE *f;

    // try to open the file
    if ((f = fopen (fname, "rb")) == NULL) {
	// can't open file, thus no rights
	return -1;
    }

    // scan the entire trustee list
    while (fread (&dr, sizeof (TRUST_DSKRECORD), 1, f)) {
	// convert the record into something useful
	trust_conv_rec (&dr, rec);

	// is this our user id?
	if (rec->obj_id == objid) {
	    // yes. we got the record
	    fclose (f);
	    return 0;
	}
    }

    // close the file
    fclose (f);

    // no such record
    return -1;
}

/*
 * trust_scan_rights (int volno, uint32 objid, char* path, TRUST_RECORD*
 * rec)
 *
 * This will scan path [path] on volume [volno] for trustees. The function will
 * only return trustees for object [objid]. The trustee will be copied to [rec].
 * This will return TRUST_ERROR_OK on success or TRUST_ERROR_xxx on failure.
 *
 */
int
trust_scan_rights (int volno, uint32 objid, char* path, TRUST_RECORD* rec) {
    char		tmp[VOL_MAX_VOL_PATH_LEN];
    int			i;
    TRUST_DSKRECORD	dr;
    char*		ptr;

    while (1) {
	// build the filename
	snprintf (tmp, VOL_MAX_VOL_PATH_LEN, "%s/%s", path, TRUST_SYSFILE);

	// try to grab the rights
	if (trust_grab_rights (tmp, objid, rec) == 0) {
	    // this worked. we've got the record
	    return TRUST_ERROR_OK;
	}

	// the trustee is not for this directory. try a level lower
	ptr = strrchr (path, '/');
	if (ptr != NULL) {
	    // we got it. is the path equal to the volume root?
	    if (!strcmp (path, volume[volno].path)) {
		// yes. we did all levels. set the rights to nothing
		bzero (rec, sizeof (TRUST_RECORD));
		return TRUST_ERROR_NORECORD;
	    }

	    // now, turn that charachter into a nul
	    *ptr = 0;
	} else {
	    // hmm... no slash. probably no rights anway ?
	    break;
	}
    }

    // NOTREACHED
    return -1;
}

/*
 * trust_get_rights (int c, uint8 dh, char* path, TRUST_RECORD*
 * rec)
 *
 * This will scan path [path] of directory handle [dh] of connection [c] for
 * trustees. The function will only return trustees for object [objid]. The
 * trustee will be copied to [rec]. This will return TRUST_ERROR_OK on success
 * or TRUST_ERROR_xxx on failure.
 *
 */
int
trust_get_rights (int c, uint8 dh, char* path, TRUST_RECORD* rec) {
    char		realpath[VOL_MAX_VOL_PATH_LEN];
    char		tmp[VOL_MAX_VOL_PATH_LEN];
    int			i, j;
    char*		ptr;
    BINDERY_PROP	prop;
    BINDERY_VAL		val;
    uint32		id;

    // are we a supervisor?
    if (conn[c]->object_supervisor) {
	// yes. auto-grant everything
	rec->obj_id = conn[c]->object_id; rec->rights = 0xffff;
	return TRUST_ERROR_OK;
    }

    // figure out what the path would be
    bzero (rec, sizeof (TRUST_RECORD));
    if (fs_build_path (c, dh, path, realpath, VOL_MAX_VOL_PATH_LEN, 0) != FS_ERROR_OK) {
	// can't build path... auto-deny
	return TRUST_ERROR_OK;
    }

    // is this path equal to {SYS}/login ?
    snprintf (tmp, VOL_MAX_VOL_PATH_LEN, "%s/login", volume[conn[c]->dh[dh].volno].path);
    if (!strncmp (realpath, tmp, strlen (tmp))) {
	// yes. auto-grant read and file scan
	rec->obj_id = conn[c]->object_id;
	rec->rights = (TRUST_RIGHT_READ | TRUST_RIGHT_FILESCAN) << 8;
	return TRUST_ERROR_OK;
    }

    // build the trustee file name
    if ((i = trust_build_trustfile (c, dh, path, realpath, VOL_MAX_VOL_PATH_LEN)) != TRUST_ERROR_OK) {
	// this failed. complain
	return i;
    }
    DPRINTF (10, LOG_TRUSTEE, "[DEBUG] get_rights(): PATH = '%s'\n", realpath);

    // get rid of everything after the final '/'
    if ((ptr = strrchr (realpath, '/')) == NULL) {
	// weird... no slash found. path is probably bogus now. complain
	return FS_ERROR_INVALIDPATH;
    }
    *ptr = 0;

    DPRINTF (10, LOG_TRUSTEE, "[DEBUG] get_rights(): path '%s'\n", realpath);

    // scan rights for the object itself
    strcpy (tmp, realpath);
    j = trust_scan_rights (conn[c]->dh[dh].volno, conn[c]->object_id, tmp, rec);
    if (j == TRUST_ERROR_NORECORD) {
	// there's no such record. grab the 'SECURITY_EQUALS' property from
	// the object
	if (bindio_read_prop (conn[c]->object_id, "SECURITY_EQUALS", 1, &val, &prop) == BINDERY_ERROR_OK) {
	    // yeppee, this worked, now. scan for each object in here
   	    for (i = 0; i < (128 / 4); i++) {
		// build the number
		id  = (uint8)(val.data[i    ]) << 24;
		id |= (uint8)(val.data[i + 1]) << 16;
		id |= (uint8)(val.data[i + 2]) <<  8;
		id |= (uint8)(val.data[i + 3]);

		// is this object zero ?
		if (id) {
		    // no, it's not. scan for the trustee
	 	    strcpy (tmp, realpath);
    		    i = trust_scan_rights (conn[c]->dh[dh].volno, id, tmp, rec);
		    if (i == TRUST_ERROR_OK) {
			// yay, we got it. return
			return i;
		    }
		}
	    }
	}
    }
    return j;
}

/*
 * trust_grant_right (int c, uint8 dh, char* path, uint32 objid, uint16 rights)
 *
 * This will grant right [rights] to object [objid] on path [path] of directory
 * handle [dh] and connection [c]. It will return TRUST_ERROR_OK on success or
 * xxx_ERROR_xxx on failure.
 *
 */
int
trust_grant_right (int c, uint8 dh, char* path, uint32 objid, uint16 rights) {
    char		realpath[VOL_MAX_VOL_PATH_LEN];
    FILE*		f;
    TRUST_DSKRECORD	dr;
    TRUST_RECORD	rec;
    int			i;

    // build the trustee file name
    if ((i = trust_build_trustfile (c, dh, path, realpath, VOL_MAX_VOL_PATH_LEN)) != TRUST_ERROR_OK) {
	// this failed. complain
	return i;
    }

    // try to open the file
    DPRINTF (10, LOG_TRUSTEE, "[DEBUG] Trustee file '%s'\n", realpath);
    if ((f = fopen (realpath, "r+b")) == NULL) {
	// this failed. perhaps we can create it?
	if ((f = fopen (realpath, "w+b")) == NULL) {
	    // nope... can't create the file either. complain
	    return TRUST_ERROR_IOERROR;
	} else {
	    // chmod the file correctly
	    if (fchmod (fileno (f), conf_trusteemode) < 0) {
		// warn the user
		DPRINTF (0, LOG_TRUSTEE, "[WARN] Can't set file mode for file '%s'\n", realpath);
	    }
	}
    }

    // browse the file
    while (fread (&dr, sizeof (TRUST_DSKRECORD), 1, f)) {
	// convert the structure
	trust_conv_rec (&dr, &rec);

	// is this trustee already for this object?
	if (rec.obj_id == objid) {
	    // yes. update the mask and write it back
	    dr.rights[0] = (rights >> 8); dr.rights[1] = (rights & 0xff);
	    fseek (f, ftell (f) - sizeof (TRUST_DSKRECORD), SEEK_SET);
	    if (!fwrite (&dr, sizeof (TRUST_DSKRECORD), 1, f)) {
		// this failed. complain
	        fclose (f);
		return TRUST_ERROR_IOERROR;
	    }

	    // yay, this worked. return ok status
	    fclose (f);
	    return TRUST_ERROR_OK;
	}
    }

    // the trustee was not found. add the one
    dr.obj_id[0] = (objid >> 24); dr.obj_id[1] = (objid >>  16);
    dr.obj_id[2] = (objid >>  8); dr.obj_id[3] = (objid & 0xff);
    dr.rights[0] = (rights >> 8); dr.rights[1] = (rights & 0xff);

    // write the entry
    if (!fwrite (&dr, sizeof (TRUST_DSKRECORD), 1, f)) {
	// this failed. complain
	fclose (f);
	return TRUST_ERROR_IOERROR;
    }

    // close the file
    fclose (f);

    // it worked
    return TRUST_ERROR_OK;
}

/*
 * trust_remove_right (int c, uint8 dh, char* path, uint32 objid)
 *
 * This will remove the right granted to object [objid] on path [path] of
 * directory handle [dh] and connection [c]. It will return TRUST_ERROR_OK on
 * success or xxx_ERROR_xxx on failure.
 *
 */
int
trust_remove_right (int c, uint8 dh, char* path, uint32 objid) {
    char		realpath[VOL_MAX_VOL_PATH_LEN];
    FILE*		f;
    TRUST_DSKRECORD	dr;
    TRUST_RECORD	rec;
    int			i, pos, maxpos;

    // build the trustee file name
    if ((i = trust_build_trustfile (c, dh, path, realpath, VOL_MAX_VOL_PATH_LEN)) != TRUST_ERROR_OK) {
	// this failed. complain
	return i;
    }

    // try to open the file
    DPRINTF (10, LOG_TRUSTEE, "[DEBUG] Trustee file '%s'\n", realpath);
    if ((f = fopen (realpath, "r+b")) == NULL) {
	// this failed. return an error
	return TRUST_ERROR_IOERROR;
    }

    // scan for the right
    while (fread (&dr, sizeof (TRUST_DSKRECORD), 1, f)) {
	// convert the right
	trust_conv_rec (&dr, &rec);

	// if this is the right, break out
	if (rec.obj_id == objid) break;

	// otherwise keep looking
    }

    // do we have the right?
    if (rec.obj_id != objid) {
	// no. return an error code
	fclose (f);
	return TRUST_ERROR_NOTFOUND;
    }

    // get the trustee position and the final position
    pos = ftell (f) - sizeof (TRUST_DSKRECORD);
    fseek (f, 0, SEEK_END);
    maxpos = ftell (f) - sizeof (TRUST_DSKRECORD);

    // are these two positions equal?
    rewind (f);
    if (pos == maxpos) {
	// yes. we can now just truncate the file. do it
	if (ftruncate (fileno (f), pos) < 0) {
	    // this failed. complain
	    fclose (f);
	    return TRUST_ERROR_IOERROR;
	}
    } else {
	// damn, we can't. grab the last trustee
	fseek (f, maxpos, SEEK_SET);
	if (!fread (&dr, sizeof (TRUST_DSKRECORD), 1, f)) {
	    // this failed. complain
	    fclose (f);
	    return TRUST_ERROR_IOERROR;
	}

	// seek to the original position
	fseek (f, pos, SEEK_SET);

	// overwrite the trustee record
	if (!fwrite (&dr, sizeof (TRUST_DSKRECORD), 1, f)) {
	    // this failed. complain
	    fclose (f);
 	    return TRUST_ERROR_IOERROR;
	}

	// truncate the file
	rewind (f);
	if (ftruncate (fileno (f), maxpos) < 0) {
	    // this failed. complain
	    fclose (f);
	    return TRUST_ERROR_IOERROR;
	}
    }
    

    // it worked
    fclose (f);
    return TRUST_ERROR_OK;
}

/*
 * trust_check_rights (int c, int dh, char* path, uint16 right)
 *
 * This will verify whether connection [c] possesses right [right] over path
 * [path] in directory handle [dh]. It will return 0 if it does or -1 if
 * it doesn't.
 * 
 */
int
trust_check_rights (int c, int dh, char* path, uint16 right) {
    TRUST_RECORD rec;

    // get the rights
    DPRINTF (11, LOG_TRUSTEE, "[DEBUG] trust_check_rights(): dh=%u,path='%s' (dh->path='%s')\n", dh, path, conn[c]->dh[dh].path);
    if (trust_get_rights (c, dh, path, &rec) != TRUST_ERROR_OK) {
	// this failed. complain
	return -1;
    }

    // it's all about the bits, now...
    DPRINTF (11, LOG_TRUSTEE, "[DEBUG] trust_check_rights(): dh=%u,path='%s' -> rights=0x%x, wanted=0x%x gives %u\n", dh, path, (rec.rights) >> 8, right, (((rec.rights >> 8) & right) == right) ? 0 : -1);
    return (((rec.rights >> 8) & right) == right) ? 0 : -1;
}
