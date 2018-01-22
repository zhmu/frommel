/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netipx/ipx.h>
#include "config.h"
#include "conn.h"
#include "fs.h"
#include "ncp.h"
#include "net.h"
#include "misc.h"
#include "rconsole.h"

#ifdef RCONSOLE
#ifndef RCONSOLE_STANDALONE

int	      fd_rcon = -1;
RCON_SESSION  rcon_session[RCON_MAX_SESSIONS];

/*
 * rcon_find_free_session()
 *
 * This will scan for a free session. If it is found, the number of the session
 * will be returned, otherwise -1.
 *
 */
int
rcon_find_free_session() {
   int i;

    // scan them all
    for (i = 0; i < RCON_MAX_SESSIONS; i++)
	// is this session in use?
	if (rcon_session[i].sockno < 0)
	    // no. return this id
	    return i;

    // no available sessions
    return -1;
}

/*
 * rcon_init()
 *
 * This will initialize the remote console stuff.
 *
 */
void
rcon_init() {
    int i;

    // create the socket
    fd_rcon = net_create_socket (SOCK_STREAM, NET_PORT_RCONSOLE, IPXPROTO_SPX);
    if (fd_rcon < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] can't create remote console socket\n");
	exit (1);
    }

    // listen on this socket for connections
    if (listen (fd_rcon, 1) < 0) {
	perror ("listen()");
	exit (1);
    }

    // clear out the session table
    for (i = 0; i < RCON_MAX_SESSIONS; i++)
	rcon_session[i].sockno = -1;
}

/*
 * rcon_send_reply (int slotno, char* data, int size)
 *
 * This will send the reply to [slotno]. This will add [size] bytes of [data]
 * to the packet.
 *
 */
void
rcon_send_reply (int slotno, char* data, int size) {
    RCON_REPLY	rp;

    // zero the reply out
    bzero (&rp, sizeof (RCON_REPLY));
}

/*
 * rcon_init_session (struct sockaddr_ipx* from)
 *
 * This will create a new session originating from [from].
 *
 */
void
rcon_init_session (struct sockaddr_ipx* from) {
    int i, j;
    struct sockaddr_ipx sf;

    // scan for a free slot
    i = rcon_find_free_session();
    if (i < 0) {
	// there are none available. silent ignore the request (XXX)
	DPRINTF (2, "[RCON] Out of slots\n");
	return;
    }

    DPRINTF (9, "INIT_SESSION(): session %u allocated\n", i);

    // accept the connection
    j = sizeof (struct sockaddr_ipx);
    if ((rcon_session[i].sockno = accept (fd_rcon, (struct sockaddr*)&sf, (socklen_t*)&j)) < 0) {
	DPRINTF (9, "ACCEPT() failed\n");
	perror ("accept()");
	return;
    }

    if (send (rcon_session[i].sockno, &i, 1, 0) < 0) {
	DPRINTF (9, "SEND() failed\n");
	perror ("send()");
    }

    DPRINTF (9, "[RCON] Accepted connection from %s\n", ipx_ntoa (sf.sipx_addr));

    // fill in the socket address
    //bcopy (from, &rcon_session[i].addr, sizeof (struct sockaddr_ipx));
}

/*
 * rcon_handle_request (char* packet, int size, struct sockaddr_ipx* from)
 *
 * This will handle [size] bytes of remote console packet [packet] from
 * node [from].
 *
 */
void
rcon_handle_request (char* packet, int size, struct sockaddr_ipx* from) {
    RCON_REQUEST*	rq = (RCON_REQUEST*)packet;
    uint16		request;

    // build the request and get the data
    request = (rq->request[0] << 8) | (rq->request[1]);
    packet += 2;

    // debugging
    DPRINTF (8, "[DEBUG] RCON: Request 0x%x received\n", request);
}

/*
 * rcon_handle_conn (struct sockaddr_ipx* from)
 *
 * This will handle a packet from [from].
 *
 */
void
rcon_handle_conn (struct sockaddr_ipx* from) {
    DPRINTF (10, "[RCON] Connection request\n");

    // build a session
    rcon_init_session (from);
}
#else
/*
 * rcon_init()
 *
 * This will initialize the remote console stuff.
 *
 */
void
rcon_init() {
    // we're standalone, so just do nothing
}
#endif
#endif
