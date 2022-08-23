/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * Watchdog code
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
#include "config.h"
#include "conn.h"
#include "net.h"
#include "sap.h"
#include "wdog.h"

int	fd_wdog = -1;

/*
 * wdog_init()
 *
 * This will initialize watchdog services.
 *
 */
void
wdog_init() {
    // create a socket
    fd_wdog = net_create_socket (SOCK_DGRAM, 0, IPXPROTO_UNKWN);
    if (fd_wdog < 0) {
	// this failed. complain
	DPRINTF (0, LOG_WDOG, "[FATAL] Cannot create watchdog socket\n");
	exit (1);
    }
}

/*
 * wdog_send_watchdog (int c, char sign_char)
 *
 * This will send a watchdog to connection [c] with signature char [sign_char].
 *
 */
void
wdog_send_watchdog (int c, char sign_char) {
    WDOG_PACKET pkt;
    struct sockaddr_ipx client_addr;
    struct ipx_addr addr;

    // build the packet
    pkt.cid = c;
    pkt.sign_char = sign_char;

    // build the address
    bzero (&addr, sizeof (addr));
    bcopy (&IPX_NET (conn[c]->addr.sipx_addr), &IPX_NET (addr), sizeof (IPX_NET (addr)));
    bcopy (&IPX_HOST (conn[c]->addr.sipx_addr), &IPX_HOST (addr), sizeof (IPX_HOST (addr)));
    IPX_PORT (addr) = htons (ntohs (IPX_PORT (conn[c]->addr.sipx_addr)) + 1);

    memcpy (&client_addr.sipx_addr, &addr, sizeof (struct ipx_addr));
    client_addr.sipx_family = AF_IPX;
    client_addr.sipx_len = sizeof (struct sockaddr_ipx);
    
    // off it goeeees!
    sendto (fd_wdog, (char*)&pkt, sizeof (WDOG_PACKET), 0, (struct sockaddr*)&client_addr, sizeof (struct sockaddr_ipx));
}

/*
 * wdog_handle_packet (char* data, int size, struct sockaddr_ipx* from)
 *
 * This will handle [size] bytes of watchdog packet [data] from connection
 * [from].
 *
 */
void
wdog_handle_packet (char* data, int size, struct sockaddr_ipx* from) {
    WDOG_PACKET*	pkt;

    DPRINTF (8, LOG_WDOG, "[NOTICE] Watchdog packet received from %s\n", ipx_ntoa (from->sipx_addr));

    // is the data length correct?
    if (size != sizeof (WDOG_PACKET)) {
	// no. complain
	DPRINTF (6, LOG_WDOG, "[WARN] Watchdog packet from %s has an invalid size (%u bytes instead of required %u)\n", ipx_ntoa (from->sipx_addr), size, sizeof (WDOG_PACKET));
	return;
    }

    // cast the packet into the structure
    pkt = (WDOG_PACKET*)data;

    // is the connection number in range?
    if (pkt->cid > conf_nofconns) {
	// no. discard the packet
	DPRINTF (6, LOG_WDOG, "[WARN] Watchdog packet from %s has an invalid connection number %u\n", ipx_ntoa (from->sipx_addr), size);
	return;
    }

    // is the connection correct?
    if ((!bcmp ((char*)&IPX_NET (conn[pkt->cid]->addr.sipx_addr), (char*)&IPX_NET (from->sipx_addr), sizeof (IPX_NET (from->sipx_addr)))) && (!bcmp ((char*)&IPX_HOST (conn[pkt->cid]->addr.sipx_addr), (char*)&IPX_HOST (from->sipx_addr), sizeof (IPX_HOST (from->sipx_addr))))) {
	// yes. what is the status?
	switch (pkt->sign_char) {
	    case 'N': // connection still lives (XXX - novell doc says Y)
		      conn[pkt->cid]->last_time = time ((time_t*)NULL);
		      conn[pkt->cid]->wdog_time = (time_t)0;
		      break;
	    default: // what's this?
		     DPRINTF (6, LOG_WDOG, "[WARN] Watchdog packet from %s has an invalid connection charachter '%c'\n", ipx_ntoa (from->sipx_addr), pkt->sign_char);
	}
    }
}
