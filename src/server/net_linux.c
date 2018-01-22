/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * Network code for Linux
 *
 * NOTE: A large portion of this code, particularely the net_ipx_xxx functions,
 * were copied from LWARED.
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/route.h>
#include <netipx/ipx.h>
#include <net/if.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include "config.h"
#ifdef RCONSOLE
#include "rconsole.h"
#endif
#include "net.h"
#include "ncp.h"

struct ipx_addr server_addr;
struct sockaddr_ipx my_addr;

IPXNode ipx_this_node = {0, 0, 0, 0, 0, 0};

/*
 * net_ipx_assign_node (IPXNode dest, IPXNode src)
 *
 * This will assign node [dest] to [src].
 *
 */
void
net_ipx_assign_node (IPXNode dest, IPXNode src) {
    memcpy (dest, src, sizeof (IPXNode));
}

/*
 * net_ipx_scan_rtable (IPXrtScanFunc f, void* data)
 *
 * This will scan the kernel routing table, according to [f] and [data].
 *
 */
int
net_ipx_scan_rtable (IPXrtScanFunc f, void* data) {
	int sock, i;
	struct ifreq itf_table[MAX_IFC];
	struct ifconf itf_info;
	struct ifreq* ifr;
	struct sockaddr_ipx* sipx;
	int bytes_left;

	// try to create a socket
	sock=socket(AF_IPX,SOCK_DGRAM,PF_IPX);
	if (sock < 0) {
	    // this failed. complain
	    perror ("net_ipx_scan_rtable(): socket()");
	    return -1;
	}

	// query the information we need
	itf_info.ifc_len=sizeof(itf_table);
	itf_info.ifc_buf=(char*)itf_table;
	if (ioctl(sock,SIOCGIFCONF,(void *)&itf_info) < 0) {
	    // this failed. complain
	    perror ("net_ipx_scan_rtable(): ioctl()");
	    return -1;
	}

	// scan the entire structure
	bytes_left = itf_info.ifc_len;
	for(ifr=itf_info.ifc_req; bytes_left>=sizeof(*ifr);ifr++,bytes_left-=sizeof(*ifr)) {
		int type=0;
		
		sipx=(struct sockaddr_ipx*)&(ifr->ifr_addr);

    DPRINTF (10, "[DEBUG] Scan address is %04x:", sipx->sipx_network);
    for (i = 0;i < 5; i++)
	DPRINTF (10, "%02x", sipx->sipx_node[i]);
    DPRINTF (10, "\n");
		
		if (sipx->sipx_special&IPX_INTERNAL) type|=IPX_KRT_INTERNAL;

		// got the entry?
		if (!f(sipx->sipx_network,sipx->sipx_network, sipx->sipx_node,type,data)) {
		    // yeah. inform the user
		    close(sock);
	    return -1;
		}
  	}

	// nothing was found
  	close(sock);
  	return 1;
}

struct scan_ifc_info {
	IPXifcScanFunc f;
	void* data;
};

/*
 * net_ipx_scan_iface (IPXNet net, IPXNet rt_net, IPXNode rt_node,
 *			    int type, void* data)
 *
 * This will scan for an interface.
 *
 */
static int
net_ipx_scan_iface (IPXNet net,IPXNet rt_net,IPXNode rt_node,int type,void* data) {
	struct scan_ifc_info* p=(struct scan_ifc_info*) data;

	if ((type & IPX_KRT_ROUTE)==0)
		return (p->f)(net,rt_node,type,p->data);
	else
		return 1;
}

/*
 * net_ipx_scan_ifaces (IPXifcScanFunc f,void* data)
 *
 * This will scan the kernel interfaces list using function [f].
 *
 */
int
net_ipx_scan_ifaces(IPXifcScanFunc f,void* data) {
	struct scan_ifc_info sii;
	sii.f=f;
	sii.data=data;
	return net_ipx_scan_rtable (net_ipx_scan_iface, (void*)&sii);
}

/*
 * net_ipx_scan_addr (IPXNet net, IPXNode node, int type, void* data)
 *
 * This will can the kernel interface list.
 *
 */
static int
net_ipx_scan_addr(IPXNet net,IPXNode node,int type,void* data)
{
	struct sockaddr_ipx* addr=(struct sockaddr_ipx*)data;
	
	addr->sipx_network=net;
	net_ipx_assign_node (addr->sipx_node, node);
	if (type & IPX_KRT_INTERNAL) return 0;
	return 1;
}

/*
 * net_ipx_get_internet_addr (struct sockaddr_ipx* addr)
 *
 * This will get our primary IPX address and kick it to [addr]. It will return
 * 0 on success or -1 on failure.
 *
 */
int
net_ipx_get_internet_addr(struct sockaddr_ipx* addr) {
	addr->sipx_family=AF_IPX;
	addr->sipx_type=IPX_USER_PTYPE;
	addr->sipx_port=0;
	addr->sipx_network=IPX_THIS_NET;
	net_ipx_assign_node (addr->sipx_node, IPX_THIS_NODE);
	return net_ipx_scan_ifaces (net_ipx_scan_addr, (void*)addr) < 0 ? -1 : 0;	
}

/*
 * net_init()
 *
 * This will initialize the networking engine. Any errors will cause the engine
 * to print a message and quit
 *
 */
void
net_init() {
    int i;

    // retrieve our own IPX address
    if (net_ipx_get_internet_addr (&my_addr) < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] Cannot retrieve our own IPX address\n");
	exit (1);
    }

    // [EVIL HACK MODE]
    my_addr.sipx_network = 0x101;
    my_addr.sipx_node[0] = 0x00;
    my_addr.sipx_node[1] = 0x50;
    my_addr.sipx_node[2] = 0xfc;
    my_addr.sipx_node[3] = 0x39;
    my_addr.sipx_node[4] = 0x8f;
    my_addr.sipx_node[5] = 0xeb;

    DPRINTF (10, "[DEBUG] Our own address is %04x:", my_addr.sipx_network);
    for (i = 0;i < 5; i++)
	DPRINTF (10, "%02x", my_addr.sipx_node[i]);
    DPRINTF (10, "\n");

    // convert the structure to the BSD style :)
    bcopy (&my_addr.sipx_node, &server_addr.x_host.c_host, IPX_NODE_LEN);
    bcopy ((char*)&my_addr.sipx_network, (char*)&server_addr.x_net.c_net, 4);
}

/*
 * net_create_socket (int stat, int port, int type)
 *
 * This will create a socket of IPX type [type] and bind it to [port]. It will
 * return the socket descriptor on success or -1 on failure. [stat] indicates
 * whether this is a DGRAM or STREAM-ing socket.
 *
 */
int
net_create_socket (int stat, int port, int type) {
    int s, i;
    struct sockaddr_ipx addr;

    // first, create a socket
    s = socket (AF_IPX, stat, PF_IPX);
    if (s < 0) {
	// this failed. complain
	return -1;
    }

    // bind it
    bzero (&my_addr, sizeof (my_addr));
    my_addr.sipx_port = port;
    my_addr.sipx_type = type;
    if (bind (s, (struct sockaddr*)&my_addr, sizeof (my_addr)) < 0) {
	// this failed. complain
	perror ("bind()");
	return -1;
    }

    // enable broadcasts
    i = 1;
    if (setsockopt (s, SOL_SOCKET, SO_BROADCAST, &i, sizeof (i)) < 0) {
	// this failed. complain
	return -1;
    }

    // return the sockt number
    return s;
}

/*
 * net_broadcast_packet (char* data, int size, int port)
 *
 * This will broadcast [size] bytes of [data] to everyone on all IPX networks
 * on port [port].
 *
 */
void
net_broadcast_packet (char* data, int size, int port) {
    struct sockaddr_ipx addr;
    int s;

    // create a socket
    //s = socket (AF_IPX, SOCK_DGRAM, PF_IPX);
    s = net_create_socket (SOCK_DGRAM, 0, 0);
    if (s < 0)
	// this failed. complain
	return;

    // zero out the address
    bzero (&addr, sizeof (struct sockaddr_ipx));
    addr.sipx_family = AF_IPX;
    addr.sipx_port = htons (port);
    memset (&addr.sipx_node, 0xff, 6);

    // send the packet
    if (sendto (s, data, size, 0, (sockaddr*)&addr, sizeof (struct sockaddr_ipx)) < 0) {
	perror ("sendto()");
    }

    close (s);
}

/*
 * net_recv_packet (int fd, char* data, int size, struct sockaddr_ipx* from)
 *
 * This will attempt to receive a packet from socket [fd] in [data]. The
 * maximum size allowed is [size]. [from] will be set to the sender of the
 * packet. This will return the number of bytes read on success or -1 on
 * failure.
 *
 */
int
net_recv_packet (int fd, char* data, int size, struct sockaddr_ipx* from) {
    int sz = sizeof (struct sockaddr_ipx);

    from->sipx_family = PF_IPX;
    return recvfrom (fd, data, size, 0, (struct sockaddr*)from, (socklen_t*)&sz);
}

/* 
 * net_poll()
 *
 * This will poll the NCP socket and tell the NCP module to handle it as needed.
 *
 */
void
net_poll() {
    fd_set fdvar;
    struct sockaddr_ipx from;
    int size, i, j;
    char buf[PACKET_MAXLEN];

    // build the table of sockets we wish to be notified of
    FD_ZERO (&fdvar);
    FD_SET (fd_ncp, &fdvar);
    i = fd_ncp;
    #ifdef RCONSOLE
        #ifndef RCONSOLE_STANDALONE
	FD_SET (fd_rcon, &fdvar); i = fd_rcon;

	// add all remote console sockets
	for (j = 0; j < RCON_MAX_SESSIONS; j++)
	    // is this socket in use?
	    if (rcon_session[i].sockno > -1) {
		// yes. add it
		printf ("[%u]\n", rcon_session[i].sockno);
		i = rcon_session[j].sockno;
		FD_SET (i, &fdvar);
	    }
	#endif
    #endif

    // is any data there?
    if (select (i + 1, &fdvar, (fd_set*)NULL, (fd_set*)NULL, (struct timeval*)NULL) < 0) {
	// no, return
	return;
    }

    // if the NCP subsystem has data, handle it
    if (FD_ISSET (fd_ncp, &fdvar)) {
	// read the data
	size = net_recv_packet (fd_ncp, buf, PACKET_MAXLEN, &from);

	// if this succeeded, handle the packet
	if (size > 0)
	    ncp_handle_packet (buf, size, &from);
    }

    #ifdef RCONSOLE
        #ifndef RCONSOLE_STANDALONE
	// if the remote console subsystem has data, handle it
	if (FD_ISSET (fd_rcon, &fdvar)) {
	    // handle the socket
	    rcon_handle_conn (&from);
	}
	#endif
    #endif
}
