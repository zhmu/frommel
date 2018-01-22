/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include "frommel.h"
#include "misc.h"

#define  RCON_MAX_SESSIONS	8	// maximum rconsole sessions at once

typedef struct {
    int			sockno;		// socket number
} RCON_SESSION;

typedef struct {
    uint8	request[2] PACKED;	// request
    uint8	magic[4] PACKED;	// magic
} RCON_REQUEST;

typedef struct {
    uint8	reply[2] PACKED;	// reply code
    uint8	magic[4] PACKED;	// magic
    uint8	unk[16] PACKED;		// ???
} RCON_REPLY;

extern RCON_SESSION rcon_session[RCON_MAX_SESSIONS];

void	 rcon_init();
void	 rcon_handle_packet (char*, int, struct sockaddr_ipx*);

extern int fd_rcon;
