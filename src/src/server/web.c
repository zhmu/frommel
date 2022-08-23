#include "defs.h"

#ifdef WITH_WEB_INTERFACE
/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * This code does FART (Frommel Administration and Reconfiguration Tool)
 * 
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "fs.h"
#include "net.h"
#include "misc.h"
#include "web.h"

int	fd_web = -1;
int	fd_web_conn[WEB_MAX_CONNS];
char*	web_header = NULL;
char*	web_footer = NULL;
char*	web_parms = NULL;

// values for web_show_template() now
int	web_temp_conn = -1;
int	web_temp_vol = -1;
char*	web_temp_msg = NULL;

/*
 * web_send_response (int s, char* format, ...)
 *
 * This will printf [format] along with everything else to socket [s].
 *
 */
void
web_send_response (int s, char* format, ...) {
    va_list ap;
    char* ptr;

    // build the buffer
    va_start (ap, format);
    vasprintf (&ptr, format, ap);
    va_end (ap);

    // send the string to the socket
    write (s, ptr, strlen (ptr));

    // free the memory
    free (ptr); 
}

/*
 * web_find_free_conn()
 *
 * This will return an available web connection number or -1 if none are
 * available.
 *
 */
int
web_find_free_conn() {
    int i;

    // scan the connections
    for (i = 0; i < WEB_MAX_CONNS; i++)
	if (fd_web_conn[i] < 0)
	    // we got one. return it
	    return i;

    // none are available
    return -1;
}

/*
 * web_load_template (char* filename)
 *
 * This will load template file [filename] to malloc()-ed memory and return a
 * pointer to that memory. Any failure will cause NULL to be returned.
 *
 */
char*
web_load_template (char* filename) {
    char* ptr;
    char  temp[VOL_MAX_VOL_PATH_LEN];
    int	  length;
    FILE* f;

    // build the template file name
    snprintf (temp, VOL_MAX_VOL_PATH_LEN - 1, "%s/%s", conf_web_templatedir, filename);

    // try to open the file
    if ((f = fopen (temp, "rb")) == NULL) {
	// this failed. complain
	DPRINTF (4, LOG_WEB, "[WARN] [WEB] Cannot load template file '%s'\n", temp);
	return NULL;
    }

    // grab the file length
    fseek (f, 0, SEEK_END); length = ftell (f); rewind (f);

    // allocate memory
    if ((ptr = (char*)malloc (length + 1)) == NULL) {
	// this failed. complain
	DPRINTF (4, LOG_WEB, "[WARN] [WEB] Cannot allocate memory for template file '%s' (%lu bytes were needed)\n", temp, length + 1);
	return NULL;
    }

    // read the file
    if (!fread (ptr, length, 1, f)) {
	// this failed. complain
	free (ptr);
	DPRINTF (4, LOG_WEB, "[WARN] [WEB] Cannot read template file '%s'\n", temp);
	return NULL;
    }

    // make the data nul-terminated
    ptr[length] = 0;

    // close the file
    fclose (f);

    // return the pointer
    return ptr;
}

/*
 * web_init()
 *
 * This will initialize the web interface.
 *
 */
void
web_init() {
    struct sockaddr_in sin;
    int    i;

    // we have no web connections now
    for (i = 0; i < WEB_MAX_CONNS; i++)
	fd_web_conn[i] = -1;

    // are web services enabled?
    if (!conf_web_enabled) {
	// no. just return
	DPRINTF (0, LOG_WEB, "[NOTICE] [WEB] Web services disabled\n");
	return;
    }

    // allocate the socket
    if ((fd_web = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
	// this failed. complain
	DPRINTF (0, LOG_WEB, "[CRIT] [WEB] Cannot create socket, web services disabled\n");
	return;

    }

    // bind the socket
    bzero (&sin, sizeof (sin));
    sin.sin_len = sizeof (struct sockaddr_in);
    sin.sin_family = AF_INET;
    sin.sin_port = htons (conf_web_port);
    if (bind (fd_web, (struct sockaddr*)&sin, sizeof (sin)) < 0) {
	// this failed. complain
	close (fd_web); fd_web = -1;
	DPRINTF (0, LOG_WEB, "[CRIT] [WEB] Cannot bind socket, web services disabled\n");
	return;
    }

    // tell the socket to listen
    if (listen (fd_web, 5) < 0) {
	// this failed. complain
	close (fd_web); fd_web = -1;
	DPRINTF (0, LOG_WEB, "[CRIT] [WEB] Cannot bind socket, web services disabled\n");
	return;
    }

    // we got the socket all set up! now, grab the header and footer templates
    if ((web_header = web_load_template (WEB_TEMPLATE_HEADER)) == NULL) {
	// this failed. complain
	close (fd_web); fd_web = -1;
	DPRINTF (0, LOG_WEB, "[CRIT] [WEB] Cannot read header template, web services disabled\n");
	return;
    }

    // do the footer, too
    if ((web_footer = web_load_template (WEB_TEMPLATE_FOOTER)) == NULL) {
	// this failed. complain
	free (web_header); close (fd_web); fd_web = -1;
	DPRINTF (0, LOG_WEB, "[CRIT] [WEB] Cannot read footer template, web services disabled\n");
	return;
    }
}

/* 
 * web_send_header (int s, int status, char* msg)
 *
 * This will send a valid HTTP/1.1 header to socket [s], along with status
 * [status] and message [msg].
 *
 */
void
web_send_header (int s, int status, char* msg) {
    char   temp[1024];
    char   timebuf[1024];
    time_t now;

    // build the response
    snprintf (temp, sizeof (temp) - 1, "HTTP/1.1 %d %s\r\n", status, msg);
    write (s, temp, strlen (temp));

    // grab the time and send it over
    now = time ((time_t*)NULL);
    strftime (timebuf, sizeof (timebuf) - 1, RFC1123FMT, gmtime (&now));
    snprintf (temp, sizeof (temp) - 1, "Date: %s\r\n", timebuf);
    write (s, temp, strlen (temp));

    // inform the user about our wonderful server software :)
    snprintf (temp, sizeof (temp) - 1, "Server: %s %s %s\r\n", FS_DESC_COMPANY, FS_DESC_REVISION, FS_DESC_COPYRIGHT);
    write (s, temp, strlen (temp));

    // request authentication
    snprintf (temp, sizeof (temp) - 1, "WWW-Authenticate: Basic realm=\"%s\"\r\n", WEB_REALM);
    write (s, temp, strlen (temp));

    // send the MIME type
    snprintf (temp, sizeof (temp) - 1, "Content-Type: text/html\r\n\r\n");
    write (s, temp, strlen (temp));
}

/*
 * web_send_error (int s, int errcode, char* msg, char* expl)
 *
 * This will send HTTP error code [errcode] to socket [s] along with message
 * [msg] and explanation [expl].
 *
 */
void
web_send_error (int s, int errcode, char* msg, char* expl) {
    char	temp[1024];

    // send a header
    web_send_header (s, errcode, msg);

    // send <h1>Error</h1> to the socket along with the explanation
    snprintf (temp, sizeof (temp) - 1, "<h1>Error</h1><p>%s", expl);
    write (s, temp, strlen (temp));
}

/* 
 * web_resolve_value (char* valuename)
 *
 * This will resolve the value name {[valuename]} into one of the WEB_VALUE_xxx
 * constants. If the value is unknown, WEB_VALUE_UNKNOWN will be returned.
 *
 */
int
web_resolve_value (char* valuename) {
    // try to figure out the value name
    if (!strcasecmp (valuename, "SERVER_NAME")) return WEB_VALUE_SERVER_NAME;
    if (!strcasecmp (valuename, "SERVER_VERSION")) return WEB_VALUE_SERVER_VERSION;
    if (!strcasecmp (valuename, "USED_CONNECTIONS")) return WEB_VALUE_USED_CONNS;
    if (!strcasecmp (valuename, "TOTAL_CONNECTIONS")) return WEB_VALUE_TOTAL_CONNS;
    if (!strcasecmp (valuename, "MOUNTED_VOLS")) return WEB_VALUE_MOUNTED_VOLS;
    if (!strcasecmp (valuename, "VOL_NAMES")) return WEB_VALUE_VOL_NAMES;
    if (!strcasecmp (valuename, "CONN_LIST")) return WEB_VALUE_CONN_LIST;
    if (!strcasecmp (valuename, "CONN_NO")) return WEB_VALUE_CONN_NO;
    if (!strcasecmp (valuename, "CONN_ADDR")) return WEB_VALUE_CONN_ADDR;
    if (!strcasecmp (valuename, "CONN_OBJECT")) return WEB_VALUE_CONN_OBJECT;
    if (!strcasecmp (valuename, "CONN_LOGINTIME")) return WEB_VALUE_CONN_LOGINTIME;
    if (!strcasecmp (valuename, "CONN_CLEAR")) return WEB_VALUE_CONN_CLEAR;
    if (!strcasecmp (valuename, "CONN_SEND_MSG")) return WEB_VALUE_CONN_SENDMSG;
    if (!strcasecmp (valuename, "VOL_LIST")) return WEB_VALUE_VOL_LIST;
    if (!strcasecmp (valuename, "VOL_NO")) return WEB_VALUE_VOL_NO;
    if (!strcasecmp (valuename, "VOL_NAME")) return WEB_VALUE_VOL_NAME;
    if (!strcasecmp (valuename, "VOL_PATH")) return WEB_VALUE_VOL_PATH;
    if (!strcasecmp (valuename, "VOL_DISMOUNT")) return WEB_VALUE_VOL_DISMOUNT;

    // what's this?
    return WEB_VALUE_UNKNOWN;
}

/*
 * web_do_conn_list (int s)
 *
 * This will set up a connection list for socket [s].
 *
 */
void
web_do_conn_list (int s) {
    char* templ;
    char* templ_org;

    // get the template
    if ((templ_org = web_load_template (WEB_TEMPLATE_CONN_LIST)) == NULL) {
	// can't dump.
	return;
    }

    // duplicate the template
    templ = strdup (templ_org);

    // do all the connections
    for (web_temp_conn = 1; web_temp_conn <= conf_nofconns; web_temp_conn++) {
	// is this connection used?
	if (conn[web_temp_conn]->status != CONN_STATUS_UNUSED) {
	    // yes. copy the template over
	    strcpy (templ, templ_org);

            // show the template
            web_show_template (s, templ);
	}
    }

    // free the template
    free (templ); free (templ_org);
}

/*
 * web_do_vol_list (int s)
 *
 * This will set up a volume list for socket [s].
 *
 */
void
web_do_vol_list (int s) {
    char* templ;
    char* templ_org;

    // get the template
    if ((templ_org = web_load_template (WEB_TEMPLATE_VOL_LIST)) == NULL) {
	// can't dump.
	return;
    }

    // duplicate the template
    templ = strdup (templ_org);

    // do all the volumes
    for (web_temp_vol = 0; web_temp_vol < VOL_MAX_VOLS; web_temp_vol++) {
	// is this volume used?
	if (volume[web_temp_vol].flags) {
	    // yes. copy the template over
	    strcpy (templ, templ_org);

            // show the template
            web_show_template (s, templ);
	}
    }

    // free the template
    free (templ); free (templ_org);
}

/*
 * web_insert_value (char* valuename, char* dest)
 *
 * This will insert the value of {[valuename]} into [dest] for socket [s].
 *
 */
void
web_insert_value (char* valuename, char* dest, int s) {
    char value[WEB_MAX_VALUE_LEN];
    int  valno, i, j;
    BINDERY_OBJ	obj;

    // resolve the value
    valno = web_resolve_value (valuename);

    // default the value to nothing
    value[0] = 0;

    // fill it in
    switch (valno) {
	case WEB_VALUE_SERVER_NAME: // server name
				    snprintf (value, WEB_MAX_VALUE_LEN, conf_servername);
				    break;
     case WEB_VALUE_SERVER_VERSION: // server version
				    snprintf (value, WEB_MAX_VALUE_LEN, "%u.%u",conf_nwversion / 100, conf_nwversion % 100);
				    break;
	 case WEB_VALUE_USED_CONNS: // used connections
				    snprintf (value, WEB_MAX_VALUE_LEN, "%u", conn_count_used ());
				    break;
	case WEB_VALUE_TOTAL_CONNS: // total connections
				    snprintf (value, WEB_MAX_VALUE_LEN, "%u", conf_nofconns);
				    break;
       case WEB_VALUE_MOUNTED_VOLS: // mounted volumes
				    snprintf (value, WEB_MAX_VALUE_LEN, "%u", vol_count_vols ());
				    break;
          case WEB_VALUE_VOL_NAMES: // mounted volumes
				    for (i = 0; i < VOL_MAX_VOLS; i++)
					// is this volume in use?
					if (volume[i].flags) {
					    // yes. do we have previous volumes?
					    if (*value)
						// yes. add a ', ' first
						snprintf (value, WEB_MAX_VALUE_LEN, "%s, ", value);
				    	// add the volume name itself
					snprintf (value, WEB_MAX_VALUE_LEN, "%s%s", value, volume[i].name);
				    }
				    break;
  	  case WEB_VALUE_CONN_LIST: // connection list
				    web_do_conn_list (s);
				    break;
	    case WEB_VALUE_CONN_NO: // connection number
				    snprintf (value, WEB_MAX_VALUE_LEN, "%s%u", value, web_temp_conn);
				    break;
	  case WEB_VALUE_CONN_ADDR: // connection address
				    snprintf (value, WEB_MAX_VALUE_LEN, "%s%s", value, ipx_ntoa (conn[web_temp_conn]->addr.sipx_addr));
				    break;
	case WEB_VALUE_CONN_OBJECT: // connection object 
				    // is the connection marked as attached?
				    if (conn[web_temp_conn]->status == CONN_STATUS_ATTACHED)
					// yes. object is 'NOT-LOGGED-IN' then
				        snprintf (value, WEB_MAX_VALUE_LEN, "NOT-LOGGED-IN");
				    else
					// try to look up the object name
					if (bindio_scan_object_by_id (conn[web_temp_conn]->object_id, NULL, &obj) < 0)
					    // the object is not known. use ???
					    snprintf (value, WEB_MAX_VALUE_LEN, "???");
					else
					    // use the user name
					    snprintf (value, WEB_MAX_VALUE_LEN, "%s", obj.name);
				    break;
	 case WEB_VALUE_CONN_CLEAR: // clear connection.
				    if (web_temp_conn)
					conn_clear (web_temp_conn);
				    break;
       case WEB_VALUE_CONN_SENDMSG: // send message
				    if ((web_temp_conn) && (web_temp_msg != NULL))
					strcpy ((char*)conn[web_temp_conn]->message, web_temp_msg);
				    break;
  	   case WEB_VALUE_VOL_LIST: // volume list
				    web_do_vol_list (s);
				    break;
	     case WEB_VALUE_VOL_NO: // volume number
				    snprintf (value, WEB_MAX_VALUE_LEN, "%s%u", value, web_temp_vol);
				    break;
	   case WEB_VALUE_VOL_NAME: // volume name
				    snprintf (value, WEB_MAX_VALUE_LEN, "%s%s", value, volume[web_temp_vol].name);
				    break;
	  case WEB_VALUE_VOL_PATH: // volume path
				    snprintf (value, WEB_MAX_VALUE_LEN, "%s%s", value, volume[web_temp_vol].path);
				    break;
       case WEB_VALUE_VOL_DISMOUNT: // dismount volume
				    // SYS-tem volume?
				    if (!strcasecmp (volume[web_temp_vol].name, "SYS")) {
					// yes. no way we're going to dismount
					// this!
				        snprintf (value, WEB_MAX_VALUE_LEN, "Can't dismount SYS volume");
				    } else {
					// is this volume in use?
					if (fs_volume_inuse (web_temp_vol) < 0) {
					    // no. kill it
					    fs_delete_volume (web_temp_vol);
				            snprintf (value, WEB_MAX_VALUE_LEN, "Success");
					} else {
					    // yes. chicken out
				            snprintf (value, WEB_MAX_VALUE_LEN, "Volume is in use");
					}
				    }
				    break;
			   default: // what's this?
				    snprintf (value, WEB_MAX_VALUE_LEN, "???");
    }

    // add the value
    snprintf (dest, WEB_MAX_LINE_LEN, "%s%s", dest, value);
}

/*
 * web_show_template (int s, char* template_data)
 *
 * This will build the output of template [template_data] and send that to
 * socket [s].
 *
 */
void
web_show_template (int s, char* template_data) {
    char* line;
    char* ptr;
    char* ptr2;
    char  temp_line[WEB_MAX_LINE_LEN];
    int   temp_ptr;

    // keep doing the template
    ptr = template_data;
    do {
	// get the current line
	line = ptr;

	// scan for a newline
	ptr = strchr (line, '\n');
	if (ptr != NULL) {
	    // we've got a new line. zap it
	    *ptr = 0; ptr++;
	}

	// now, [line] points to the line to process
        temp_ptr = 0; bzero (&temp_ptr, WEB_MAX_LINE_LEN);

	// is the char anything but '{' ?
	while (*line) {
	    // is this a { thingy?
	    if (*line == '{') {
		// yes. scan for the '}'
		ptr2 = strchr (line + 1, '}');
		if (ptr2 != NULL) {
		    // got ya. isolate the substring
		    *ptr2 = 0;

		    // insert this value
		    web_insert_value (line + 1, temp_line, s);
		    temp_ptr = strlen (temp_line);

		    // skip the substring
		    line = ptr2;
		} else {
		    // it wasn't found. just add the char
		    temp_line[temp_ptr++] = *line;
		}
	    } else {
		// no. just add it to the buffer
		temp_line[temp_ptr++] = *line;
	    }

	    // are we about to hit the ending of the temponary line?
	    if (temp_ptr >= (WEB_MAX_LINE_LEN - 2)) {
		// yes. scan for the end of the line (minus 1 because of
		// line++) so everything else is skipped
		line = strchr (line, 0) - 1;
	    }
	    line++;
	}

	// terminate the line and send it over
	temp_line[temp_ptr++] = '\n'; temp_line[temp_ptr] = 0;
	write (s, temp_line, strlen (temp_line));	
    } while ((ptr != NULL) && (*ptr));
}

/*
 * web_resolve_parm (char* parm)
 *
 * This will resolve parameter name [parm] to one of the WEB_PARM_xxx constants.
 *
 */
int
web_resolve_parm (char* parm) {
    // try to resolve the parameter
    if (!strcasecmp (parm, "conn")) return WEB_PARM_CONN;
    if (!strcasecmp (parm, "message")) return WEB_PARM_MESSAGE;
    if (!strcasecmp (parm, "vol")) return WEB_PARM_VOL;

    // what's this?
    return WEB_PARM_UNKNOWN;
}

/*
 * web_handle_parms ()
 *
 * This will handle web parameter pair [web_parms].
 *
 */
void
web_handle_parms () {
    char* ptr;
    char* ptr2;
    char* pos;
    char* value;
    int   i;

    // start at the beginning
    pos = web_parms;

    // handle them all
    do {
	// save the beginning of the line
	ptr = pos;

	// first, isolate a single pair, like 'a=b'
	ptr2 = strchr (ptr, '&');
	if (ptr2 == NULL)
	    // no more pairs. in that case, end at the string end
	    ptr2 = strchr (ptr, 0);

	// get rid of the & thing
	if (*ptr2) {
	    *ptr2 = 0; ptr2++;
	}

	// isolate the name and value
	value = strchr (ptr, '=');
	if (value != NULL) {
	    *value = 0; value++;
	}

	// the name is now stored in [ptr] and the value in [value]. do we have
	// a value?
	if (value != NULL) {
	    // yes. figure out the parameter
	    i = web_resolve_parm (ptr);
	    switch (i) {
		 case WEB_PARM_CONN: // connection number
				     web_temp_conn = atoi (value);
				     break;
	      case WEB_PARM_MESSAGE: // message
				     web_temp_msg = value;
				     break;
	          case WEB_PARM_VOL: // volume
				     web_temp_vol = atoi (value);
				     break;
			    default: // what's this?
				     DPRINTF (11, LOG_WEB, "[DEBUG] [WEB] Skipped unknown parameter '%s' with value '%s'\n", ptr, value);
	    }
	}

	// next position, please
	pos = ptr2;
    } while (*pos);
}

static int b64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
    };

/*
 * web_b64decode (const char* str, char* space, int size )
 *
 * This will decode Base64 string [str] to plain ASCII [space]. It will never
 * write more than [size] bytes in [space].
 *
 * This code has been copied from mini_httpd by Jef Poskanzer <jef@acme.com>
 *
 */
int
web_b64decode (const char* str, char* space, int size) {
    const char* cp;
    int space_idx, phase;
    int d, prev_d;
    unsigned char c;

    space_idx = 0;
    phase = 0;
    for ( cp = str; *cp != '\0'; ++cp )
	{
	d = b64_decode_table[*cp];
	if ( d != -1 )
	    {
	    switch ( phase )
		{
		case 0:
		++phase;
		break;
		case 1:
		c = ( ( prev_d << 2 ) | ( ( d & 0x30 ) >> 4 ) );
		if ( space_idx < size )
		    space[space_idx++] = c;
		++phase;
		break;
		case 2:
		c = ( ( ( prev_d & 0xf ) << 4 ) | ( ( d & 0x3c ) >> 2 ) );
		if ( space_idx < size )
		    space[space_idx++] = c;
		++phase;
		break;
		case 3:
		c = ( ( ( prev_d & 0x03 ) << 6 ) | d );
		if ( space_idx < size )
		    space[space_idx++] = c;
		phase = 0;
		break;
		}
	    prev_d = d;
	    }
	}
    return space_idx;
}

/*
 * web_handle_packet (int s, char* packet, int size)
 *
 * This will handle [size] bytes of WEB packet [packet] from socket [s].
 *
 */
void
web_handle_packet (int s, char* packet, int size) {
    char* ptr;
    char* ptr2;
    char* template_buf;
    char* headers;
    char* auth = NULL;
    char  template_name[WEB_MAX_TEMPLATE_NAME];
    char  authinfo[WEB_MAX_VALUE_LEN];
    int   category, subpage, i;

    DPRINTF (12, LOG_WEB, "[DEBUG] [WEB] Got data: '%s'\n", packet);

    // ASCIIz it
    packet[size - 1] = 0;

    // first of all, isolate the very first line (that contains the GET stuff)
    headers = strchr (packet, '\n');
    if (headers == NULL) {
	// hmm... there aren't any newlines here. probably bogus, discard it
	web_send_error (s, 400, "Bad Request", "This request was not understood");
	return;
    }

    // terminate the first part at this newline (we've isolated the headers
    // now)
    *headers = 0; headers++;

    // is the first word GET?
    if (strncasecmp ("GET ", packet, 4)) {
	// no. this is unsupported (now)
	web_send_error (s, 501, "Not supported", "Sorry, but this action is not supported");
	return;
    }

    // figure out where the path begins
    ptr = strchr (packet, ' ');
    if (ptr == NULL) {
	// there's no space here. it's bogus, complain
	web_send_error (s, 400, "Bad Request", "This request was not understood");
	return;
    }

    // now, scan for the first non-space
    while (*ptr == ' ') {
	// next charachter, please
	ptr++;

	// end of string?
	if (*ptr == 0) {
	    // yes. this is bogus, complain
	    web_send_error (s, 400, "Bad Request", "This request was not understood");
	    return;
	}
    }

    // [ptr] now points to the file... now, scan for the first ' ' after the
    // name so we have the name of the file
    ptr2 = strchr (ptr, ' ');
    if (ptr2 == NULL) {
	// hmm... the HTTP specification says there should be a HTTP/x.y
	// part after the filename. therefore, this request violates the
	// RFC and should not be served. picky, aren't we? ;)
	web_send_error (s, 400, "Bad Request", "This request was not understood");
	return;
    }
    *ptr2 = 0;

    // is the first charachter a slash?
    if (*ptr != '/') {
	// no. this request is faulty. complain
	web_send_error (s, 400, "Bad Request", "This request was not understood");
	return;
    }
    ptr++;

    // if there's something we hate in filenames, it are slashes and spaces.
    // kill them
    while ((ptr2 = strchr (ptr, '/')) != NULL) *ptr2 = 0;
    while ((ptr2 = strchr (ptr, ' ')) != NULL) *ptr2 = 0;

    // now, we may have some parameters. scan for the '?'
    web_parms = strchr (ptr, '?');
    if (web_parms != NULL) {
	// yes, we do. cut off the filename and the parameters
	*web_parms = 0; web_parms++;

	// handle the parameters
	web_handle_parms ();
    }

    // construct the template name
    snprintf (template_name, WEB_MAX_TEMPLATE_NAME, "%s.html", ptr);

    // is a file name given?
    if (!*ptr) {
	// no. use overview.html
	snprintf (template_name, WEB_MAX_TEMPLATE_NAME, "overview.html", ptr);
    }

    // we should have the file now. now, figure out the HTTP headers
    i = 1;
    do {
	// scan for a newline in the headers
	ptr = strchr (headers, '\n');
	if (ptr == NULL) {
	    // there is none. this is probably the last line then. Grab the
	    // terminating NUL
	    ptr = strchr (headers, 0);
	    i = 0;
	}

	// terminate this string
	*ptr = 0; ptr++;

	// is this the Authentication header?
	if (!strncasecmp ("Authorization:", headers, 14)) {
	    // yes. we've got the authorization header
	    auth = headers + 14;
	    while (*auth == ' ') auth++;

	    // get rid of the very annoying '\r' thing
	    ptr = strchr (auth, '\r');
	    if (ptr != NULL)
		*ptr = 0;
	}

	// ignore everything else
	headers = ptr;
    } while (i);

    // is authentication information given?
    if (auth == NULL) {
	// no. query for it
	web_send_error (s, 401, "Unauthorized", "Authorization is required");
	return;
    }

    // is the Authentication of type 'Basic' ?
    if (strncasecmp ("Basic", auth, 5)) {
	// no. complain
	web_send_error (s, 401, "Unauthorized", "Authorization is required");
	return;
    }

    // now, scan for the real authentication information
    auth += 5;
    while (*auth == ' ') auth++;

    // okay, now decode this weird Base64 stuff into something useful
    bzero (&authinfo, WEB_MAX_VALUE_LEN);
    web_b64decode (auth, authinfo, WEB_MAX_VALUE_LEN - 1);

    // scan for a colon
    ptr = strchr (authinfo, ':');
    if (ptr == NULL) {
	// there is none. complain
	web_send_error (s, 401, "Unauthorized", "Authorization is required");
	return;
    }
    *ptr = 0; ptr++;

    // now, [authinfo] is the username and [ptr] is the password. is the
    // information correct?
    if ((strcmp (authinfo, conf_web_username)) || (strcmp (ptr, conf_web_password))) {
	// no. complain
	web_send_error (s, 401, "Unauthorized", "Authorization is required");
	return;
    }

    DPRINTF (11, LOG_WEB, "[DEBUG] [WEB] Requested file '%s' became '%s' with parameters '%s'\n", ptr, template_name, web_parms);

    // is this file readable ?
    if ((template_buf = web_load_template (template_name)) == NULL) {
	// nope. 404 the user.
	web_send_error (s, 404, "File not found", "Sorry, but this file could not be found");
	return;
    }

    // send the header
    web_send_header (s, 200, "OK");
    web_send_response (s, web_header);
    web_show_template (s, template_buf);
    web_send_response (s, web_footer);

    // get rid of the template
    free (template_buf);
}

/* 
 * web_handle_conn()
 *
 * This will handle an incoming connection on socket [fd_web].
 *
 */
void
web_handle_conn() {
    int	      no;
    socklen_t size = sizeof (struct sockaddr_in);
    struct    sockaddr_in sin;

    // scan for an available web slot
    no = web_find_free_conn();
    if (no < 0) {
	// there are none. complain
	DPRINTF (0, LOG_WEB, "[INFO] [WEB] Out of slots, connection dropped\n");
	return;
    }
 
    // accept the connection
    if ((fd_web_conn[no] = accept (fd_web, (struct sockaddr*)&sin, &size)) < 0) {
	// this failed. complain
	DPRINTF (0, LOG_WEB, "[INFO] [WEB] Unable to accept connection\n");
	return;
    }
}

/*
 * web_handle_data (int s)
 *
 * This will handle data on socket [s].
 *
 */
void
web_handle_data (int s) {
    int size;
    char packet[WEB_MAX_PACKET_LEN + 1];

    // grab the data
    size = recv (s, packet, WEB_MAX_PACKET_LEN, 0);
    if (size > 0)
	// handle the data
	web_handle_packet (s, packet, size);
}
#endif
