/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * Network code
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "fs.h"
#include "net.h"
#include "misc.h"
#include "wdog.h"

CONNECTION* conn[65535];
int conn_loginenabled = 1;

/*
 * conn_find_free_conn()
 *
 * This will scan for a free connection number. It will return the connection
 * number on success or -1 on failure.
 *
 */
int
conn_find_free_conn() {
    int i;

    // scan them all
    for (i = 1; i <= conf_nofconns; i++) {
	// if we can use this one, return the number
	if (conn[i]->status == CONN_STATUS_UNUSED)
	    return i;
    }

    // no connections found
    return -1;
}

/*
 * conn_count_used()
 *
 * This will count the number of used connections and return the count. 
 *
 */
int
conn_count_used() {
    int i, j = 0;

    // scan them all
    for (i = 1; i <= conf_nofconns; i++) {
	// is this one used?
	if (conn[i]->status != CONN_STATUS_UNUSED)
	    // yes. increment the counter
	    j++;
    }

    // return the count
    return j;
}

/*
 * conn_scan_client_by_addr (struct ipx_addr* addr)
 *
 * This will return the connection number of client [addr], or -1 if the
 * host is not connected.
 *
 */
int
conn_scan_client_by_addr (struct ipx_addr* addr) {
    int i;

    // if [addr] is null, it's an auto-fail
    if (addr == NULL)
	return -1;

    // scan them all
    for (i = 1; i <= conf_nofconns; i++)
	// is it this one?
	if ((!bcmp ((char*)&IPX_NET (conn[i]->addr.sipx_addr), (char*)&IPX_NETPTR (addr), sizeof (IPX_NETPTR (addr))) && (!bcmp ((char*)&IPX_HOST (conn[i]->addr.sipx_addr), (char*)&IPX_HOSTPTR (addr), sizeof (IPX_HOSTPTR (addr))))))
	    // yes. return the number
	    return i;

    // the client is not connected
    return -1;
}

/*
 * conn_attach_client (struct ipx_addr* addr)
 *
 * This will connect client with IPX address [addr] to the server. It will
 * return the connection number on success or -1 on failure. [addr]'s of NULL
 * are supported.
 *
 */
int
conn_attach_client (struct sockaddr_ipx* addr) {
    int c;

    // do we have a valid address?
    if (addr)
        // yes. do we already have this address?
	c = conn_scan_client_by_addr (&addr->sipx_addr);
    else
	// no. make sure we allocate a new connection
	c = -1;

    if (c < 0) {
	// no.  get a free connection number
	c = conn_find_free_conn();

	// if no more available, leave
	if (c < 0)
	    return -1;
    }

    // clear the connection
    conn_logout (c);

    // fix up the address
    if (addr)
	bcopy (addr, &conn[c]->addr, sizeof (struct sockaddr_ipx));

    // fix up the login time
    conn[c]->login_time = time ((time_t*)NULL);
    conn[c]->last_time = time ((time_t*)NULL);
    conn[c]->wdog_time = (time_t)0;

    // return the connection number
    return c;
}

/*
 * conn_logout (int c)
 *
 * This will mark connection [c] as logged-out. It will log the current user out
 * and close all files.
 *
 */
void
conn_logout (int c) {
    int i;

    // close all files
    for (i = 0; i < CONN_FH_PER_CONN; i++) {
	// is this file opened?
	if (conn[c]->fh[i].fd != -1) {
	    // yes. close the file
	    close (conn[c]->fh[i].fd);
	    conn[c]->fh[i].fd = -1;
	}
    }

    // set the status to attached now
    conn[c]->status = CONN_STATUS_ATTACHED;

    // get rid of all directory handles
    bzero (&conn[c]->dh, sizeof (DIRHANDLE) * CONN_DH_PER_CONN);

    // get rid of all search handles
    bzero (&conn[c]->sh, sizeof (SEARCHANDLE) * CONN_SH_PER_CONN);

    // set up the first directory handle
    if (fs_con_alloc_dh (c, FS_DH_PERM, 0, "login") != 0) {
	// this failed (it must be handle #0!). complain
	DPRINTF (0, LOG_CONN, "[WARN] [%u] conn_attach_client(): first directory handle is not handle #0!\n", c);
    }

    // copy handle #0 to #1
    bcopy (&conn[c]->dh[0], &conn[c]->dh[1], sizeof (DIRHANDLE));

    // no one is logged in here now
    conn[c]->object_id = 0xffffffff; conn[c]->object_type = 0xffff;
    conn[c]->object_supervisor = 0;

    // messaging is enabled, without a current message
    conn[c]->message[0] = 0; conn[c]->message_ok = 1;
}

/*
 * conn_clear (int c)
 *
 * This will clear connection [c].
 *
 */
void
conn_clear (int c) {
    // log the connection out
    conn_logout (c);

    // and just wipe the connection
    bzero (conn[c], sizeof (CONNECTION));
}

/*
 * conn_init()
 *
 * This will initialize the connection manager.
 *
 */
void
conn_init() {
    int i;

    // allocate memory for the connections
    for (i = 0; i <= conf_nofconns; i++) {
        if ((conn[i] = (CONNECTION*)malloc (sizeof (CONNECTION))) == NULL) {
	    // this failed. complain
	   fprintf (stderr, "[FATAL] Cannot allocate memory for the connections\n");
	   exit (0xfe);
        }

       // clear the connection
       bzero (conn[i], sizeof (CONNECTION));
    }
}

/*
 * conn_get_bind_accesslevel (int c)
 *
 * This will return connection [c]'s access level to the bindery.
 *
 */
uint8
conn_get_bind_accesslevel (int c) {
    // is the user logged in?
    if (conn[c]->object_id == 0xffffffff) {
	// no. it's access level 0, then
	return 0x00;
    }

    // is this user a supervisor?
    if (conn[c]->object_supervisor) {
	// yes. it's access level 2, then
	return 0x33;
    }

    // just a logged in user has access level 1
    return 0x22;
}

/*
 * conn_is_manager (int c)
 *
 * This will return 0 is connection [c] is a manger, otherwise -1.
 *
 */
int
conn_is_manager (int c) {
    // are we a supervisor?
    if (conn[c]->object_supervisor) {
	// yes. supervisors are always managers
	return 0;
    }

    // now, it's all about whether we appear or not in the MANAGERS property of
    // the supervisor
    return (bind_is_objinset (0, 0, NULL, conn[c]->object_id, "MANAGERS", 0, NULL, 1) == BINDERY_ERROR_OK) ? 0 : -1;
}

/*
 * conn_is_operator (int c)
 *
 * This will return 0 if connection [c] is a console operator, otherwise -1.
 *
 */
int
conn_is_operator (int c) {
    // are we are a supervisor?
    if (conn[c]->object_supervisor) {
	// yes. we always have them
	return 0;
    }

    // now, it's all up to the user being listed in the OPERATORS propery of the
    // file server object.
    return (bind_is_objinset (0, 0x4, conf_servername, 0, "OPERATORS", 0, NULL, conn[c]->object_id) == BINDERY_ERROR_OK) ? 0 : -1;
}

/*
 * conn_cleanup()
 *
 * This will clean up any old connections.
 *
 */
void
conn_cleanup() {
    int i;
    time_t now;

    // get the current time
    now = time ((time_t*)NULL);

    // scan them all
    for (i = 1; i <= conf_nofconns; i++)
	// is this connection in use?
	if (conn[i]->status != CONN_STATUS_UNUSED) {
	    // no. has it been unused for over CONN_TIMEOUT_LENGTH?
	    if (now > (conn[i]->last_time + CONN_TIMEOUT_LENGTH)) {
		// yes. have we already sent a watchdog?
		if (conn[i]->wdog_time == (time_t)0) {
		    // no. send a watchdog
		    DPRINTF (8, LOG_CONN, "[NOTICE] Sending watchdog packet to connection %u\n", i);
		    conn[i]->wdog_time = time ((time_t*)NULL);
		    wdog_send_watchdog (i, '?');
		} else {
		    // yes. drop the connection
		    DPRINTF (8, LOG_CONN, "[NOTICE] No watchdog reply from connection %u, dropping connection\n", i);
		    conn_clear (i);
		}
	    }
	}
}
