/*
 * Frommel 2.0 - gasp_client.c
 *
 * (c) 2001, 2002 Rink Springer
 *
 * Generic Administrative Service Protocol client code
 * 
 */
#include <signal.h>
#include "defs.h"
#include "gasp.h"

/*
 * gasp_send_data (char* data, int size)
 *
 * This will send [size] bytes of [data] to the GASP socket.
 *
 */
void
gasp_send_data (char* data, int size) {
    // send the data
    net_send_local_packet (data, size, IPXPORT_GASP);
}

/*
 * gasp_send_request (int sessionid, int command, char* data, int size)
 *
 * This will send [size] bytes of [data] with command [command] to the console
 * server. Session ID [sessionid] will be used.
 *
 */
void
gasp_send_request (int sessionid, int command, char* data, int size) {
    GASP_REQUEST req;

    // build the packet
    req.type[0] = (GASP_TYPE_COMMAND >>   8);
    req.type[1] = (GASP_TYPE_COMMAND & 0xff);
    req.sessionid[0] = (sessionid >>   8);
    req.sessionid[1] = (sessionid & 0xff);
    req.command[0] = (command >>   8);
    req.command[1] = (command & 0xff);

    // append the data
    if (size)
	bcopy (data, &req.data, size);

    // off it goes!
    gasp_send_data ((char*)&req, size + 6);
}

