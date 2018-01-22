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
    uint8 operation[2]		PACKED;
    uint8 service_type[2]	PACKED;
    uint8 server_name[48]	PACKED;
    uint8 net_addr[4]		PACKED;
    uint8 node_addr[6]		PACKED;
    uint8 socket_addr[2]	PACKED;
    uint8 hops[2]		PACKED;
} SAP_PACKET;

typedef struct {
    uint16 service_type;
    uint8  server_name[48];
    uint8  net_addr[4];
    uint8  node_addr[6];
    uint16 socket_addr;
    uint16 hops;
    time_t last_update;
} SAP_ENTRY;

extern int fd_sap;
extern SAP_ENTRY sap_service[SAP_MAX_SERVICES];

void sap_init();
void sap_cleanup();
void sap_handle_packet (char*, int, struct sockaddr_ipx*);
