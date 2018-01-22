/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * NCP subfunction 87 code
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "frommel.h"
#include "ncp.h"
#include "misc.h"

/*
 * ncp_handle_genbasevolno (int c, char* data, int size)
 *
 * This will handle NCP 87 22 (Generate directory base and volume number) calls.
 *
 */
void
ncp_handle_genbasevolno (int c, char* data, int size) {
    DPRINTF (9, LOG_NCP, "[%u] NCP 87 22: Generate directory base and volume number (XXX: TODO!)\n", c);

    ncp_send_compcode (c, 0xff);
}

/*
 * ncp_handle_r87 (int c, char* data, int size)
 *
 * This will handle [size] bytes of NCP 87 subfunction packet [data] for
 * connection [c].
 * 
 */
void
ncp_handle_r87 (int c, char* data, int size) {
    uint8 sub_func = *(data);

    // fix up the buffer offset
    data++;

    // handle the subfunction
    switch (sub_func) {
        case 22: //  generate dir base and volume number
		 ncp_handle_genbasevolno (c, data, size);
		 break;
	default: // what's this?
		 DPRINTF (6, LOG_NCP, "[INFO] [%u] Unsupported NCP function 87 %u, ignored\n", c, sub_func);
		 ncp_send_compcode (c, 0xff);
	 	 break;
    }
}
