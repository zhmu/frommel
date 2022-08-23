/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "defs.h"
#include "net.h"
#include "misc.h"
#include "sap.h"

#ifndef WITH_MYSQL
#ifndef WITH_PGSQL
FILE*	net_obj = NULL;
FILE*	net_prop = NULL;
FILE*	net_val = NULL;

// BINDIO_MODULE is the bindery module we actually use
#define BINDIO_MODULE "binderyio.c"

/*
 * bindio_open(char* mode)
 *
 * This will open the bindery datafiles. This will return zero on success or
 * a negative number of failure. They will be opened using file mode [mode].
 *
 */
int
bindio_open(char* mode) {
    char tmp[1024];			/* XXX */

    fprintf (stderr, "[INFO] Bindery module '%s' initializing\n", BINDIO_MODULE);

    sprintf (tmp, "%s/net$obj.sys", conf_binderypath);
    if ((net_obj = fopen (tmp, mode)) == NULL) {
	fprintf (stderr, "[WARN] bindio_open(): can't open '%s'\n", tmp);
	return -1;
    }

    sprintf (tmp, "%s/net$prop.sys", conf_binderypath);
    if ((net_prop = fopen (tmp, mode)) == NULL) {
	fprintf (stderr, "[WARN] bindio_open(): can't open '%s'\n", tmp);
	return -1;
    }

    sprintf (tmp, "%s/net$val.sys", conf_binderypath);
    if ((net_val = fopen (tmp, mode)) == NULL) {
	fprintf (stderr, "[WARN] bindio_open(): can't open '%s'\n", tmp);
	return -1;
    }

    // it worked
    return 0;
}

/*
 * bindio_conv_object (NETOBJ_RECORD* objrec, BINDERY_OBJ* obj)
 *
 * This will convert the disk structure [objrec] to an useful structure [obj].
 *
 */
void
bindio_conv_object (NETOBJ_RECORD* objrec, BINDERY_OBJ* obj) {
    bzero (obj, sizeof (BINDERY_OBJ));

    obj->id = (objrec->object_id[0] << 24) |
	      (objrec->object_id[1] << 16) |
	      (objrec->object_id[2] <<  8) | 
	      (objrec->object_id[3]);
    obj->type = (objrec->object_type[0] << 8) |
		(objrec->object_type[1]);
    bcopy (&objrec->object_name, &obj->name, 47);
    obj->name[objrec->object_namelen] = 0;
    obj->flags = objrec->object_flags;
    obj->security = objrec->object_security;
    obj->prop_id = (objrec->prop_id[0] << 24) |
	     	   (objrec->prop_id[1] << 16) |
	      	   (objrec->prop_id[2] <<  8) | 
	      	   (objrec->prop_id[3]);
}

/*
 * bindio_conv_prop (NETPROP_RECORD* proprec, BINDERY_PROP* prop)
 *
 * This will convert the disk structure [proprec] to an useful structure [prop].
 *
 */
void
bindio_conv_prop (NETPROP_RECORD* proprec, BINDERY_PROP* prop) {
    bzero (prop, sizeof (BINDERY_PROP));

    prop->id = (proprec->prop_id[0] << 24) |
	       (proprec->prop_id[1] << 16) |
	       (proprec->prop_id[2] <<  8) |
	       (proprec->prop_id[3]);
    bcopy (&proprec->prop_name, &prop->name, 15); 
    prop->name[proprec->prop_namelen] = 0;
    prop->flags = proprec->prop_flag;
    prop->security = proprec->prop_security;
    prop->object_id = (proprec->prop_objid[0] << 24) |
	       	      (proprec->prop_objid[1] << 16) |
	       	      (proprec->prop_objid[2] <<  8) |
	       	      (proprec->prop_objid[3]);
    prop->value_id = (proprec->prop_valueid[0] << 24) |
	       	     (proprec->prop_valueid[1] << 16) |
	       	     (proprec->prop_valueid[2] <<  8) |
	       	     (proprec->prop_valueid[3]);
    prop->next_id = (proprec->prop_nextid[0] << 24) |
	       	    (proprec->prop_nextid[1] << 16) |
	       	    (proprec->prop_nextid[2] <<  8) |
	       	    (proprec->prop_nextid[3]);
}

/*
 * bindio_conv_val (NETVAL_RECORD* valrec, BINDERY_VAL* val)
 *
 * This will convert the disk structure [valrec] to an useful structure [val].
 *
 */
void
bindio_conv_val (NETVAL_RECORD* valrec, BINDERY_VAL* val) {
    bzero (val, sizeof (BINDERY_VAL));

    val->id = (valrec->val_id[0] << 24) |
	      (valrec->val_id[1] << 16) |
	      (valrec->val_id[2] <<  8) |
	      (valrec->val_id[3]);
    val->prop_id = (valrec->val_propid[0] << 24) |
	           (valrec->val_propid[1] << 16) |
	           (valrec->val_propid[2] <<  8) |
	           (valrec->val_propid[3]);
    val->next_id = (valrec->val_nextid[0] << 24) |
	           (valrec->val_nextid[1] << 16) |
	           (valrec->val_nextid[2] <<  8) |
	           (valrec->val_nextid[3]);
    val->size = (valrec->val_size[0] << 8) |
		(valrec->val_size[1]);
    bcopy (&valrec->val_data, &val->data, 128);
}

/*
 * bindio_prop_seek (uint32 id, NETPROP_RECORD* proprec, BINDERY_PROP* prop)
 *
 * This will seek to property id [id] within the property file. It will return
 * zero on success and -1 on failure. [proprec] will hold the record of the
 * property on success, and [prop] will hold the converted structure. If
 * one of these fields is NULL, a local temp variable will be used.
 *
 */
int
bindio_prop_seek (uint32 id, NETPROP_RECORD* proprec, BINDERY_PROP* prop) {
    uint32 propid;
    BINDERY_PROP lprop;
    NETPROP_RECORD lproprec;
    BINDERY_PROP* vprop = &lprop;
    NETPROP_RECORD* vproprec = &lproprec;

    // if a field is given, use that
    if (prop) vprop = prop;
    if (proprec) vproprec = proprec;

    // seek from the beginning
    rewind (net_prop);
    while (fread (vproprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	// convert the structure
	bindio_conv_prop (vproprec, vprop);

	// is the ID valid?
	if (vprop->id == id) {
	    // we got the property!
	    return 0;
	}
    }

    // the property was not found
    return -1;
}

/*
 * bindio_dump()
 *
 * This will dump all bindery objects and properties.
 *
 */
void
bindio_dump() {
    NETOBJ_RECORD objrec;
    BINDERY_OBJ	  obj;
    BINDERY_PROP  prop;
    uint32 propid, objid, i;

    // grab all objects
    rewind (net_obj);
    while (fread (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	bindio_conv_object (&objrec, &obj);
	printf ("- Object '%s' (id=0x%x,security=0x%x,type=0x%x)\n", obj.name, obj.id, obj.security, obj.type);

	propid = obj.prop_id;
	while (bindio_prop_seek (propid, NULL, &prop) == 0) {
	    propid = prop.next_id;

	    printf ("    - Property '%s' (id=0x%x,security=0x%x,nextid=0x%x)\n", prop.name, prop.id, prop.security, prop.next_id);
	}
    }
}

/*
 * bindio_scan_object (char* objname, uint16 type, NETOBJ_RECORD* objrec,
 *		       BINDERY_OBJ* obj)
 *
 * This will scan the bindery for object [objname], type [type]. If it is
 * found, this will set [objrec] to the object record and [obj] to the converted
 * object record and return 0. on any failure, -1 will be returned. If any
 * of the pointers is NULL, a local pointer will be used.
 *
 */
int
bindio_scan_object (char* objname, uint16 type, NETOBJ_RECORD* objrec, BINDERY_OBJ* obj) {
    NETOBJ_RECORD lor;
    BINDERY_OBJ lob;
    NETOBJ_RECORD* orj = &lor;
    BINDERY_OBJ* ob = &lob;

    // if we have any not-null pointers, use those
    if (objrec) orj = objrec;
    if (obj) ob = obj;

    // walk through the bindery file
    rewind (net_obj);
    while (fread (orj, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	/* convert the disk record to something useful */
	bindio_conv_object (orj, ob);

	/* compare the object's name and type */
	if (!strncmp (objname, (char*)ob->name, 48) && (ob->type == type)) {
	    // this matches. we found the object
	    return 0;
	}
    }

    // no matches
    return -1;
}

/*
 * bindio_scan_object_by_id (uint32 objid, NETVAL_OBJ* objrec, BINDERY_OBJ* obj)
 *
 * This will scan the bindery for object [objid]. If it is found, this will set
 * [objrec] to the object record and [obj] to the converted record and return 0.
 * On any failure, -1 will be returned. If any pointer is null, a local variable
 * will be used.
 *
 */
int
bindio_scan_object_by_id (uint32 objid, NETOBJ_RECORD* objrec, BINDERY_OBJ* obj) {
    BINDERY_OBJ bobj;
    NETOBJ_RECORD bobjrec;
    BINDERY_OBJ* vobj = &bobj;
    NETOBJ_RECORD* vobjrec = &bobjrec;

    // use the non-null arguments, if present
    if (objrec) vobjrec = objrec;
    if (obj) vobj = obj;

    // walk through the bindery file
    rewind (net_obj);
    while (fread (vobjrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	// convert the object to something useful
	bindio_conv_object (vobjrec, vobj);

	// is this the object we seek?
	if (vobj->id == objid) {
	    // yes. we got it
	    return 0;
	}
    }

    // the object wasn't found
    return -1;
}

/*
 * bindio_scan_propid (uint32 propid, NETPROP_RECORD* proprec, BINDERY_PROP*
 *		       prop)
 *
 * This will scan the bindery for property ID [propid]. If it is found, this
 * will set [proprec] to the property record and set [prop] to the converted
 * record and return 0. on any failure, -1 will be returned. If any pointer
 * is null, a local variable will be used.
 *
 */
int
bindio_scan_propid (uint32 propid, NETPROP_RECORD* proprec, BINDERY_PROP* prop) {
    NETPROP_RECORD  lproprec;
    BINDERY_PROP    lprop;
    NETPROP_RECORD* vproprec = &lproprec;
    BINDERY_PROP*   vprop = &lprop;
    uint16 i;

    // if a not-null pointer is given, use it
    if (proprec) vproprec = proprec;
    if (prop) vprop = prop;

    // walk through the bindery file
    rewind (net_prop);
    while (fread (vproprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	// convert the property record to something useful
	bindio_conv_prop (vproprec, vprop);

	// is this the property we seek?
	if (vprop->id == propid) {
	    // yes. we got it
	    return 0;
	}
    }

    // the object wasn't found
    return -1;
}

/*
 * bindio_scan_prop_nextid (uint32 propid, NETPROP_RECORD* proprec,
 *			    BINDERY_PROP* prop)
 *
 * This will scan the bindery for a property that has the [nextid] field set
 * to [propid]. If is found, this will set [proprec] to the property record and
 * [prop] to the converted property and return 0. on any failure, -1 will be
 * returned.  *
 */
int
bindio_scan_prop_nextid (uint32 propid, NETPROP_RECORD* proprec, BINDERY_PROP* prop) {
    NETPROP_RECORD lproprec;
    BINDERY_PROP lprop;
    NETPROP_RECORD* vproprec = &lproprec;
    BINDERY_PROP* vprop = &lprop;
    uint16 i;

    // if we have non-null values, use them
    if (proprec) vproprec = proprec;
    if (prop) vprop = prop;

    // walk through the bindery file
    rewind (net_prop);
    while (fread (vproprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	// convert the record to something useful
	bindio_conv_prop (vproprec, vprop);

	// is this the property we seek?
	if (vprop->next_id == propid) {
	    // yes. we got it
	    return 0;
	}
    }

    // the object wasn't found
    return -1;
}

/*
 * bindio_add_object (char* objname, uint16 type, uint8 flags, uint8 security,
 *                    uint8* errcode)
 *
 * This will create a new bindery object, [objname]. It will be given type
 * [type], flags [flags] and security settings [security]. If all went OK, this
 * will return the object ID, otherwise -1. If [errcode] is not NULL, it will
 * be set to the error code.
 *
 */
int
bindio_add_object (char* objname, uint16 type, uint8 flags, uint8 security, uint8* errcode) {
    NETOBJ_RECORD obj;
    int id;

    // do we already have this object?
    if (!bindio_scan_object (objname, type, NULL, NULL)) {
	// yes, we do. complain
	if (errcode) { *errcode = BINDERY_ERROR_OBJEXISTS; };
	return -1;
    }

    // generate an object id (XXX)
    id = random ();

    // it doesn't exist. build the [obj] record
    bzero (&obj, sizeof (NETOBJ_RECORD));
    obj.object_id[0] = (id >> 24); obj.object_id[1] = (id >> 16);
    obj.object_id[2] = (id >>  8); obj.object_id[3] = (id & 0xff);
    obj.object_type[0] = (type >> 8); obj.object_type[1] = (type & 0xff);
    obj.object_namelen = strlen (objname);
    bcopy (objname, &obj.object_name, obj.object_namelen);
    obj.object_flags = flags; obj.object_security = security;
    obj.prop_id[0] = obj.prop_id[1] = obj.prop_id[2] = obj.prop_id[3] = 0xff;

    // now, add the object to the chain
    fseek (net_obj, 0, SEEK_END);
    if (!fwrite (&obj, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	// this failed. return an error
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // this worked perfectly. return the id
    return id;
}

/*
 * bindio_scan_prop (uint32 objid, char* propname, BINDERY_PROP* prop)
 *
 * This will scan the bindery for property [propname] of object [objid]. It will
 * return -1 on failure otherwise 0. [prop] will hold the property record, and
 * is used for disk reads.
 *
 */
int
bindio_scan_prop (uint32 objid, char* propname, BINDERY_PROP* prop) {
    NETPROP_RECORD proprec;

    // walk through the entire property file
    rewind (net_prop);
    while (fread (&proprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	// construct the structure
	bindio_conv_prop (&proprec, prop);

	// is this the property we are looking for?
	if ((prop->object_id == objid) && (!strncmp ((char*)prop->name, propname, 15))) {
	    // yes. we've got it!
	    return 0;
	}
    }

    // the property could not be found
    return -1;
}

/*
 * bindio_add_prop (uint32 objid, char* propname, uint8 flags, uint8 security,
 * char* data, uint8* errcode)
 *
 * This will add a property [propname], which will be given flags [flags] and
 * security [security], to object [objid]. It will assign data [data] to the
 * property. On success, the new property ID is returned, otherwise -1. If
 * [errcode] is not NULL, it will be set to the error code.
 *
 */
int
bindio_add_prop (uint32 objid, char* propname, uint8 flags, uint8 security, char* data, uint8* errcode) {
    BINDERY_OBJ obj;
    NETOBJ_RECORD objrec;
    NETPROP_RECORD proprec;
    NETVAL_RECORD valrec;
    BINDERY_PROP prop;
    uint32 valid, propid, nextpropid;

    // grab the object record
    if (bindio_scan_object_by_id (objid, NULL, &obj) < 0) {
	// there's no such object. complain
	if (errcode) { *errcode = BINDERY_ERROR_NOSUCHOBJECT; };
	return -1;
    }

    // does this property already exist?
    if (!bindio_scan_prop (objid, propname, &prop)) {
	// yes. complain
	if (errcode) { *errcode = BINDERY_ERROR_PROPEXISTS; };
	return -1;
    }

    // get a value and property id (XXX)
    valid = random(); propid = random();

    // build the property record
    bzero (&proprec, sizeof (NETPROP_RECORD));
    proprec.prop_id[0] = (propid >> 24); proprec.prop_id[1] = (propid >> 16);
    proprec.prop_id[2] = (propid >>  8); proprec.prop_id[3] = (propid & 0xff);
    proprec.prop_namelen = strlen (propname);
    bcopy (propname, &proprec.prop_name, proprec.prop_namelen);
    proprec.prop_flag = flags; proprec.prop_security = security;
    proprec.prop_objid[0] = (objid >> 24); proprec.prop_objid[1] = (objid >> 16);
    proprec.prop_objid[2] = (objid >>  8); proprec.prop_objid[3] = (objid & 0xff);
    proprec.prop_valueid[0] = (valid >> 24); proprec.prop_valueid[1] = (valid >> 16);
    proprec.prop_valueid[2] = (valid >>  8); proprec.prop_valueid[3] = (valid & 0xff);
    proprec.prop_nextid[0] = 0xff; proprec.prop_nextid[1] = 0xff;
    proprec.prop_nextid[2] = 0xff; proprec.prop_nextid[3] = 0xff;

    // write this to the property file
    fseek (net_prop, 0, SEEK_END);
    if (!fwrite (&proprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	// this failed. return an error
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // build the value record
    bzero (&valrec, sizeof (NETVAL_RECORD));
    valrec.val_id[0] = (valid >> 24); valrec.val_id[1] = (valid >> 16);
    valrec.val_id[2] = (valid >>  8); valrec.val_id[3] = (valid & 0xff);
    valrec.val_propid[0] = (propid >> 24); valrec.val_propid[1] = (propid >> 16);
    valrec.val_propid[2] = (propid >>  8); valrec.val_propid[3] = (propid & 0xff);
    valrec.val_nextid[0] = 0xff; valrec.val_nextid[1] = 0xff;
    valrec.val_nextid[2] = 0xff; valrec.val_nextid[3] = 0xff;
    valrec.val_size[0] = 0x01; valrec.val_size[1] = 0x00;
    if (data != NULL)
	bcopy (data, &valrec.val_data, 128);

    // write this to the value file
    fseek (net_val, 0, SEEK_END);
    if (!fwrite (&valrec, sizeof (NETVAL_RECORD), 1, net_val)) {
	// this failed. return an error
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // does this object have a property id?
    if (obj.prop_id == 0xffffffff) {
	// no, not really. good, we can just directly modify the object then
	fseek (net_obj, ftell (net_obj) - sizeof (NETOBJ_RECORD), SEEK_SET);

	// read the raw structure
	if (!fread (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	    // this failed. return an error
	    if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	    return -1;
	}

	// fix it up
	objrec.prop_id[0] = (propid >> 24); objrec.prop_id[1] = (propid >> 16);
	objrec.prop_id[2] = (propid >>  8); objrec.prop_id[3] = (propid & 0xff);

	// overwrite this with the new data
	fseek (net_obj, ftell (net_obj) - sizeof (NETOBJ_RECORD), SEEK_SET);
	if (!fwrite (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	    // this failed. return an error
	    if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	    return -1;
	}
    } else {
	// looks like it does. look for the final property and add the new
	// property
	nextpropid = obj.prop_id;
	while (bindio_prop_seek (nextpropid, &proprec, NULL) == 0) {
	    bindio_conv_prop (&proprec, &prop);
	    nextpropid = prop.next_id;
	    if (nextpropid == 0xffffffff) break;
	}

	// we just passed the final property of this object. fix it up
	proprec.prop_nextid[0] = (propid >> 24); 
	proprec.prop_nextid[1] = (propid >> 16); 
	proprec.prop_nextid[2] = (propid >>  8); 
	proprec.prop_nextid[3] = (propid & 0xff);

	// write it back
	fseek (net_prop, ftell (net_prop) - sizeof (NETPROP_RECORD), SEEK_SET);
	if (!fwrite (&proprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	    // this failed. complain
	    if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	    return -1;
	}
    }

    // all went ok
    return propid;
}

/*
 * bindio_scan_val (uint32 valid, BINDERY_VAL* val)
 *
 * This will scan the value datafile for value id [valid]. It will copy the
 * result to [val]. It will return 0 on success or -1 on failure.
 *
 */
int
bindio_scan_val (uint32 valid, BINDERY_VAL* val) {
    NETVAL_RECORD valrec;

    rewind (net_val);

    // scan all
    while (fread (&valrec, sizeof (NETVAL_RECORD), 1, net_val)) {
	// convert the value into something usuable
	bindio_conv_val (&valrec, val);

	// is the value correct?
	if (val->id == valid) {
	    // yeah, we got it
	    return 0;
	}
    }

    // the value was not found
    return -1;
}

/*
 * bindio_read_prop (uint32 objid, char* propname, uint8 segno, BINDERY_VAL*
 * 	  	     val, BINDERY_PROP* prop)
 *
 * This will retrieve segment [segno] from property [propname] of object
 * [objid]. It will copy the result to [dest]. On success, BINDERY_ERROR_OK
 * will be returned. On failure, BINDERY_ERROR_xxx will be returned. [proprec]
 * will hold the property record.
 *
 */
int
bindio_read_prop (uint32 objid, char* propname, uint8 segno, BINDERY_VAL* val, BINDERY_PROP* prop) {
    // scan for the property
    if (bindio_scan_prop (objid, propname, prop) < 0) {
	// it was not found. return an error code
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // XXX: segment number

    // scan for the value itself
    if (bindio_scan_val (prop->value_id, val) < 0) {
	// this failed. complain
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // it worked. inform the user
    return BINDERY_ERROR_OK;
}

/*
 * bindio_write_prop (uint32 objid, char* propname, uint8 segno, uint8 more,
 *		    char* val)
 *
 * This will write the [segno]-th segment [val] to property [propname] of object
 * [ojbid]. If [more] is not zero, it will truncate the set. On success,
 * BINDERY_ERROR_OK will be returned. On failure, BINDERY_ERROR_xxx will be
 * returned.
 *
 */
int
bindio_write_prop (uint32 objid, char* propname, uint8 segno, uint8 more, char* dataval) {
    NETVAL_RECORD  valrec;
    BINDERY_PROP   prop;
    BINDERY_VAL	   val;

    // scan for the property
    if (bindio_scan_prop (objid, propname, &prop) < 0) {
	// it was not found. return an error code
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // XXX: segment number
    if (segno != 1) {
	// complain
	DPRINTF (0, LOG_BINDERYIO, "[INFO] bindio_write_prop(): multiple segments aren't supported just yet\n");
	return BINDERY_ERROR_IOERROR;
    }

    // XXX: more flag
    if (more) {
	DPRINTF (0, LOG_BINDERYIO, "[INFO] bindio_write_prop(): truncating segments isn't supported just yet\n");
	return BINDERY_ERROR_IOERROR;
    }

    // scan for the value itself
    if (bindio_scan_val (prop.value_id, &val) < 0) {
	// this failed. complain
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // we got it. now, seek back and overwrite it
    fseek (net_val, ftell (net_val) - sizeof (NETVAL_RECORD), SEEK_SET);
    if (!fread (&valrec, sizeof (NETVAL_RECORD), 1, net_val)) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // set the new value
    bcopy (dataval, valrec.val_data, 128);

    // overwrite the value
    fseek (net_val, ftell (net_val) - sizeof (NETVAL_RECORD), SEEK_SET);
    if (!fwrite (&valrec, sizeof (NETVAL_RECORD), 1, net_val)) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // it worked. inform the user
    return BINDERY_ERROR_OK;
}

/*
 * bindio_scan_object_wild (char* objname, uint16 type, uint32 last_obj,
 *			    BINDERY_OBJ* obj)
 *
 * This will scan the bindery for object [objname], type [type]. If [last_obj]
 * is not 0xffffffff, it will use that ID as the object to begin scanning from.
 * If [type] is anything but 0xffff, the type must match as well. If the object
 * is found, this will set [obj] to the object record and return 0. on any
 * failure, -1 will be returned.
 *
 */
int
bindio_scan_object_wild (char* objname, uint16 type, uint32 last_obj, BINDERY_OBJ* obj) {
    NETOBJ_RECORD objrec;
    uint16 i;

    // do we need to originate the scan from somewhere specific?
    if (last_obj != 0xffffffff) {
	// yes. scan for this object
	if (bindio_scan_object_by_id (last_obj, NULL, NULL) < 0) {
	    // this object doesn't even exist. complain
	    return -1;
	}
    } else {
	// no. start from the beginning
	rewind (net_obj);
    }

    // keep getting objects from the file
    while (fread (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	// convert the disk structure to something useful
 	bindio_conv_object (&objrec, obj);

	// is this type correct ?
	if ((obj->type == type) || (type == 0xffff)) {
	    // yes. what about the name ?
	    if (match_bind ((char*)obj->name, objname, 47) == 0) {
		// yay, that's correct too! return positive status
		return 0;
	    }
	}
    }

    // no matches
    return -1;
}

/*
 * bindio_change_object (uint32 objid, uint32 new_objid, char* new_name)
 *
 * This will change object [objid]. If [new_objid] is not 0xffffffff, it will
 * change the object ID to [new_objid]. If [new_name] is not NULL, it will
 * rename the object to [new_name]. It will return BINDERY_ERROR_OK on success
 * or BINDERY_ERROR_xxx on failure.
 *
 */
int
bindio_change_object (uint32 objid, uint32 new_objid, char* new_name) {
    NETOBJ_RECORD  objrec;
    BINDERY_OBJ	   obj;

    // get the object record
    if (bindio_scan_object_by_id (objid, &objrec, &obj) < 0) {
	// this failed. complain
	return BINDERY_ERROR_NOSUCHOBJECT;
    }

    // need to alter the object id?
    if (new_objid != 0xffffffff) {
	// yes. fix it up
	objrec.object_id[0] = (uint8)(new_objid >>  24);
	objrec.object_id[1] = (uint8)(new_objid >>  16);
	objrec.object_id[2] = (uint8)(new_objid >>   8);
	objrec.object_id[3] = (uint8)(new_objid & 0xff);
    }

    // need a new name ?
    if (new_name != NULL) {
	// yes. fix it
	strncpy ((char*)objrec.object_name, new_name, 46);
    }

    // now, seek back on the disk
    fseek (net_obj, ftell (net_obj) - sizeof (NETOBJ_RECORD), SEEK_SET);

    // overwrite the record
    if (!fwrite (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // yay, it worked. inform the user
    return BINDERY_ERROR_OK;
}

/*
 * bindio_delete_prop (uint32 propid)
 *
 * This will delete property [propid]. It will return BINDERY_ERROR_OK on
 * success or BINDERY_ERROR_xxx on failure.
 *
 */
int
bindio_delete_prop (uint32 propid) {
    NETOBJ_RECORD		objrec;
    NETPROP_RECORD		proprec, tmproprec;
    NETVAL_RECORD		valrec;
    BINDERY_OBJ			obj;
    BINDERY_PROP		prop;
    BINDERY_VAL			val;
    int				rec_pos, lastrec_pos;
    uint32			nextid, objid;

    // grab the property's record
    if (bindio_scan_propid (propid, &proprec, &prop) < 0) {
	// there's no such property. complain
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // convert the raw record to something useful
    objid = prop.object_id; nextid = prop.next_id;

    DPRINTF (10, LOG_BINDERYIO, "DELETE_PROP() -> object ID = 0x%x, prop ID = 0x%x next prop ID = 0x%x\n", objid, prop.id, prop.next_id);
    bindio_dump();

    // okay, we have the property record in [prop]. now, grab the value
    // record
    if (bindio_scan_val (prop.value_id, &val) < 0) {
	// there is none. complain
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // now, grab the offset of this record and of the the very last record
    rec_pos = ftell (net_val) - sizeof (NETVAL_RECORD);

    // seek to the very end minus the last record size
    fseek (net_val, 0, SEEK_END);
    lastrec_pos = ftell (net_val) - sizeof (NETVAL_RECORD);
    fseek (net_val, lastrec_pos, SEEK_SET);

    // have we reached the end of the value datafile?
    if (rec_pos == lastrec_pos) {
	// yes. it's easy now, just truncate the file
	if (ftruncate (fileno (net_val), lastrec_pos) < 0) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}
    } else {
	// no. damn, we need to grab the very last record, move it over this
	// one and truncate the file. do it

	// read the record
	if (!fread (&valrec, sizeof (NETVAL_RECORD), 1, net_val)) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}

	// overwrite the old record
	fseek (net_val, rec_pos, SEEK_SET);
	if (!fwrite (&valrec, sizeof (NETVAL_RECORD), 1, net_val)) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}
    }

    // now, grab the offset of this record and of the the very last record
    rec_pos = ftell (net_prop) - sizeof (NETPROP_RECORD);

    // seek to the very end minus the last record size
    fseek (net_prop, 0, SEEK_END);
    lastrec_pos = ftell (net_prop) - sizeof (NETPROP_RECORD);
    fseek (net_prop, lastrec_pos, SEEK_SET);

    DPRINTF (10, LOG_BINDERYIO, "[rec_pos=%u,lastrec_pos=%u]\n", rec_pos, lastrec_pos);

    // okay, the value file is updated. now, handle the property file. do we
    // have the last record here?
    if (rec_pos == lastrec_pos) {
	// yes. it's easy now, just truncate the file
	rewind (net_prop);
	if (ftruncate (fileno (net_prop), lastrec_pos) < 0) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}
    } else {
	// no. damn, we need to grab the very last record, move it over this
	// one and truncate the file. do it

	// read the very last record
	fseek (net_prop, lastrec_pos, SEEK_SET);
	if (!fread (&proprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}

	// overwrite the old record
	fseek (net_prop, rec_pos, SEEK_SET);
	if (!fwrite (&proprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}

	// truncate the file
	if (ftruncate (fileno (net_prop), lastrec_pos) < 0) {
 	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}
    }

    // now, scan for the property that has our property as next id
    if (bindio_scan_prop_nextid (propid, NULL, NULL) < 0) {
	// there is none. perhaps the object itself references to this
	// property. get the object record

	bindio_dump();
	DPRINTF (10, LOG_BINDERYIO, "---\n");

	if (bindio_scan_object_by_id (objid, NULL, &obj) < 0) {
	    // this failed. complain
	    return BINDERY_ERROR_NOSUCHOBJECT;
	}

	// does this object reference to this property?
	if (obj.prop_id == propid) {
	    // yes. update the record
	    objrec.prop_id[0] = (uint8)(nextid >>  24);
	    objrec.prop_id[1] = (uint8)(nextid >>  16);
	    objrec.prop_id[2] = (uint8)(nextid >>   8);
	    objrec.prop_id[3] = (uint8)(nextid & 0xff);

	    // write the record back
	    fseek (net_obj, ftell (net_obj) - sizeof (NETOBJ_RECORD), SEEK_SET);
	    if (!fwrite (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
		// this failed. complain
		return BINDERY_ERROR_IOERROR;
	    }
	}
	return BINDERY_ERROR_OK;
    }

    // okay, we got it. now, fill in the next property id
    tmproprec.prop_nextid[0] = (uint8)(nextid >>  24);
    tmproprec.prop_nextid[1] = (uint8)(nextid >>  16);
    tmproprec.prop_nextid[2] = (uint8)(nextid >>   8);
    tmproprec.prop_nextid[3] = (uint8)(nextid & 0xff);

    // write the updated property to the disk
    fseek (net_prop, ftell (net_prop) - sizeof (NETPROP_RECORD), SEEK_SET);
    if (!fwrite (&tmproprec, sizeof (NETPROP_RECORD), 1, net_prop)) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // wohoo, it all worked! return positive status
    return BINDERY_ERROR_OK;
}

/*
 * bindio_delete_obj (uint32 objid)
 *
 * This will delete object [objid] from the bindery, along with all properties
 * and values. It will return BINDERY_ERROR_OK on success and BINDERY_ERROR_xxx
 * on failure.
 *
 */
int
bindio_delete_obj (uint32 objid) {
    NETOBJ_RECORD	objrec;
    NETPROP_RECORD	proprec;
    BINDERY_OBJ		obj;
    BINDERY_PROP	prop;
    uint32		propid;
    int			rec_pos, lastrec_pos, i;

    // scan for the object record
    if (bindio_scan_object_by_id (objid, &objrec, &obj) < 0) {
	// there's no such object
	return BINDERY_ERROR_NOSUCHOBJECT;
    }
    rec_pos = ftell (net_obj) - sizeof (NETOBJ_RECORD);

    // figure out the last object's position
    fseek (net_obj, 0, SEEK_END);
    lastrec_pos = ftell (net_obj) - sizeof (NETOBJ_RECORD);

    // yay, we got the object. now, zap all properties
    propid = obj.prop_id;
    while (propid != 0xffffffff) {
	// grab the property
	if (bindio_scan_propid (propid, &proprec, &prop) < 0) break;

	// get rid of this property
  	DPRINTF (10, LOG_BINDERYIO, "[DEBUG] Delete property 0x%u --> ", propid);
	if ((i = bindio_delete_prop (propid)) != BINDERY_ERROR_OK) {
	    // this failed. return an error
	    DPRINTF (10, LOG_BINDERYIO, " FAILURE (error 0x%x)\n", i);
	    return i;
	}
	DPRINTF (10, LOG_BINDERYIO," OK\n");
	
	// next property, please
	propid = prop.next_id;
    }

    DPRINTF (10, LOG_BINDERYIO, "OBJ:[rec_pos=%u,lastrec_pos=%u]\n", rec_pos, lastrec_pos);

    // is the object the last one in the file?
    if (rec_pos == lastrec_pos) {
	// yes. just truncate the file
	rewind (net_obj);
	if (ftruncate (fileno (net_obj), rec_pos) < 0) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}
    } else {
	// no. now, read the last record, overwrite the old one and truncate
	// the file
	fseek (net_obj, lastrec_pos, SEEK_SET);
	if (!fread (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}

	// okay, now seek over to the old position
	fseek (net_obj, rec_pos, SEEK_SET);

	// overwrite the object there
	if (!fwrite (&objrec, sizeof (NETOBJ_RECORD), 1, net_obj)) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}
	
	// and finally, truncate the file
	rewind (net_obj);
	if (ftruncate (fileno (net_obj), lastrec_pos) < 0) {
	    // this failed. complain
	    return BINDERY_ERROR_IOERROR;
	}
    }

    return BINDERY_ERROR_OK;
}

/*
 * bindio_truncate()
 *
 * This will truncate the bindery datafiles, resetting them to nothing.
 *
 */
void
bindio_truncate() {
    rewind (net_obj); ftruncate (fileno (net_obj), 0);
    rewind (net_prop); ftruncate (fileno (net_prop), 0);
    rewind (net_val); ftruncate (fileno (net_val), 0);
}
#endif /* WITH_PGSQL */
#endif /* WITH_MYSQL */
