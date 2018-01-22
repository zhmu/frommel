/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * SAP code
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
#include "conn.h"
#include "net.h"
#include "sap.h"

int 	  fd_sap = -1;
SAP_ENTRY sap_service[SAP_MAX_SERVICES];

/*
 * sap_broadcast_identity(uint16 type)
 *
 * This will broadcast our SAP packet over the local network. It will
 * use service type [type].
 *
 */
void
sap_broadcast_identity(uint16 type) {
    SAP_PACKET sap;

    // send the SAP packet to everyone
    bzero (&sap, sizeof (SAP_PACKET));
    sap.operation[1] = 2;		// general service response
    sap.service_type[0] = (type >>   8); // type
    sap.service_type[1] = (type & 0xff);
    bcopy (conf_servername, sap.server_name, strlen (conf_servername));
    bcopy (&IPX_NET (server_addr), &sap.net_addr, sizeof (IPX_NET (server_addr)));
    bcopy (&IPX_HOST (server_addr), &sap.node_addr, sizeof (IPX_HOST (server_addr)));
    sap.socket_addr[0] = 0x04; sap.socket_addr[1] = 0x51;
    sap.hops[1] = 1;

    // broadcast it over the network
    net_broadcast_packet ((char*)&sap, sizeof (SAP_PACKET), IPXPORT_SAP);
}

/*
 * sap_event(int i)
 *
 * This will get called every so-and-so seconds, and will tell everyone
 * about our name. [i] is a dummy variable required for signal().
 *
 */
void
sap_event (int i) {
    // we're a file server
    sap_broadcast_identity (0x4);

    // do we have remote console abilities?
    #ifdef RCONSOLE
        // yes. tell the clients that, too
        sap_broadcast_identity (0x107);
    #endif

    // get rid of any old servers
    sap_cleanup();

    // XXX: this doesn't belong here
    conn_cleanup();
}

/*
 * sap_scan_service (char* name, uint16 type)
 *
 * This will scan for service [name] of type [type]. It will return the
 * service number of the server if it is found or -1 on failure.
 *
 */
int
sap_scan_service (char* name, uint16 type) {
    int i;

    // scan the service table
    for (i = 0; i < SAP_MAX_SERVICES; i++) {
	// is this the service we're looking for?
	if ((!strncasecmp (name, (char*)sap_service[i].server_name, 48)) &&
	    (sap_service[i].service_type == type)) {
	    // yes, we got it
	    return i;
	}
    }

    // the service wasn't found
    return -1;
}

/*
 * sap_scan_freeserviceno ()
 *
 * This will scan the service table for available slots. It will return the
 * slot number on success or -1 on failure.
 *
 */
int
sap_scan_freeserviceno () {
    int i;

    // scan the service table
    for (i = 0; i < SAP_MAX_SERVICES; i++)
	// is this service available?
	if (!sap_service[i].service_type)
	    // yes. return the number
	    return i;

    // no available slots
    return -1;
}

/*
 * sap_handle_response (char* data, int size)
 *
 * This will handle [size] bytes of SAP general or nearest response [data].
 *
 */
void
sap_handle_response (char* data, int size) {
    SAP_PACKET* sp;
    BINDERY_OBJ obj;
    BINDERY_NETADDR* na;
    char tmpdata[128];
    int i, objid;
    uint8 errcode;

    // is the SAP response as big as it should be?
    if ((!size) || ((size % sizeof (SAP_PACKET)) != 0)) {
	// no. complain
	DPRINTF (9, LOG_SAP, "[INFO] SAP: Bogus response packet received, discarded\n");
	return;
    }

    // check out the data
    sp = (SAP_PACKET*)data;

    // browse it
    do {
	// do we already have this service?
	i = sap_scan_service ((char*)sp->server_name, GET_BE16 (sp->service_type));
	if (i < 0) {
	    // no. scan for an unused slot
	    i = sap_scan_freeserviceno ();
	    if (i < 0) {
		// we're out of free service slots. complain
		DPRINTF (1, LOG_SAP, "[WARN] SAP: Out of service slots\n");
		return;
	    }

	    DPRINTF (10, LOG_SAP, "[DEBUG] Adding server type 0x%x '%s' to the table\n", GET_BE16 (sp->service_type), sp->server_name);
	} else {
	    // yes. update it
	    DPRINTF (10, LOG_SAP, "[DEBUG] Updating server type 0x%x '%s' to the table\n", GET_BE16 (sp->service_type), sp->server_name);
	}

	// update or add the service
	sap_service[i].service_type = GET_BE16 (sp->service_type);
	bcopy (sp->server_name, sap_service[i].server_name, 48);
	bcopy (sp->net_addr, sap_service[i].net_addr, 4);
	bcopy (sp->node_addr, sap_service[i].node_addr, 6);
	sap_service[i].socket_addr = GET_BE16 (sp->socket_addr);
	sap_service[i].hops = GET_BE16 (sp->hops);
 	sap_service[i].last_update = time ((time_t*)NULL);

	// is this a Novell server object?
	if (GET_BE16 (sp->service_type) == 4) {
	    // yes. is this server known in the bindery ?
	    if (bindio_scan_object (sp->server_name, GET_BE16 (sp->service_type), NULL, &obj) < 0) {
		DPRINTF (10, LOG_SAP, "[DEBUG] Server '%s' (type 0x%x) not in the bindery, adding\n", sp->server_name, GET_BE16 (sp->service_type));

		// create the server object
		if ((objid = bindio_add_object (sp->server_name, 0x4, 0, 0x40, &errcode)) < 0) {
		    // this failed. warn the user
		    DPRINTF (0, LOG_SAP, "[WARN] Could not add server '%s' to the server bindery, error 0x%x\n", sp->server_name, errcode);
		} else {
		    // create the NET_ADDRESS property
		    bzero (&tmpdata, 128);
		    if (bindio_add_prop (objid, "NET_ADDRESS", 0, 0x40, tmpdata, &errcode) < 0) {
			DPRINTF (0, LOG_SAP, "[WARN] Could not create 'NET_ADDRESS' property for server '%s', error 0x%x\n", sp->server_name, errcode);
		    }
		}
	    } else {
		// use the object id from the object
		objid = obj.id;
	    }

	     // do we have a valid object id?
	     if (objid > 0) {
		// yes. update the address
		na = (BINDERY_NETADDR*)&tmpdata;
		bcopy (sp->socket_addr, na->sock_addr, 2);
		bcopy (sp->net_addr, na->net_addr, 4);
		bcopy (sp->node_addr, na->node_addr, 6);
		errcode = bindio_write_prop (objid, "NET_ADDRESS", 1, 0, tmpdata);
		if (errcode != BINDERY_ERROR_OK) {
		    // this failed. complain
		    DPRINTF (0, LOG_SAP, "[WARN] Could not modify 'NET_ADDRESS' property for server '%s', error 0x%x\n", sp->server_name, errcode);
		}
	    }
	}

	// next entry
	size -= sizeof (SAP_PACKET); sp++;
    } while (size > 0);
}

/*
 * sap_handle_genserv (char* data, int size, struct sockaddr_ipx* addr)
 *
 * This will handle [size] bytes of the general or nearest service request
 * [data] from host [addr].
 *
 */
void
sap_handle_genserv (char* data, int size, struct sockaddr_ipx* addr) {
    int j, k;
    SAP_PACKET* sp;
    uint16 type, operation;
    char packet[PACKET_MAXLEN];

    // is the size okay?
    if (size != 4) {
	// no. discard the packet
	DPRINTF (9, LOG_SAP, "[INFO] SAP: Bogus request packet received, discarded\n");
	return;
    }

    // build a pointer to the packet and get the operation and service types
    sp = (SAP_PACKET*)data;
    operation = GET_BE16 (sp->operation);
    type = GET_BE16 (sp->service_type);

    // add all services until we have no more or have too much
    k = 0; sp = (SAP_PACKET*)packet;
    for (j = 0; j < SAP_MAX_SERVICES; j++) {
	// is the type correct?
	if (sap_service[j].service_type == type) {
	    // yes. add it
	    bzero (sp, sizeof (SAP_PACKET));
	    sp->operation[0] = ((operation + 1) >>   8);
	    sp->operation[1] = ((operation + 1) & 0xff);
	    sp->service_type[0] = (sap_service[j].service_type >>   8);
	    sp->service_type[1] = (sap_service[j].service_type & 0xff);
 	    bcopy (sap_service[j].server_name, sp->server_name, 48);
	    bcopy (sap_service[j].net_addr, sp->net_addr, 4);
	    bcopy (sap_service[j].node_addr, sp->node_addr, 6);
	    sp->socket_addr[0] = (sap_service[j].socket_addr >>   8);
	    sp->socket_addr[1] = (sap_service[j].socket_addr & 0xff);
	    sp->hops[0] = (sap_service[j].hops >>   8);
	    sp->hops[1] = (sap_service[j].hops & 0xff);

	    // increment the counter
	    k++; sp++;

	    // enough items?
	    if (((k + 1) * sizeof (SAP_PACKET)) > PACKET_MAXLEN)
		// yes. break out of the loop
		break;
	}
    }

    // send the reply
    if (sendto (fd_sap, (char*)packet, sizeof (SAP_PACKET) * k, 0, (struct sockaddr*)addr, sizeof (struct sockaddr_ipx)) < 0) {
	// this failed. complain
	perror ("?");
	DPRINTF (4, LOG_SAP, "[WARN] SAP: couldn't send reply\n");
    }
}

/*
 * sap_cleanup()
 *
 * This will get rid of any dead servers on the SAP list.
 *
 */
void
sap_cleanup() {
    int i;
    time_t now;

    // get the current time
    now = time ((time_t*)NULL);

    // scan all sap servers
    for (i = 0; i < SAP_MAX_SERVICES; i++)
	// is the service used?
	if (sap_service[i].service_type) {
	    // yes. has it been unupdated for over SAP_TIMEOUT_LENGTH ?
	    if (now > (sap_service[i].last_update + SAP_TIMEOUT_LENGTH)) {
		// yes. get rid of the dead server
		DPRINTF (8, LOG_SAP, "[NOTICE] Removing seemingly dead server '%s' from the server list\n", sap_service[i].server_name);
		sap_service[i].service_type = 0;
	    }
	}
}

/*
 * sap_handle_packet (char* data, int size, struct sockaddr_ipx* addr)
 *
 * This will handle a [size] bytes of SAP packet [data] from address [addr].
 *
 */
void
sap_handle_packet (char* data, int size, struct sockaddr_ipx* addr) {
    int		type;
    SAP_PACKET* sp = (SAP_PACKET*)data;

    // fetch the sap type
    type = (sp->operation[0] << 8) | (sp->operation[1]);

    // handle this
    switch (type) {
	case 1: // general service request
		DPRINTF (9, LOG_SAP, "[INFO] SAP: General service request received\n");
		sap_handle_genserv (data, size, addr);
		break;
	case 2: // general service response
		DPRINTF (9, LOG_SAP, "[INFO] SAP: General service response received\n");
		sap_handle_response (data, size);
		break;
	case 3: // nearest service request. are we nearest?
		if (conf_sap_nearest) {
		    // yes. handle the request
		    DPRINTF (9, LOG_SAP, "[INFO] SAP: Nearest service request received, answering\n");
		    sap_handle_genserv (data, size, addr);
		} else {
		    // no. discard it
		    DPRINTF (9, LOG_SAP, "[INFO] SAP: Nearest service request received, discarded\n");
		}
		break;
	case 4: // nearest service response
		DPRINTF (9, LOG_SAP, "[INFO] SAP: Nearest service response received\n");
		sap_handle_response (data, size);
		break;
       default: // what's this?
		DPRINTF (8, LOG_SAP, "[INFO] SAP: Unknown SAP packet type 0x%x, discarded\n", type);
		break;
    }
}

/*
 * sap_init()
 *
 * This will initialize our SAP code.
 *
 */
void
sap_init() {
    struct itimerval timer;

    // do we need to handle the SAP services ourselves?
    if (conf_sap_enabled) {
	// yes. create the SAP socket
        fd_sap = net_create_socket (SOCK_DGRAM, IPXPORT_SAP, IPXPROTO_NCP);
	if (fd_sap < 0) {
	    // this failed. complain
	    fprintf (stderr, "[FATAL] can't create SAP socket\n");
	    exit (1);
	}
    }

    // clear out the SAP service table
    bzero (&sap_service, SAP_MAX_SERVICES * sizeof (SAP_ENTRY));

    // connect signal ALARM to the SAP broadcast procedure
    signal (SIGALRM, sap_event);

    // broadcast our identity
    sap_event (0);

    // set a timer so the SIGALRM will be raised every SAP_UPDATE_INTERVAL
    // seconds
    bzero (&timer, sizeof (struct itimerval));
    timer.it_interval.tv_sec = conf_sap_interval;
    timer.it_value.tv_sec = conf_sap_interval;

    if (setitimer (ITIMER_REAL, &timer, NULL) < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] Can't create SAP update timer\n");
	exit (1);
    }
}
