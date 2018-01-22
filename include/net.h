/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <sys/types.h>
#include <netipx/ipx.h>

#ifndef __NET_H__
#define __NET_H__

#include "misc.h"

#define NET_PORT_RCONSOLE	0x8104

#ifdef LINUX
// Linux doesn't seem to know the ipx_addr struct, so we'll define it ourselves
union ipx_host {
    u_char		c_host[6];
    u_short		s_host[3];
};

union ipx_net {
    u_char		c_net[4];
    u_short		s_net[2];
};

struct ipx_addr {
    union ipx_net	x_net;
    union ipx_host	x_host;
    u_short		x_port;
};

#define IPX_MAX_ERROR	(255)
#define IPX_THIS_NET 	(0)
#define IPX_THIS_NODE	(ipx_this_node)
#define IPX_BROADCAST	(ipx_broadcast_node)
#define IPX_AUTO_PORT	(0)
#define IPX_USER_PTYPE	(0)
#define IPX_IS_INTERNAL (1)

typedef unsigned char IPXNode[6];
typedef unsigned long int IPXNet;
typedef unsigned short int IPXPort;

extern IPXNode ipx_this_node;

#define MAX_IFC (256)

#define IPX_KRT_INTERNAL (1)
#define IPX_KRT_ROUTE    (2)

typedef int (*IPXifcScanFunc)(IPXNet,IPXNode,int,void*);
typedef int (*IPXrtScanFunc)(IPXNet,IPXNet,IPXNode,int,void*);

int ipx_kern_get_internet_addr(struct sockaddr_ipx* addr);

// linux has a weird struct sockaddr_ipx, but OK...
#define IPX_GET_ADDR
#else
// *BSD has a much better one, IMHO :)
#define IPX_GET_ADDR(x) ((x)->sipx_addr)
#endif

#ifndef FREEBSD
// Only FreeBSD seems to define these...
#define	IPXPORT_NCP	0x0451
#define	IPXPORT_SAP	0x0452
#endif

#define IPXPORT_GASP	0x045f
#define IPXPORT_LINK	0x0460
#define IPXPORT_CONSOLE	0

#ifdef FREEBSD
#define IPX_NET(x) ((x).x_net.c_net)
#define IPX_HOST(x) ((x).x_host.c_host)
#define IPX_PORT(x) ((x).x_port)

#define IPX_NETPTR(x) ((x)->x_net.c_net)
#define IPX_HOSTPTR(x) ((x)->x_host.c_host)
#define IPX_PORTPTR(x) ((x)->x_port)
#elif OPENBSD
#define IPX_NET(x) ((x).ipx_net.c_net)
#define IPX_HOST(x) ((x).ipx_host.c_host)
#define IPX_PORT(x) ((x).ipx_port)

#define IPX_NETPTR(x) ((x)->ipx_net.c_net)
#define IPX_HOSTPTR(x) ((x)->ipx_host.c_host)
#define IPX_PORTPTR(x) ((x)->ipx_port)
#endif

void net_init();
void net_broadcast_packet (char*, int, int);
void net_send_local_packet (char*, int, int);
void net_poll();
int net_create_socket (int, int, int);

extern struct ipx_addr server_addr;

#endif
