/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "net.h"
#include "misc.h"
#include "sap.h"

/*
 * bind_init()
 *
 * This will initialize the bindery services.
 *
 */
void
bind_init() {
    int objid;
    NETOBJ_RECORD obj;
    BINDERY_NETADDR* na;
    char data[128];
    uint8 i;

    // try to open the bindery files
    if (bindio_open("r+b") < 0) 
	// this failed. need to re-recreate them ?
        if (frommel_options & FROMMEL_RECREATE_BINDERY) {
	    // yes. try to create them
	    if (bindio_open ("w+b") < 0) {
		// this failed. complain
		fprintf (stderr, "[FATAL] bind_init(): cannot create bindery files\n");
		exit (1);
	    }
	} else {
	    // no. leave
	    fprintf (stderr, "[FATAL] bind_init(): cannot open bindery files\n");
	    exit (1);
	}

    // need to recreate the bindery?
    if (frommel_options & FROMMEL_RECREATE_BINDERY) {
	// yes. do it
	fprintf (stderr, "[INFO] bind_init(): re-creating bindery...");

	// clean the bindery
	bindio_truncate();

	// create the SUPERVISOR object
	if ((objid = bindio_add_object ("SUPERVISOR", 0x1, 0, 0x31, &i)) < 0) {
	    fprintf (stderr, "[FATAL] Can't add user object 'SUPERVISOR', error 0x%x\n", i);
	    exit (1);
	}

	// fix up the supervisor, to have that legendary ID of 1 :)
	if ((i = bindio_change_object (objid, 1, NULL)) != BINDERY_ERROR_OK) {
	    fprintf (stderr, "[FATAL] Can't change user object ID of object 'SUPERVISOR' to 1, error 0x%x\n", i);
	    exit (1);
	}
	objid = 1;

	// create the password property
	if (bindio_add_prop (objid, "PASSWORD", 0, 0x44, NULL, &i) < 0) {
	    fprintf (stderr, "[FATAL] Cannot create 'PASSWORD' property in object 'SUPERVISOR': error 0x%x\n", i);
	    exit (1);
	}

	// create the identification property
	bzero (&data, 128); strcpy (data, "Super User");
	if (bindio_add_prop (objid, "IDENTIFICATION", 0, 0x31, data, &i) < 0) {
	    fprintf (stderr, "[FATAL] Cannot create 'IDENTIFICATION' property in object 'SUPERVISOR': error 0x%x\n", i);
	    exit (1);
	}

	// create the 'SECURITY_EQUALS' property
	if (bindio_add_prop (objid, "SECURITY_EQUALS", BINDERY_FLAG_SET, 0x32, NULL, &i) < 0) {
	    fprintf (stderr, "[FATAL] Cannot create 'SECURITY_EQUALS' property in object 'SUPERVISOR': error 0x%x\n", i);
	    exit (1);
	}

	// create the 'GROUPS_I'M_IN' property
	if (bindio_add_prop (objid, "GROUPS_I'M_IN", BINDERY_FLAG_SET, 0x32, NULL, &i) < 0) {
	    fprintf (stderr, "[FATAL] Cannot create 'GROUPS_I'M_IN' property in object 'SUPERVISOR': error 0x%x\n", i);
	    exit (1);
	}

	// finally, change the password
	if (bind_set_pwd (objid, "") < 0) {
	    fprintf (stderr, "[FATAL] Can't set SUPERVISOR password\n");
	    exit (1);
	}

	// create the 'EVERYONE' group
	if ((objid = bindio_add_object ("EVERYONE", 0x2, 0, 0x31, &i)) < 0) {
	    fprintf (stderr, "[FATAL] Can't add group 'EVERYONE', error 0x%x\n", i);
	    exit (1);
	}

	// create the 'IDENTIFICATION' property
	bzero (&data, 128); strcpy (data, "Everyone group");
	if (bindio_add_prop (objid, "IDENTIFICATION", BINDERY_FLAG_SET, 0x31, data, &i) < 0) {
	    fprintf (stderr, "[FATAL] Cannot create 'IDENTIFICATION' property in object 'EVERYONE': error 0x%x\n", i);
	    exit (1);
	}

	// create the 'GROUP_MEMBERS' property
	bzero (&data, 128);
	if (bindio_add_prop (objid, "GROUP_MEMBERS", BINDERY_FLAG_SET, 0x31, data, &i) < 0) {
	    fprintf (stderr, "[FATAL] Cannot create 'GROUP_MEMBERS' property in object 'EVERYONE': error 0x%x\n", i);
	    exit (1);
	}

	// create the server object
        if ((objid = bindio_add_object (conf_servername, 0x4, 0, 0x40, &i)) < 0) {
	    fprintf (stderr, "[FATAL] Can't add server object '%s', error 0x%x\n", conf_servername, i);
	    exit (1);
	}

	// create the 'NET_ADDRESS' property
	bzero (&data, 128);
	na = (BINDERY_NETADDR*)&data;
	na->sock_addr[0] = (IPXPORT_NCP >> 8);
	na->sock_addr[1] = (IPXPORT_NCP & 0xff);
	bcopy (IPX_NET (server_addr), na->net_addr, 4);
	bcopy (IPX_HOST (server_addr), na->node_addr, 6);
	if (bindio_add_prop (objid, "NET_ADDRESS", 0, 0x40, data, &i) < 0) {
	    fprintf (stderr, "[FATAL] Cannot create 'NET_ADDRESS' property in object '%s': error 0x%x\n", conf_servername, i);
	    exit (1);
	}

	fprintf (stderr, " done\n");
    }
    
    bindio_dump();
}

/*
 * bind_add_remove_objfromset (int c, uint16 obj_type, char* obj_name,
 *			         char* propname, uint16 memtype, char* memname,
 *			         uint8 add_del)
 *
 * This will either add or delete object [mem_name] to list property [prop_name]
 * of object [obj_name]. [obj_type] is the type of object [obj_name], and
 * [mem_type] is the type of object [mem_name]. If [add_del] is zero, the object
 * will be added, otherwise it will be deleted. This function will return
 * BINDERY_ERROR_OK on success or BINDERY_ERROR_xxx on failure. If [c] is not
 * zero, it will be used as a connection to verify the permissions.
 *
 */
int
bind_add_remove_objfromset (int c, uint16 obj_type, char* obj_name, char* prop_name, uint16 mem_type, char* mem_name, uint8 add_del) {
    BINDERY_OBJ		obj, mem;
    BINDERY_PROP	prop;
    BINDERY_VAL		val;
    uint32		no;
    int			i, ok = 0;

    // first, we need the object itself
    if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0)
	// this failed. there's no such object
	return BINDERY_ERROR_NOSUCHOBJECT;

    // grab the property
    if (bindio_scan_prop (obj.id, prop_name, &prop) < 0)
	// this failed. there's no such property
	return BINDERY_ERROR_NOSUCHPROPERTY;

    // do we have a connection?
    if (c > 0)
	// yes. do we have permissions to write here?
	if (bind_check_access (c, obj.id, prop.security, 1) < 0)
	    // no. complain
	    return BINDERY_ERROR_SECURITY;

    // okay, grab the member's object now
    if (bindio_scan_object (mem_name, mem_type, NULL, &mem) < 0)
	// this failed. theres no such objectj
	return BINDERY_ERROR_NOSUCHOBJECT;

    // grab the value
    if (bindio_scan_val (prop.value_id, &val) < 0)
	// this failed. there's no such property
	return BINDERY_ERROR_NOSUCHPROPERTY;

    // okay, now [mem.id] has to be added to [prop.data]. scan the entire
    // property for a blank space
    ok = 0;
    for (i = 0; i < (128 / 4); i++) {
	// build the number
	no  = (uint8)(val.data[i    ]) << 24;
	no |= (uint8)(val.data[i + 1]) << 16;
	no |= (uint8)(val.data[i + 2]) <<  8;
	no |= (uint8)(val.data[i + 3]);

	// do we need to delete the value ?
	if (add_del) {
	    // yes. do the ID codes match up ?
	    if (no == mem.id) {
		// yes. get rid of it
		val.data[i    ] = 0; val.data[i + 1] = 0;
		val.data[i + 2] = 0; val.data[i + 3] = 0;

		// we did something
		ok = 1;
	    }
	} else {
	    // is it already there without us adding something?
	    if ((no == mem.id) && (!ok))
		// yes. complain (XXX)
		return BINDERY_ERROR_IOERROR;

	    // is it blank and have we not yet added something ?
	    if ((!no) && (!ok)) {
		// yes. insert the value
		val.data[i    ] = (uint8)(mem.id >>  24);
		val.data[i + 1] = (uint8)(mem.id >>  16);
		val.data[i + 2] = (uint8)(mem.id >>   8);
		val.data[i + 3] = (uint8)(mem.id & 0xff);

		// we did something
		ok = 1;
	    }
	}
    }

    // ok, did this work?
    if (!ok) {
	// no. return an error code (XXX)
	DPRINTF (10, LOG_BINDERY, "[DEBUG] bind_add_remove_objfromset(): list full or member not found\n");
	return BINDERY_ERROR_IOERROR;
    }

    // this seemed to work. now, overwrite the property
    return bindio_write_prop (obj.id, prop_name, 1, 0, (char*)val.data);
}

/*
 * bind_is_objinset (int c, uint16 obj_type, char* obj_name, uint32 obj_id,
 *		     char* prop_name, uint16 mem_type, char* mem_name,
 *		     uint32 mem_id)
 *
 * This will check whether object [mem_name] of type [mem_type] can be found
 * within property [prop_name] of object [obj_name], type [obj_type]. It will
 * return BINDERY_ERROR_OK on success or BINDERY_ERROR_xxx on failure. If
 * [mem_name] is NULL, ID [mem_id] will be used. If [obj_name] is NULL, ID
 * [obj_id] will be used. If [c] is not zero, it will be used to check
 * permissions.
 *
 */
int
bind_is_objinset (int c, uint16 obj_type, char* obj_name, uint32 obj_id, char* prop_name, uint16 mem_type, char* mem_name, uint32 mem_id) {
    BINDERY_OBJ		obj, mem;
    BINDERY_PROP	prop;
    BINDERY_VAL		val;
    uint32		no;
    int			i, ok = 0;

    // is the object name a NULL?
    if (obj_name != NULL) {
        // no. grab the object itself
	if (bindio_scan_object (obj_name, obj_type, NULL, &obj) < 0) {
	    // this failed. there's no such object
	    return BINDERY_ERROR_NOSUCHOBJECT;
	}
    } else {
	// yes. use the ID number passed
	obj.id = obj_id;
    }

    // grab the property
    if (bindio_scan_prop (obj.id, prop_name, &prop) < 0) {
	// this failed. there's no such property
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // do we need to verify the security?
    if (c > 0) {
	// yes. do we have enough rights to read this property?
	if (bind_check_access (c, obj.id, prop.security, 0) < 0) {
	    // no. complain
	    return BINDERY_ERROR_SECURITY;
	}
    }

    // is the member object a NULL?
    if (mem_name != NULL) {
       // no. grab the member's object now
       if (bindio_scan_object (mem_name, mem_type, NULL, &mem) < 0)
	    // this failed. theres no such objectj
	    return BINDERY_ERROR_NOSUCHOBJECT;
    } else {
	// yes. use the ID number passed
	mem.id = mem_id;
    }

    // grab the value
    if (bindio_scan_val (prop.value_id, &val) < 0)
	// this failed. there's no such property
	return BINDERY_ERROR_NOSUCHPROPERTY;

    // okay, now [mem.id] could be in [val.data]. scan the entire
    // property
    ok = 0;
    for (i = 0; i < (128 / 4); i++) {
	// build the number
	no  = (uint8)(val.data[i    ]) << 24;
	no |= (uint8)(val.data[i + 1]) << 16;
	no |= (uint8)(val.data[i + 2]) <<  8;
	no |= (uint8)(val.data[i + 3]);

	// is this object here?
	if (no == mem.id)
	    // yes. return positive status
	    return BINDERY_ERROR_OK;
    }

    // hmm... no such object. complain
    return BINDERY_ERROR_NOSUCHMEMBER;
}

/*
 * bind_obj_is_supervisor (uint32 objid)
 *
 * This will verify whether object [objid] is a supervisor or a supervisor
 * equivelant user. It will return 0 on success (user is a supervisor) or -1
 * on failure.
 *
 */
int
bind_obj_is_supervisor (uint32 objid) {
    NETVAL_RECORD	valrec;

    // if the user ID is 1, the user is always a supervisor
    if (objid == 1) return 0;

    // we are a supervisor if object ID 1 is listed in the SECURITY_EQUALS
    // property. check this
    if (bind_is_objinset (0, 0, NULL, objid, "SECURITY_EQUALS", 0, NULL, 1) == BINDERY_ERROR_OK)
	// yay, we're a supervisor...
	return 0;

    // the user is not a supervisor.
    return -1;
}

/*
 * bind_check_access (int c, uint32 objid, int8 security, int rw)
 *
 * This will verify the access of connection [c] to object [objid]. [security]
 * should be the security mask. [rw] should be 0 if reading and 1 if writing.
 * This will return -1 on failure (access denied) or 0 on success.
 *
 */
int
bind_check_access (int c, uint32 objid, uint8 security, int rw) {
    uint8	mask = security;

    // are we an object supervisor of this object ?
    if (bind_is_objinset (0, 0, NULL, objid, "OBJ_SUPERVISORS", 0x1, NULL, conn[c]->object_id) == BINDERY_ERROR_OK)
	// yes. we can do whatever we want
	return 0;

    // need to write?
    if (rw)
	// yes. we need the lower nibble
	mask >>= 4;
    else
	// no. we need the upper nibble
	mask &= 0xf;

    // figure out the rights
    switch (mask) {
	 case 0x0: // anyone
	  	   return 0;
	 case 0x1: // logged in only
		   return (conn[c]->object_id == 0xffffffff) ? -1 : 0;
	 case 0x2: // object itself and up only
		   if (conn[c]->object_supervisor) return 0;

		   return (conn[c]->object_id == objid) ? 0 : -1;
	 case 0x3: // supervisor only
		   return (conn[c]->object_supervisor) ? 0 : -1;
	 case 0x4: // OS only
		   return 0;
    }

    // for anything else, deny access
    DPRINTF (7, LOG_BINDERY, "[WARN] Invalid security mask 0x%x for object 0x%x\n", mask, objid);
    return -1;
}

/*
 * bind_verify_pwd (uint32 obj_id, char* pwd, char* key)
 *
 * This will verify the crypted password [pwd] using key [key] against object
 * ID [obj_id]. It will return zero on success or -1 on failure.
 *
 */
int
bind_verify_pwd (uint32 obj_id, char* pwd, char* key) {
    char		tmp[8];
    uint8		s_uid[4];
    BINDERY_PROP	prop;
    BINDERY_VAL		val;

    // first of all, grab the PASSWORD property
    if (bindio_read_prop (obj_id, "PASSWORD", 1, &val, &prop) != BINDERY_ERROR_OK) {
	// this failed. complain
	return -1;
    }

    // encrypt the password and check whether it's ok
    nw_encrypt ((unsigned char*)key, (unsigned char*)val.data, (unsigned char*)tmp);
    return (memcmp (pwd, tmp, 8) == 0) ? 0 : -1;
}

/*
 * bind_set_pwd (uint32 obj_id, char* pwd)
 *
 * This will change the password of object [obj_id] to [pwd]. It will return
 * 0 on success or -1 on failure.
 *
 */
int
bind_set_pwd (uint32 obj_id, char* pwd) {
    uint8	new_pwd[128];
    uint8	s_uid[4];
    int		i;

    s_uid[0] = (uint8)(obj_id >> 24);
    s_uid[1] = (uint8)(obj_id >> 16);
    s_uid[2] = (uint8)(obj_id >>  8);
    s_uid[3] = (uint8)(obj_id & 0xff);

    // XXX: range check ?
    shuffle (s_uid, (unsigned char*)pwd, strlen (pwd), new_pwd);

    // write the hash
    i = bindio_write_prop (obj_id, "PASSWORD", 1, 0, (char*)new_pwd);
    if (i != BINDERY_ERROR_OK) {
	// this failed. complain
	return -1;
    }

    return 0;
}
