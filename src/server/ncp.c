/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * NCP code
 * 
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef LINUX
#include <sys/vfs.h>
#endif
#include "config.h"
#include "conn.h"
#include "fs.h"
#include "ncp.h"
#include "net.h"
#include "misc.h"
#include "trustee.h"

int fd_ncp = -1;

/*
 * ncp_init()
 *
 * This will initialize the NCP services. It will quit on any error.
 *
 */
void
ncp_init() {
    // create the socket
    fd_ncp = net_create_socket (SOCK_DGRAM, IPXPORT_NCP, IPXPROTO_NCP);
    if (fd_ncp < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] can't create NCP socket\n");
	exit (1);
    }
}

/*
 * ncp_create_conn (struct sockaddr_ipx* from)
 *
 * This will create a connection for a client from [from].
 *
 */
void
ncp_create_conn (struct sockaddr_ipx* from) {
   int i;
   NCP_REPLY r;

   DPRINTF (9, LOG_NCP, "[INFO] NCP 0x1111: Request service connection\n");

   i = conn_attach_client (from);
   if (i < 0) {
	// this failed. send an error code (XXX)
	DPRINTF (2, LOG_NCP, "[WARN] Out of service connections\n");
	return;
   }

   // it worked. inform the client
   DPRINTF (6, LOG_NCP, "[INFO] [%u] Station %s connected\n", i, ipx_ntoa (IPX_GET_ADDR (from)));

   // send this data
   ncp_send_compcode (i, 0);
}

/*
 * ncp_destroy_conn (int c, struct sockaddr_ipx* from)
 *
 * This will destroy service connection [c].
 *
 */
void
ncp_destroy_conn (int c, struct sockaddr_ipx* from) {
    NCP_REPLY r;

    DPRINTF (6, LOG_NCP, "[INFO] Station %s disconnected, connection number %u\n", ipx_ntoa (IPX_GET_ADDR (from)), c);

    // it worked. inform the client
    bzero (&r, sizeof (NCP_REPLY));
    r.rep_type[0] = 0x33; r.rep_type[1] = 0x33;
    r.con_no = (c & 0xff);
    r.task_no = conn[c]->task_no;
    r.seq_no = conn[c]->seq_no;

    // tell the client it's ok
    sendto (fd_ncp, (char *)&r, 8, 0, (struct sockaddr*)from, sizeof (struct sockaddr_ipx));

    // zero out the connection
    bzero (conn[c], sizeof (CONNECTION));
}

/*
 * ncp_send_compcode_addr (struct sockaddr_ipx* addr, uint8 task_no,
 *			   uint8 seq_no, uint8 comp_code, uint8 flags)
 *
 *
 * This will send completion code [comp_code] to address [addr], using task no
 * [task_no], sequence number [seq_no], connection number [c] and flags [flags].
 *
 */
void
ncp_send_compcode_addr (struct sockaddr_ipx* addr, int c, uint8 task_no, uint seq_no, uint8 comp_code, uint8 flags) {
    NCP_REPLY r;

    // build the reply
    bzero (&r, sizeof (NCP_REPLY));
    r.rep_type[0] = 0x33; r.rep_type[1] = 0x33;
    r.con_no = c;
    r.task_no = task_no;
    r.seq_no = seq_no;
    r.comp_code = comp_code;
    r.conn_stat = flags;

    // send the reply
    sendto (fd_ncp, (char*)&r, 8, 0, (struct sockaddr*)addr, sizeof (struct sockaddr_ipx));
}

/*
 * ncp_send_compcode (int c, uint8 comp_code)
 *
 * This will send completion code [comp_code] to connection [c].
 *
 */
void
ncp_send_compcode (int c, uint8 comp_code) {
    NCP_REPLY r;

    // if it's not an error, log this at crazy high debugging levels
    if (comp_code != 0)
	DPRINTF (10, LOG_NCP, "[DEBUG] [%u] Error code 0x%x\n", c, comp_code);

    // send the packet
    ncp_send_reply (c, &r, 0, comp_code);
}

/*
 * ncp_send_reply (int c, NCP_REPLY* r, int size, uint8 comp_code)
 *
 * This will send [size] + 8 bytes of NCP reply [r] to connection [c]. The reply * will have completion code [comp_code].
 *
 */
void
ncp_send_reply (int c, NCP_REPLY* r, int size, uint8 comp_code) {
    // fix up the packet
    r->rep_type[0] = 0x33; r->rep_type[1] = 0x33;
    r->seq_no = conn[c]->seq_no; r->task_no = conn[c]->task_no;
    r->con_no = (c & 0xff); r->reserved = (c >> 8);
    r->conn_stat = 0; r->comp_code = comp_code;

    // is the server about to go down?
    if (down_flag) {
	// yes, set the flag in the connection status
	r->conn_stat |= 0x8;
    }

    // do we have a pending message?
    if (conn[c]->message[0]) {
	// yes. set the flag
	r->conn_stat |= 0x40;
    }

    // send the packet
    sendto (fd_ncp, (char *)r, size + 8, 0, (struct sockaddr*)&conn[c]->addr, sizeof (struct sockaddr_ipx));
}

/*
 * ncp_handle_buffersize (int c, char* data, int size)
 *
 * This will handle NCP 33's, negotiate buffer size.
 *
 */
void
ncp_handle_buffersize (int c, char* data, int size) {
    NCP_REPLY r;

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 33: Negotiate buffer size\n", c);

    // build the reply
    r.data[0] = (PACKET_BUFSIZE >> 8); r.data[1] = (PACKET_BUFSIZE & 0xff);

    // send the reply
    ncp_send_reply (c, &r, 2, 0);
}

/*
 * ncp_handle_timedate (int c)
 *
 * This will handle NCP 20: Get file server date and time requests for
 * connection [c].
 *
 */
void
ncp_handle_timedate (int c) {
    NCP_REPLY r;
    NCP_SERVERTIME* t;
    time_t timer;
    struct tm* s_tm;

    // set up the buffers
    t = (NCP_SERVERTIME*)r.data;
    bzero (t, sizeof (NCP_SERVERTIME));

    // fill them out
    time (&timer); s_tm = localtime (&timer);
    t->year = s_tm->tm_year;
    t->month = s_tm->tm_mon + 1;
    t->day = s_tm->tm_mday;
    t->hour = s_tm->tm_hour;
    t->minute = s_tm->tm_min;
    t->second = s_tm->tm_sec;
    t->dayofweek = s_tm->tm_wday;

    // send the reply
    ncp_send_reply (c, &r, sizeof (NCP_SERVERTIME), 0);
}

/*
 * ncp_handle_fs_init (int c, char* data, int size)
 *
 * This will handle NCP 62 (file search initialize) requests.
 *
 */
void
ncp_handle_fs_init (int c, char* data, int size) {
    uint8	    dh, path_len;
    char  	    path[NCP_MAX_FIELD_LEN];
    NCP_REPLY	    r;
    NCP_SEARCHINIT* si;
    int		    i;

    NCP_REQUIRE_LENGTH_MIN (2);

    dh = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;

    NCP_REQUIRE_LENGTH (path_len + 2);
    bzero (path, NCP_MAX_FIELD_LEN);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 62: File search initialize (dh=%u,path='%s')\n", c, dh, path);

    // convert the path to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // allocate the search handle
    i = fs_con_alloc_sh (c, dh, path);
    if (i < 0) {
	// this failed. complain
	ncp_send_compcode (c, 0xff);
	return;
    }

    // build the reply
    si = (NCP_SEARCHINIT*)r.data;
    bzero (si, sizeof (NCP_SEARCHINIT));
    si->volno = conn[c]->dh[dh].volno;
    si->dirid[0] = (i >> 8); si->dirid[1] = (i & 0xff);
    si->seqno[0] = 0xff; si->seqno[1] = 0xff;
    si->rights = 0xff;				// XXX: rights

    // send the reply
    ncp_send_reply (c, &r, sizeof (NCP_SEARCHINIT), 0);
}

/*
 * ncp_handle_fs_cont (int c, char* data, int size)
 *
 * This will handle NCP 63 (file search continue) requests.
 *
 */
void
ncp_handle_fs_cont (int c, char* data, int size) {
    uint8	    volno, attr, path_len;
    uint16	    sh, seqno, destseq;
    char  	    path[NCP_MAX_FIELD_LEN];
    char  	    tmp[NCP_MAX_FIELD_LEN];
    char  	    tempath[NCP_MAX_FIELD_LEN];
    DIR*	    dir;
    struct dirent*  dent;
    struct stat	    fs;
    NCP_REPLY	    r;
    NCP_SEARCHFILE* sf;
    NCP_SEARCHDIR*  sd;
    int		    i;

    NCP_REQUIRE_LENGTH_MIN (7);

    volno = GET_BE8 (data); data++;
    sh = GET_BE16 (data); data += 2;
    seqno = GET_BE16 (data); data += 2;
    attr = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (7 + path_len);
    bzero (path, NCP_MAX_FIELD_LEN);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 63: File search continue (sh=%u,seqno=%u,attr=%u,path='%s')\n", c, sh, seqno, attr, path);

    // convert the path to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // open the directory
    dir = opendir (conn[c]->sh[sh].path);
    if (dir == NULL) {
	// this failed. complain
	DPRINTF (8, LOG_NCP, "[WARN] [%u] NCP 63: cannot open directory '%s'\n", c, conn[c]->sh[sh].path);
	ncp_send_compcode (c, 0xff);
	return;
    }

    // calculate the search numbers
    destseq = (seqno + 1); seqno = 0xffff;
    while (1) {
        dent = readdir (dir);
        if (dent == NULL) break;

        // is this entry ok?
        if ((match_fn (dent->d_name, path) || (!strcmp (dent->d_name, path))) && (strcmp (dent->d_name, TRUST_SYSFILE) != 0)) {
            // yeah. is this a directory?
            if ((dent->d_type & DT_DIR) != 0) {
                // yes. do we need to look for those?
                if ((attr & 0x10) != 0) {
                    // yes. are these not the '.', '..'?
                    if ((strcmp (dent->d_name, ".") != 0) && (strcmp (dent->d_name, "..") != 0)) {
                        // no. do we have rights in this directory?
			if (conn[c]->sh[sh].relpath[0]) {
			    snprintf (tmp, NCP_MAX_FIELD_LEN, "%s/%s", conn[c]->sh[sh].relpath, dent->d_name);
			} else {
			    strcpy (tmp, dent->d_name);
			}
			i = trust_check_rights (c, conn[c]->sh[sh].dh, tmp, TRUST_RIGHT_FILESCAN);
			DPRINTF (10, LOG_NCP, "CHECK_RIGHTS(): c=%u,tmp='%s',result=%i\n", c, tmp, i);
			if (i == 0) {
			    // yes. increment the sequence number
                            seqno++;
			}
                    }
                }
            } else {
                // no. do we need to look for files?
                if (((attr & 0x10) == 0) && (conn[c]->sh[sh].scan_right)) {
                    // yes. increment the sequence number
                    seqno++;
                }
            }
        }

        // if we have reached our sequence number, say it's cool
        if (seqno == destseq) break;
    }

    // build the pointers
    sf = (NCP_SEARCHFILE*)r.data;
    sd = (NCP_SEARCHDIR*)r.data;
    bzero (sf, sizeof (NCP_SEARCHFILE));

    // was the outcome positive?
    if (dent != NULL) {
        // yes. build the output buffer
        sf->seqno[0] = (destseq >> 8); sf->seqno[1] = (destseq & 0xff)
;
        sf->dirid[0] = (sh >> 8); sf->dirid[1] = (sh & 0xff);
        for (i = 0; i < 14; i++) sf->filename[i] = toupper (dent->d_name[i]);

	// build the complete file name
	snprintf (tempath, NCP_MAX_FIELD_LEN, "%s/%s", conn[c]->sh[sh].path, dent->d_name);

        // stat() this file
        i = stat (tempath, &fs);
        if (i < 0) {
	    DPRINTF (8, LOG_NCP, "[WARN] [%u] NCP 63: cannot fstat '%s'\n", c, tempath);
        }

	// need to return a file?
	if ((attr & 0x10) == 0) {
            // yes. build the buffer for a file
            sf->attr = 0x00; sf->mode = 0xff; // TODO
            i = fs.st_size;
            sf->length[0] = (i >> 24); sf->length[1] = (i >> 16);
            sf->length[2] = (i >>  8); sf->length[3] = (i & 0xff);
            i = date2nw (fs.st_mtime);
            sf->create_date[0] = (i >> 8); sf->create_date[1] = (i & 0xff);
            i = date2nw (fs.st_atime);
            sf->access_date[0] = (i >> 8); sf->access_date[1] = (i & 0xff);
            i = date2nw (fs.st_mtime);
            sf->update_date[0] = (i >> 8); sf->update_date[1] = (i & 0xff);
            i = time2nw (fs.st_mtime);
            sf->update_time[0] = (i >> 8); sf->update_time[1] = (i & 0xff);
        } else {
            // no. build the buffer for a directory
            sd->attr = 0x10; sd->mode = 0xff; // TODO
            i = date2nw (fs.st_mtime);
            sd->create_date[0] = (i >> 8); sd->create_date[1] = (i & 0xff);
            i = time2nw (fs.st_mtime);
            sd->create_time[0] = (i >> 8); sd->create_time[1] = (i & 0xff);
            sd->owner_id[3] = 0x1; // TODO: owner
            sd->magic[0] = 0xd1; sd->magic[1] = 0xd1;
	}

 	// send the reply
	ncp_send_reply (c, &r, sizeof (NCP_SEARCHFILE), 0);
    } else {
	// no more files found
	ncp_send_compcode (c, 0xff);
    }

    closedir (dir);
}

/*
 * ncp_handle_fopen (int c, char* data, int size)
 *
 * This will handle NCP 76 (open file) requests.
 *
 */
void
ncp_handle_fopen (int c, char* data, int size) {
    uint8	    dh, attr, rights, path_len;
    char  	    path[NCP_MAX_FIELD_LEN];
    int		    i, handle;
    struct stat	    fs;
    NCP_REPLY	    r;
    NCP_OPENFILE*   of;

    NCP_REQUIRE_LENGTH_MIN (4);

    dh = GET_BE8 (data); data++;
    attr = GET_BE8 (data); data++;
    rights = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (4 + path_len);
    bzero (path, NCP_MAX_FIELD_LEN);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 76: Open file (dh=%u,attr=%u,rights=%u,path='%s')\n", c, dh, attr, rights, path);

    // convert the path to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // are the rights set to compatibility mode?
    if (rights & 0x10) {
	// yes. handle them
	attr = 0;
	if (rights & 1) attr |= 0x1;			// read only
	if (rights & 2) attr |= 0x2;			// write only

	// are they both set?
	if (attr == 0x3)
	    // yes. zap that and use read/write mode
	    attr |= 0x4;

	DPRINTF (10, LOG_NCP, "[DEBUG] fs_open_file(): rights 0x%x override attribute, attr is now 0x%x\n", rights, attr);
    }

    // try to open the file
    i = fs_open_file (c, dh, attr, path, &handle);
    if (i != FS_ERROR_OK) {
	// this failed. return an error
	DPRINTF (10, LOG_NCP, "[DEBUG] [%u] fs_open_file() failed, error code 0x%x\n", c, i);
	ncp_send_compcode (c, i);
	return;
    }

    // fstat() the file
    if (fstat (conn[c]->fh[handle].fd, &fs) < 0) {
	// this failed. complain
	DPRINTF (10, LOG_NCP, "[DEBUG] [%u] fstat() failed\n", c);
	close (conn[c]->fh[handle].fd);
	conn[c]->fh[handle].fd = -1;
	ncp_send_compcode (c, FS_ERROR_IOERROR);
	return;
    }

    // ok, we got it. build the result
    of = (NCP_OPENFILE*)r.data;
    bzero (of, sizeof (NCP_OPENFILE));
    of->handle[4] = (handle >> 8); of->handle[5] = (handle & 0xff);
    for (i = 0; i < 14; i++) of->name[i] = toupper (path[i]);
    of->attr = 0x20;		// XXX
    i = fs.st_size;
    of->length[0] = (i >> 24); of->length[1] = (i >> 16);
    of->length[2] = (i >>  8); of->length[3] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    of->create_date[0] = (i >>  8); of->create_date[1] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    of->access_date[0] = (i >>  8); of->access_date[1] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    of->update_date[0] = (i >>  8); of->update_date[1] = (i & 0xff);
    i = time2nw (fs.st_mtime);
    of->update_time[0] = (i >>  8); of->update_time[1] = (i & 0xff);

    // send the packet
    ncp_send_reply (c, &r, sizeof (NCP_OPENFILE), 0);
}

/*
 * ncp_handle_fread (int c, char* data, int size)
 *
 * This will handle NCP 72 (read from a file) calls.
 *
 */
void
ncp_handle_fread (int c, char* data, int size) {
    uint16 	handle, len;
    uint32 	offset;
    int    	odd, i;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (13);

    data   += 5;
    handle = GET_BE16 (data); data += 2;
    offset = GET_BE32 (data); data += 4;
    len    = GET_BE16 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 72: Read from a file (handle=%u,offset=%u,len=%u)\n", c, handle, offset, len);

    // XXX: check handle for range

    // is this file opened?
    if (conn[c]->fh[handle].fd < 0) {
	// no. return an error
	ncp_send_compcode (c, FS_ERROR_BADHANDLE);
	return;
    }

    // seek to the correct position
    if (lseek (conn[c]->fh[handle].fd, offset, SEEK_SET) < 0) {
	DPRINTF (10, LOG_NCP, "fread(): lseek() failed!\n");
	ncp_send_compcode (c, FS_ERROR_IOERROR);
	return;
    }

    odd = (offset & 1);

    i = read (conn[c]->fh[handle].fd, (char*)(r.data + 2 + odd), len);
    if (i < 0) {
	// this failed.
	ncp_send_compcode (c, FS_ERROR_IOERROR);
	return;
    }

    // fix up the size
    r.data[0] = (uint8)(i >> 8); r.data[1] = (uint8)(i & 0xff);

    // send the reply
    ncp_send_reply (c, &r, i + 2 + odd, 0);
}

/*
 * ncp_handle_fclose (int c, char* data, int size)
 *
 * This will handle NCP 66 (close file) calls.
 *
 */
void
ncp_handle_fclose (int c, char* data, int size) {
    uint16 handle;

    NCP_REQUIRE_LENGTH (7);

    data += 5;
    handle = GET_BE16 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 66: Close file (handle=%u)\n", c, handle);

    // return whatever fs_close_file() tells us
    ncp_send_compcode (c, fs_close_file (c, handle));
}

/*
 * ncp_handle_eoj (int c, char* data, int size)
 *
 * This will handle NCP 24 (End of Job) calls.
 *
 */
void
ncp_handle_eoj (int c, char* data, int size) {
    int i;

    NCP_REQUIRE_LENGTH (0);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 24: End of job\n", c);

    // get rid of all search handles
    for (i = 0; i < CONN_SH_PER_CONN; i++) {
	conn[c]->sh[i].path[0] = 0;
    }

    ncp_send_compcode (c, 0x00);
}

/*
 * ncp_handle_logout (int c, char* data, int size)
 *
 * This will handle NCP 25 (Logout) calls.
 *
 */
void
ncp_handle_logout (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (0);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 25: Logout\n", c);

    // log the user out
    conn_logout (c);

    ncp_send_compcode (c, 0x00);
}

/*
 * ncp_handle_maxpacket (int c, char* data, int size)
 *
 *
 * This will handle NCP 97 (Get Big Packet NCP Max Packet Size) calls.
 *
 */
void
ncp_handle_maxpacket (int c, char* data, int size) {
    uint8	   sec_flag;
    uint16	   prop_size;
    NCP_REPLY	   r;
    NCP_MAXPACKET* mp;

    NCP_REQUIRE_LENGTH (3);

    prop_size = GET_BE16 (data); data++;
    sec_flag  = GET_BE8 (data);

    #ifdef PBURST_WORKS
	DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 97: Get big NCP packet size (proposed max=%u,security=%u)\n", c, prop_size, sec_flag);

	mp = (NCP_MAXPACKET*)r.data;
	mp->accepted_size[0] = (prop_size >> 8);
	mp->accepted_size[1] = (prop_size & 0xff);
	mp->echo_socket[0] = (PORT_ECHO >> 8);
	mp->echo_socket[1] = (PORT_ECHO & 0xff);
	mp->security_flag = 0;				// XXX

	ncp_send_reply (c, &r, sizeof (NCP_MAXPACKET), 0);
    #else
	DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 97: Get big NCP packet size (proposed max=%u,security=%u), unsupported\n", c, prop_size, sec_flag);

	ncp_send_compcode (c, 0xfb);
    #endif
}

/*
 * ncp_handle_filesearch (int c, char* data, int size)
 *
 * This will handle NCP 64 (Search for a File) requests.
 *
 */
void
ncp_handle_filesearch (int c, char* data, int size) {
    uint16	    seqno, destseq;
    uint8	    dh;
    uint8	    attr;
    uint8	    namelen;
    int		    i;
    char	    name[NCP_MAX_FIELD_LEN];
    char	    path[VOL_MAX_VOL_PATH_LEN];
    char  	    tempath[NCP_MAX_FIELD_LEN];
    char*	    pattern;
    NCP_REPLY	    r;
    NCP_SEARCHFILE* sf;
    DIR*	    dir;
    struct dirent*  dent;
    struct stat	    fs;

    NCP_REQUIRE_LENGTH_MIN (6);

    bzero (name, NCP_MAX_FIELD_LEN);
    seqno   = GET_BE16 (data); data += 2;
    dh      = GET_BE8 (data); data++;
    attr    = GET_BE8 (data); data++; 
    namelen = GET_BE8 (data); data++;
    bcopy (data, name, namelen);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 64: Search for a file (seqno=%u,dh=%u,attr=%u,name='%s')\n", c, seqno, dh, attr, name);

    // convert the path to lowercase
    for (i = 0; i < namelen; i++) name[i] = tolower (name[i]);

    // build the path
    i = fs_build_path (c, dh, name, path, VOL_MAX_VOL_PATH_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. complain
	ncp_send_compcode (c, i);
	return;
    }

    // now, split the path into a path and a pattern
    pattern = (char*)strrchr (path, '/');
    if (pattern == NULL) {
	// hmm... this path is garbeled. complain
	ncp_send_compcode (c, 0xff);
	return;
    }

    // turn the slash into a zero and increment it so the pattern points to the
    // pattern
    *pattern = 0; pattern++;

    // now, we need to scan for [pattern] in [path]
    dir = opendir (path);
    if (dir == NULL) {
	// there's no such directory. complain
	DPRINTF (10, LOG_NCP, "[INFO] [%u] Can't open directory '%s'\n", c, path);
	ncp_send_compcode (c, 0xff);
	return;
    }

    // calculate the search numbers
    destseq = (seqno + 1); seqno = 0xffff;
    while (1) {
        dent = readdir (dir);
        if (dent == NULL) break;

        // is this entry ok?
        if (match_fn (dent->d_name, pattern) || (!strcmp (dent->d_name, pattern))) {
	    // yes. increment the sequence number
	    seqno++;
        }

        // if we have reached our sequence number, say it's cool
        if (seqno == destseq) break;
    }

    closedir (dir);

    // build the pointers
    sf = (NCP_SEARCHFILE*)r.data;
    bzero (sf, sizeof (NCP_SEARCHFILE));

    // was the outcome positive?
    if (dent == NULL) {
	// no. complain
	ncp_send_compcode (c, 0xff);
	return;
    }

    // yes. build the output buffer
    sf->seqno[0] = (destseq >> 8); sf->seqno[1] = (destseq & 0xff);
    for (i = 0; i < 14; i++) sf->filename[i] = toupper (dent->d_name[i]);

    // build the complete file name
    snprintf (tempath, NCP_MAX_FIELD_LEN, "%s/%s", path, dent->d_name);

    // stat() this file
    i = stat (tempath, &fs);
    if (i < 0) {
	DPRINTF (8, LOG_NCP, "[WARN] [%u] NCP 64: cannot fstat '%s'\n", c, tempath);
    }

    // build the buffer for a file
    sf->attr = 0x00; sf->mode = 0xff; // TODO
    i = fs.st_size;
    sf->length[0] = (i >> 24); sf->length[1] = (i >> 16);
    sf->length[2] = (i >>  8); sf->length[3] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    sf->create_date[0] = (i >> 8); sf->create_date[1] = (i & 0xff);
    i = date2nw (fs.st_atime);
    sf->access_date[0] = (i >> 8); sf->access_date[1] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    sf->update_date[0] = (i >> 8); sf->update_date[1] = (i & 0xff);
    i = time2nw (fs.st_mtime);
    sf->update_time[0] = (i >> 8); sf->update_time[1] = (i & 0xff);

    // send the reply
    ncp_send_reply (c, &r, sizeof (NCP_SEARCHFILE), 0);
}

/*
 * ncp_send_volinfo (int c, int volno, struct stat* fs)
 *
 * This will send volume info from status [fs] and volume [volno] to [c].
 *
 */
void
ncp_send_volinfo (int c, int volno, struct statfs* st) {
    int			scale, i;
    NCP_REPLY		r;
    NCP_VOLINFO*	vi;

    // build a pointer
    vi = (NCP_VOLINFO*)r.data;
    bzero (vi, sizeof (NCP_VOLINFO));

    // build the result
    scale = 1;
    while (st->f_blocks / scale > 0xffff) scale += 2;
    i = scale;
    vi->sec_per_cluster[0] = (i >> 8); vi->sec_per_cluster[1] = (i & 0xff);
    i = st->f_blocks / scale;
    vi->total_clusters[0] = (i >> 8); vi->total_clusters[1] = (i & 0xff);
    i = st->f_bavail / scale;
    vi->avail_clusters[0] = (i >> 8); vi->avail_clusters[1] = (i & 0xff);
    i = st->f_files;
    vi->tot_dir_slots[0] = (i >> 8); vi->tot_dir_slots[1] = (i & 0xff);
    i = st->f_ffree;
    vi->avail_dir_slots[0] = (i >> 8); vi->avail_dir_slots[1] = (i & 0xff);
    bcopy (volume[volno].name, vi->vol_name, 16);

    // is this volume fixed?
    if (volume[volno].flags & VOL_FLAG_REMOVABLE) {
	// yes. turn on the appropriate flag
        vi->remove_flag[0] = 0xff; vi->remove_flag[1] = 0xff;
    }

    // send this packet
    ncp_send_reply (c, &r, sizeof (NCP_VOLINFO), 0);
}

/*
 * ncp_send_volinfobyno (int c, char* data, int size)
 *
 * This will handle NCP 18 (Get volume info with number) calls.
 *
 */
void
ncp_send_volinfobyno (int c, char* data, int size) {
    uint8 	 volno;
    char  	 path[VOL_MAX_VOL_PATH_LEN];
    struct	 statfs st;
    int		 i;

    NCP_REQUIRE_LENGTH (1);

    volno = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 18: Get volume info with number (volno=%u)\n", c, volno);

    // is this volume within range?
    if (volno >= VOL_MAX_VOLS) {
	// no. complain
	DPRINTF (10, LOG_NCP, "[INFO] Out of range\n");
	ncp_send_compcode (c, 0xff);
	return;
    }

    // is this volume in use?
    if ((volume[volno].flags & VOL_FLAG_USED) == 0) {
	// no. complain
	DPRINTF (10, LOG_NCP, "[INFO] Unused volume\n");
	ncp_send_compcode (c, 0xff);
	return;
    }

    // get the filesystem statistics
    if (statfs (volume[volno].path, &st) < 0) {
	// this failed. complain
	DPRINTF (5, LOG_NCP, "[WARN] [%u] NCP 22 21: I/O error\n", c);
	ncp_send_compcode (c, 0xff);
	return;
    }

    // send the reply
    ncp_send_volinfo (c, volno, &st);
}

/*
 * ncp_handle_fcreate (int c, char* data, int size)
 *
 * This will handle NCP 67 (create file) requests.
 *
 */
void
ncp_handle_fcreate (int c, char* data, int size) {
    uint8	    dh, attr, rights, path_len;
    char  	    path[NCP_MAX_FIELD_LEN];
    int		    i, handle;
    struct stat	    fs;
    NCP_REPLY	    r;
    NCP_OPENFILE*   of;

    NCP_REQUIRE_LENGTH_MIN (4);

    dh = GET_BE8 (data); data++;
    attr = GET_BE8 (data); data++;
    path_len = GET_BE8 (data); data++;
    bzero (path, NCP_MAX_FIELD_LEN);
    bcopy (data, path, path_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 67: Create file (dh=%u,attr=%u,path='%s')\n", c, dh, attr, path);

    // convert the path to lowercase
    for (i = 0; i < path_len; i++) path[i] = tolower (path[i]);

    // try to open the file
    i = fs_open_file (c, dh, 0x84, path, &handle);
    if (i != FS_ERROR_OK) {
	// this failed. return an error
	DPRINTF (10, LOG_NCP, "[DEBUG] [%u] fs_open_file() failed, error code 0x%x\n", c, i);
	ncp_send_compcode (c, i);
	return;
    }

    // chmod() this file correctly
    if (fchmod (conn[c]->fh[handle].fd, conf_filemode) < 0) {
	DPRINTF (6, LOG_NCP, "[WARN] [%u] Can't chmod file '%s' correctly\n", c, path);
    }

    // fstat() the file
    if (fstat (conn[c]->fh[handle].fd, &fs)) {
	// eeuw, the file already exists. complain (XXX)
	close (conn[c]->fh[handle].fd);
	conn[c]->fh[handle].fd = -1;
	ncp_send_compcode (c, FS_ERROR_IOERROR);
	return;
    }

    // ok, we got it. build the result
    of = (NCP_OPENFILE*)r.data;
    bzero (of, sizeof (NCP_OPENFILE));
    of->handle[4] = (handle >> 8); of->handle[5] = (handle & 0xff);
    for (i = 0; i < 14; i++) of->name[i] = toupper (path[i]);
    of->attr = 0x20;		// XXX
    i = fs.st_size;
    of->length[0] = (i >> 24); of->length[1] = (i >> 16);
    of->length[2] = (i >>  8); of->length[3] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    of->create_date[0] = (i >>  8); of->create_date[1] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    of->access_date[0] = (i >>  8); of->access_date[1] = (i & 0xff);
    i = date2nw (fs.st_mtime);
    of->update_date[0] = (i >>  8); of->update_date[1] = (i & 0xff);
    i = time2nw (fs.st_mtime);
    of->update_time[0] = (i >>  8); of->update_time[1] = (i & 0xff);

    // send the packet
    ncp_send_reply (c, &r, sizeof (NCP_OPENFILE), 0);
}

/*
 * ncp_handle_frename (c, data, size);
 *
 * This will handle NCP 69 (Rename File) calls.
 *
 */
void
ncp_handle_frename (int c, char* data, int size) {
    uint8	    source_dh, dest_dh, source_len, dest_len;
    char  	    source[NCP_MAX_FIELD_LEN];
    char  	    dest[NCP_MAX_FIELD_LEN];
    char  	    source_file[NCP_MAX_FIELD_LEN];
    char  	    dest_file[NCP_MAX_FIELD_LEN];
    char  	    source_tmp[NCP_MAX_FIELD_LEN];
    char  	    dest_tmp[NCP_MAX_FIELD_LEN];
    int		    i;
    struct stat	    st;
    char*	    ptr;

    NCP_REQUIRE_LENGTH_MIN (7);
    bzero (source, NCP_MAX_FIELD_LEN);
    bzero (dest, NCP_MAX_FIELD_LEN);

    source_dh = GET_BE8 (data); data += 2;
    source_len = GET_BE8 (data); data++;
    bcopy (data, source, source_len); data += source_len;
    dest_dh = GET_BE8 (data); data++;
    dest_len = GET_BE8 (data); data++;
    bcopy (data, dest, dest_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 69: Rename file (sourcedh=%u,source='%s',destdh=%u,dest='%s')\n", c, source_dh, source, dest_dh, dest);

    // lowercase the source and dest
    for (i = 0; i < source_len; i++) source[i] = tolower (source[i]);
    for (i = 0; i < dest_len; i++) dest[i] = tolower (dest[i]);

    // fix up the slashes and dots
    for (i = 0; i < source_len; i++) {
	if (source[i] == '/') source[i] = '\\';
	if ((uint8)source[i] == 0xae) source[i] = '.';
    }
    for (i = 0; i < dest_len; i++)
	if (dest[i] == '/') dest[i] = '\\';
	if ((uint8)dest[i] == 0xae) dest[i] = '.';

    // copy the files to the temp buffers
    strcpy (source_tmp, source);
    strcpy (dest_tmp, dest);

    // remove the file name from the temp thingies
    ptr = strrchr (source_tmp, '/'); if (ptr != NULL) *ptr = 0;
    ptr = strrchr (dest_tmp, '/'); if (ptr != NULL) *ptr = 0;

    DPRINTF (10, LOG_NCP, "[DEBUG] source_tmp='%s',dest_tmp='%s'\n", source_tmp, dest_tmp);

    // do we have modification rights?
    if (trust_check_rights (c, source_dh, source_tmp, TRUST_RIGHT_MODIFY) < 0) {
	// no. complain
	ncp_send_compcode (c, FS_ERROR_NORENAMEPRIV);
	return;
    }
    if (trust_check_rights (c, dest_dh, dest_tmp, TRUST_RIGHT_MODIFY) < 0) {
	// no. complain
	ncp_send_compcode (c, FS_ERROR_NORENAMEPRIV);
	return;
    }

    // build the source file
    i = fs_build_path (c, source_dh, source, source_file, NCP_MAX_FIELD_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. complain
	ncp_send_compcode (c, i);
	return;
    }

    // build the destination file
    i = fs_build_path (c, dest_dh, dest, dest_file, NCP_MAX_FIELD_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. complain
	ncp_send_compcode (c, i);
	return;
    }

    // does the source file exist?
    if (stat (source_file, &st) < 0) {
	// no. complain
	ncp_send_compcode (c, FS_ERROR_ALLNAMESEXIST);
	return;
    }

    // rename the file
    ncp_send_compcode (c, (rename (source_file, dest_file) < 0) ? FS_ERROR_IOERROR : 0);
}

/*
 * ncp_handle_fwrite (int c, char* data, int size)
 *
 * This will handle NCP 73 (Write to a file) calls.
 *
 */
void
ncp_handle_fwrite (int c, char* data, int size) {
    uint16	handle, len;
    uint32	offset;
    int		i;

    data += 5;
    handle = GET_BE16 (data); data += 2;
    offset = GET_BE32 (data); data += 4;
    len    = GET_BE16 (data); data += 2;

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 73: Write to file (handle=%u,offset=%u,len=%u)\n", c, handle, offset, len);
    
    // XXX: check handle for range

    // is this file opened?
    if (conn[c]->fh[handle].fd < 0) {
	// no. complain
	ncp_send_compcode (c, FS_ERROR_BADHANDLE);
	return;
    }

    // seek to the correct position
    if (lseek (conn[c]->fh[handle].fd, offset, SEEK_SET) < 0) {
	// this failed. complain
	ncp_send_compcode (c, FS_ERROR_IOERROR);
	return;
    }

    // is the length zero ?
    if (len == 0) {
	// yes. truncate the file
	i = (ftruncate (conn[c]->fh[handle].fd, offset) < 0) ? 0xff : 0;
    } else {
	// no. write the data
	i = (write (conn[c]->fh[handle].fd, data, len) < 0) ? 0xff : 0;
    }

    // send the result
    ncp_send_compcode (c, i);
}

/*
 * ncp_handle_ferase (int c, char* data, int size)
 *
 * This will handle NCP 68 (Erase File) calls.
 *
 */
void
ncp_handle_ferase (int c, char* data, int size) {
    uint8		dh, attr, name_len;
    char  		name[NCP_MAX_FIELD_LEN];
    char  		path[VOL_MAX_VOL_PATH_LEN];
    char  		tempath[VOL_MAX_VOL_PATH_LEN];
    char*		pattern;
    int			i;
    DIR*		dir;
    struct dirent* 	dent;

    NCP_REQUIRE_LENGTH_MIN (4);

    bzero (name, NCP_MAX_FIELD_LEN);
    dh = GET_BE8 (data); data++;
    attr = GET_BE8 (data); data++;
    name_len = GET_BE8 (data); data++;
    bcopy (data, name, name_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 68: Erase file (dh=%u,attr=%u,name='%s')\n", dh, attr, name);

    // lowercase the filename
    for (i = 0; i < name_len; i++) name[i] = tolower (name[i]);

    // build the temp path
    strcpy (tempath, name);

    // fix up the slashes
    for (i = 0; i < name_len; i++)
	if (tempath[i] == '\\') tempath[i] = '/';

    // verify the directory rights (XXX: broken!)
    DPRINTF (10, LOG_NCP, "[DEBUG] trust_check_rights(): c=%u,dh=%u,path='%s'\n", c, dh, tempath);
    if (trust_check_rights (c, dh, tempath, TRUST_RIGHT_ERASE) < 0) {
	// we don't have enough rights. complain
	ncp_send_compcode (c, FS_ERROR_NODELETEPRIV);
	return;
    }

    // build the file name
    i = fs_build_path (c, dh, name, path, VOL_MAX_VOL_PATH_LEN, 0);
    if (i != FS_ERROR_OK) {
	// this failed. complain
	ncp_send_compcode (c, i);
	return;
    }

    // now, find the last occourence of '/' in [path].
    pattern = (char*)strrchr (path, '/');
    if (pattern == NULL) {
	// hmm... this path is garbeled. complain
	ncp_send_compcode (c, 0xff);
	return;
    }

    // turn the slash into a zero and increment it so the pattern points to the
    // pattern
    *pattern = 0; pattern++;

    // now, we have the directory in [path] and the pattern in [pattern]. open
    // the directory
    dir = opendir (path);
    if (dir == NULL) {
	// this failed. complain
	ncp_send_compcode (c, FS_ERROR_INVALIDPATH);
	return;
    }

    // now, zap anything that matches
    i = 0;
    while (1) {
	dent = readdir (dir);
	if (dent == NULL) break;

	// does this match and is it NOT a directory?
	if ((match_fn (dent->d_name, pattern)) && (dent->d_type != DT_DIR)) {
	    // yes. construct the path
	    snprintf (tempath, VOL_MAX_VOL_PATH_LEN, "%s/%s", path, dent->d_name);

	    // can we get rid of the file ?
	    if (unlink (tempath) < 0) {
		// no. return an error code
		ncp_send_compcode (c, 0xff);
		return;
	    }

	    // another day... another corpse... another reason, for me to...
	    i++;
	}
    }

    // it's all up to the OS now
    ncp_send_compcode (c, (i == 0) ? 0xff : 0);
}

/*
 * ncp_handle_fsize (int c, char* data, int size)
 *
 * This will handle NCP 71 (Get current size of file) calls.
 *
 */
void
ncp_handle_fsize (int c, char* data, int size) {
    uint16	handle;
    struct stat	st;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (7);

    data += 5;
    handle = GET_BE16 (data); data += 2;

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 71: Get current size of file (handle=%u)\n", c, handle);

    // XXX: range

    // is this file opened?
    if (conn[c]->fh[handle].fd < 0) {
	// no. complain
	ncp_send_compcode (c, FS_ERROR_BADHANDLE);
	return;
    }

    // stat() the file
    if (fstat (conn[c]->fh[handle].fd, &st) < 0) {
	// this failed. complain
	DPRINTF (10, LOG_NCP, "[WARN] stat() failed\n");
	ncp_send_compcode (c, 0xff);
	return;
    }

    // grab the size and fill it in
    r.data[0] = (uint8)(st.st_size >>  24);
    r.data[1] = (uint8)(st.st_size >>  16);
    r.data[2] = (uint8)(st.st_size >>   8);
    r.data[3] = (uint8)(st.st_size & 0xff);

    ncp_send_reply (c, &r, 4, 0);
}

/*
 * ncp_handle_request (int c, NCP_REQUEST* r, char* data, int size)
 *
 * This will handle request [r] from client [c]. [size] bytes of data [data]
 * will be used.
 *
 */
void
ncp_handle_request (int c, NCP_REQUEST* r, char* data, int size) {
    int func_no;

    // get the function number
    func_no = r->req_code;

    // take care of it
    switch (func_no) {
	case 18: // get volume info with number
		 ncp_send_volinfobyno (c, data, size);
		 break;
	case 20: // get file server date and time
		 ncp_handle_timedate (c);
		 break;
	case 21: // messaging services
		 ncp_handle_r21 (c, data, size);
		 break;
	case 22: // connection services
		 ncp_handle_r22 (c, data, size);
		 break;
	case 23: // connection services
		 ncp_handle_r23 (c, data, size);
		 break;
	case 24: // end of job
		 ncp_handle_eoj (c, data, size);
		 break;
	case 25: // logout
		 ncp_handle_logout (c, data, size);
		 break;
	case 33: // negotiate buffer size
		 ncp_handle_buffersize (c, data, size);
		 break;
	case 34: // tts services
		 ncp_handle_r34 (c, data, size);
		 break;
	case 62: // file search initialize
		 ncp_handle_fs_init (c, data, size);
		 break;
	case 63: // file search continue
		 ncp_handle_fs_cont (c, data, size);
		 break;
	case 64: // search for a file
		 ncp_handle_filesearch (c, data, size);
		 break;
	case 66: // close file
		 ncp_handle_fclose (c, data, size);
		 break;
	case 67: // create file
		 ncp_handle_fcreate (c, data, size);
	 	 break;
	case 68: // erase file
		 ncp_handle_ferase (c, data, size);
	  	 break;
	case 69: // rename file
		 ncp_handle_frename (c, data, size);
		 break;
	case 71: // get current size of file
		 ncp_handle_fsize (c, data, size);
		 break;
	case 72: // read from a file
		 ncp_handle_fread (c, data, size);
		 break;
	case 73: // write to a file
		 ncp_handle_fwrite (c, data, size);
		 break;
	case 76: // open file
		 ncp_handle_fopen (c, data, size);
		 break;
	case 87: // filesystem services
		 ncp_handle_r87 (c, data, size);
		 break;
	case 97: // get big packet ncp max packet size
		 ncp_handle_maxpacket (c, data, size);
		 break;
       case 101: // packet burst request
		 DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 101: Packet Burst Request (unsupported)\n", c);
		 ncp_send_compcode (c, 0xff);
	 	 break;
	default: // what's this?
		 DPRINTF (6, LOG_NCP, "[INFO] [%u] Unsupported NCP function %u, ignored\n", c, func_no);
		 ncp_send_compcode (c, 0xff);
	   	 break;
    }
}

/*
 * ncp_handle_packet (char* packet, int size, struct sockaddr_ipx* from)
 *
 * This will handle [size] bytes of IPX packet [packet]. The source of the
 * packet should be in [from].
 *
 */
void
ncp_handle_packet (char* packet, int size, struct sockaddr_ipx* from) {
    NCP_REQUEST* r;
    char* data;
    int req_type, c;

    // is this packet too small?
    if (size < sizeof (NCP_REQUEST)) {
	// yes. probably bogus, discard it
	DPRINTF (6, LOG_NCP, "[WARN] Bogus NCP packet (too small)\n");
	exit (1);
    }

    // cast the packet into something we understand
    r = (NCP_REQUEST*)packet;
    data = (char*)(packet + sizeof (NCP_REQUEST));
    size -= sizeof (NCP_REQUEST);

    // build the request type
    req_type = (r->req_type[0] << 8) |
	       (r->req_type[1]);

    // is this a file server request?
    if ((req_type == 0x2222) || (req_type == 0x5555)) {
	// yes. get the connection number
	c = (r->reserved << 8) | (r->con_no);

	// is the connection in range ?
	if ((c < 0) || (c > conf_nofconns)) {
	    // no. complain
	    DPRINTF (1, LOG_NCP, "[WARN] Node %s submitted out of range connection number %u\n", ipx_ntoa (IPX_GET_ADDR (from)), c);
	    ncp_send_compcode_addr (from, c, r->task_no, r->seq_no, CONN_ERROR_FAILURE, 0);
	    return;
	}

	// check station address
	#ifndef LINUX
	if (bcmp (&(IPX_GET_ADDR (from)), &conn[c]->addr.sipx_addr, sizeof (struct ipx_addr))) {
	#else
	if (bcmp (&(IPX_GET_ADDR (from)), &conn[c]->addr, sizeof (struct ipx_addr))) {
	#endif
	    // it's not identical. complain
	    #ifndef LINUX
	    DPRINTF (1, LOG_NCP, "[WARN] Node %s tried to hijack connection %u (connection has address %s)\n", ipx_ntoa (IPX_GET_ADDR (from)), c, ipx_ntoa (conn[c]->addr.sipx_addr));
	    #else
	    DPRINTF (1, LOG_NCP, "[WARN] Node %s tried to hijack connection %u (connection has address %s)\n", ipx_ntoa (IPX_GET_ADDR (from)), c, ipx_ntoa (&conn[c]->addr));
	    #endif
	    ncp_send_compcode_addr (from, c, r->task_no, r->seq_no, CONN_ERROR_FAILURE, 0);
	    return;
	}

	// set the task, sequence number and last action timestamp
	conn[c]->task_no = r->task_no;
	conn[c]->seq_no = r->seq_no;
	conn[c]->last_time = time ((time_t*)NULL);
    }

    // handle the request types
    switch (req_type) {
	case 0x1111: // create service connection
		     ncp_create_conn (from);
		     break;
	case 0x2222: // file server request
		     ncp_handle_request (c, r, data, size);
		     break;
	case 0x5555: // destroy service connection
		     ncp_destroy_conn (c, from);
		     break;
	    default: // unknown request type
		     DPRINTF (6, LOG_NCP, "[WARN] Unknown NCP request type 0x%x\n", req_type);
		     break;
    }
}
