/*
 * Frommel 2.0 - link.c
 *
 * (c) 2001, 2002 Rink Springer
 *
 * Server linking code.
 * 
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netipx/ipx.h>
#include "bindery.h"
#include "config.h"
#include "defs.h"
#include "gasp.h"
#include "link.h"
#include "conn.h"
#include "net.h"
#include "sap.h"

int fd_link = -1;

/*
 * link_scan_server (char* sname)
 *
 * This will return the server index of server [sname], or -1 if it couldn't be
 * found.
 *
 */
int
link_scan_server (char* sname) {
    int i;

    // scan all servers
    for (i = 0; i < LINK_MAX_SERVERS; i++)
	// match?
	if (!strcmp (conf_link_servers[i].servername, sname))
	    // yes. we got the server
	    return i;

    // there is no such server. complain
    return -1;
}

/*
 * link_init()
 *
 * This will initialize server linking.
 *
 */
void
link_init() {
    int i, j;
    struct sockaddr_ipx link_addr;
    struct ipx_addr addr;
    GASP_REQUEST r;
    LINK_HELLO h;

    // is server linking enabled?
    if (!conf_link_enabled)
	// no. don't initialize anything
	return;

    // create the server linking socket
    fd_link = net_create_socket (SOCK_DGRAM, IPXPORT_LINK, IPXPROTO_NCP);
    if (fd_link < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] [LINK] Can't create socket, exiting\n");
	exit (1);
    }

    // status log
    DPRINTF (0, LOG_LINK, "[INFO] [LINK] Server linking initializing, %d server%s in the list\n", conf_link_nofservers, (conf_link_nofservers != 1) ? "s" : "");

    // build the server address...
    bcopy (&IPX_HOST (server_addr), IPX_HOST (addr), 6);
    bcopy (&IPX_NET (server_addr), IPX_NET (addr), 4);
    IPX_PORT (addr) = htons (IPXPORT_LINK);

    // ...in a format sendto() wants
    memcpy (&link_addr.sipx_addr, &addr, sizeof (struct ipx_addr));
    link_addr.sipx_family = AF_IPX;
    link_addr.sipx_len = sizeof (struct sockaddr_ipx);

    // build the HELLO packet
    bzero (&h, sizeof (LINK_HELLO));
    strncpy (h.servername, conf_servername, 48);
    h.key[0] = (conf_link_key >>  24);
    h.key[1] = (conf_link_key >>  16);
    h.key[2] = (conf_link_key >>   8);
    h.key[3] = (conf_link_key & 0xff);

    // build the request header
    r.type[0] = (GASP_TYPE_LINKCMD >>   8);
    r.type[1] = (GASP_TYPE_LINKCMD & 0xff);
    r.sessionid[0] = r.sessionid[1] = 0xff;
    r.command[0] = (LINK_CMD_HELLO >>   8);
    r.command[1] = (LINK_CMD_HELLO & 0xff);
    bcopy (&h, &r.data, sizeof (LINK_HELLO));

    // send requests too all servers, requesting a link
    for (i = 0; i < conf_link_nofservers; i++) {
	// build the address 
	for (j = 0; j < 6; j++)
	    link_addr.sipx_addr.x_host.c_host[j] = conf_link_servers[i].addr[j];

	// debugging
	DPRINTF (9, LOG_LINK, "[DEBUG] [LINK] Requesting connection to server '%s' at %s\n", conf_link_servers[i].servername, ipx_ntoa (link_addr.sipx_addr));

	if (sendto (fd_link, &r, sizeof (LINK_HELLO) + 6, 0, (struct sockaddr*)&link_addr, sizeof (struct sockaddr_ipx)) < 0) {
	    DPRINTF (0, LOG_LINK, "[NOTICE] [LINK] Can't send request packet to server '%s' at %s\n", conf_link_servers[i].servername, ipx_ntoa (link_addr.sipx_addr));
	}
    }
}

/*
 * link_send_response (int sessionid, int statuscode, struct sockaddr_ipx* addr,
 *		       char* data, int size)
 *
 * This will send a response packet with optional [size] bytes of [data] to
 * [addr], along with session id [sessionid] and status code [statuscode].
 *
 */
void
link_send_response (int sessionid, int statuscode, struct sockaddr_ipx* addr, char* data, int size) {
    GASP_REPLY r;

    // build the reply packet
    r.type[0] = (GASP_TYPE_LINKREPLY >>   8);
    r.type[1] = (GASP_TYPE_LINKREPLY & 0xff);
    r.sessionid[0] = (sessionid >>   8);
    r.sessionid[1] = (sessionid & 0xff);
    r.statuscode = statuscode;

    // do we have data?
    if ((data != NULL) && (size > 0))
	// yes. add it
	bcopy (data, r.data, size);

    // off you go now
    sendto (fd_link, (char*)&r, size + 5, 0, (struct sockaddr*)addr, sizeof (struct sockaddr_ipx));
}

/*
 * link_handle_hello (GASP_PACKET* p, struct sockaddr_ipx* addr)
 *
 * This will handle HELLO packet [p] from [addr].
 *
 */
void
link_handle_hello (GASP_PACKET* p, struct sockaddr_ipx* addr) {
    LINK_HELLO* h = (LINK_HELLO*)p->data;
    int i;

    LINK_REQUIRE_LENGTH (sizeof (LINK_HELLO));

    // debugging
    DPRINTF (4, LOG_LINK, "[NOTICE] [LINK] Server '%s' requesting linkup\n", h->servername);

    // do we know this server?
    if ((i = link_scan_server (h->servername)) < 0) {
	// no. complain
	DPRINTF (0, LOG_LINK, "[NOTICE] [LINK] Link to server '%s' denied, server not configured\n");
	return;
    }

    // does the node address match?
    if (bcmp (&conf_link_servers[i].addr, addr->sipx_addr.x_host.c_host, 6)) {
	DPRINTF (0, LOG_LINK, "[NOTICE] [LINK] Link to server '%s' denied, node address does not match\n");
	return;
    }

    // it worked. send the reply
    link_send_response (p->sessionid, LINK_STATUS_OK, addr, NULL, 0);
}

/*
 * link_handle_command (GASP_PACKET* p, struct sockaddr_ipx* addr)
 *
 * This will handle link packet [p] from address [addr].
 *
 */
void
link_handle_command (GASP_PACKET* p, struct sockaddr_ipx* addr) {
    switch (p->command) {
	case LINK_CMD_HELLO: // server greeting
			     DPRINTF (10, LOG_LINK, "[DEBUG] [LINK] Got an HELLO packet from %s, answering\n", ipx_ntoa (addr->sipx_addr));
			     link_handle_hello (p, addr);
			     break;
		    default: // what's this?
			     DPRINTF (8, LOG_LINK, "[NOTICE] [LINK] Unknown command 0x%x from %s received, discarded\n", p->command, ipx_ntoa (addr->sipx_addr));
    }
}

/*
 * link_handle_packet (char* data, int size, struct sockaddr_ipx* addr)
 *
 * This will handle [size] bytes of link packet [data] from [addr].
 *
 */
void
link_handle_packet (char* data, int size, struct sockaddr_ipx* addr) {
    GASP_REQUEST* r = (GASP_REQUEST*)data;
    GASP_PACKET p;

    // is the packet long enough ?
    if (size < 5) {
	// no. discard it
	DPRINTF (6, LOG_LINK, "[NOTICE] [LINK] Discarded too short packet from host %s\n", ipx_ntoa (addr->sipx_addr));
	return;
    }

    // convert the packet
    p.type = (r->type[0] << 8) | r->type[1];
    p.sessionid = (r->sessionid[0] << 8) | r->sessionid[1];
    p.command = (r->command[0] << 8) | r->command[1];
    p.datalen = (size - 6);
    p.data = r->data;

    // is this a command?
    if (p.type == GASP_TYPE_LINKCMD) {
	// yes. handle the command
	link_handle_command (&p, addr);
	return;
    }

    // is this a reply?
    if (p.type == GASP_TYPE_LINKREPLY) {
	// yes. handle the reply
	return;
    }
}
