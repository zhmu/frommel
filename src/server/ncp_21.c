/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * NCP subfunction 21 code
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "frommel.h"
#include "ncp.h"
#include "misc.h"

/*
 * ncp_handle_enable_bcast (int c, char* data, int size)
 *
 * This will handle NCP 21 3 (Enable broadcasts) calls.
 *
 */
void
ncp_handle_enable_bcast (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 21 3: Enable broadcasts\n", c);

    conn[c]->message_ok = 1;

    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_getmsg (int c, char* data, int size)
 *
 * This will handle NCP 21 11 (Get broadcast message) 
 *
 */
void
ncp_handle_getmsg (int c, char* data, int size) {
    uint8	len;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 21 11: Get broadcast message\n", c);

    // copy the message
    len = strlen ((char*)conn[c]->message);
    r.data[0] = len;
    bcopy (&conn[c]->message, (char*)(r.data + 1), len);

    // clear the message
    conn[c]->message[0] = 0;

    // send the message
    ncp_send_reply (c, &r, len + 1, 0);
}

#ifdef ALLOW_CONSOLE_COMMANDS
/*
 * ncp_handle_consolecmd (int c, BINDERY_OBJ* obj, char message)
 *
 * This will handle console command [message] from object [obj] at connection
 * [c]. It will return zero if the command was handeled or -1 if not. [message]
 * should point immediately after the pound sign.
 *
 */
int
ncp_handle_consolecmd (int c, BINDERY_OBJ* obj, char* message) {
    // do we need to shut the server down ?
    if (!strcasecmp (message, "SHUTDOWN")) {
	// yes. pull the plug [XXX]
	// TODO

	// inform the user
	DPRINTF (0, LOG_NCP, "[NOTICE] Server going down in %u seconds, by the request of %s[%u]\n", SERVER_DOWN_TIMEOUT, obj->name, c);
	return 0;
    }

    // no such command
    return 0;
}
#endif

/*
 * ncp_handle_console_bcast (int c, char* data, int size)
 *
 * This will handle NCP 21 9 (Broadcast to console) calls.
 *
 */
void
ncp_handle_console_bcast (int c, char* data, int size) {
    uint8		message_len;
    char		message[CONN_MAX_MSG_LEN];
    char*		ptr;
    BINDERY_OBJ		obj;

    NCP_REQUIRE_LENGTH_MIN (2);
    bzero (message, CONN_MAX_MSG_LEN);
    message_len = GET_BE8 (data); data++;
    bcopy (data, message, message_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 21 9: Broadcast to console (message='%s')\n", c, message);

    // get the object's name
    if (bindio_scan_object_by_id (conn[c]->object_id, NULL, &obj) < 0) {
	// use a dummy name
	bzero (&obj, sizeof (BINDERY_OBJ));
	strcpy ((char*)obj.name, "???");
    }

    #ifdef ALLOW_CONSOLE_COMMANDS
    // are we supervisor (equivalent)?
    if (conn[c]->object_supervisor) {
	// yes. does it contain a pound sign?
	if ((ptr = strchr (message, '#')) != NULL) {
	    // yes. handle the command
	    if (!ncp_handle_consolecmd (c, &obj, (ptr + 1))) {
		// this worked. tell the client it's ok and leave
		ncp_send_compcode (c, 0);
		return;
	    }
	}
    }
    #endif

    // print the message
    DPRINTF (0, LOG_NCP, "[INFO] Console message from %s[%u]: %s\n", obj.name, c, message);

    ncp_send_compcode (c, 0);
}

/*
 * ncp_send_bcast_old (int c, char* data, int size)
 *
 * This will handle NCP 21 0 (Send broadcast message) calls.
 *
 */
void
ncp_send_bcast_old (int c, char* data, int size) {
    uint8	nofstations, msg_len;
    char*	station;
    char	message[CONN_MAX_MSG_LEN];
    uint32	st;
    int		i;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH_MIN (3);
    bzero (message, CONN_MAX_MSG_LEN);
    nofstations = GET_BE8 (data); data++;
    station = data; data += nofstations;
    msg_len = GET_BE8 (data); data++;
    bcopy (data, message, msg_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 21 0: Send broadcast message (nofstations=%u,message='%s')\n", c, nofstations, message);

    // browse all connections
    r.data[0] = nofstations;
    for (i = 0; i < nofstations; i++) {
	// grab the number
	st = GET_BE8 (station); station++;

	// XXX: check range
	DPRINTF (10, LOG_NCP, "[DEBUG] Station %u\n", st);

	// send the message
	strcpy ((char*)conn[c]->message, message);

	// fill out the reply
        r.data[1 + i] = 0;
    }

    ncp_send_reply (c, &r, nofstations + 1, 0);
}

/*
 * ncp_handle_getmsg_old (int c, char* data, int size)
 *
 * This will handle NCP 21 1 (Get broadcast message)
 *
 */
void
ncp_handle_getmsg_old (int c, char* data, int size) {
    uint8	len;
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 21 1: Get broadcast message\n", c);

    // copy the message
    len = strlen ((char*)conn[c]->message);
    r.data[0] = len;
    bcopy (&conn[c]->message, (char*)(r.data + 1), len);

    // clear the message
    conn[c]->message[0] = 0;

    // send the message
    ncp_send_reply (c, &r, len + 1, 0);
}

/*
 * ncp_handle_disable_bcast (int c, char* data, int size)
 *
 * This will handle NCP 21 2 (Disable broadcasts) calls.
 *
 */
void
ncp_handle_disable_bcast (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 21 2: Disable broadcasts\n", c);

    conn[c]->message_ok = 0;

    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_r21 (int c, char* data, int size)
 *
 * This will handle [size] bytes of NCP 21 subfunction packet [data] for
 * connection [c].
 * 
 */
void
ncp_handle_r21 (int c, char* data, int size) {
    uint8 sub_func = *(data + 2);
    uint16 sub_size;

    // get the substructure size
    sub_size = (uint8)((*(data)) << 8)| (uint8)(*(data + 1));

    // does this size match up?
    if ((sub_size + 2) != size) {
	// no. complain
	DPRINTF (6, LOG_NCP, "[WARN] [%u] Got NCP 21 %u packet with size %u versus %u expected, adjusting\n", c, sub_func, sub_size + 2, size);
	sub_size = (size - 2);
    }

    // fix up the buffer offset
    data += 3;

    // handle the subfunction
    switch (sub_func) {
	 case 0: // send broadcast message
		 ncp_send_bcast_old (c, data, sub_size);
		 break;
	 case 1: // get message
		 ncp_handle_getmsg_old (c, data, sub_size);
		 break;
 	 case 2: // disable broadcasts
		 ncp_handle_disable_bcast (c, data, sub_size);
		 break;
 	 case 3: // enable broadcasts
		 ncp_handle_enable_bcast (c, data, sub_size);
		 break;
	 case 9: // broadcast to console
		 ncp_handle_console_bcast (c, data, sub_size);
		 break;
	case 11: // get message
		 ncp_handle_getmsg (c, data, sub_size);
		 break;
	default: // what's this?
		 DPRINTF (6, LOG_NCP, "[INFO] [%u] Unsupported NCP function 21 %u, ignored\n", c, sub_func);
		 ncp_send_compcode (c, 0xff);
	 	 break;
    }
}
