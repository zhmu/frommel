/*
 * Frommel 2.0
 *
 * (c) 2001, 2002 Rink Springer
 * 
 */
#include "frommel.h"
#include "net.h"
#include "misc.h"

extern int fd_gasp;

#define GASP_DATASIZE			512	/* maximum data size */
#define GASP_MAX_SESSIONS		8	/* maximum number of sessions */

#define GASP_TYPE_COMMAND		0x6666	/* command packet */
#define GASP_TYPE_REPLY			0x7777  /* reply packet */
#define GASP_TYPE_LINKCMD		0x8888  /* link command */
#define GASP_TYPE_LINKREPLY		0x9999  /* link reply */

#define GASP_STATUS_UNUSED		0	/* unused */
#define GASP_STATUS_CONNECTED		1	/* connected */
#define GASP_STATUS_AUTH		2	/* authenticated */

#define GASP_CMD_HELLO			0	/* hello */
#define GASP_CMD_GETLOGINKEY		1	/* get login key */
#define GASP_CMD_AUTH			2	/* authenticate command */
#define GASP_CMD_DISCON			3	/* disconnect */
#define GASP_CMD_DOWN			4	/* down the server */
#define GASP_CMD_LQUERY			5	/* query login status */
#define GASP_CMD_LENABLE		6	/* enable logins */
#define GASP_CMD_LDISABLE		7	/* disable logins */
#define GASP_CMD_BROADCAST		8	/* broadcast console message */
#define GASP_CMD_GETINFO		9	/* query server info */
#define GASP_CMD_GETCONN		10	/* get connection info */
#define GASP_CMD_CLEARCONN		11	/* clear connection */
#define GASP_CMD_GETSESS		12	/* get session information */
#define GASP_CMD_GETSAPINFO		13	/* get SAP information */

#define GASP_STATUS_OK			0	/* ok */
#define GASP_STATUS_NOTAUTH		1	/* not authenticated */
#define GASP_STATUS_BADAUTH		2	/* bad authentication info */
#define GASP_STATUS_OUTOFSESS		3	/* out of sessions */
#define GASP_STATUS_NOCONN		4	/* no such connection */
#define GASP_STATUS_NOSESS		5	/* no such session */
#define GASP_STATUS_NOSAP		6	/* no such sap entry */
#define GASP_STATUS_BADSESS		253	/* bad session */
#define GASP_STATUS_BADCMD		254	/* bad command */ 
#define GASP_STATUS_UNKNOWNCMD		255	/* unknown command */

typedef struct {
    uint8  type[2]		PACKED;		/* packet type */
    uint8  sessionid[2] 	PACKED;		/* session id */
    uint8  command[2]		PACKED;		/* command */
    uint8  data[GASP_DATASIZE]	PACKED;		/* data */
} GASP_REQUEST;

typedef struct {
    uint16 type;				/* packet type */
    uint16 sessionid;				/* session id */
    uint16 command;				/* command */
    uint16 datalen;				/* data length */
    char*  data;				/* data */
} GASP_PACKET;

typedef struct {
    uint8  type[2]      	PACKED;		/* packet type */
    uint8  sessionid[2] 	PACKED;		/* session id */
    uint8  statuscode   	PACKED;		/* status code */
    uint8  data[GASP_DATASIZE]	PACKED;		/* data */
} GASP_REPLY;

typedef struct {
    uint16 type;			/* packet type */
    uint16 sessionid;			/* session id */
    uint8  statuscode;			/* status code */
    char*  data;			/* data */
} GASP_REPLY_PACKET;

typedef struct {
    int    status;			/* used flag */
    struct sockaddr_ipx addr;		/* socket address */
    uint8  key[8];			/* login key */
} GASP_SESSION;

typedef struct {
    uint8  name[48] PACKED;		/* server name */
    uint16 version PACKED;		/* server version */
    uint32 nofconns PACKED;		/* number of connections */
    uint8  nofsessions PACKED;		/* number of sessions */
} GASP_SERVERINFO;

typedef struct {
    uint8  id[4] PACKED;		/* object id */
    uint8  type[2] PACKED;		/* object type */
    uint8  name[48] PACKED;		/* object name */
    uint8  addr[12] PACKED;		/* object address */
} GASP_CONINFO;

typedef struct {
    uint8  status PACKED;		/* status */
    uint8  addr[12] PACKED;		/* object address */
} GASP_SESSINFO;

typedef struct {
    uint8  key[8] PACKED;		/* encryption key */
} GASP_HELLO;

typedef struct {
    uint8   type[2] PACKED;		/* service type */
    uint8   name[48] PACKED;		/* service name */
    uint8   addr[12] PACKED;		/* service address */
} GASP_SAPINFO;

void gasp_init();
void gasp_handle_packet (char*, int, struct sockaddr_ipx*);
void gasp_send_request (int, int, char*, int);
