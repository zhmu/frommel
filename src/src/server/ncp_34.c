/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * NCP subfunction 34 code
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "frommel.h"
#include "ncp.h"
#include "misc.h"

/*
 * ncp_handle_tts_isavail (int c, char* data, int size)
 *
 * This will handle NCP 34 0 (TTS Is Available) calls.
 *
 */
void
ncp_handle_tts_isavail (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (0);

    DPRINTF (9, LOG_NCP, "[%u] NCP 34 0: TTS Is available\n", c);

    // no TTS here
    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_r34 (int c, char* data, int size)
 *
 * This will handle [size] bytes of NCP 34 subfunction packet [data] for
 * connection [c].
 * 
 */
void
ncp_handle_r34 (int c, char* data, int size) {
    uint8 sub_func = *(data);

    // fix up the buffer offset and size
    data++; size--;

    // handle the subfunction
    switch (sub_func) {
	 case 0: // tts: is available
		 ncp_handle_tts_isavail (c, data, size);
		 break;
	default: // what's this?
		 DPRINTF (6, LOG_NCP, "[INFO] [%u] Unsupported NCP function 34 %u, ignored\n", c, sub_func);
		 ncp_send_compcode (c, 0xff);
	 	 break;
    }
}
