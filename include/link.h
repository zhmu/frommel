/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include "frommel.h"
#include "net.h"
#include "misc.h"

#ifndef __LINK_H__
#define __LINK_H__

#define LINK_MAX_SERVERS		8	/* maximum number of linked
						   servers */

#define LINK_CMD_HELLO			0	/* greeting */

#define LINK_STATUS_OK			0	/* ok */
#define LINK_STATUS_BADSESSION		1	/* bad session id */
#define LINK_STATUS_UNKSERVER		2	/* unknown server */

typedef struct {
    char  servername[48];			/* name of the server */
    uint8 addr[8];				/* server address */
} LINK_SERVER;

typedef struct {
    char  servername[48] PACKED;		/* name of our server */
    uint8 key[4] PACKED;			/* key */
} LINK_HELLO;

#define LINK_REQUIRE_LENGTH(x) if (p->datalen != (x)) { DPRINTF (8, LOG_LINK, "[WARN] [LINK] Packet discarded (size %u is not equal to required %u)\n", p->datalen, x); return; }

extern int fd_link;

void link_init();
void link_handle_packet (char*, int, struct sockaddr_ipx*);
#endif
