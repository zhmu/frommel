/*
 * Frommel 2.0 - main.c
 *
 * (c) 2001, 2002 Rink Springer
 * 
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "console.h"
#include "fs.h"
#include "gasp.h"
#include "link.h"
#include "misc.h"
#include "ncp.h"
#include "net.h"
#include "rconsole.h"
#include "sap.h"
#include "trustee.h"
#include "volume.h"
#include "wdog.h"
#include "web.h"

char* config_file = NULL;
int   down_flag = 0;

/*
 * parse_options (int argc, char** argv)
 *
 * This will parse all command line options passed to the program.
 *
 */
void
parse_options (int argc, char** argv) {
    char ch;
    char* ptr;

    frommel_options = 0;
    while ((ch = getopt (argc, argv, "rhl:d:")) != -1) {
	switch (ch) {
		case 'h': // help
			  fprintf (stderr, "Frommel %s - (c) 2001 Rink Springer\n\n", FROMMEL_VERSION);
			  fprintf (stderr, "Usuage: frommel [-rh] [-l filename]\n\n");
			  fprintf (stderr, "        -r			Recreate bindery\n");
			  fprintf (stderr, "        -l [filename]		Set log file to [filename]\n");
			  fprintf (stderr, "        -h			Show this help\n");
	  		  exit (1);
		case 'r': // recreate bindery
			  frommel_options |= FROMMEL_RECREATE_BINDERY;
			  break;
		case 'l': // log file
    			  logfile = fopen (optarg, "w+");
    			  if (logfile == NULL) {
			      fprintf (stderr, "[WARN] Cannot open log file '%s', logging disabled\n", optarg);
			  }
			  break;
		case 'd': // debugging level
			  debuglevel = strtol (optarg, &ptr, 10);
			  if (optarg == ptr) {
			      // this is not a number. complain
			      fprintf (stderr, "[CRIT] Debugging level '%s' invalid, it must be an number\n", optarg);
			      exit (0xfe);
			 }
			 break;
	}
    }
}

/*
 * down_event (int i)
 *
 * This will be called when the server goes really down. [i] is a dummy
 * variable required for signal(), which calls this procedure.
 *
 */
void
down_event (int i) {
    fprintf (stderr, "[INFO] FINAL Server shutdown time reached\n\n");

    // XXX: we _should_ make a neat exit...
    exit (0);
}

/*
 * poll()
 *
 * This will poll all subsystems for data and handle it as needed.
 *
 */
void
poll() {
    fd_set fdvar;
    struct sockaddr_ipx from;
    int size, i, j;
    char buf[PACKET_MAXLEN];

    // build the table of sockets we wish to be notified of
    FD_ZERO (&fdvar);
    FD_SET (fd_ncp, &fdvar); i = fd_ncp;
    FD_SET (fd_wdog, &fdvar);
    if (i < fd_wdog) i = fd_wdog;

    // do we have a SAP socket?
    if (fd_sap != -1) {
	// yes. add it to the list, too
	FD_SET (fd_sap, &fdvar);
	if (i < fd_sap) i = fd_sap;
    }

    #ifdef WITH_WEB_INTERFACE
    // do we have a WEB socket?
    if (fd_web != -1) {
	// yes. add it to the list, too
	FD_SET (fd_web, &fdvar);
	if (i < fd_web) i = fd_web;

	// add all web sessions
	for (j = 0; j < WEB_MAX_CONNS; j++)
	    // is this socket in use?
	    if (fd_web_conn[j] != -1) {
		// yes. add it
		FD_SET (fd_web_conn[j], &fdvar);
		if (i < fd_web_conn[j]) i = fd_web_conn[j];
	    }
    }
    #endif

    #ifdef RCONSOLE
        #ifndef RCONSOLE_STANDALONE
	FD_SET (fd_rcon, &fdvar);
	if (i < fd_rcon) i = fd_rcon;

	// add all remote console sockets
	for (j = 0; j < RCON_MAX_SESSIONS; j++)
	    // is this socket in use?
	    if (rcon_session[i].sockno > -1) {
		// yes. add it
		printf ("[%u]\n", rcon_session[i].sockno);
		if (i < rcon_session[j].sockno) i = rcon_session[j].sockno;
		FD_SET (rcon_session[j].sockno, &fdvar);
	    }
	#endif
    #endif

    FD_SET (fd_gasp, &fdvar);
    if (i < fd_gasp) i = fd_gasp;

/*
    FD_SET (fd_link, &fdvar);
    if (i < fd_link) i = fd_link;*/

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

    // if the SAP subsystem has data, handle it
    if (fd_sap != -1)
	if (FD_ISSET (fd_sap, &fdvar)) {
	    // read the data
	    size = net_recv_packet (fd_sap, buf, PACKET_MAXLEN, &from);

	    // if this succeeded, handle the packet
	    if (size > 0)
		sap_handle_packet (buf, size, &from);
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

    #ifdef WITH_WEB_INTERFACE
    // if the web interface has data, handle it
    if (fd_web != -1)
	if (FD_ISSET (fd_web, &fdvar)) {
	    // handle the connection
	    web_handle_conn ();
	}

    // check all web sessions
    for (j = 0; j < WEB_MAX_CONNS; j++)
	// is this socket in use?
	if (fd_web_conn[j] != -1)
	    // is this the one?
	    if (FD_ISSET (fd_web_conn[j], &fdvar)) {
		// yes. handle it
		web_handle_data (fd_web_conn[j]);

		// close the socket and reset the descriptor
		close (fd_web_conn[j]); fd_web_conn[j] = -1;
	    }
    #endif

    // if the watchdog subsystem has data, handle it
    if (FD_ISSET (fd_wdog, &fdvar)) {
	// read the data
	size = net_recv_packet (fd_wdog, buf, PACKET_MAXLEN, &from);

	// if this succeeded, handle the packet
	if (size > 0)
	    wdog_handle_packet (buf, size, &from);
    }

    // if the console subsystem has data, handle it
    if (fd_gasp != -1)
	if (FD_ISSET (fd_gasp, &fdvar)) {
	    // read the data
	    size = net_recv_packet (fd_gasp, buf, PACKET_MAXLEN, &from);

	    // if this succeeded, handle the packet
	    if (size > 0)
		gasp_handle_packet (buf, size, &from);
	}

    // if the linking system has data, handle it
    if (fd_link != -1)
	if (FD_ISSET (fd_link, &fdvar)) {
	    // read the data
	    size = net_recv_packet (fd_link, buf, PACKET_MAXLEN, &from);

	    // if this succeeded, handle the packet
	    if (size > 0)
		link_handle_packet (buf, size, &from);
	}
}

/*
 * main (int argc,char** argv)
 *
 * This is the main code
 * 
 */
int
main (int argc,char** argv) {
    int seq = 0;
    TRUST_RECORD rec;
    char name[VOL_MAX_VOL_PATH_LEN];

    #ifdef FREEBSD
    // seed the random number generator
    srandomdev();
    #endif

    // handle all options
    parse_options (argc, argv);

    // we need to initialize the volume manager, since the configuration module
    // adds them as needed.
    vol_init();

    // parse the configuration file
    parse_config (config_file, 0);

    // initialize the subsystems
    fs_init();
    net_init();
    #ifdef RCONSOLE
    rcon_init();
    #endif
    conn_init();
    sap_init();
    bind_init();
    ncp_init();
    wdog_init();
    #ifdef WITH_WEB_INTERFACE
    web_init();
    #endif
    gasp_init();
    link_init();

    // now, get rid of those evil root privileges...
    if ((setgid (conf_gid) < 0) || (setuid (conf_uid) < 0)) {
	// this failed... quit
	fprintf (stderr, "[CRIT] Unable to switch user/group privileges\n");
	exit (1);
    }

    // do we need to log to a file?
    if ((strlen (conf_logfile) > 0) && (logfile == NULL)) {
	// yes. open the log file
	if ((logfile = fopen (conf_logfile, "a")) == NULL) {
	    // this failed. complain
	    fprintf (stderr, "[WARN] Cannot open logfile '%s'\n", conf_logfile);
	}
    }

    while (1) {
	poll();
    }
    
    return 0;
}
