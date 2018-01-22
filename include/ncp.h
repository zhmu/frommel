/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include "frommel.h"
#include "misc.h"

#define NCP_DATASIZE		4096	// length of the data

typedef struct {
    uint8  req_type[2] PACKED;		// request type
    uint8  seq_no PACKED;		// sequence number
    uint8  con_no PACKED;		// connection number
    uint8  task_no PACKED;		// task number
    uint8  reserved PACKED;		// reserved
    uint8  req_code PACKED;		// request code
} NCP_REQUEST;

typedef struct {
    uint8  rep_type[2] PACKED;		// reply type
    uint8  seq_no PACKED;		// sequence number
    uint8  con_no PACKED;		// connection number
    uint8  task_no PACKED;		// task number
    uint8  reserved PACKED;		// reserved
    uint8  comp_code PACKED;		// completion code
    uint8  conn_stat PACKED;		// connection status
    char   data[NCP_DATASIZE] PACKED;	// data
} NCP_REPLY;

typedef struct {
    uint8  name[48] PACKED;		// server name
    uint8  version PACKED;		// version
    uint8  subversion PACKED;		// subversion
    uint8  max_conns[2] PACKED;		// maximum connections
    uint8  used_conns[2] PACKED;	// connections in use
    uint8  nof_vols[2] PACKED;		// number of volumes
    uint8  revision PACKED;		// revision level
    uint8  sft_level PACKED;		// sft level
    uint8  tts_level PACKED;		// tts level
    uint8  max_inuseconns[2] PACKED;	// maximum number of conns ever used
    uint8  account_version PACKED;	// accounting version
    uint8  vap_version PACKED;		// vap version
    uint8  queue_version PACKED;	// queue version
    uint8  print_version PACKED;	// print version
    uint8  vc_version PACKED;		// virtual console version
    uint8  rest_level PACKED;		// restriction level
    uint8  int_bridge PACKED;		// internet bridge
    uint8  mixedmode_flag PACKED;	// mixed mode flag
    uint8  reserved[59] PACKED;		// reserved
} NCP_SERVERINFO;

typedef struct {
   uint8   year PACKED;			// year
   uint8   month PACKED;		// month
   uint8   day PACKED;			// day
   uint8   hour PACKED;			// hour
   uint8   minute PACKED;		// minute
   uint8   second PACKED;		// second
   uint8   dayofweek PACKED;		// day of week
} NCP_SERVERTIME;

typedef struct {
   uint8   sec_per_cluster[2] PACKED;	// sectors per cluster
   uint8   total_clusters[2] PACKED;	// total clusters
   uint8   avail_clusters[2] PACKED;	// available clusters
   uint8   tot_dir_slots[2] PACKED;	// total directory slots
   uint8   avail_dir_slots[2] PACKED;	// available directory slots
   uint8   vol_name[16] PACKED;		// volume name
   uint8   remove_flag[2] PACKED;	// removable flag
} NCP_VOLINFO;

typedef struct {
   uint8   dh PACKED;			// directory handle
   uint8   rights PACKED;		// access rights
} NCP_DIRHANDLE;

typedef struct {
   uint8   volno PACKED;		// volume number
   uint8   dirid[2] PACKED;		// directory id
   uint8   seqno[2] PACKED;		// sequence number
   uint8   rights PACKED;		// access rights
} NCP_SEARCHINIT;

typedef struct {
   uint8   seqno[2] PACKED;		// sequence number
   uint8   dirid[2] PACKED;		// directory id
   uint8   filename[14] PACKED;		// file name
   uint8   attr PACKED;			// attributes
   uint8   mode PACKED;			// file mode
   uint8   length[4] PACKED;		// length
   uint8   create_date[2] PACKED;	// creation date
   uint8   access_date[2] PACKED;	// access date
   uint8   update_date[2] PACKED;	// update date
   uint8   update_time[2] PACKED;	// update time
} NCP_SEARCHFILE;

typedef struct {
   uint8   seqno[2] PACKED;		// sequence number
   uint8   dirid[2] PACKED;		// directory id
   uint8   filename[14] PACKED;		// file name
   uint8   attr PACKED;			// attributes
   uint8   mode PACKED;			// file mode
   uint8   create_date[2] PACKED;	// creation date
   uint8   create_time[2] PACKED;	// access date
   uint8   owner_id[4] PACKED;		// update date
   uint8   reserved2[2] PACKED;		// reserved
   uint8   magic[2] PACKED;		// magic id
} NCP_SEARCHDIR;

typedef struct {
    uint8  handle[6] PACKED;		// file handle
    uint8  reserved[2] PACKED;		// reserved
    uint8  name[14] PACKED;		// file name
    uint8  attr PACKED;			// attribute
    uint8  exec_type PACKED;		// execute type
    uint8  length[4] PACKED;		// file length
    uint8  create_date[2] PACKED;	// creation date
    uint8  access_date[2] PACKED;	// last access date
    uint8  update_date[2] PACKED;	// last update date
    uint8  update_time[2] PACKED;	// last update time
} NCP_OPENFILE;

typedef struct {
    uint8  accesslevel PACKED;		// bindery access level
    uint8  object_id[4] PACKED;		// object id
} NCP_ACCESSLEVEL;

typedef struct {
    uint8  data[128] PACKED;		// data
    uint8  more_flag PACKED;		// more flag
    uint8  flags PACKED;		// flags
} NCP_READPROP;

typedef struct {
    uint8  object_id[4] PACKED;		// object id
    uint8  object_type[2] PACKED;	// object type
    uint8  object_name[48] PACKED;	// object name
} NCP_OBJID;

typedef struct {
    uint8  id[4] PACKED;		// user id
    uint8  type[2] PACKED;		// user type
    uint8  name[48] PACKED;		// user name
    uint8  login_time[7] PACKED;	// login time
    uint8  reserved PACKED;		// reserved
} NCP_STATIONINFO;

typedef struct {
    uint8  addr[12] PACKED;		// address
    uint8  type PACKED;			// type
} NCP_INTERADDR;

typedef struct {
    uint8  accepted_size[2] PACKED;	// accepted max size
    uint8  echo_socket[2] PACKED;	// echo socket
    uint8  security_flag PACKED;	// security flag
} NCP_MAXPACKET;

typedef struct {
    uint8  object_id[4] PACKED;		// object id
    uint8  object_type[2] PACKED;	// object type
    uint8  object_name[48] PACKED;	// object name
    uint8  object_flags PACKED;		// object flags
    uint8  object_security PACKED;	// object security
    uint8  object_hasprop PACKED;	// object property flag
} NCP_SCANOBJ;

typedef struct {
    uint8  serial_no[4] PACKED;		// serial number
    uint8  app_no[2] PACKED;		// application number
} NCP_SERIALNO;

typedef struct {
    uint8  net_addr[4] PACKED;		// network address
    uint8  node_addr[6] PACKED;		// node address
    uint8  socket_no[2] PACKED;		// socket number
} NCP_INTERADDROLD;

typedef struct {
    uint8  restriction[4] PACKED;	// space restrictions
    uint8  inuse[4] PACKED;		// space in use
} NCP_SPACEREST;

typedef struct {
    uint8  count PACKED;		// trustee count
    uint8  object_id[80] PACKED;	// object id
    uint8  rights[40] PACKED;		// rights
} NCP_SCANTRUST;

typedef struct {
    uint8  totalblocks[4] PACKED;	// total blocks
    uint8  freeblocks[4] PACKED;	// free blocks
    uint8  purgeable[4] PACKED;		// purgeable blocks
    uint8  nonpurgeable[4] PACKED;	// non-purgeable blocks
    uint8  totaldirentries[4] PACKED;	// total dir entries
    uint8  availdirentries[4] PACKED;	// available dir entries
    uint8  reserved[4] PACKED;		// reserved
    uint8  sectorsperblock PACKED;	// sectors per block
    uint8  namelen PACKED;		// volume name length
} NCP_VOLPURGEINFO;

typedef struct {
    uint8  seqno[4] PACKED;		// sequence number
    uint8  subdir[4] PACKED;		// subdirectory
    uint8  attr[4] PACKED;		// attribute
    uint8  id PACKED;			// unique id
    uint8  flags PACKED;		// flags
    uint8  namspace PACKED;		// name space
    uint8  namelen PACKED;		// name length
    uint8  name[12] PACKED;		// name
    uint8  create_time[2] PACKED;	// creation time
    uint8  create_date[2] PACKED;	// creation date
    uint8  create_id[4] PACKED;		// creator id
    uint8  arch_time[2] PACKED;		// archive time
    uint8  arch_date[2] PACKED;		// archive date
    uint8  archive_id[4] PACKED;	// archive id
    uint8  update_time[2] PACKED;	// update time
    uint8  update_date[2] PACKED;	// update date
    uint8  update_id[4] PACKED;		// update id
    uint8  size[4] PACKED;		// file size
    uint8  reserved[44] PACKED;		// reserved
    uint8  rights_mask[2] PACKED;	// rights mask
    uint8  access_date[2] PACKED;	// last access date
    uint8  reserved2[28] PACKED;	// reserved
} NCP_SCANFILE;

typedef struct {
    uint8  seqno[4] PACKED;		// sequence number
    uint8  subdir[4] PACKED;		// subdirectory
    uint8  attr[4] PACKED;		// attribute
    uint8  id PACKED;			// unique id
    uint8  flags PACKED;		// flags
    uint8  namspace PACKED;		// name space
    uint8  namelen PACKED;		// name length
    uint8  name[12] PACKED;		// name
    uint8  create_time[2] PACKED;	// creation time
    uint8  create_date[2] PACKED;	// creation date
    uint8  create_id[4] PACKED;		// creator id
    uint8  arch_time[2] PACKED;		// archive time
    uint8  arch_date[2] PACKED;		// archive date
    uint8  archive_id[4] PACKED;	// archive id
    uint8  next_trustee[4] PACKED;	// next trustee
    uint8  reserved[48] PACKED;		// reserved
    uint8  max_space[4] PACKED;		// maximum space
    uint8  rights_mask[2] PACKED;	// rights mask
    uint8  reserved2[26] PACKED;	// reserved
} NCP_SCANDIR;

typedef struct {
    uint8  dirpath[16] PACKED;		// directory path
    uint8  create_date[2] PACKED;	// creation date
    uint8  create_time[2] PACKED;	// creation time
    uint8  owner_id[2] PACKED;		// owner trustee id
    uint8  rights PACKED;		// access rights
    uint8  reserved PACKED;		// reserved
    uint8  next_no[2] PACKED;		// next search number
} NCP_SCANDIRINFO;

#define NCP_ERROR_WRONGLENGTH	0xff	// wrong packet length
#define NCP_MAX_FIELD_LEN	1024	// maximum size of a NCP field

#define NCP_REQUIRE_LENGTH(x) if (size != (x)) { DPRINTF (8, LOG_NCP, "[WARN] Packet discarded (size %u is not equal to required %u)\n", size, x); ncp_send_compcode (c, NCP_ERROR_WRONGLENGTH); return; }
#define NCP_REQUIRE_LENGTH_MIN(x) if (size < (x)) { DPRINTF (8, LOG_NCP, "[WARN] Packet discarded (size %u is not equal or greater than required %u)\n", size, x); ncp_send_compcode (c, NCP_ERROR_WRONGLENGTH); return; }

void ncp_init();

void ncp_send_compcode (int, uint8);
void ncp_send_reply (int, NCP_REPLY*, int, uint8);

void ncp_send_volinfo (int, int, struct statfs*);

void ncp_handle_r21 (int c, char* data, int size);
void ncp_handle_r22 (int c, char* data, int size);
void ncp_handle_r23 (int c, char* data, int size);
void ncp_handle_r34 (int c, char* data, int size);
void ncp_handle_r87 (int c, char* data, int size);
void ncp_handle_packet (char*, int, struct sockaddr_ipx*);

extern int fd_ncp;
