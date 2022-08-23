/*
 * Frommel 2.0 - gasp_server.c
 *
 * (c) 2001, 2002 Rink Springer
 *
 * Generic Administrative Service Protocol server code
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
#include "conn.h"
#include "net.h"
#include "sap.h"

int fd_gasp = -1;
GASP_SESSION gasp_session[GASP_MAX_SESSIONS];

/*
 * gasp_init()
 *
 * This will initialize the GASP module.
 *
 */
void
gasp_init() {
    int i;

    // mark all sessions as unused
    for (i = 0; i < GASP_MAX_SESSIONS; i++)
	gasp_session[i].status = GASP_STATUS_UNUSED;

    // create the server socket
    fd_gasp = net_create_socket (SOCK_DGRAM, IPXPORT_GASP, IPXPROTO_NCP);
    if (fd_gasp < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] [GASP] Can't create socket, exiting\n");
	exit (1);
    }
}

/*
 * gasp_send_reply (struct sockaddr_ipx* addr, int sessionid, int statuscode,
 *		    char* data, int size)
 *
 * This will send [size] bytes of [data] to [addr], along with session ID
 * [sessionid] and status code [statuscode].
 *
 */
void
gasp_send_reply (struct sockaddr_ipx* addr, int sessionid, int statuscode, char* data, int size) {
    GASP_REPLY rep;

    // build the reply
    rep.type[0] = (GASP_TYPE_REPLY >>   8);
    rep.type[1] = (GASP_TYPE_REPLY & 0xff);
    rep.sessionid[0] = (sessionid >>   8);
    rep.sessionid[1] = (sessionid & 0xff); 
    rep.statuscode = (statuscode & 0xff);

    // copy data as needed
    if (size)
	bcopy (data, rep.data, size);

    // off it goes
    sendto (fd_gasp, (char*)&rep, size + 5, 0, (struct sockaddr*)addr, sizeof (struct sockaddr_ipx));
}

/*
 * gasp_send_reply_session (int sessionid, int statuscode, char* data,
 *			    int size)
 *
 * This will send [size] bytes of [data] to session [sessionid] and status code
 * [statuscode].
 *
 */
void
gasp_send_reply_session (int sessionid, int statuscode, char* data, int size) {
    // chain it through
    gasp_send_reply (&gasp_session[sessionid].addr, sessionid, statuscode, data, size);
}

/*
 * gasp_handle_hello (struct sockaddr_ipx* addr, char* data, int size)
 *
 * This will handle greetings from [addr].
 *
 */
void
gasp_handle_hello (int sessionid, struct sockaddr_ipx* addr, char* data, int size) {
    int i, j;
    GASP_HELLO h;

    // scan for a connection
    for (i = 0; i < GASP_MAX_SESSIONS; i++)
	// is this session unused?
	if (gasp_session[i].status == GASP_STATUS_UNUSED)
	    // yes. break loose
	    break;

    // all connections used?
    if (i == GASP_MAX_SESSIONS) {
	// yes. inform the client
        DPRINTF (4, LOG_GASP, "[NOTICE] [GASP] Out of sessions\n");
        gasp_send_reply (addr, sessionid, GASP_STATUS_OUTOFSESS, NULL, 0);
	return;
    }

    // allocate this connection
    gasp_session[i].status = GASP_STATUS_CONNECTED;
    bcopy (addr, &gasp_session[i].addr, sizeof (struct sockaddr_ipx));
    for (j = 0; j < 8; j++)
	gasp_session[i].key[j] = h.key[j] = (uint8)(rand() & 0xff);

    // inform the user of the new session and login key
    gasp_send_reply_session (i, GASP_STATUS_OK, (char*)&h, sizeof (GASP_HELLO));
}

/*
 * gasp_handle_auth (int sessionid, char* data, int size)
 *
 * This will handle [size] bytes of GASP AUTHentication packet [data] from
 * session id [sessionid].
 */
void
gasp_handle_auth (int sessionid, char* data, int size) {
    int i;

    // got a key?
    if (size != 8) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_BADCMD, NULL, 0);
	return;
    }

    // correct password?
    if (bind_verify_pwd (1, data, (char*)gasp_session[sessionid].key) < 0) {
	// no. inform the client
	gasp_send_reply_session (sessionid, GASP_STATUS_BADAUTH, NULL, 0);
	return;
    }

    // we're authenticated now
    gasp_session[sessionid].status = GASP_STATUS_AUTH;

    // inform the client
    gasp_send_reply_session (sessionid, GASP_STATUS_OK, NULL, 0);
}

/*
 * gasp_send_serverinfo (sessionid)
 *
 * This will send server information to session [sessionid].
 *
 */
void
gasp_send_serverinfo (int sessionid) {
    GASP_SERVERINFO si;

    // fill out the packet
    bcopy (conf_servername, &si.name, 48);
    si.version = conf_nwversion;
    si.nofconns = conf_nofconns;
    si.nofsessions = GASP_MAX_SESSIONS;

    // send the packet
    gasp_send_reply_session (sessionid, GASP_STATUS_OK, (char*)&si, sizeof (GASP_SERVERINFO));
}

/*
 * gasp_send_coninfo (int sessionid, char* data, int size)
 *
 * This will send connection information to session [sessionid] as needed.
 *
 */
void
gasp_send_coninfo (int sessionid, char* data, int size) {
    uint32	    conn_no;
    BINDERY_OBJ	    obj;
    GASP_CONINFO    ci;

    // do we have 4 bytes of data?
    if (size != 4) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_BADCMD, NULL, 0);
	return;
    }

    // grab the connection umber
    conn_no  = GET_BE32 (data);

    // is this within range?
    if ((conn_no < 1) || (conn_no > conf_nofconns)) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_NOCONN, NULL, 0);
	return;
    }

    // grab the object name
    if (bindio_scan_object_by_id (conn[conn_no]->object_id, NULL, &obj) < 0) {
	// this failed... zero out the object
	bzero (&obj, sizeof (BINDERY_OBJ));
    }

    // build the result
    bzero (&ci, sizeof (GASP_CONINFO));
    ci.id[0] = (conn[conn_no]->object_id >>  24);
    ci.id[1] = (conn[conn_no]->object_id >>  16);
    ci.id[2] = (conn[conn_no]->object_id >>   8);
    ci.id[3] = (conn[conn_no]->object_id & 0xff);
    ci.type[0] = (conn[conn_no]->object_type >>   8);
    ci.type[1] = (conn[conn_no]->object_type & 0xff);
    bcopy (obj.name, &ci.name, 48);
    bcopy (&IPX_NET (conn[conn_no]->addr.sipx_addr), ci.addr, 4);
    bcopy (&IPX_HOST (conn[conn_no]->addr.sipx_addr), (ci.addr + 4), 6);

    // send the packet
    gasp_send_reply_session (sessionid, GASP_STATUS_OK, (char*)&ci, sizeof (GASP_CONINFO));
}

/*
 * gasp_clear_conn (int sessionid, char* data, int size)
 *
 * This will handle the clearing of connections.
 *
 */
void
gasp_clear_conn (int sessionid, char* data, int size) {
    uint32	 conn_no;
    BINDERY_OBJ	 obj;
    GASP_CONINFO ci;

    // do we have 4 bytes of data?
    if (size != 4) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_BADCMD, NULL, 0);
	return;
    }

    // grab the connection umber
    conn_no  = GET_BE32 (data);

    // is this within range?
    if ((conn_no < 1) || (conn_no > conf_nofconns)) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_NOCONN, NULL, 0);
	return;
    }

    // kill the connection
    conn_clear (conn_no);

    // logging
    DPRINTF (0, LOG_GASP, "[NOTICE] Connection %u cleared due to GASP request\n", conn_no);

    // send the success message
    gasp_send_reply_session (sessionid, GASP_STATUS_OK, NULL, 0);
}

/*
 * gasp_send_broadcast (int sessionid, char* data, int size)
 *
 * This will send a broadcast to everyone.
 *
 */
void
gasp_send_broadcast (int sessionid, char* data, int size) {
    char msg[GASP_DATASIZE];
    int  i;

    // grab the message
    bzero (&msg, GASP_DATASIZE);
    bcopy (data, msg, size);

    // send it away
    for (i = 1; i <= conf_nofconns; i++) {
	// is this connection used?
	if (conn[i]->status != CONN_STATUS_UNUSED)
	    // yes. send the message
	    strcpy (conn[i]->message, msg);
    }

    // logging
    DPRINTF (0, LOG_GASP, "[NOTICE] Message '%s' broadcast due to GASP request\n", msg);

    // send the success message
    gasp_send_reply_session (sessionid, GASP_STATUS_OK, NULL, 0);
}

/*
 * gasp_send_sessinfo (int sessionid, char* data, int size)
 *
 * This will handle retrieving session information.
 *
 */
void
gasp_send_sessinfo (int sessionid, char* data, int size) {
    uint32 	     sess_no;
    GASP_SESSINFO    si;

    // do we have 4 bytes of data?
    if (size != 4) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_BADCMD, NULL, 0);
	return;
    }

    // grab the session number
    sess_no  = GET_BE32 (data);

    // is this within range?
    if ((sess_no < 0) || (sess_no > GASP_MAX_SESSIONS)) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_NOSESS, NULL, 0);
	return;
    }

    // build the response
    si.status = gasp_session[sess_no].status;
    bcopy (&IPX_NET (gasp_session[sess_no].addr.sipx_addr), si.addr, 4);
    bcopy (&IPX_HOST (gasp_session[sess_no].addr.sipx_addr), (si.addr + 4), 6);

    // send the packet
    gasp_send_reply_session (sessionid, GASP_STATUS_OK, (char*)&si, sizeof (GASP_SESSINFO));
}

/*
 * gasp_send_sapinfo (int sessionid, char* data, int size)
 *
 * This will handle retrieving SAP information.
 *
 */
void
gasp_send_sapinfo (int sessionid, char* data, int size) {
    uint32 	 indexno;
    GASP_SAPINFO si;

    // do we have 4 bytes of data?
    if (size != 4) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_BADCMD, NULL, 0);
	return;
    }

    // grab the session number
    indexno  = GET_BE32 (data);
 
    // is this within range?
    if ((indexno < 0) || (indexno >= SAP_MAX_SERVICES)) {
	// no. complain
        gasp_send_reply_session (sessionid, GASP_STATUS_NOSAP, NULL, 0);
	return;
    }

    // fill out the info
    si.type[0] = (sap_service[indexno].service_type >>   8);
    si.type[1] = (sap_service[indexno].service_type & 0xff);
    bcopy (sap_service[indexno].server_name, si.name, 48);
    bcopy (sap_service[indexno].net_addr, si.addr, 4);
    bcopy (sap_service[indexno].node_addr, (si.addr + 4), 6);

    // send the packet
    gasp_send_reply_session (sessionid, GASP_STATUS_OK, (char*)&si, sizeof (GASP_SAPINFO));
}

/*
 * gasp_handle_packet (char* data, int size, struct sockaddr_ipx* addr)
 *
 * This will handle [size] bytes of console server packet [data] from [addr].
 *
 */
void
gasp_handle_packet (char* data, int size, struct sockaddr_ipx* addr) {
    GASP_REQUEST* req = (GASP_REQUEST*)data;
    GASP_PACKET p;

    // is the packet long enough?
    if (size < 6) {
	// no. discard it
	DPRINTF (4, LOG_GASP, "[WARN] [GASP] Packet from size %u discared from host '%s', packet is too small\n", size, ipx_ntoa (IPX_GET_ADDR (addr)));
	return;
    }

    // split up the packet
    p.type = (req->type[0] << 8) | req->type[1];
    p.sessionid = (req->sessionid[0] << 8) | req->sessionid[1];
    p.command = (req->command[0] << 8) | req->command[1];
    p.data = (char*)&req->data;

    // pull the header from the size
    size -= 6;

    // do we have a valid session id?
    if (p.sessionid == 0xffff) {
	// no. is this command anything else than hello ?
	if (p.command != GASP_CMD_HELLO) {
	    // no. complain
	    gasp_send_reply (addr, p.sessionid, GASP_STATUS_BADSESS, NULL, 0);
	    return;
	}

	// handle the hello command
	gasp_handle_hello (p.sessionid, addr, p.data, size);
	return;
    }

    // are we authenticated?
    if (gasp_session[p.sessionid].status == GASP_STATUS_CONNECTED) {
	// no. is the command anything else than authenticate?
	if (p.command != GASP_CMD_AUTH) {
	    // yes. complain
	    gasp_send_reply (addr, p.sessionid, GASP_STATUS_NOTAUTH, NULL, 0);
	    return;
	}

	// handle the authenticate command as needed
	gasp_handle_auth (p.sessionid, p.data, size);
	return; 
     }

     // handle the commands
     switch (p.command) {
	case GASP_CMD_DISCON: // disconnect
			      gasp_session[p.sessionid].status = GASP_STATUS_UNUSED;
			      gasp_send_reply (addr, p.sessionid, GASP_STATUS_OK, NULL, 0);
			      return;
       case GASP_CMD_LENABLE: // enable logins [XXX]
			      gasp_send_reply (addr, p.sessionid, GASP_STATUS_OK, NULL, 0);
			      return;
      case GASP_CMD_LDISABLE: // disable logins [XXX]
	    		      gasp_send_reply (addr, p.sessionid, GASP_STATUS_OK, NULL, 0);
			      return;
     case GASP_CMD_BROADCAST: // broadcast console message
			      gasp_send_broadcast (p.sessionid, p.data, size);
	 		      return;
	  case GASP_CMD_DOWN: // down the server
	  		      gasp_send_reply (addr, p.sessionid, GASP_STATUS_OK, NULL, 0);

			      DPRINTF (0, LOG_GASP, "[NOTICE] Server down due to a remote console request\n");
			      exit (0);
       case GASP_CMD_GETINFO: // query server info
			      gasp_send_serverinfo (p.sessionid);
			      return;
       case GASP_CMD_GETCONN: // get connection information
			      gasp_send_coninfo (p.sessionid, p.data, size);
			      return;
     case GASP_CMD_CLEARCONN: // clear connection
			      gasp_clear_conn (p.sessionid, p.data, size);
			      return;
       case GASP_CMD_GETSESS: // get session information
			      gasp_send_sessinfo (p.sessionid, p.data, size);
			      return;
    case GASP_CMD_GETSAPINFO: // request SAP information
			      gasp_send_sapinfo (p.sessionid, p.data, size);
			      return;
     }

     // unknown command. complain
     DPRINTF (4, LOG_GASP, "[INFO] [GASP] Unknown command 0x%x, ignoring\n", p.command);
     gasp_send_reply (addr, p.sessionid, GASP_STATUS_UNKNOWNCMD, NULL, 0);
}
