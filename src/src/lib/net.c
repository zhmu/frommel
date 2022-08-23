/*
 * Frommel 2.0
 *
 * (c) 2001, 2002 Rink Springer
 *
 * Network code
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/route.h>
#include <netipx/ipx.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include "config.h"
#include "gasp.h"
#ifdef RCONSOLE
#include "rconsole.h"
#endif
#include "net.h"
#include "ncp.h"
#include "sap.h"
#include "wdog.h"
#include "web.h"

struct ipx_addr server_addr;

#define ROUNDUP(a) ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP ((n)->sa_len))

/*
 * rt_xaddrs(cp, cplim, rtinfo)
 *
 * This will convert the odd adress to something useful to us.
 *
 */
static void
rt_xaddrs(caddr_t cp, caddr_t cplim, struct rt_addrinfo* rtinfo){
    struct sockaddr *sa;
    int i;

    memset(rtinfo->rti_info, 0, sizeof(rtinfo->rti_info));
    for (i = 0; (i < RTAX_MAX) && (cp < cplim); i++) {
	if ((rtinfo->rti_addrs & (1 << i)) == 0)
            continue;
        rtinfo->rti_info[i] = sa = (struct sockaddr *)cp;
        ADVANCE (cp, sa);
    }
}

/*
 * net_ipxscan (int addrcount, struct sockaddr_dl* sdl, struct if_msghdr* ifm,
 *		struct ifa_msghdr* ifam, struct ipx_addr* addr)
 *
 * This will check for a reasonable IPX interface.
 *
 */
int
net_ipxscan (int addrcount, struct sockaddr_dl* sdl, struct if_msghdr* ifm, struct ifa_msghdr* ifam, struct ipx_addr* addr) {
    struct rt_addrinfo info;
    struct sockaddr_ipx* sipx;
    int s;

    // create a socket
    if ((s = socket (PF_IPX, SOCK_DGRAM, 0)) < 0) {
	// this failed. complain
	return -1;
    }

    while (addrcount > 0) {
	info.rti_addrs = ifam->ifam_addrs;
	rt_xaddrs ((char*)(ifam + 1), ifam->ifam_msglen + (char*)ifam, &info);

	addrcount--;
	ifam = (struct ifa_msghdr *)((char *)ifam + ifam->ifam_msglen);
	if (info.rti_info[RTAX_IFA]->sa_family == AF_IPX) {
	    sipx = (struct sockaddr_ipx *)info.rti_info[RTAX_IFA];
	    if (ipx_nullnet (sipx->sipx_addr)) continue;
	    if (ipx_nullnet (*addr) || ipx_neteq (sipx->sipx_addr, *addr)) {
		*addr = sipx->sipx_addr;
		close (s);
		return 0;
	    }
	}
    }

    return -1;
}

/*
 * net_findif (char* ifname, struct ipx_addr* addr) {
 *
 * This will scan for an IPX interface called [ifname]. If [ifname] is NULL,
 * it will scan for al interfaces. If [ifname][0] == 0, it will also return the
 * name of the interface. [addr] will contain the full IPX address on success.
 * This will return 0 on success or -1 on failure.
 *
 */
int
net_findif (char* ifname, struct ipx_addr* addr) {
    int mib[6]; size_t needed;
    char  name[32];	// XXX
    char* buf;
    char* lim;
    char* next;
    struct if_msghdr* ifm, *nextifm;
    struct ifa_msghdr* ifam;
    struct sockaddr_dl *sdl;
    struct rt_addrinfo info;
    struct sockaddr_ipx *sipx;
    int addrcount, foundit, all, flags;

    all = 0;
    if (ifname != NULL) {
	strncpy (name, ifname, sizeof (name) - 1);
	if (name[0] == 0) all = 1;
    } else {
	all = 1;
    }

    mib[0] = CTL_NET; mib[1] = PF_ROUTE; mib[2] = 0; mib[3] = AF_IPX;
    mib[4] = NET_RT_IFLIST; mib[5] = 0;

    // build the query list
    if (sysctl (mib, 6, NULL, &needed, NULL, 0) < 0) {
	// this failed. die
	fprintf (stderr, "[ERROR] cannot scan for IPX interfaces (does your kernel have 'options IPX' ?)\n");
	exit (1);
    }

    // allocate memory for the info
    if ((buf = (char*)malloc (needed)) == NULL) {
	// this failed. die
	fprintf (stderr, "[ERROR] out of memory while scanning IPX interfaces\n");
	exit (1);
    }

    // now, query the info
    if (sysctl (mib, 6, buf, &needed, NULL, 0) < 0) {
	// this failed. die
	fprintf (stderr, "[ERROR] cannot scan for IPX interfaces\n");
	exit (1);
    }

    lim = buf + needed;
    next = buf; foundit = 0;
    while (next < lim) {
	ifm = (struct if_msghdr*)next;
        if (ifm->ifm_type == RTM_IFINFO) {
	    // this is a valid entry
	    sdl = (struct sockaddr_dl *)(ifm + 1);
	    flags = ifm->ifm_flags;
	} else {
	    // what's this?
	    fprintf (stderr, "[FATAL] ipx_scanif(): got bogus entry %u when parsing NET_RT_IFLIST\n", ifm->ifm_type);
	    exit (1);
	}

	next += ifm->ifm_msglen;
	ifam = NULL;
	addrcount = 0;
	while (next < lim) {
	    nextifm = (struct if_msghdr*)next;
	    if (nextifm->ifm_type != RTM_NEWADDR) break;
	    if (ifam == NULL) ifam = (struct ifa_msghdr*)nextifm;
	    addrcount++;
	    next += nextifm->ifm_msglen;
	}

	if (all) {
	    // ensure it's up
	    if ((flags & IFF_UP) == 0) continue;
	    strncpy (name, sdl->sdl_data, sdl->sdl_nlen);
	    name[sdl->sdl_nlen] = 0;
	} else {
	    // check the name
	    if (strlen (name) != sdl->sdl_nlen) continue;
	    if (strncmp (name, sdl->sdl_data, sdl->sdl_nlen) != 0) continue;
	}

	foundit = net_ipxscan (addrcount, sdl, ifm, ifam, addr);
	if (foundit == 0) {
	    if (ifname != NULL && ifname[0] == 0) {
		strncpy (ifname, sdl->sdl_data, sdl->sdl_nlen);
	  	ifname[sdl->sdl_nlen] = 0;
	    }
	    break;
	}
    }
    free (buf);

    return (foundit == 0) ? 0 : -1;
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
    if (net_findif (conf_ifname, &server_addr) < 0) {
        fprintf (stderr, "[FATAL] could not find specified network interface (does the '%s' have an IPX address?), exiting\n", conf_ifname);
        exit (1);
    }   
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
    struct ipx ipxdp;

    // first, create a socket
    s = socket (PF_IPX, stat, 0);
    if (s < 0) {
	// this failed. complain
	return -1;
    }

    // bind it
    bzero (&addr, sizeof (addr));
    addr.sipx_family = AF_IPX;
    addr.sipx_len = sizeof (addr);
    IPX_PORT (addr.sipx_addr) = htons (port);
    if (bind (s, (struct sockaddr*)&addr, sizeof (addr)) < 0) {
	// this failed. complain
	return -1;
    }

    // set the default headers
    ipxdp.ipx_pt = type;
    bcopy (&server_addr, &ipxdp.ipx_sna, sizeof (struct ipx_addr));
    if (setsockopt (s, 0, SO_DEFAULT_HEADERS, &ipxdp, sizeof (ipxdp)) < 0) {
	// this failed. complain
	return -1;
    }

    // enable broadcasts
    i = 1;
    if (setsockopt (s, SOL_SOCKET, SO_BROADCAST, &i, sizeof (i)) < 0) {
	// this failed. complain
	return -1;
    }

    // we need to accept all packets, also to special nodes
    i = 1;
    if (setsockopt (s, 0, SO_ALL_PACKETS, &i, sizeof (i)) < 0) {
	// this failed. complain
	return -1;
    }

    // this worked. return the socket descriptor
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
    int s, i;
    struct sockaddr_ipx sap_addr;
    struct ipx_addr addr;
 
    // first, create a socket
    s = net_create_socket (SOCK_DGRAM, 0, 0);
    if (s < 0) {
	// this failed. complain
	fprintf (stderr, "[WARN] net_broadcast_packet(): can't create IPX socket\n");
	return;
    }

    // set the address
    bzero (&addr, sizeof (struct ipx_addr));

    // figure out the network interface to use
    if (net_findif (conf_ifname, &addr) < 0) {
	// this didn't work. complain
	fprintf (stderr, "[WARN] net_broadcast_packet(): can't find an appropriate IPX interface '%s'\n", conf_ifname);
	return;
    }

    // to everyone, please
    memset (&IPX_HOST (addr), 0xff, 6);
    bcopy (&IPX_NET (server_addr), IPX_NET (addr), 4);
    IPX_PORT (addr) = htons (port);

    memcpy (&sap_addr.sipx_addr, &addr, sizeof (struct ipx_addr));
    sap_addr.sipx_family = AF_IPX;
    sap_addr.sipx_len = sizeof (struct sockaddr_ipx);

    // send the packet
    if (sendto (s, data, size, 0, (struct sockaddr*)&sap_addr, sizeof (struct sockaddr_ipx)) < 0) {
	// this failed. complain
	fprintf (stderr, "[WARN] net_broadcast_packet(): unable to send the IPX broadcast\n");
	return;
    }

    // get rid of the socket
    close (s);
}

/*
 * net_send_local_packet (char* data, int size, int port)
 *
 * This will send [size] bytes of [data] to the local host on port [port].
 *
 */
void
net_send_local_packet (char* data, int size, int port) {
    int s, i;
    struct sockaddr_ipx sap_addr;
    struct ipx_addr addr;
 
    // first, create a socket
    s = net_create_socket (SOCK_DGRAM, 0, 0);
    if (s < 0) {
	// this failed. complain
	fprintf (stderr, "[WARN] net_send_local_packet(): can't create IPX socket\n");
	return;
    }

    // set the address
    bzero (&addr, sizeof (struct ipx_addr));

    // figure out the network interface to use
    if (net_findif (conf_ifname, &addr) < 0) {
	// this didn't work. complain
	fprintf (stderr, "[WARN] net_send_local_packet(): can't find an appropriate IPX interface '%s'\n", conf_ifname);
	return;
    }

    // to just us, please
    bcopy (&IPX_HOST (server_addr), IPX_HOST (addr), 6);
    bcopy (&IPX_NET (server_addr), IPX_NET (addr), 4);
    IPX_PORT (addr) = htons (port);

    memcpy (&sap_addr.sipx_addr, &addr, sizeof (struct ipx_addr));
    sap_addr.sipx_family = AF_IPX;
    sap_addr.sipx_len = sizeof (struct sockaddr_ipx);

    // send the packet
    if (sendto (s, data, size, 0, (struct sockaddr*)&sap_addr, sizeof (struct sockaddr_ipx)) < 0) {
	// this failed. complain
	fprintf (stderr, "[WARN] net_send_local_packet(): unable to send the IPX broadcast\n");
	return;
    }

    // get rid of the socket
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
