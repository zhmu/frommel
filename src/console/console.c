/*
 * Frommel 2.0 - console.c
 *
 * This will take care of the command console.
 *
 * (c) 2001, 2002 Rink Springer
 * 
 */
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "console.h"
#include "defs.h"
#include "frommel.h"
#include "gasp.h"
#include "fs.h"
#include "ncp.h"
#include "net.h"
#include "misc.h"
#include "sap.h"
#include "web.h"

CON_COMMAND* con_cmdtable = NULL;
GASP_SERVERINFO con_serverinfo;
int   con_nofcmds = 0;
int   con_sessionid = 0xffff;
int   fd_con = -1;
char  con_pwd[_PASSWORD_LEN];

/*
 * con_poll()
 *
 * This will poll for data. It will return zero on success or -1 on failure.
 *
 */
int
con_poll() {
    fd_set fdvar;

    // build the table of sockets we wish to be notified of
    FD_ZERO (&fdvar);
    FD_SET (fd_con, &fdvar);

    // is any data there?
    if (select (fd_con + 1, &fdvar, (fd_set*)NULL, (fd_set*)NULL, (struct timeval*)NULL) < 0) {
	// no, return
	return -1;
    }

    return 0;
}

/*
 * con_handle_packet (char* data, int size, GASP_REPLY_PACKET* rp)
 *
 * This will handle [size] bytes of console packet [data]. It will put the
 * handeled packet into [rp]. On success, zero will be returned, otherwise
 * -1.
 *
 */
int
con_handle_packet (char* data, int size, GASP_REPLY_PACKET* rp) {
    GASP_REPLY* p;

    // is the packet large enough?
    if (size < 5)
	// no. discard it
	return -1;

    // convert all fields
    p = (GASP_REPLY*)data;
    rp->type = (p->type[0] << 8) | (p->type[1]);
    rp->sessionid = (p->sessionid[0] << 8) | (p->sessionid[1]);
    rp->statuscode = p->statuscode;
    rp->data = p->data;

    // is this packet of the correct type?
    if (rp->type != GASP_TYPE_REPLY)
	return -1;

    // does our session id match?
    if (rp->sessionid != con_sessionid)
	// no. did we used to have an no session id?
	if (con_sessionid != 0xffff) {
	    // no. discard the packet
	    return -1;
	} else {
	    // yes. use the new session id
	    con_sessionid = rp->sessionid;
	}

    // this worked. inform the user
    return 0;
}

/*
 * con_send_request (int command, char* data, int size)
 *
 * This will send [size] bytes of [data] to the local host on port [port].
 *
 */
void
con_send_request (int command, char* data, int size) {
    int s, i;
    struct sockaddr_ipx sap_addr;
    struct ipx_addr addr;
    GASP_REQUEST req;

    // build the packet
    req.type[0] = (GASP_TYPE_COMMAND >>   8);
    req.type[1] = (GASP_TYPE_COMMAND & 0xff);
    req.sessionid[0] = (con_sessionid >>   8);
    req.sessionid[1] = (con_sessionid & 0xff);
    req.command[0] = (command >>   8);
    req.command[1] = (command & 0xff);

    // append the data
    if (size)
	bcopy (data, &req.data, size);

    // to just us, please
    bcopy (&IPX_HOST (server_addr), IPX_HOST (addr), 6);
    bcopy (&IPX_NET (server_addr), IPX_NET (addr), 4);
    IPX_PORT (addr) = htons (IPXPORT_GASP);

    memcpy (&sap_addr.sipx_addr, &addr, sizeof (struct ipx_addr));
    sap_addr.sipx_family = AF_IPX;
    sap_addr.sipx_len = sizeof (struct sockaddr_ipx);

    // send the packet
    if (sendto (fd_con, &req, size + 6, 0, (struct sockaddr*)&sap_addr, sizeof (struct sockaddr_ipx)) < 0) {
	// this failed. complain
	fprintf (stderr, "[WARN] con_send_request(): unable to send the IPX packet\n");
    }
}

/*
 * con_done()
 *
 * This will close the console connection.
 *
 */
void
con_done() {
    // tell the server we're off
    con_send_request (GASP_CMD_DISCON, NULL, 0);
}

/*
 * con_get_reply (GASP_REPLY_PACKET* p)
 *
 * This will wait for a packet to arrive and handle it. On success, [p] will be
 * filled out with the packet information and zero will be returned. On failure,
 * -1 will be returned.
 *
 */
int
con_get_reply (GASP_REPLY_PACKET* p) {
    int size;
    char data[GASP_DATASIZE];

    // wait for a packet
    if (con_poll() < 0)
	// this failed. complain
	return -1;

    // read the packet
    size = read (fd_con, data, GASP_DATASIZE);
    if (size <= 0)
	// this failed. complain
	return -1;

    // handle the packet
    return con_handle_packet (data, size, p);
}

/*
 * con_resolve_error (int code)
 *
 * This will resolve error code [error] to a textual representation.
 *
 */
char*
con_resolve_error (int code) {
    switch (code) {
	case GASP_STATUS_OK: // ok
				return "no error";
   case GASP_STATUS_NOTAUTH: // not authenticated
				return "not authenticated";
   case GASP_STATUS_BADAUTH: // bad authentication info
				return "authentication failure";
 case GASP_STATUS_OUTOFSESS: // out of sessions
				return "out of sessions";
    case GASP_STATUS_NOCONN: // no such connection
				return "no such connection";
    case GASP_STATUS_BADCMD: // bad command parameters
			 	return "bad command parameters";
case GASP_STATUS_UNKNOWNCMD: // unknown command
				return "unknown command";
    }

    // what's this?
    return "unknown error... arggh!";
}

/*
 * con_execute_cmd (int cmd, char* msg)
 *
 * This will execute command [cmd] with [size bytes of data [data], and display
 * message [msg].
 *
 */
void
con_execute_cmd (int cmd, char* msg, char* data, int size) {
    GASP_REPLY_PACKET p;

    // send the message
    printf (msg); fflush (stdout);

    // signal the server daemon
    con_send_request (cmd, data, size);

    // fetch the reply
    if (con_get_reply (&p) < 0) {
	// this failed. complain
	printf (" network failure\n");
	return;
    }

    // good status code?
    if (p.statuscode == GASP_STATUS_OK) {
	// yes. inform the user
	printf (" success\n");
    } else {
	// no. complain
	printf (" failure, %s\n", con_resolve_error (p.statuscode));
    }
}

/*
 * con_cmd_exit(char* args)
 *
 * This will handle the console 'EXIT' command.
 *
 */
void
con_cmd_exit(char* args) {
    // inform the user
    printf ("Console terminated\n");

    // close the connection
    con_done();

    // bye!
    exit (0);
}

/*
 * con_cmd_down(char* args)
 *
 * This will handle the console 'DOWN' command.
 *
 */
void
con_cmd_down(char* args) {
    con_execute_cmd (GASP_CMD_DOWN, "Requesting server shutdown...", NULL, 0);

    // get outta here ourselves, too
    exit (0);
}

/*
 * con_cmd_enable_logins (char* cmd)
 *
 * This will enable logins.
 *
 */
void
con_cmd_enable_logins (char* cmd) {
    con_execute_cmd (GASP_CMD_LENABLE, "Enabling server logins...", NULL, 0);
}

/*
 * con_cmd_disable_logins (char* cmd)
 *
 * This will disable logins.
 *
 */
void
con_cmd_disable_logins (char* cmd) {
    con_execute_cmd (GASP_CMD_LDISABLE, "Disabling server logins...", NULL, 0);
}

/*
 * con_cmd_list_conns(char* cmd)
 *
 * This will list all connections.
 *
 */
void
con_cmd_list_conns(char* cmd) {
    int		      i, j;
    uint8	      tmp[4];
    uint32	      objid;
    uint16	      type;
    GASP_REPLY_PACKET p;
    GASP_CONINFO*     ci;

    // build the page
    printf ("con# type obj# address      name\n");
 
    // do all connections
    for (i = 1; i <= con_serverinfo.nofconns; i++) {
	// query information
	tmp[0] = (i >>  24); tmp[1] = (i >>  16); tmp[2] = (i >>  8);
	tmp[3] = (i & 0xff);
 	con_send_request (GASP_CMD_GETCONN, (char*)tmp, 4);

	// fetch the reply
	if (con_get_reply (&p) < 0) {
	    // this failed. complain
	    printf (" network failure\n");
	    return;
	}

	// good status code?
	if (p.statuscode == GASP_STATUS_OK) {
	    // yes. show the info
	    ci = (GASP_CONINFO*)p.data;

	    // is this an attached connection?
	    if ((unsigned short)GET_BE32 (ci->id) == (unsigned short)0xffff) {
		// yes. use 'NOT-LOGGED-IN' as the object name
		strcpy (ci->name, "NOT-LOGGED-IN");
	    }

	    // is this connection in use?
	    if (GET_BE16 (ci->type) != 0) {
		// yes. show the info
		printf ("%4u %04x %04x ", i, (unsigned short)GET_BE32 (ci->id), (unsigned short)GET_BE16 (ci->type));

		// show the address
		for (j = 4; j < 10; j++)
		    printf ("%02x", ci->addr[j]);

		printf (" %s\n", ci->name);
	   }
	}
    }
}

/*
 * con_cmd_clear_conn(char* cmd)
 *
 * This will clear a connection.
 *
 */
void
con_cmd_clear_conn(char* cmd) {
    int			 conn_no;
    uint8		 tmp[4];

    // get the connection number
    conn_no = atoi (cmd);
    if (!conn_no) {
	// this failed. complain
	printf ("You must supply a valid numeric connection number\n");
	return;
    }

    // build the packet
    tmp[0] = (conn_no >>  24); tmp[1] = (conn_no >>  16);
    tmp[2] = (conn_no >>  8);  tmp[3] = (conn_no & 0xff);

    // bye!
    con_execute_cmd (GASP_CMD_CLEARCONN, "Clearing connection...", tmp, 4);
}

/*
 * con_cmd_broadcast (char* arg)
 *
 * This will broadcast [arg] to everyone on the network.
 *
 */
void
con_cmd_broadcast (char* arg) {
    // got an argument?
    if (arg == NULL) {
	// no. complain
	printf ("You must supply a message to broadcast\n");
	return;
    }

    // send it over as-is
    con_execute_cmd (GASP_CMD_BROADCAST, "Broadcasting message...", arg, strlen (arg));
}

/*
 * con_cmd_lock(char* arg)
 *
 * This will lock the console.
 *
 */
void
con_cmd_lock(char* arg) {
    char* pwd;

    while (1) {
	// grab the password
        pwd = getpass ("Console locked. Please enter the SUPERVISOR password to unlock: ");

	// match?
	if (!strcasecmp (pwd, con_pwd))
	    // yes. unlock
	    return;
    }
}

/*
 * con_bind_command (char* cmd, char* desc, void (*func)(char*))
 *
 * This will bind command [cmd] to procedure [func], with description [desc].
 *
 */
void
con_bind_command (char* cmd, char* desc, void (*func)(char*)) {
    CON_COMMAND* cptr;
    void* ptr;

    // resize the buffer
    if ((ptr = realloc (con_cmdtable, sizeof (CON_COMMAND) * (con_nofcmds + 1))) == NULL) {
	// this failed. complain
	fprintf (stderr, "con_add_command(): out of memory while trying to add command '%s', command not operational\n", cmd);
	return;
    }

    // use the new pointer
    con_cmdtable = ptr;

    // add the command
    cptr = (CON_COMMAND*)(con_cmdtable + con_nofcmds);
    strncpy (cptr->command, cmd, CON_MAX_CMD_LEN);
    strncpy (cptr->desc, desc, CON_MAX_DESC_LEN);
    cptr->func = func;

    // an extra command got added. increment the counter
    con_nofcmds++;
}

/*
 * con_handle_command (char* cmd, char* arg)
 *
 * This will handle console command [cmd] with optional arguments [arg].
 *
 */
void
con_handle_command(char* cmd, char* arg) {
    CON_COMMAND* cptr = con_cmdtable;
    int i = 0;

    // scan the command table for [cmd]
    while ((i < con_nofcmds) && (strcasecmp (cmd, cptr->command))) {
	// next entry
	cptr++; i++;
    }

    // did we get the command?
    if (i == con_nofcmds) {
	// no. complain
	printf ("Unknown command '%s'\n", cmd);
	return;
    }

    // launch the command
    cptr->func (arg);
}

/*
 *
 * con_go()
 *
 * This will launch the console.
 *
 */
void
con_go() {
    char  text[CON_MAX_LINE_LEN + 1];
    char* cmd;
    char* arg;
    char* ptr;
    int   i;

    printf ("Console launched\n\n");
    while (1) {
	// display the prompt
	printf ("%s> ", con_serverinfo.name);
	fgets (text, CON_MAX_LINE_LEN, stdin);

	// remove any annoying newlines
	while ((ptr = strrchr (text, '\n')) != NULL) *ptr = 0;

	// do we have a space?
	cmd = text;
	if ((ptr = strchr (text, ' ')) != NULL) {
	    // yes. turn it into a null, so we can split the command and
	    // arguments
	    *ptr = 0; ptr++;
	    arg = ptr;
	} else {
	    // just a single command, no arguments
	    arg = NULL;
	}

	// end of file marker supplied?
	if (feof (stdin)) {
	    // yes. use 'EXIT' as the command
	    strcpy (cmd, "EXIT");
	}

	// blank command?
	if (strlen (cmd)) {
	    // no. handle it
	    con_handle_command (cmd, arg);
	}
    }
}

/*
 * con_handle_responses()
 *
 * Ths will take care of the server responses.
 *
 */
void
con_handle_responses() {
    fd_set fdvar;
    struct sockaddr_ipx from;
    int size, i, j;
    char buf[PACKET_MAXLEN];

    // build the table of sockets we wish to be notified of
    FD_ZERO (&fdvar);
    FD_SET (fd_con, &fdvar);

    // is any data there?
    if (select (fd_con + 1, &fdvar, (fd_set*)NULL, (fd_set*)NULL, (struct timeval*)NULL) < 0) {
	// no, return
	return;
    }

    // read the data
    size = net_recv_packet (fd_con, buf, PACKET_MAXLEN, &from);

    // if this succeeded, forward the packet
    if (size > 0)
        write (fd_con, buf, size);
}

/*
 * con_init()
 *
 * This will initialize the connection with the server. Any failure will cause
 * the program to quit.
 *
 */
void
con_init() {
    GASP_REPLY_PACKET p;
    char* pwd;
    char  key[8];
    char  buf[128];
    uint8 id[4];
    int   i;

    // request the password
    pwd = getpass ("Please enter the SUPERVISOR password: ");
    strcpy (con_pwd, pwd);

    // uppercase the password
    for (i = 0; i < strlen (con_pwd); i++)
	con_pwd[i] = toupper (con_pwd[i]);

    // inform the user
    printf ("Connecting to server..."); fflush (stdout);

    // authenticate ourselves
    con_send_request (GASP_CMD_HELLO, NULL, 0);

    // fetch the reply
    if (con_get_reply (&p) < 0) {
	// this failed. complain
	printf (" network failure\n");
	exit (1);
    }

    // build the key
    id[0] = 0; id[1] = 0; id[2] = 0; id[3] = 1;
    shuffle (id, con_pwd, strlen (con_pwd), buf);
    nw_encrypt (p.data, buf, key);

    // authenticate ourselves
    con_send_request (GASP_CMD_AUTH, key, 8);

    // fetch the reply
    if (con_get_reply (&p) < 0) {
	// this failed. complain
	printf (" network failure\n");
	con_done();
	exit (1);
    }

    // check the result
    if (p.statuscode != GASP_STATUS_OK) {
	// this failed. complain
	printf (" failure, %s\n", con_resolve_error (p.statuscode));
	con_done();
	exit (1);
    }

    // request server information
    con_send_request (GASP_CMD_GETINFO, NULL, 0);

    // fetch the reply
    if (con_get_reply (&p) < 0) {
	// this failed. complain
	printf (" network failure\n");
	con_done();
	exit (1);
    }

    // check the result
    if (p.statuscode != GASP_STATUS_OK) {
	// this failed. complain
	printf (" failure, %s\n", con_resolve_error (p.statuscode));
	con_done();
	exit (1);
    }

    // display the info
    bcopy (p.data, &con_serverinfo, sizeof (GASP_SERVERINFO));

    printf (" connected to server '%s', running NetWare %u.%u\n", con_serverinfo.name, (con_serverinfo.version / 100), (con_serverinfo.version % 100));
}

/*
 * con_cmd_sessions(char* cmd)
 *
 * This will list all sessions.
 *
 */
void
con_cmd_sessions(char* cmd) {
    int		      i, j;
    uint8	      tmp[4];
    uint32	      objid;
    uint16	      type;
    GASP_REPLY_PACKET p;
    GASP_SESSINFO*    si;
    char	      state[32];

    // build the page
    printf ("sess# stat address\n");
 
    // do all connections
    for (i = 0; i < con_serverinfo.nofsessions; i++) {
	// query information
	tmp[0] = (i >>  24); tmp[1] = (i >>  16); tmp[2] = (i >>  8);
	tmp[3] = (i & 0xff);
 	con_send_request (GASP_CMD_GETSESS, (char*)tmp, 4);

	// fetch the reply
	if (con_get_reply (&p) < 0) {
	    // this failed. complain
	    printf (" network failure\n");
	    return;
	}

	// good status code?
	if (p.statuscode == GASP_STATUS_OK) {
	    // yes. show the info
	    si = (GASP_SESSINFO*)p.data;

	    // build the state
	    switch (si->status) {
         case GASP_STATUS_CONNECTED: strcpy (state, "conn"); break;
	      case GASP_STATUS_AUTH: strcpy (state, "auth"); break;
			       default: strcpy (state, "???");
	    }

	    // is this connection in use?
	    if (si->status != GASP_STATUS_UNUSED) {
		// yes. show the info
		printf ("%5u %s ", i, state);

		// show the address
		for (j = 4; j < 10; j++)
		    printf ("%02x", si->addr[j]);

		printf ("\n");
	   }
	}
    }
}

/*
 * con_cmd_sap(char* cmd)
 *
 * This will list all SAP information.
 *
 */
void
con_cmd_sap(char* cmd) {
    int			 i, j;
    uint8		 tmp[4];
    GASP_REPLY_PACKET p;
    GASP_SAPINFO*     si;

    // build the page
    printf ("type address      name\n");

    // do all sap entries
    i = 0;
    while (1) {
	// query information
	tmp[0] = (i >>  24); tmp[1] = (i >>  16); tmp[2] = (i >>  8);
	tmp[3] = (i & 0xff);
 	con_send_request (GASP_CMD_GETSAPINFO, (char*)tmp, 4);

	// fetch the reply
	if (con_get_reply (&p) < 0) {
	    // this failed. complain
	    printf (" network failure\n");
	    return;
	}

	// good status code?
	if (p.statuscode != GASP_STATUS_OK) {
	    // no. is it GASP_STATUS_NOSAP?
	    if (p.statuscode == GASP_STATUS_OK) {
		// no. show the error
		printf (" failure, %s\n", con_resolve_error (p.statuscode));
	    }

	    // cancel the loop
	    break;

	}

	// show the info
	si = (GASP_SAPINFO*)p.data;

	// is this entry in use?
	if (GET_BE16 (si->type) != 0) {
	    // yes. display it
	    printf ("%04x ", GET_BE16 (si->type));

	    // show the address
	    for (j = 4; j < 10; j++)
		printf ("%02x", si->addr[j]);

	    // show the server name
	    printf (" %s\n", si->name);
	}

	// next entry
	i++;
    }
}

/*
 * find_server(char* name,struct* ipx_addr addr)
 *
 * This will try to look for server [name]. If it is found, it will return
 * the address in [addr] and return zero, otherwise -1.
 *
 */
int
find_server(char* name,struct ipx_addr* addr) {
    int s;
    struct ipx_addr xaddr;
    struct sockaddr_ipx from;
    struct sockaddr_ipx sap_addr;
    SAP_PACKET p;
    SAP_PACKET* sp;
    char packet[PACKET_MAXLEN];
    int  size;
    fd_set fdvar;

    // create the socket
    s = net_create_socket (SOCK_DGRAM, 0, 0);
    if (s < 0) return -1;

    // broadcast so loud hopefully the server will hear it :)
    bzero (&xaddr, sizeof (struct ipx_addr));
    memset (&IPX_HOST (xaddr), 0xff, 6);
    bcopy (&IPX_NET (server_addr), IPX_NET (xaddr), 4);
    IPX_PORT (xaddr) = htons (IPXPORT_SAP);
    memcpy (&sap_addr.sipx_addr, &xaddr, sizeof (struct ipx_addr));
    sap_addr.sipx_family = AF_IPX;
    sap_addr.sipx_len = sizeof (struct sockaddr_ipx);

    // build the packet (general request, for a server)
    bzero (&p, sizeof (SAP_PACKET));
    p.operation[1] = 1; p.service_type[1] = 4;

    // send the packet
    if (sendto (s, &p, 4, 0, (struct sockaddr*)&sap_addr, sizeof (struct sockaddr_ipx)) < 0) {
	// this failed. close the socket and complain
	close (s);
	return -1;
    }

    // wait until we get a response
    FD_ZERO (&fdvar);
    FD_SET (s, &fdvar);
    if (select (s + 1, &fdvar, (fd_set*)NULL, (fd_set*)NULL, (struct timeval*)NULL) < 0) {
	// this failed. close the socket and complain
	close (s);
	return -1;
    }

    // we got something. read the data
    size = net_recv_packet (s, packet, PACKET_MAXLEN, &from);
    if (size > 0) {
	// build a pointer
        sp = (SAP_PACKET*)packet;

	// is the size valid?
	if ((!size) || (size % sizeof (SAP_PACKET) != 0)) {
	    // no. complain
	    close (s);
	    printf ("BOGUS REPLY!\n");
	    return;
	}
	
	// walk the packet
	while (1) {
	    // match?
	    if (!strcasecmp (name, sp->server_name)) {
		// yes. build the address
		bcopy (sp->net_addr, IPX_NETPTR (addr), 4);
		bcopy (sp->node_addr, IPX_HOSTPTR (addr), 6);
		return 0;
	    }

	    // next pointer
	    sp++; size -= sizeof (SAP_PACKET);

	    // if nothing is left, leave
	    if (!size) break;
	}
    }

    // server not found
    close (s);
    return -1;
}

/*
 * parse_parms (int argc, char* argv[])
 *
 * This will take care of [argc] parameters in [argv].
 *
 */
void
parse_parms (int argc, char* argv[]) {
    char* sname;
    int i;
    struct ipx_addr ia;

    // any parameters?
    if (argc < 2)
	// no. leave
	return;

    // ok, we have an argument. blindly assume it's the server name
    sname = argv[1];

    // uppercase the name
    for (i = 0; i < strlen (sname); i++)
	sname[i] = toupper (sname[i]);

    // scan for the server
    printf ("Searching for server '%s'...", sname); fflush (stdout);
    if (find_server (sname, &ia) < 0) {
	// this failed. complain
	printf (" failure, server not found\n");
	exit (1);
    }

    printf (" found it at %s\n", ipx_ntoa (ia));

    // use this address now
    bcopy (IPX_NET (ia), IPX_NET (server_addr), 4);
    bcopy (IPX_HOST (ia), IPX_HOST (server_addr), 6);
}

/*
 * main(int argc,char* argv[])
 *
 * This is the console's main code.
 *
 */
int
main(int argc,char* argv[]) {
    // initialize the volume manager
    vol_init();

    // read the configuration file as needed
    parse_config (NULL, 0);

    // initialize the networking code
    net_init();

    // build a socket to use for all console communications
    fd_con = net_create_socket (SOCK_DGRAM, 0, IPXPROTO_RAW);
    if (fd_con < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] Unable to create socket\n");
	exit (1);
    }

    // handle the arguments
    parse_parms (argc, argv);

    // initialize the connection
    con_init();

    // bind all commands
    con_bind_command ("exit", "This will exit the console", &con_cmd_exit);
    con_bind_command ("down", "This will down the server", &con_cmd_down);
    con_bind_command ("enable", "This will enable logins", &con_cmd_enable_logins);
    con_bind_command ("disable", "This will disable logins", &con_cmd_disable_logins);
    con_bind_command ("list", "This will list all current connections", &con_cmd_list_conns);
    con_bind_command ("clear", "This will clear a connection", &con_cmd_clear_conn);
    con_bind_command ("broadcast", "This will send a broadcast message", &con_cmd_broadcast);
    con_bind_command ("lock", "This will lock the server console", &con_cmd_lock);
    con_bind_command ("sessions", "This will display session information", &con_cmd_sessions);
    con_bind_command ("sap", "This will display the current SAP list", &con_cmd_sap);

    // off we gooo
    con_go();

    // all went ok
    return 0;
}
