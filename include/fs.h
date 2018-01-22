/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#ifndef __FS_INCLUDED__
#define __FS_INCLUDED__

#include <sys/types.h>
#include <netipx/ipx.h>
#include "misc.h"

#define FS_DH_UNUSED		0	// unused
#define FS_DH_PERM		1	// handle is permanent
#define FS_DH_TEMP		2	// handle is temporarily

#define FS_IGNORE_DH_PATH	1	// ignore DH path
#define FS_IGNORE_CONN		2	// ignore connection

#define FS_ERROR_OK		0	// no errors
#define FS_ERROR_OUTOFHANDLES	0x81	// out of handles
#define FS_ERROR_IOERROR	0x83	// IO error
#define FS_ERROR_BADHANDLE	0x88	// invalid handle
#define FS_ERROR_NODELETEPRIV	0x8a	// no delete privileges
#define FS_ERROR_NORENAMEPRIV	0x8b	// no rename privileges
#define FS_ERROR_ALLNAMESEXIST	0x92	// all names exist
#define FS_ERROR_CANTWRITE	0x94	// no write privileges
#define FS_ERROR_NOSUCHVOLUME	0x98	// no such volume
#define FS_ERROR_BAD_DH		0x9b	// bad directory handle
#define FS_ERROR_INVALIDPATH	0x9c	// invalid path 
#define FS_ERROR_REMAPERROR	0xfa	// remap error
#define FS_ERROR_NOFILE		0xff	// no such file

void fs_init ();
int fs_con_find_free_dh (int);
int fs_con_alloc_dh (int, int, int, char*);
int fs_build_path (int, int, char*, char*, int, int);
int fs_con_dealloc_dh (int, int);
int fs_con_alloc_sh (int, int, char*);
int fs_open_file (int, int, uint8, char*, int*);
int fs_close_file (int, int);
int fs_volume_inuse (int);

#endif
