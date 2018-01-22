/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include "frommel.h"
#include "misc.h"

#define  TRUST_SYSFILE "$TRUSTEE.SYS"			// trustee datafile

#define  TRUST_ERROR_OK		0			// no error
#define  TRUST_ERROR_NOTFOUND	0xfe			// trustee not found
#define  TRUST_ERROR_IOERROR	0xff			// IO error

#define  TRUST_ERROR_NORECORD	0x100			// no record

#define  TRUST_RIGHT_READ		0x001		// read
#define  TRUST_RIGHT_WRITE		0x002		// write
#define  TRUST_RIGHT_CREATE		0x008		// create
#define  TRUST_RIGHT_ERASE		0x010		// erase
#define  TRUST_RIGHT_ACCESSCONTROL	0x020		// access control
#define  TRUST_RIGHT_FILESCAN		0x040		// filescan
#define  TRUST_RIGHT_MODIFY		0x080		// modify
#define  TRUST_RIGHT_SUPERVISORY	0x100		// supervisory

typedef struct {
    uint8	obj_id[4] PACKED;			// object id
    uint8	rights[2] PACKED;			// rights
} TRUST_DSKRECORD;

typedef struct {
    uint32	obj_id;					// object id
    uint16	rights;					// rights
} TRUST_RECORD;

int trust_build_trustfile (int, uint8, char*, char*, int);

int trust_grant_right (int, uint8, char*, uint32, uint16);
int trust_remove_right (int, uint8, char*, uint32);
int trust_get_rights (int, uint8, char*, TRUST_RECORD*);
int trust_check_rights (int, int, char*, uint16);

void trust_conv_rec (TRUST_DSKRECORD*, TRUST_RECORD*);
