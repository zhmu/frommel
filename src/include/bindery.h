/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include "frommel.h"

#include "misc.h"

typedef struct {
    uint8 object_id[4] PACKED;			/* object id */
    uint8 object_type[2] PACKED;		/* object type */
    uint8 object_namelen PACKED;		/* object name length */
    uint8 object_name[47] PACKED;		/* object name */
    uint8 object_flags PACKED;			/* object flags */
    uint8 object_security PACKED;		/* object security */
    uint8 prop_id[4] PACKED;			/* property id */
    uint8 unknown[4] PACKED;			/* ??? */
} NETOBJ_RECORD;

typedef struct {
    uint8 prop_id[4] PACKED;			/* property id */
    uint8 prop_namelen PACKED;			/* name length */
    uint8 prop_name[15] PACKED;			/* property name */
    uint8 prop_flag PACKED;			/* property flag */
    uint8 prop_security PACKED;			/* property security */
    uint8 prop_objid[4] PACKED;			/* property object id */
    uint8 prop_valueid[4] PACKED;		/* property value id */
    uint8 prop_nextid[4] PACKED;		/* next property id */
} NETPROP_RECORD;

typedef struct {
    uint8 val_id[4] PACKED;			/* value id */
    uint8 val_propid[4] PACKED;			/* property id */
    uint8 val_nextid[4] PACKED;			/* next id */
    uint8 val_size[2] PACKED;			/* segment size */
    uint8 val_data[128] PACKED;			/* data */
} NETVAL_RECORD;

typedef struct {
    uint32 id;					/* object id */
    uint16 type;				/* object type */
    uint8  name[48];				/* object name */
    uint8  flags;				/* flags */
    uint8  security;				/* security */
    uint32 prop_id;				/* property id */
    uint32 reserved;				/* ?? */
} BINDERY_OBJ;

typedef struct {
    uint32 id;					/* property id */
    uint8  name[16];				/* property name */
    uint8  flags;				/* property flags */
    uint8  security;				/* property security */
    uint32 object_id;				/* object id */
    uint32 value_id;				/* value id */
    uint32 next_id;				/* next property id */
} BINDERY_PROP;

typedef struct {
    uint32 id;					/* value id */
    uint32 prop_id;				/* property id */
    uint32 next_id;				/* next value id */
    uint16 size;				/* number of segments */
    uint8  data[128];				/* data */
} BINDERY_VAL;

typedef struct {
    uint8  net_addr[4];				/* network address */
    uint8  node_addr[6];			/* node address */
    uint8  sock_addr[2];			/* socket address */
} BINDERY_NETADDR;

#define BINDERY_FLAG_SET		1	/* it's a set */

#define BINDERY_ERROR_OK		0x00	// no error
#define BINDERY_ERROR_BADPASSWORD	0xde	// bad password
#define BINDERY_ERROR_NOSUCHMEMBER	0xea	// no such member
#define BINDERY_ERROR_PROPEXISTS	0xed	// property exists
#define BINDERY_ERROR_OBJEXISTS		0xee	// object exists
#define BINDERY_ERROR_SECURITY		0xf1	// bindery security
#define BINDERY_ERROR_NOSUCHPROPERTY	0xfb	// no such property
#define BINDERY_ERROR_NOSUCHOBJECT	0xfc	// no such object
#define BINDERY_ERROR_IOERROR		0xff	// IO error

int bindio_open(char*);
void bindio_truncate();
void bindio_dump();

void bind_init();

int bindio_scan_object (char*, uint16, NETOBJ_RECORD*, BINDERY_OBJ*);
int bindio_scan_object_by_id (uint32, NETOBJ_RECORD*, BINDERY_OBJ*);
int bindio_scan_propid (uint32, NETPROP_RECORD*, BINDERY_PROP*);
int bindio_scan_prop (uint32, char*, BINDERY_PROP*);
int bindio_scan_val (uint32, BINDERY_VAL*);

int bindio_add_object (char*, uint16, uint8, uint8, uint8*);
int bindio_add_prop (uint32, char*, uint8, uint8, char*, uint8*);
int bindio_read_prop (uint32, char*, uint8, BINDERY_VAL*, BINDERY_PROP*);
int bindio_write_prop (uint32, char*, uint8, uint8, char*);
int bindio_scan_object_wild (char*, uint16, uint32, BINDERY_OBJ*);
int bindio_change_object (uint32, uint32, char*);
int bindio_delete_prop (uint32);
int bindio_delete_obj (uint32);

int bind_add_remove_objfromset (int, uint16, char*, char*, uint16, char*, uint8);
int bind_is_objinset (int, uint16, char*, uint32, char*, uint16, char*, uint32);
int bind_obj_is_supervisor (uint32);

int bind_check_access (int, uint32, uint8, int);

int bind_set_pwd (uint32, char*);
int bind_verify_pwd (uint32, char*, char*);
