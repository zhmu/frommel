/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * NCP subfunction 23 code
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "ncp.h"
#include "net.h"
#include "misc.h"

/*
 * ncp_handle_server_info (int c, char* data, int size)
 *
 * This will send our server info to connection [c].
 * 
 */
void
ncp_handle_server_info (int c, char* data, int size) {
    NCP_SERVERINFO* info;
    NCP_REPLY r;
    int i;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 17: Get file server information\n", c);

    // build a pointer
    info = (NCP_SERVERINFO*)r.data;
    bzero (info, sizeof (NCP_SERVERINFO));

    // fill the info out
    strncpy ((char*)info->name, conf_servername, 48);
    info->version = conf_nwversion / 100;
    info->subversion = conf_nwversion % 100;
    info->max_conns[0] = (conf_nofconns >> 8);
    info->max_conns[1] = (conf_nofconns & 0xff);
    i = conn_count_used ();
    info->used_conns[0] = (i >> 8); info->used_conns[1] = (i & 0xff);
    info->max_inuseconns[0] = (i >> 8); info->max_inuseconns[1] = (i & 0xff);
    i = VOL_MAX_VOLS;
    info->nof_vols[0] = (i >> 8); info->nof_vols[1] = (i & 0xff);
    info->rest_level = 1; info->sft_level = 2; info->tts_level = 1;
    info->account_version = 1; info->vap_version = 1;
    info->queue_version = 1; info->print_version = 1;
    info->vc_version = 1;

    // send this packet
    ncp_send_reply (c, &r, sizeof (NCP_SERVERINFO), 0);
}

/*
 * ncp_handle_accesslevel (int c, char* data, int size)
 *
 * This will handle the NCP 23 70 (get bindery access level) call.
 *
 */
void
ncp_handle_accesslevel (int c, char* data, int size) {
    NCP_REPLY		r;
    NCP_ACCESSLEVEL*	al;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 70: Get bindery access level\n", c);

    // fill out the buffer
    al = (NCP_ACCESSLEVEL*)r.data;

    al->accesslevel = conn_get_bind_accesslevel (c);
    al->object_id[0] = (conn[c]->object_id >>  24);
    al->object_id[1] = (conn[c]->object_id >>  16);
    al->object_id[2] = (conn[c]->object_id >>   8);
    al->object_id[3] = (conn[c]->object_id & 0xff);

    // send the packet
    ncp_send_reply (c, &r, sizeof (NCP_ACCESSLEVEL), 0);
}

/*
 * ncp_handle_readprop (int c, char* data, int size)
 *
 * This will handle NCP 23 61 (read property value) calls.
 *
 */
void
ncp_handle_readprop (int c, char* data, int size) {
    uint16 	  	obj_type;
    uint8  	  	obj_len, prop_len, seg_no;
    char   	  	obj_name[NCP_MAX_FIELD_LEN];
    char   	 	prop_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ   	obj;
    BINDERY_PROP	prop;
    BINDERY_VAL		val;
    NCP_REPLY	  	r;
    NCP_READPROP* 	rp;
    int		  	i;

    NCP_REQUIRE_LENGTH_MIN (6);

    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (prop_name, NCP_MAX_FIELD_LEN);

    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    seg_no    = GET_BE8 (data); data++;
    prop_len  = GET_BE8 (data); data++;
    bcopy (data, prop_name, prop_len);

    NCP_REQUIRE_LENGTH (6 + prop_len + obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 61: Read property value (type=%u,objname='%s',segno=%u,propname='%s')\n", c, obj_type, obj_name, seg_no, prop_name);

    // if [seg_no] is not 1, complain (XXX)
    if (seg_no != 1) {
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHPROPERTY);
	return;
    }

    // scan for the object
    i = bindio_scan_object (obj_name, obj_type, NULL, &obj);
    if (i < 0) {
	// this failed. complain
	DPRINTF (10, LOG_NCP, "[DEBUG] No such object\n");
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // get the property
    i = bindio_read_prop (obj.id, prop_name, seg_no, &val, &prop);
    if (i != BINDERY_ERROR_OK) {
	// this failed. return an error code
	DPRINTF (10, LOG_NCP, "[DEBUG] Error 0x%x\n", i);
	ncp_send_compcode (c, i);
	return;
    }

    // do we have enough access to read this property?
    if (bind_check_access (c, obj.id, prop.security, 0) < 0) {
	// no. send an error code
	ncp_send_compcode (c, BINDERY_ERROR_SECURITY);
	return;
    }

    // it worked. construct the result
    rp = (NCP_READPROP*)r.data;
    bcopy (val.data, rp->data, 128);
    rp->more_flag = 0;			// XXX
    rp->flags = 0;			// XXX

    // send the reply
    ncp_send_reply (c, &r, sizeof (NCP_READPROP), 0);
}

/*
 * ncp_handle_getobjid (int c, char* data, int size)
 *
 * This will handle NCP 23 53 (Get bindery object ID) calls.
 *
 */
void
ncp_handle_getobjid (int c, char* data, int size) {
    uint16	  obj_type;
    uint8  	  obj_len;
    char   	  obj_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ	  obj;
    NCP_REPLY	  r;
    NCP_OBJID*    oid;

    NCP_REQUIRE_LENGTH_MIN (4);

    bzero (obj_name, NCP_MAX_FIELD_LEN);

    obj_type = GET_BE16 (data); data += 2;
    obj_len  = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (4 + obj_len);
    bcopy (data, obj_name, obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 53: Get bindery object ID (type=%u,objname='%s')\n", c, obj_type, obj_name);

    // scan for the object
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// there's no such object. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // XXX: we _should_ have read access to this object, according to novell
    // docs... but it doesn't work if we check for them... bug ?

    // fill out the result
    oid = (NCP_OBJID*)r.data;
    oid->object_id[0] = (obj.id >> 24); oid->object_id[1] = (obj.id >>  16);
    oid->object_id[2] = (obj.id >>  8); oid->object_id[3] = (obj.id & 0xff);
    oid->object_type[0] = (obj.type >> 8);
    oid->object_type[1] = (obj.type & 0xff);
    bcopy (obj.name, oid->object_name, 48);

    // send the result
    ncp_send_reply (c, &r, sizeof (NCP_OBJID), 0);
}

/*
 * ncp_handle_loginkey (int c, char* data, int size)
 *
 * This will handle NCP 23 23 (Get login key) calls.
 *
 */
void
ncp_handle_loginkey (int c, char* data, int size) {
    NCP_REPLY r;
    int i;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 23: Get login key\n", c);

    // build a login key
    for (i = 0; i < 8; i++) {
	r.data[i] = conn[c]->login_key[i] = (uint8)(random() & 0xff);
    }

    // send the result
    ncp_send_reply (c, &r, 8, 0);
}

/*
 * ncp_handle_keyedlogin (int c, char* data, int size)
 *
 * This will handle NCP 23 24 (Keyed object login) calls.
 *
 */
void
ncp_handle_keyedlogin (int c, char* data, int size) {
    uint8	  key[8], obj_len;
    uint16	  obj_type;
    char   	  obj_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ   obj;

    NCP_REQUIRE_LENGTH_MIN (12);

    bzero (obj_name, NCP_MAX_FIELD_LEN);

    bcopy (data, key, 8); data += 8;
    obj_type = GET_BE16 (data); data += 2;
    obj_len  = GET_BE8 (data); data++;
    NCP_REQUIRE_LENGTH (12 + obj_len);
    bcopy (data, obj_name, obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 24: Keyed object login (type=%u,objname='%s')\n", c, obj_type, obj_name);

    // get the object information
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// this object doesn't exist. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // yay, the object exists! check the password
    if (bind_verify_pwd (obj.id, (char*)key, (char*)conn[c]->login_key) < 0) {
	ncp_send_compcode (c, BINDERY_ERROR_BADPASSWORD);
	return;
    }

    // all looks ok. modify the connection state
    conn[c]->status = CONN_STATUS_LOGGEDIN;
    conn[c]->object_type = obj_type;
    conn[c]->object_id = obj.id;
    conn[c]->object_supervisor = (bind_obj_is_supervisor (obj.id) < 0) ? 0 : 1;
    if (conn[c]->object_supervisor) {
	DPRINTF (4, LOG_NCP, "[INFO] [%u] Supervisor login, username '%s'\n", c, obj.name);
    }

    // it went ok!
    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_loggedinfo (int c, int conn_no)
 *
 * This will send logged-in information of connection [conn_no] to client [c].
 *
 */
void
ncp_handle_loggedinfo (int c, int conn_no) {
    NCP_REPLY	     r;
    NCP_STATIONINFO* si;
    BINDERY_OBJ	     obj;
    struct tm*	     t;

    // get the information about this object
    if (bindio_scan_object_by_id (conn[conn_no]->object_id, NULL, &obj) < 0) {
	// this failed... zero out the object
	bzero (&obj, sizeof (BINDERY_OBJ));
    }

    // build the result
    si = (NCP_STATIONINFO*)r.data;
    bzero (si, sizeof (NCP_STATIONINFO));
    si->id[0] = (conn[conn_no]->object_id >>  24);
    si->id[1] = (conn[conn_no]->object_id >>  16);
    si->id[2] = (conn[conn_no]->object_id >>   8);
    si->id[3] = (conn[conn_no]->object_id & 0xff);
    si->type[0] = (conn[conn_no]->object_type >>   8);
    si->type[1] = (conn[conn_no]->object_type & 0xff);
    bcopy (obj.name, si->name, 47);

    // build the login time
    t = localtime (&conn[conn_no]->login_time);
    si->login_time[0] = t->tm_year;
    si->login_time[1] = t->tm_mon + 1;
    si->login_time[2] = t->tm_mday;
    si->login_time[3] = t->tm_hour;
    si->login_time[4] = t->tm_min;
    si->login_time[5] = t->tm_sec;
    si->login_time[6] = t->tm_wday;

    // send this reply
    ncp_send_reply (c, &r, sizeof (NCP_STATIONINFO), 0);
}

/*
 * ncp_handle_getloggedinfo (int c, char* data, int size)
 *
 * This will handle NCP 23 28 (Get station's logged info) calls.
 *
 */
void
ncp_handle_getloggedinfo (int c, char* data, int size) {
    uint32   	     conn_no;

    NCP_REQUIRE_LENGTH (5);
    conn_no  = GET_LE32 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 28: Get station's logged info (c=%u)\n", c, conn_no);

    // connection number ok?
    if (CONN_RANGE_OK (conn_no)) {
	// yes. send the info
        ncp_handle_loggedinfo (c, conn_no);
	return;
    }

    // send an error code
    ncp_send_compcode (c, CONN_ERROR_FAILURE);
}

/*
 * ncp_handle_getloggedinfo2 (int c, char* data, int size)
 *
 * This will handle NCP 23 22 (Get station's logged info (old)) calls.
 *
 */
void
ncp_handle_getloggedinfo2 (int c, char* data, int size) {
    uint8   	     conn_no;

    NCP_REQUIRE_LENGTH (2);
    conn_no  = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 22: Get station's logged info (c=%u)\n", c, conn_no);

    // connection number ok?
    if (CONN_RANGE_OK (conn_no)) {
	// yes. send the info
        ncp_handle_loggedinfo (c, conn_no);
	return;
    }

    // send an error code
    ncp_send_compcode (c, CONN_ERROR_FAILURE);
}

/*
 * ncp_handle_getobjname (int c, char* data, int size)
 *
 * This will handle NCP 23 54 (Get bindery object name) calls.
 *
 */
void
ncp_handle_getobjname (int c, char* data, int size) {
    uint32	  obj_id;
    uint16	  obj_type;
    BINDERY_OBJ	  obj;
    NCP_REPLY	  r;
    NCP_OBJID*    oid;

    NCP_REQUIRE_LENGTH (5);

    obj_id  = GET_BE32 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 54: Get bindery object name (id=%u)\n", c, obj_id);

    // get the object's record
    if (bindio_scan_object_by_id (obj_id, NULL, &obj) < 0) {
	// this failed. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // do we have enough access to read this object's name ?
    if (bind_check_access (c, obj.id, obj.security, 0) < 0) {
	// no. send an error code
	ncp_send_compcode (c, BINDERY_ERROR_SECURITY);
	return;
    }

    // fill out the result
    oid = (NCP_OBJID*)r.data;
    oid->object_id[0] = (obj.id >> 24); oid->object_id[1] = (obj.id >>  16);
    oid->object_id[2] = (obj.id >>  8); oid->object_id[3] = (obj.id & 0xff);
    oid->object_type[0] = (obj.type >> 8);
    oid->object_type[1] = (obj.type & 0xff);
    bcopy (obj.name, oid->object_name, 48);

    // send the result
    ncp_send_reply (c, &r, sizeof (NCP_OBJID), 0);
}

/*
 * ncp_handle_getinteraddr (int c, char* data, int size)
 *
 * This will handle NCP 23 26 (Get internet address) calls.
 *
 */
void
ncp_handle_getinteraddr (int c, char* data, int size) {
    uint32 	   conn_no;
    NCP_REPLY	   r;
    NCP_INTERADDR* ia;

    NCP_REQUIRE_LENGTH (5);

    conn_no  = GET_BE32 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 26: Get internet address (c=%u)\n", conn_no);

    // build the reply
    ia = (NCP_INTERADDR*)r.data;
    bzero (ia, sizeof (NCP_INTERADDR));		// XXX
    ia->type = 2;				// NCP

    // XXX: address
    ncp_send_reply (c, &r, sizeof (NCP_INTERADDR), 0);
}

/*
 * ncp_handle_writeprop (int c, char* data, int size)
 *
 * This will handle NCP 23 62 (write property value) calls.
 *
 */
void
ncp_handle_writeprop (int c, char* data, int size) {
    uint16 	  	obj_type;
    uint8  	  	obj_len, prop_len, seg_no, more_flag;
    char   	  	obj_name[NCP_MAX_FIELD_LEN];
    char   	  	prop_name[NCP_MAX_FIELD_LEN];
    char 	  	prop_val[128];
    BINDERY_OBJ   	obj;
    BINDERY_PROP	prop;
    NCP_REPLY	  	r;
    NCP_READPROP* 	rp;
    int		  	i;

    NCP_REQUIRE_LENGTH_MIN (135);

    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (prop_name, NCP_MAX_FIELD_LEN);

    obj_type = GET_BE16 (data); data += 2;
    obj_len  = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    seg_no = GET_BE8 (data); data++;
    more_flag = GET_BE8 (data); data++;
    prop_len = GET_BE8 (data); data++;
    bcopy (data, prop_name, prop_len); data += prop_len;
    bcopy (data, prop_val, 128);

    NCP_REQUIRE_LENGTH (135 + prop_len + obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 62: Write property value (type=%u,objname='%s',segno=%u,moreflag=%u,propname='%s')\n", c, obj_type, obj_name, seg_no, more_flag, prop_name);

    // get the object's record
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// this failed. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // scan for the property
    if (bindio_scan_prop (obj.id, prop_name, &prop) < 0) {
	// this failed. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHPROPERTY);
	return;
    }

    // do we have enough access to write this property?
    if (bind_check_access (c, obj.id, prop.security, 1) < 0) {
	// no. send an error code
	ncp_send_compcode (c, BINDERY_ERROR_SECURITY);
	return;
    }

    // modify the property's value
    ncp_send_compcode (c, bindio_write_prop (obj.id, prop_name, seg_no, more_flag, prop_val));
}

/*
 * ncp_handle_scanobj (int c, char* data, int size)
 *
 * This will handle NCP 23 55 (Scan bindery object) calls.
 *
 */
void
ncp_handle_scanobj (int c, char* data, int size) {
    uint32	  last_obj;
    uint16	  obj_type;
    uint8	  obj_len;
    char   	  obj_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ   obj;
    NCP_REPLY	  r;
    NCP_SCANOBJ*  sob;

    NCP_REQUIRE_LENGTH_MIN (8);

    bzero (obj_name, NCP_MAX_FIELD_LEN);
    last_obj  = GET_BE32 (data); data += 4;
    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len);

    DPRINTF (9, LOG_NCP, "[%u] NCP 23 55: Scan Bindery Object (lastobj=0x%x,type=0x%x,name='%s')\n", c, last_obj, obj_type, obj_name);

    // scan for the object
    if (bindio_scan_object_wild (obj_name, obj_type, last_obj, &obj) < 0) {
	// no matches. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // we got a match. build the result
    sob = (NCP_SCANOBJ*)r.data;
    sob->object_id[0] = (obj.id >> 24); sob->object_id[1] = (obj.id >>  16);
    sob->object_id[2] = (obj.id >>  8); sob->object_id[3] = (obj.id & 0xff);
    sob->object_type[0] = (obj.type >> 8);
    sob->object_type[1] = (obj.type & 0xff);
    bcopy (obj.name, sob->object_name, 48);
    sob->object_flags = obj.flags;
    sob->object_security = obj.security;
    sob->object_hasprop = 1;

    // send the result
    ncp_send_reply (c, &r, sizeof (NCP_SCANOBJ), 0);
}

/*
 * ncp_handle_createobj (int c, char* data, int size)
 *
 * This will handle NCP 23 50 (Create bindery object) calls.
 *
 */
void
ncp_handle_createobj (int c, char* data, int size) {
    uint8	flags;
    uint8	security;
    uint16	obj_type;
    uint8	name_len;
    uint8	error;
    char	obj_name[NCP_MAX_FIELD_LEN];
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH_MIN (6);

    bzero (obj_name, NCP_MAX_FIELD_LEN);
    flags     = GET_BE8 (data); data++;
    security  = GET_BE8 (data); data++;
    obj_type  = GET_BE16 (data); data += 2;
    name_len  = GET_BE8 (data); data++;
    bcopy (data, obj_name, name_len);

    DPRINTF (9, LOG_NCP, "[%u] NCP 23 50: Create Bindery Object (flags=%i,security=0x%x,type=0x%x,name='%s')\n", c, flags, security, obj_type, obj_name);

    // are we a supervisor?
    if (!conn[c]->object_supervisor) {
	// no. complain
	ncp_send_compcode (c, BINDERY_ERROR_SECURITY);
	return;
    }

    // add the object
    if (bindio_add_object (obj_name, obj_type, flags, security, &error) < 0) {
	// this failed. return the error code
        ncp_send_compcode (c, error);
	return;
    }

    // this worked. return OK status
    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_createprop (int c, char* data, int size)
 *
 * This will handle NCP 23 57 (Create Property) calls.
 *
 */
void
ncp_handle_createprop (int c, char* data, int size) {
    uint16 	obj_type;
    uint8  	obj_len;
    uint8  	prop_len;
    uint8	prop_flags;
    uint8	prop_security;
    uint8	error;
    char	obj_name[NCP_MAX_FIELD_LEN];
    char	prop_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ	obj;

    NCP_REQUIRE_LENGTH_MIN (9);

    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (prop_name, NCP_MAX_FIELD_LEN);
    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    prop_flags = GET_BE8 (data); data++;
    prop_security = GET_BE8 (data); data++;
    prop_len   = GET_BE8 (data); data++;
    bcopy (data, prop_name, prop_len);

    DPRINTF (9, LOG_NCP, "[%u] NCP 23 57: Create Property (objtype=0x%x,objname='%s',propflags=%i,propsecurity=0x%x,propname='%s')\n", c, obj_type, obj_name, prop_flags, prop_security, prop_name);

    // scan for the object
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// this failed. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // do we have enough access?
    if (bind_check_access (c, obj.id, obj.security, 1) < 0) {
	// no. complain
	ncp_send_compcode (c, BINDERY_ERROR_SECURITY);
	return;
    }

    // add the property
    if (bindio_add_prop (obj.id, prop_name, prop_flags, prop_security, NULL, &error) < 0) {
	// this failed. complain
	ncp_send_compcode (c, error);
	return;
    }

    // it all worked. inform the client
    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_objaccesslevel (int c, char* data, int size)
 *
 * This will handle the NCP 23 72 (get bindery object access level) call.
 *
 */
void
ncp_handle_objaccesslevel (int c, char* data, int size) {
    uint32		obj_id;
    NCP_REPLY		r;
    NCP_ACCESSLEVEL*	al;

    NCP_REQUIRE_LENGTH (5);

    obj_id  = GET_BE32 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 72: Get bindery object access level (id=0x%x)\n", c, obj_id);

    // are we a supervisor?
    if (conn[c]->object_supervisor) {
	// yes. we have supervisory over this object
	r.data[0] = 0x33;
    } else {
	// are we this object?
	if (conn[c]->object_id == obj_id) {
	    // yes. we have control over ourselves
	    r.data[0] = 0x22;
	} else {
	    // are we an object supervisor of this object?
	    if (bind_is_objinset (0, 0, NULL, obj_id, "OBJ_SUPERVISORS", 0x1, NULL, conn[c]->object_id) == BINDERY_ERROR_OK) {
		// yes. wohoo
		r.data[0] = 0x33;
	    } else {
		// are we logged in?
		if (conn[c]->object_id == 0xffffffff) {
	 	    // no. we've got no access
		    r.data[0] = 0x00;
		} else {
		    // yes. we've got minor rights
		    r.data[0] = 0x11;
		}
	    }
	}
    }

    // send the packet
    ncp_send_reply (c, &r, 1, 0);
}

/*
 * ncp_handle_consoleprivs (int c, char* data, int size)
 *
 * This will handle the NCP 23 200 (Check console privileges) call.
 *
 */
void
ncp_handle_consoleprivs (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 200: Check console privileges\n", c);
    ncp_send_compcode (c, (conn_is_operator (c) < 0) ? 0xc6 : 0);
}

/*
 * ncp_handle_getloginstat (int c, char* data, int size)
 *
 * This will handle NCP 23 205 (Get File Server Login Status) call.
 *
 */
void
ncp_handle_getloginstat (int c, char* data, int size) {
    NCP_REPLY	r;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 205: Get file server login status\n", c);

    // copy the login allowed flag (bug: novell docs claim we need to send
    // 4 bytes, but it actually is just one)
    r.data[0] = (uint8)conn_loginenabled;

    // send the reply
    ncp_send_reply (c, &r, 1, 0);
}

/*
 * ncp_handle_keyedchangepwd (int c, char* data, int size)
 *
 * This will handle NCP 23 75 (Keyed change password) calls.
 *
 */
void
ncp_handle_keyedchangepwd (int c, char* data, int size) {
    uint16		obj_type;
    uint8		obj_len, pwd_len, err;
    uint8		key[8];
    char		obj_name[NCP_MAX_FIELD_LEN];
    char		pwd[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ		obj;
    BINDERY_PROP	prop;
    BINDERY_VAL		val;
    uint8		s_uid[4];
    char		tmp[16];

    // XXX: should care more about the length

    NCP_REQUIRE_LENGTH_MIN (15);
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (pwd, NCP_MAX_FIELD_LEN);

    bcopy (data, key, 8); data += 8;
    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    pwd_len = GET_BE8 (data); data++;
    bcopy (data, pwd, pwd_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 75: Keyed change password (objtype=0x%x,objname='%s')\n", c, obj_type, obj_name);

    // get the object
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// this object can't be found. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // convert the object ID to big endian
    s_uid[0] = (uint8)(obj.id >> 24);
    s_uid[1] = (uint8)(obj.id >> 16);
    s_uid[2] = (uint8)(obj.id >>  8);
    s_uid[3] = (uint8)(obj.id & 0xff);

    // first of all, grab the PASSWORD property
    if (bindio_read_prop (obj.id, "PASSWORD", 1, &val, &prop) == BINDERY_ERROR_OK) {
	// this worked. now, is the old password correct?
        nw_encrypt ((unsigned char*)conn[c]->login_key, (unsigned char*)val.data, (unsigned char*)tmp);
	if (memcmp (key, tmp, 8)) {
	    // nope. are we a supervisor?
	    if (conn[c]->object_supervisor) {
		// yes. compile a blank password
	        shuffle ((unsigned char*)s_uid, (unsigned char*)tmp, 0, val.data);
	        nw_encrypt ((unsigned char*)conn[c]->login_key, (unsigned char*)val.data, (unsigned char*)tmp);

	        // is it really blank?
	        if (memcmp (key, tmp, 8)) {
		    // no. complain
		    ncp_send_compcode (c, 0xff);
		    return;
		} 
	    } else {
		// no. complain
		ncp_send_compcode (c, 0xff);
	    }
	}
    } else {
	// create the password property
	if (bindio_add_prop (obj.id, "PASSWORD", 0, 0x44, NULL, &err) < 0) {
	    // this failed. complain
	    ncp_send_compcode (c, err);
	    return;
	}
    }

    // decrypt the new
    nw_decrypt_newpass ((char*)val.data,     pwd,     pwd);
    nw_decrypt_newpass ((char*)val.data + 8, pwd + 8, pwd + 8);

    // update the bindery
    ncp_send_compcode (c, bindio_write_prop (obj.id, "PASSWORD", 1, 0, pwd));
}

/*
 * ncp_handle_scanproperty (int c, char* data, int size)
 *
 * This will handle NCP 23 60 (Scan property) calls.
 *
 */
void
ncp_handle_scanproperty (int c, char* data, int size) {
    uint32	inst;
    uint16	obj_type;
    uint8	obj_len, prop_len;
    char	obj_name[NCP_MAX_FIELD_LEN];
    char	prop_name[NCP_MAX_FIELD_LEN];

    NCP_REQUIRE_LENGTH_MIN (11);
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (prop_name, NCP_MAX_FIELD_LEN);

    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    inst      = GET_BE32 (data); data += 4;
    prop_len  = GET_BE8 (data); data++;
    bcopy (data, prop_name, prop_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 60: Scan property (objtype=0x%x,obj='%s',lastinst=%u,prop='%s')\n", c, obj_type, obj_name, inst, prop_name);

    // XXX
    ncp_send_compcode (c, 0xff);
}

/*
 * ncp_handle_serialno (int c, char* data, int size)
 *
 * This will handle NCP 23 18 (Get Network Serial Number) calls.
 *
 */
void
ncp_handle_serialno (int c, char* data, int size) {
    NCP_REPLY		r;
    NCP_SERIALNO*	sn;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 18: Get network serial number\n", c);

    sn = (NCP_SERIALNO*)r.data;
    sn->serial_no[0] = (uint8)(conf_serialno >> 24);
    sn->serial_no[1] = (uint8)(conf_serialno >> 16);
    sn->serial_no[2] = (uint8)(conf_serialno >>  8);
    sn->serial_no[3] = (uint8)(conf_serialno & 0xff);
    sn->app_no[0] = (uint8)(conf_appno >> 8);
    sn->app_no[1] = (uint8)(conf_appno & 0xff);

    // send the reply
    ncp_send_reply (c, &r, sizeof (NCP_SERIALNO), 0);
}

/*
 * ncp_handle_fsdesc (int c, char* data, int size)
 *
 * This will handle NCP 23 201 (Get file server description strings) calls. 
 *
 */
void
ncp_handle_fsdesc (int c, char* data, int size) {
    NCP_REPLY	r;
    int		i;

    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 201: Get file server description strings\n", c);
 
    // XXX: length?
    bzero (r.data, 512); i = 0;
    strcpy (r.data, FS_DESC_COMPANY); i += strlen (FS_DESC_COMPANY) + 1;
    strcpy ((r.data + i), FS_DESC_REVISION); i += strlen (FS_DESC_REVISION) + 1;
    strcpy ((r.data + i), FS_DESC_REVDATE); i += strlen (FS_DESC_REVDATE) + 1;
    strcpy ((r.data + i), FS_DESC_COPYRIGHT);

    // send the reply
    ncp_send_reply (c, &r, 512, 0);
}

/*
 * ncp_handle_downserver (int c, char* data, int size)
 *
 * This will handle NCP 23 211 (Down file server) calls.
 *
 */
void
ncp_handle_downserver (int c, char* data, int size) {
    uint8	force_flag;
    BINDERY_OBJ obj;

    NCP_REQUIRE_LENGTH (2);

    force_flag = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 211: Down file server (forceflag=%u)\n", c, force_flag);

    // are we a supervisor?
    if (!conn[c]->object_supervisor) {
	// no. complain
	ncp_send_compcode (c, CONN_ERROR_NOCONSRIGHTS);
	return;
    }

    // grab the object name
    if (bindio_scan_object_by_id (conn[c]->object_id, NULL, &obj) < 0) {
	// use a dummy name
	bzero (&obj, sizeof (BINDERY_OBJ));
	strcpy ((char*)obj.name, "???");
    }

    // okay, let's kill the beast
    DPRINTF (0, LOG_NCP, "[NOTICE] Server going down in %u seconds, by the request of %s[%u]\n", SERVER_DOWN_TIMEOUT, obj.name, c);

    // dieeeee! [TODO]
    //down_server();

    // inform the client
    ncp_send_compcode (c, 0);
}

/*
 * ncp_get_interaddr_old (int c, char* data, int size)
 *
 * This will handle NCP 23 19 (Get internet address) calls.
 *
 */
void
ncp_get_interaddr_old (int c, char* data, int size) {
    uint8 		i;
    NCP_REPLY		r;
    NCP_INTERADDROLD*	ia;

    NCP_REQUIRE_LENGTH (2);

    i = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 19: Get internet address (c=%u)\n", c, i);

    // XXX: check connection range

    ia = (NCP_INTERADDROLD*)r.data;
    #ifndef LINUX
    bcopy (&IPX_NET (conn[i]->addr.sipx_addr), ia->net_addr, 4);
    bcopy (&IPX_HOST (conn[i]->addr.sipx_addr), ia->node_addr, 6);
    #else
    // TODO!
    #endif

    ncp_send_reply (c, &r, sizeof (NCP_INTERADDROLD), 0);
}

/*
 * ncp_handle_addobjtoset (int c, char* data, int size)
 *
 * This will handle NCP 23 65 (Add bindery object to set) calls.
 *
 */
void
ncp_handle_addobjtoset (int c, char* data, int size) {
    uint16		obj_type, mem_type;
    uint8		obj_len, prop_len, mem_len;
    char		obj_name[NCP_MAX_FIELD_LEN];
    char		prop_name[NCP_MAX_FIELD_LEN];
    char		mem_name[NCP_MAX_FIELD_LEN];

    // XXX: LENGTH!!!
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (prop_name, NCP_MAX_FIELD_LEN);
    bzero (mem_name, NCP_MAX_FIELD_LEN);

    obj_type  = GET_BE16 (data); data += 2;

    obj_len = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    prop_len = GET_BE8 (data); data++;
    bcopy (data, prop_name, prop_len); data += prop_len;
    
    mem_type  = GET_BE16 (data); data += 2;
    mem_len   = GET_BE8 (data); data++;
    bcopy (data, mem_name, mem_len); data += mem_len;

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 65: Add bindery object to set (objtype=0x%x,obj='%s',prop='%s',memtype=0x%x,mem='%s')\n", c, obj_type, obj_name, prop_name, mem_type, mem_name);

    ncp_send_compcode (c, bind_add_remove_objfromset (c, obj_type, obj_name, prop_name, mem_type, mem_name, 0));
}

/*
 * ncp_handle_deleteobjfromset (int c, char* data, int size)
 *
 * This will handle NCP 23 66 (Delete bindery object from set) calls.
 *
 */
void
ncp_handle_deleteobjfromset (int c, char* data, int size) {
    uint16		obj_type, mem_type;
    uint8		obj_len, prop_len, mem_len;
    char		obj_name[NCP_MAX_FIELD_LEN];
    char		prop_name[NCP_MAX_FIELD_LEN];
    char		mem_name[NCP_MAX_FIELD_LEN];

    // XXX: LENGTH!!!
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (prop_name, NCP_MAX_FIELD_LEN);
    bzero (mem_name, NCP_MAX_FIELD_LEN);

    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    prop_len  = GET_BE8 (data); data++;
    bcopy (data, prop_name, prop_len); data += prop_len;
    
    mem_type  = GET_BE16 (data); data += 2;
    mem_len   = GET_BE8 (data); data++;
    bcopy (data, mem_name, mem_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 66: Delete bindery object from set (objtype=0x%x,obj='%s',prop='%s',memtype=0x%x,mem='%s')\n", c, obj_type, obj_name, prop_name, mem_type, mem_name);

    ncp_send_compcode (c, bind_add_remove_objfromset (c, obj_type, obj_name, prop_name, mem_type, mem_name, 1));
}

/*
 * ncp_handle_isobjinset (int c, char* data, int size)
 *
 * This will handle NCP 23 67 (Is bindery object in set) calls.
 *
 */
void
ncp_handle_isobjinset (int c, char* data, int size) {
    uint16		obj_type, mem_type;
    uint8		obj_len, prop_len, mem_len;
    char		obj_name[NCP_MAX_FIELD_LEN];
    char		prop_name[NCP_MAX_FIELD_LEN];
    char		mem_name[NCP_MAX_FIELD_LEN];

    // XXX: LENGTH!!!
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bzero (prop_name, NCP_MAX_FIELD_LEN);
    bzero (mem_name, NCP_MAX_FIELD_LEN);

    obj_type  = GET_BE16 (data); data += 2;

    obj_len = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len); data += obj_len;
    prop_len = GET_BE8 (data); data++;
    bcopy (data, prop_name, prop_len); data += prop_len;
    
    mem_type  = GET_BE16 (data); data += 2;
    mem_len = GET_BE8 (data); data++;
    bcopy (data, mem_name, mem_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 67: Is bindery object to set (objtype=0x%x,obj='%s',prop='%s',memtype=0x%x,mem='%s')\n", c, obj_type, obj_name, prop_name, mem_type, mem_name);

    ncp_send_compcode (c, bind_is_objinset (c, obj_type, obj_name, 0, prop_name, mem_type, mem_name, 0));
}

/*
 * ncp_get_connlist_old (int c, char* data, int size)
 *
 * This will handle NCP 23 21 (Get object connection list) calls.
 *
 */
void
ncp_get_connlist_old (int c, char* data, int size) {
    uint16		obj_type;
    uint8		obj_len;
    char		obj_name[NCP_MAX_FIELD_LEN];
    int	 		i, count;
    BINDERY_OBJ		obj;
    NCP_REPLY		r;

    NCP_REQUIRE_LENGTH_MIN (5);

    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bcopy (data, obj_name, obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 21: Get object connection list (objtype=0x%x,objname='%s')\n", c, obj_type, obj_name);

    // we need the object ID. scan for the object record first
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// there's no such object. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // browse all connections for this ID
    count = 0;
    for (i = 1; i <= conf_nofconns; i++) {
	// does this one match?
	if ((conn[i]->object_id == obj.id) && (conn[i]->object_type == obj_type)) {
	    // yes. do we have space to store it?
	    if (count < 31) {
		// yes. store it
		r.data[count * 4    ] = (uint8)(i >>  24);
		r.data[count * 4 + 1] = (uint8)(i >>  16);
		r.data[count * 4 + 2] = (uint8)(i >>   8);
		r.data[count * 4 + 3] = (uint8)(i & 0xff);
		count++;
	    }
	}
    }

    // ok, fix the count
    r.data[0] = (uint8)count;

    // send the reply
    ncp_send_reply (c, &r, 1 + (count * 4), 0);
}

/*
 * ncp_handle_deleteobj (int c, char* data, int size)
 *
 * This will handle NCP 23 51 (Delete bindery object) calls.
 *
 */
void
ncp_handle_deleteobj (int c, char* data, int size) {
    uint16		obj_type;
    uint8		obj_len;
    char		obj_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ		obj;

    NCP_REQUIRE_LENGTH_MIN (5);

    obj_type  = GET_BE16 (data); data += 2;
    obj_len = GET_BE8 (data); data++;
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    bcopy (data, obj_name, obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 51: Delete bindery object (objtype=0x%x,objname='%s')\n", c, obj_type, obj_name);

    // are we a supervisor?
    if (!conn[c]->object_supervisor) {
	// no. complain
	ncp_send_compcode (c, BINDERY_ERROR_SECURITY);
	return;
    }

    // get the object record
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// this object doesn't exist. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // now, pass it all over to bindio_delete_obj()
    ncp_send_compcode (c, bindio_delete_obj (obj.id));
}

/*
 * ncp_handle_keyedverifypwd (int c, char* data, int size)
 *
 * This will handle NCP 23 74 (Keyed Verify Password) calls.
 *
 */
void
ncp_handle_keyedverifypwd (int c, char* data, int size) {
    uint16		obj_type;
    uint8		obj_len, pwd_len;
    uint8		key[8];
    char		obj_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ		obj;
 
    NCP_REQUIRE_LENGTH_MIN (12);
    bzero (obj_name, NCP_MAX_FIELD_LEN);

    bcopy (data, key, 8); data += 8;
    obj_type  = GET_BE16 (data); data += 2;
    obj_len   = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 74: Keyed verify password (objtype=0x%x,objname='%s')\n", c, obj_type, obj_name);

    // get the object
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// this object can't be found. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // yay, the object exists! check the password
    ncp_send_compcode (c, ((bind_verify_pwd (obj.id, (char*)key, (char*)conn[c]->login_key) < 0) ? 0xff : 0));
}

/*
 * ncp_handle_ismanager (int c, char* data, int size)
 *
 * This will handle NCP 23 73 (Is calling station a manager) calls.
 *
 */
void
ncp_handle_ismanager (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 73: Is calling station a manager\n");

    // it's all up to conn_is_manager
    ncp_send_compcode (c, conn_is_manager (c));
}

/*
 * ncp_handle_closebindery (int c, char* data, int size)
 *
 * This will handle NCP 23 68 (Close Bindery) calls.
 *
 */
void
ncp_handle_closebindery (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 68: Close bindery\n", c);

    // if we are a supervisor, pretend it worked, otherwise give an error
    ncp_send_compcode (c, (conn[c]->object_supervisor) ? 0 : 0xff);
}

/*
 * ncp_handle_openbindery (int c, char* data, int size)
 *
 * This will handle NCP 23 69 (Open Bindery) calls.
 *
 */
void
ncp_handle_openbindery (int c, char* data, int size) {
    NCP_REQUIRE_LENGTH (1);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 69: Open bindery\n", c);

    // if we are a supervisor, pretend it worked, otherwise give an error
    ncp_send_compcode (c, (conn[c]->object_supervisor) ? 0 : 0xff);
}

/*
 * ncp_handle_broadcast_old (int c, char* data, int size)
 *
 * This will handle NCP 23 209 (Send console broadcast) requests.
 *
 */
void
ncp_handle_broadcast_old (int c, char* data, int size) {
    uint8	nofstations;
    uint8	messagelen;
    char	message[CONN_MAX_MSG_LEN];
    uint8*	station;
    uint8	st;
    int		i;

    NCP_REQUIRE_LENGTH_MIN (5);
    bzero (message, CONN_MAX_MSG_LEN);
    nofstations = GET_BE8 (data); data++;
    station = (uint8*)data; data += nofstations;
    messagelen = (uint8)*data; data++;
    bcopy (data, message, messagelen);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 209: Send console broadcast (nofstations=%u,message='%s')\n", c, nofstations, message);

    // are we a console operator?
    if (conn_is_operator (c) < 0) {
	// no. not enough rights, then
	ncp_send_compcode (c, 0xc6);
	return;
    }
 
    // do we need to send it to zero connections?
    if (nofstations) {
	// no. walk the list and stuff this message into every connection's
	// message buffer
        for (i = 0; i < nofstations; i++) {
	   // get the number
	   st = GET_BE8 (station); station++;

	   // XXX: check range

	   // copy the message to the buffer
	   strcpy ((char*)conn[st]->message, message);
        }
    } else {
	// send it to everyone
	for (i = 1; i <= conf_nofconns; i++) {
	    // is this connection in use and with messages enabled?
	    if ((conn[c]->status != CONN_STATUS_UNUSED) && (conn[c]->message_ok)) {
		// yes. copy the message
		strcpy ((char*)conn[i]->message, message);
	    }
	}
    }

    // say it's all OK
    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_getobjconnlist (int c, char* data, int size)
 *
 * This will handle NCP 23 27 (Get object connection list) calls.
 *
 */
void
ncp_handle_getobjconnlist (int c, char* data, int size) {
    uint32		con_no;
    uint32		obj_type;
    uint8		obj_len;
    char  		obj_name[NCP_MAX_FIELD_LEN];
    BINDERY_OBJ 	obj;
    NCP_REPLY		r;
    int			i, count;

    NCP_REQUIRE_LENGTH_MIN (8);
    bzero (obj_name, NCP_MAX_FIELD_LEN);
    con_no = GET_BE32 (data); data += 4;
    obj_type = GET_BE16 (data); data += 2;
    obj_len = GET_BE8 (data); data++;
    bcopy (data, obj_name, obj_len);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 27: Get object connection list (con=%u,objtype=0x%x,objname='%s')\n", c, con_no, obj_type, obj_name);

    // scan for the object
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	// this failed. complain
	ncp_send_compcode (c, BINDERY_ERROR_NOSUCHOBJECT);
	return;
    }

    // now, scan the entire connections buffer for this object
    count = 0;
    for (i = (con_no + 1); i <= conf_nofconns; i++) {
	// does this object use this object ID?
	DPRINTF (10, LOG_NCP, "[DEBUG] 0x%x VS 0x%x\n", conn[i]->object_id, obj.id);
	if ((conn[i]->object_id == obj.id) && (count < 30)) {
	    // yes. add it to the object
	    r.data[1 + (count * 4) + 3] = (obj.id >>  24);
	    r.data[1 + (count * 4) + 2] = (obj.id >>  16);
	    r.data[1 + (count * 4) + 1] = (obj.id >>   8);
	    r.data[1 + (count * 4)    ] = (obj.id & 0xff);
	    count++;
	}
    }

    // fill in the number of results
    r.data[0] = (uint8)(count & 0xff);

    // send the reply
    ncp_send_reply (c, &r, (count * 4) + 1, 0);
}

/*
 * ncp_handle_scanobjtrusteepaths (int c, char* data, int size)
 *
 * This will handle NCP 23 71 (Scan bindery object trustee paths) calls.
 *
 */
void
ncp_handle_scanobjtrusteepaths (int c, char* data, int size) {
    uint8	volno;
    uint16	seqno;
    uint32	objid;

    NCP_REQUIRE_LENGTH (8);

    volno = GET_BE8  (data); data++;
    seqno = GET_BE16 (data); data += 2;
    objid = GET_BE32 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 71: Scan bindery object trustee paths (volno=%u,seqno=%u,objid=0x%x)\n", c, volno, seqno, objid);

    // check security: is the object ID simular to our own?
    if (objid != conn[c]->object_id) {
	// no. perhaps we are security equalivant to that user?
	if (bind_is_objinset (0, 0, NULL, conn[c]->object_id, "SECURITY_EQUALS", 0, NULL, objid) != BINDERY_ERROR_OK) {
	    // no, we're not... perhaps we are a supervisor, then ?
	    if (!conn[c]->object_supervisor) {
		// we're not. ok, access is denied
		ncp_send_compcode (c, BINDERY_ERROR_SECURITY);
		return;
	    }
	}
    }

    // XXX: TODO 
    ncp_send_compcode (c, 0xff);
}

/*
 * ncp_handle_clearconn (int c, char* data, int size)
 *
 * This will handle NCP 23 210 (Clear connection number) calls.
 *
 */
void
ncp_handle_clearconn (int c, char* data, int size) {
    uint8 con_no;

    NCP_REQUIRE_LENGTH (2);
    con_no = GET_BE8 (data);

    DPRINTF (9, LOG_NCP, "[INFO] [%u] NCP 23 210: Clear connection number (c=%u)\n", c, con_no);

    // XXX: connection number range!

    // is this user a supervisor?
    if (!conn[c]->object_supervisor) {
	// no. tell the user to take a hike
	ncp_send_compcode (c, CONN_ERROR_NOCONSRIGHTS);
	return;
    }

    // zap the user
    conn_clear (con_no);

    // it's ok
    ncp_send_compcode (c, 0);
}

/*
 * ncp_handle_r23 (int c, char* data, int size)
 *
 * This will handle [size] bytes of NCP 23 subfunction packet [data] for
 * connection [c].
 * 
 */
void
ncp_handle_r23 (int c, char* data, int size) {
    uint8 sub_func = *(data + 2);
    uint16 sub_size;

    // get the substructure size
    sub_size = (uint8)((*(data)) << 8)| (uint8)(*(data + 1));

    // does this size match up?
    if ((sub_size + 2) != size) {
	// no. complain
	DPRINTF (6, LOG_NCP, "[WARN] [%u] Got NCP 23 %u packet with size %u versus %u expected, adjusting\n", c, sub_func, sub_size + 2, size);
	sub_size = (size - 2);
    }

    // fix up the buffer offset
    data += 3;

    // handle the subfunction
    switch (sub_func) {
	case 17: // get file server information
		 ncp_handle_server_info (c, data, sub_size);
	 	 break;
	case 18: // get network serial number
		 ncp_handle_serialno (c, data, sub_size);
	 	 break;
	case 19: // get internet address
		 ncp_get_interaddr_old (c, data, sub_size);
		 break;
	case 21: // get object connection list
		 ncp_get_connlist_old (c, data, sub_size);
		 break;
	case 22: // get station's logged info (old)
		 ncp_handle_getloggedinfo2 (c, data, sub_size);
		 break;
	case 23: // get login key
		 ncp_handle_loginkey (c, data, sub_size);
	 	 break;	
	case 24: // keyed object login
	 	 ncp_handle_keyedlogin (c, data, sub_size);
		 break;
	case 26: // get internet address
		 ncp_handle_getinteraddr (c, data, sub_size);
		 break;
	case 27: // get object connection list
		 ncp_handle_getobjconnlist (c, data, sub_size);
		 break;
	case 28: // get station's logged info
		 ncp_handle_getloggedinfo (c, data, sub_size);
		 break;
	case 50: // create bindery object
		 ncp_handle_createobj (c, data, sub_size);
		 break;
	case 51: // delete bindery object
		 ncp_handle_deleteobj (c, data, sub_size);
		 break;
	case 53: // get bindery object id
		 ncp_handle_getobjid (c, data, sub_size);
		 break;
	case 54: // get bindery object name
		 ncp_handle_getobjname (c, data, sub_size);
		 break;
	case 55: // scan bindery object
		 ncp_handle_scanobj (c, data, sub_size);
		 break;
	case 57: // create property
		 ncp_handle_createprop (c, data, sub_size);
		 break;
	case 60: // scan property
		 ncp_handle_scanproperty (c, data, sub_size);
		 break;
	case 61: // read property value
	  	 ncp_handle_readprop (c, data, sub_size);
		 break;
	case 62: // write property value
	  	 ncp_handle_writeprop (c, data, sub_size);
		 break;
	case 65: // add bindery object to set
		 ncp_handle_addobjtoset (c, data, sub_size);
		 break;
	case 66: // delete bindery object from set
		 ncp_handle_deleteobjfromset (c, data, sub_size);
		 break;
	case 67: // is bindery object in set?
		 ncp_handle_isobjinset (c, data, sub_size);
		 break;
	case 68: // close bindery
		 ncp_handle_closebindery (c, data, sub_size);
		 break;
	case 69: // open bindery
		 ncp_handle_openbindery (c, data, sub_size);
		 break;
	case 70: // get bindery access level
		 ncp_handle_accesslevel (c, data, sub_size);
		 break;
	case 71: // scan bindery object trustee paths
		 ncp_handle_scanobjtrusteepaths (c, data, sub_size);
		 break;
	case 72: // get bindery access level to an object
		 ncp_handle_objaccesslevel (c, data, sub_size);
		 break;
	case 73: // is calling station a manager
		 ncp_handle_ismanager (c, data, sub_size);
		 break;
	case 74: // keyed verify password
		 ncp_handle_keyedverifypwd (c, data, sub_size);
		 break;
	case 75: // keyed change password
		 ncp_handle_keyedchangepwd (c, data, sub_size);
		 break;
       case 200: // check console privileges
		 ncp_handle_consoleprivs (c, data, sub_size);
		 break;
       case 201: // get file server description strings
		 ncp_handle_fsdesc (c, data, sub_size);
		 break;
       case 205: // get file server login status
		 ncp_handle_getloginstat (c, data, sub_size);
		 break;
       case 209: // send console broadcast
	 	 ncp_handle_broadcast_old (c, data, sub_size);
		 break;
       case 210: // clear connection number
		 ncp_handle_clearconn (c, data, sub_size);
		 break;
       case 211: // down file server
		 ncp_handle_downserver (c, data, sub_size);
		 break;
	default: // what's this?
		 DPRINTF (6, LOG_NCP, "[INFO] [%u] Unsupported NCP function 23 %u, ignored\n", c, sub_func);
		 ncp_send_compcode (c, 0xff);
	 	 break;
    }
}
