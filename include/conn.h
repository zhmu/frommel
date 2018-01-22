/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <sys/types.h>
#include <netipx/ipx.h>
#include <time.h>
#include "frommel.h"
#include "misc.h"

#define CONN_STATUS_UNUSED	0	// unused
#define CONN_STATUS_ATTACHED	1	// attached
#define CONN_STATUS_LOGGEDIN	2	// logged-in

#define CONN_MAX_MSG_LEN	128	// message

#define CONN_DH_MAX_PATH_LEN	256	// maximum path length
#define CONN_DH_PER_CONN	32	// directory handles per connection

#define CONN_SH_MAX_PATH_LEN	256	// maximum path length
#define CONN_SH_PER_CONN	16	// search handles per connection

#define CONN_FH_PER_CONN	64	// file handles per connection

#define CONN_ERROR_OK		0	// ok
#define CONN_ERROR_NOCONSRIGHTS	0xc6	// no console rights
#define CONN_ERROR_FAILURE	0xff	// failure

#define CONN_RANGE_OK(c) ((c>0)&&(c<=conf_nofconns))

typedef struct {
    int    flags;			// flags
    int    volno;			// volume numbr
    char   path[CONN_DH_MAX_PATH_LEN];	// path
} DIRHANDLE;

typedef struct {
    char   path[CONN_SH_MAX_PATH_LEN];	// path
    char   relpath[CONN_SH_MAX_PATH_LEN];	// relative path
    int    dh;				// directory handle
    int	   scan_right;			// file scan right
} SEARCHANDLE;

typedef struct {
    int    fd;				// file descriptor
} FILEHANDLE;

typedef struct {
    uint8  status;			// status
    struct sockaddr_ipx addr;		// client address

    uint8  seq_no;			// sequence number
    uint8  task_no;			// task number
    time_t login_time;			// login timestamp
    time_t last_time;			// last time connection did something
    time_t wdog_time;			// last time of watchdog send

    uint32 object_id;			// logged in object id
    uint16 object_type;			// logged in object type
    uint8  object_supervisor;		// set if user is a supervisor

    uint8  login_key[8];		// login key

    uint8  message[CONN_MAX_MSG_LEN];   // message
    uint8  message_ok;			// messaging enabled

    DIRHANDLE dh[CONN_DH_PER_CONN];	// directory handles
    SEARCHANDLE sh[CONN_SH_PER_CONN];	// search handles
    FILEHANDLE fh[CONN_FH_PER_CONN];	// file handles
} CONNECTION;

extern CONNECTION* conn[65535];
extern int conn_loginenabled;

void conn_init();
int conn_attach_client (struct sockaddr_ipx*);
int conn_count_used();
void conn_logout (int);
void conn_clear (int);
void conn_cleanup();

int conn_is_operator (int);
int conn_is_manager (int);

uint8 conn_get_bind_accesslevel (int);
