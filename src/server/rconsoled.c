/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netipx/ipx.h>

#define	RCON_PORT		0x8104

int	fd_rcon = -1;

/*
 * init()
 *
 * This will initialize the remote console daemon.
 *
 */
void
init() {
    struct sockaddr_ipx addr;
    struct ipx ipxdp;
    int	   i;

    // allocate the socket
    if ((fd_rcon = socket (AF_IPX, SOCK_STREAM, 0)) < 0) {
	// this failed. complain
	fprintf (stderr, "rconsoled: cannot create an IPX socket\n");
	exit (1);
    }

    // bind the socket
    bzero (&addr, sizeof (addr));
    addr.sipx_family = AF_IPX;
    addr.sipx_len = sizeof (addr);
    addr.sipx_addr.x_port = htons (RCON_PORT);
    if (bind (fd_rcon, (struct sockaddr*)&addr, sizeof (addr)) < 0) {
	// this failed. complain
	fprintf (stderr, "rconsoled: cannot bind the remote console socket\n");
	exit (1);
    }

    // tell the OS this socket uses SPX
    bzero (&ipxdp, sizeof (struct ipx));
    ipxdp.ipx_pt = IPXPROTO_SPX;
    if (setsockopt (fd_rcon, 0, SO_DEFAULT_HEADERS, &ipxdp, sizeof (ipxdp)) < 0) {
	// this failed. complain
	fprintf (stderr, "rconsoled: cannot set default headers\n");
	exit (1);
    }

    // we need to accept all packets, also to special nodes
    i = 1;
    if (setsockopt (fd_rcon, 0, SO_ALL_PACKETS, &i, sizeof (i)) < 0) {
	// this failed. complain
	fprintf (stderr, "rconsoled: cannot accept all packets on socket\n");
	exit (1);
    }

    // tell the socket to be nice and listen
    if (listen (fd_rcon, 5) < 0) {
	// this failed. complain
	fprintf (stderr, "rconsoled: cannot get socket to listen to me!\n");
	exit (1);
    }
}

/*
 * main (int argc,char** argv)
 *
 * This is the main code
 * 
 */
int
main (int argc,char** argv) {
    struct sockaddr	addr;
    socklen_t	    	i;
    fd_set		fdset;

    // initialize the socket
    init();

    // main loop, this will loop forever
    while (1) {
	// keep accepting packets
	i = sizeof (struct sockaddr);
	if (accept (fd_rcon, &addr, &i) < 0) {
	    // this failed. complain
	    perror ("accept()");	
	}

	printf ("ACCEPT-ed!\n");
    }
}
