/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include "frommel.h"
#include "net.h"
#include "misc.h"

typedef struct {
    uint8	cid PACKED;
    uint8	sign_char PACKED;
} WDOG_PACKET;

extern  int fd_wdog;

void	wdog_init();
void	wdog_send_watchdog (int, char);
void	wdog_handle_packet (char*, int, struct sockaddr_ipx*);
