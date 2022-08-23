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
#include "sap.h"

#ifdef WITH_MYSQL
#include <mysql/mysql.h>

MYSQL* mysql_conn;
MYSQL* mysql_handle;

// BINDIO_MODULE is the bindery module we actually use
#define BINDIO_MODULE "binderyio_mysql.c"

/*
 * bindio_open(char* mode)
 *
 * This will open a link to the MySQL database. It will return zero on success
 * or -1 on failure.
 *
 */
int
bindio_open(char* mode) {
    fprintf (stderr, "[INFO] Bindery module '%s' initializing\n", BINDIO_MODULE);

    // initialize MySQL
    mysql_conn = mysql_init (NULL);

     // try to connect
    mysql_handle = mysql_real_connect (mysql_conn, conf_sql_hostname, conf_sql_username, conf_sql_password, conf_sql_database, 0, NULL, 0);
    if (mysql_handle == NULL) {
	// this failed. complain
	fprintf (stderr, "[CRIT] Cannot open MySQL link to the database\n");
	return -1;
    }

    // immediately get rid of the password so it will never show up in core
    // dumps
    bzero (&conf_sql_password, CONFIG_MAX_SQL_LEN);

    // it worked
    return 0;
}

/*
 * bindio_do_query (char* query)
 *
 * This will execute query [query]. It will return 0 on success or -1 on
 * failure.
 *
 */
int
bindio_do_query (char* query) {
    // debug
    DPRINTF (12, LOG_BINDERYIO, "[DEBUG] [SQL] Execute query '%s': ", query);

    // do the query
    if (!mysql_real_query (mysql_handle, query, strlen (query))) {
	// this worked. return ok status
	DPRINTF (12, LOG_BINDERYIO, " success\n");
	return 0;
    }

    // this did not work. complain
    DPRINTF (12, LOG_BINDERYIO, " failure, error '%s'\n", mysql_error (mysql_handle));
    return -1;
}

/*
 * bindio_handle_prop_query (char* query, BINDERY_PROP* prop)
 *
 * This will handle property query [query]. It will put the results in [prop].
 * It returns 0 on success or -1 on failure.
 *
 */
int
bindio_handle_prop_query (char* query, BINDERY_PROP* prop) {
    MYSQL_RES*  result;
    MYSQL_ROW   row;

    // query for the information
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	return -1;
    }

    // this worked. grab the result handle
    result = mysql_store_result (mysql_handle);

    // did this work?
    if (result == NULL) {
        // no. complain
        return -1;
    }

    // get the results
    row = mysql_fetch_row (result);

    // got any actual results?
    if (mysql_num_rows (result) != 1) {
	// no. there's no such object, complain
	mysql_free_result (result);
	return -1;
    }
	
    // yay, this worked. do we have a valid structure?
    if (prop) {
	// yes. fill it out
	bzero (prop, sizeof (BINDERY_PROP));
	prop->id = atoi (row[0]);
	strncpy ((char*)prop->name, row[1], 15);
	prop->flags = atoi (row[2]);
	prop->security = atoi (row[3]);
	prop->object_id = atoi (row[4]);
	prop->value_id = atoi (row[5]);
    }

    // get rid of the result
    mysql_free_result (result);

    // this worked. return positive status
    return 0;
}

/*
 * bindio_handle_object_query (char* query, BINDERY_OBJ* obj)
 *
 * This will handle object query [query]. It will put the results in [obj].
 * It returns 0 on success or -1 on failure.
 *
 */
int
bindio_handle_object_query (char* query, BINDERY_OBJ* obj) {
    MYSQL_RES*  result;
    MYSQL_ROW   row;

    // query for the information
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	return -1;
    }

    // this worked. grab the result handle
    result = mysql_store_result (mysql_handle);

    // did this work?
    if (result == NULL) {
	// no. complain
	return -1;
    }

    // get the results
    row = mysql_fetch_row (result);

    // got any actual results?
    if (mysql_num_rows (result) != 1) {
	// no. there's no such object, complain
	mysql_free_result (result);
	return -1;
    }

    // yay, this worked. do we have a valid structure?
    if (obj) {
        // yes. fill it out
	bzero (obj, sizeof (BINDERY_OBJ));
	obj->id = atoi (row[0]);
	obj->type = atoi (row[1]);
	strncpy ((char*)obj->name, row[2], 15);
	obj->flags = atoi (row[3]);
	obj->security = atoi (row[4]);
	obj->prop_id = 0;
    }

    // free the result
    mysql_free_result (result);

    // this worked. return positive status
    return 0;
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
    char query[SQL_MAX_QUERY_LENGTH + 1];
    char temp[SQL_MAX_QUERY_LENGTH + 1];

    // query the database for this object
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,name,flags,security,objectid,valueid FROM prop WHERE id=%u", id);

    // pass it over to bindio_handle_prop_query()
    return bindio_handle_prop_query (query, prop);
}

/*
 * bindio_dump()
 *
 * This will dump all bindery objects and properties.
 *
 */
void
bindio_dump() {
    char query[SQL_MAX_QUERY_LENGTH + 1];
    MYSQL_RES* obj_res;
    MYSQL_RES* prop_res;
    MYSQL_ROW  obj_row, prop_row;

    // walk all objects first
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,type,name,security FROM object");
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	DPRINTF (0, LOG_BINDERYIO, "[WARN] Cannot walk object table, skipping object dump\n");
	return;
    }

    // fetch the result
    obj_res = mysql_store_result (mysql_handle);

    // keep fetching
    while ((obj_row = mysql_fetch_row (obj_res))) {
	fprintf (stderr, "- Object '%s' (id=0x%x,type=0x%x,security=0x%x)\n", obj_row[2], atoi (obj_row[0]), atoi (obj_row[1]), atoi (obj_row[3]));

	// walk the properties, too
	snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,name,security FROM prop WHERE objectid=%u", atoi (obj_row[0]));
	if (bindio_do_query (query) < 0) {
	    // this failed. complain
	    DPRINTF (0, LOG_BINDERYIO, "[WARN] Cannot walk object '%s', skipping\n", obj_row[2]);
	} else {
	    // fetch the result
	    prop_res = mysql_store_result (mysql_handle);

	    // keep fetching
	    while ((prop_row = mysql_fetch_row (prop_res))) {
		printf ("    - Property '%s' (id=0x%x,security=0x%x)\n", prop_row[1], atoi (prop_row[0]), atoi (prop_row[2]));
	    }

	    mysql_free_result (prop_res);
	}
    }

    mysql_free_result (obj_res);
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
    char query[SQL_MAX_QUERY_LENGTH + 1];
    char temp[SQL_MAX_QUERY_LENGTH + 1];

    // quote the object name
    mysql_real_escape_string (mysql_handle, temp, objname, strlen (objname));

    // query the database for this object
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,type,name,flags,security FROM object WHERE name='%s' AND type='%u'", temp, type);

    // pass it over to bindio_handle_object_query()
    return bindio_handle_object_query (query, obj);
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
    char query[SQL_MAX_QUERY_LENGTH + 1];

    // query the database for this object
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,type,name,flags,security FROM object WHERE id=%u", objid);

    // pass it over to bindio_handle_object_query()
    return bindio_handle_object_query (query, obj);
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
    char query[SQL_MAX_QUERY_LENGTH + 1];
    char temp[SQL_MAX_QUERY_LENGTH + 1];
   
    // query the database for this object
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,name,flags,security,objectid,valueid FROM prop WHERE id=%u", propid);

    // pass it over to bindio_handle_prop_query()
    return bindio_handle_prop_query (query, prop);
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
    char query[SQL_MAX_QUERY_LENGTH + 1];
    char temp[SQL_MAX_QUERY_LENGTH + 1];
    MYSQL_RES*  result;
    MYSQL_ROW   row;
    uint32	id;

    // quote the object name (XXX)
    mysql_real_escape_string (mysql_handle, temp, objname, strlen (objname));

    // do we already have this object?
    if (!bindio_scan_object (objname, type, NULL, NULL)) {
	// yes. complain
	if (errcode) { *errcode = BINDERY_ERROR_OBJEXISTS; };
	return -1;
    }

    // there's no such object. create it, then. build the query
    snprintf (query, SQL_MAX_QUERY_LENGTH, "INSERT INTO object VALUES (NULL,%u,'%s',%u,%u)", type, temp, flags, security);

    // execute the query
    if (!bindio_do_query (query)) {
	// this worked. return the id
	id = mysql_insert_id (mysql_handle);
	return id;
    }

    // it couldn't be done. return failure status
    *errcode = BINDERY_ERROR_IOERROR;
    return -1;
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
    char query[SQL_MAX_QUERY_LENGTH + 1];
    char temp[SQL_MAX_QUERY_LENGTH + 1];

    // quote the property name (XXX)
    mysql_real_escape_string (mysql_handle, temp, propname, strlen (propname));

    // query the database for this property
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,name,flags,security,objectid,valueid FROM prop WHERE objectid=%u AND name='%s'", objid, temp);

    // pass it over to bindio_handle_prop_query()
    return bindio_handle_prop_query (query, prop);
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
    char	query[SQL_MAX_QUERY_LENGTH + 1];
    char	temp[SQL_MAX_QUERY_LENGTH + 1];
    char	temp2[SQL_MAX_QUERY_LENGTH + 1];
    uint32	value_id, prop_id;

    // grab the object record
    if (bindio_scan_object_by_id (objid, NULL, &obj) < 0) {
	// there's no such object. complain
	if (errcode) { *errcode = BINDERY_ERROR_NOSUCHOBJECT; };
	return -1;
    }

    // does this property already exist?
    if (!bindio_scan_prop (objid, propname, NULL)) {
	// yes. complain
	if (errcode) { *errcode = BINDERY_ERROR_PROPEXISTS; };
	return -1;
    }

    // is data given?
    if (data) {
	// yes. quote it (XXX)
	mysql_real_escape_string (mysql_handle, temp, data, 128);
    } else {
	// no, clear temp out
	bzero (temp2, 128);
	mysql_real_escape_string (mysql_handle, temp, temp2, 128);
    }

    // add a new property value
    snprintf (query, SQL_MAX_QUERY_LENGTH, "INSERT INTO value VALUES (NULL,-1,-1,1,'%u','%s')", objid, temp);
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // get the value id
    value_id = mysql_insert_id (mysql_handle);

    // quote the property name
    mysql_real_escape_string (mysql_handle, temp, propname, strlen (propname));

    // add the new property
    snprintf (query, SQL_MAX_QUERY_LENGTH, "INSERT INTO prop VALUES (NULL,'%s',%u,%u,%u,%u)", temp, flags, security, objid, value_id);
    if (bindio_do_query (query) < 0) {
	// this failed. get rid of the bindery value we created first
        snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM value WHERE id=%u", value_id);
	bindio_do_query (query);

	// complain
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // get the property id
    prop_id = mysql_insert_id (mysql_handle);

    // update the value
    snprintf (query, SQL_MAX_QUERY_LENGTH, "UPDATE value SET propid=%u WHERE id=%u", prop_id, value_id);
    if (bindio_do_query (query) < 0) {
	// this failed. get rid of the bindery value we created first
        snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM value WHERE id=%u", value_id);
	bindio_do_query (query);

	// get rid of the property as well
        snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM prop WHERE id=%u", prop_id);
	bindio_do_query (query);

	// complain
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // all went ok. return the property id
    return prop_id;
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
    char	query[SQL_MAX_QUERY_LENGTH + 1];
    MYSQL_RES*	result;
    MYSQL_ROW   row;

    // build the query
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT propid,nextid,size,data FROM value WHERE id=%u", valid);
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	return -1;
    }

    // this worked. grab the result handle
    result = mysql_store_result (mysql_handle);

    // did this work?
    if (result == NULL) {
 	// no. complain
	return -1;
    }

    // get the results
    row = mysql_fetch_row (result);

    // got any actual results?
    if (mysql_num_rows (result) != 1) {
	// no. there's no such object, complain
	mysql_free_result (result);
	return -1;
    }

    // yay, this worked. do we have a valid structure?
    if (val) {
	// yes. fill it out
	bzero (val, sizeof (BINDERY_VAL));
	val->id = valid;
	val->prop_id = atoi (row[0]);
	val->next_id = atoi (row[1]);
	val->size = atoi (row[2]);
	bcopy (row[3], val->data, 128);
    }

    // free the result
    mysql_free_result (result);

    // this worked. return positive status
    return 0;
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
    DPRINTF (12, LOG_BINDERYIO, "[DEBUG] [SQL] READ_PROP #1: Scan prop\n");
    if (bindio_scan_prop (objid, propname, prop) < 0) {
	// it was not found. return an error code
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // XXX: segment number
    DPRINTF (12, LOG_BINDERYIO, "[DEBUG] [SQL] PROP SCAN: OK, value ID = 0x%x!\n", prop->value_id);

    // scan for the value itself
    if (bindio_scan_val (prop->value_id, val) < 0) {
	// this failed. complain
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    DPRINTF (12, LOG_BINDERYIO,"[DEBUG] [SQL] VAL SCAN: OK!\n");

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
    char	   query[SQL_MAX_QUERY_LENGTH + 1];
    char	   temp[SQL_MAX_QUERY_LENGTH + 1];

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

    // quote the data (XXX)
    mysql_real_escape_string (mysql_handle, temp, dataval, 128);

    // update the field
    snprintf (query, SQL_MAX_QUERY_LENGTH, "UPDATE value SET data='%s' WHERE id=%u", temp, prop.value_id);
    return bindio_do_query (query);
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
    char	   query[SQL_MAX_QUERY_LENGTH + 1];
    char	   temp[SQL_MAX_QUERY_LENGTH + 1];
    int		   i = 0;

    // quote the object name (XXX)
    mysql_real_escape_string (mysql_handle, temp, objname, strlen (objname));

    // build the query
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,type,name,flags,security FROM object");

    // is the ID relevant?
    if (last_obj != 0xffffffff) {
	// yes. add it
	snprintf (query, SQL_MAX_QUERY_LENGTH, "%s WHERE (id>%u)", query, last_obj);
	i++;
    }

    // need to query for the name?
    if (strcmp (objname, "*")) {
	// yes. do it
	if (!i) {
	    snprintf (query, SQL_MAX_QUERY_LENGTH, "%s WHERE", query);
	    i++;
	} else {
	    snprintf (query, SQL_MAX_QUERY_LENGTH, "%s AND", query);
	}
	snprintf (query, SQL_MAX_QUERY_LENGTH, "%s (name LIKE '%s')", query, temp);
    }

    // need to query for the type too ?
    if (type != 0xffff) {
	// yes. append that to the query
	if (!i) {
	    snprintf (query, SQL_MAX_QUERY_LENGTH, "%s WHERE", query);
	    i++;
	} else {
	    snprintf (query, SQL_MAX_QUERY_LENGTH, "%s AND", query);
	}
	snprintf (query, SQL_MAX_QUERY_LENGTH, "%s (type=%u)", query, type);
    }

    // always limit it to just one
    snprintf (query, SQL_MAX_QUERY_LENGTH, "%s LIMIT 1", query);

    // pass it over to bindio_handle_object_query()
    return bindio_handle_object_query (query, obj);
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
    BINDERY_PROP   prop;
    BINDERY_VAL	   val;
    char	   query[SQL_MAX_QUERY_LENGTH + 1];

    // grab the property's record
    if (bindio_scan_propid (propid, NULL, &prop) < 0) {
	// there's no such property. complain
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // XXX: multi-segment deletes don't work yet

    // good, we have it. get rid of the value
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM value WHERE id=%u", prop.value_id);
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // get rid of the property itself
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM prop WHERE id=%u", propid);
    if (bindio_do_query (query) < 0) {
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
    BINDERY_OBJ	   obj;
    char	   query[SQL_MAX_QUERY_LENGTH + 1];

    // scan for the object record
    if (bindio_scan_object_by_id (objid, NULL, &obj) < 0) {
	// there's no such object
	return BINDERY_ERROR_NOSUCHOBJECT;
    }

    // the object exists. get rid of all properties
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM prop WHERE objectid=%u", objid);
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // get rid of all values, too
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM value WHERE objectid=%u", objid);
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // finally, get rid of the object itself
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM object WHERE id=%u", objid);
    if (bindio_do_query (query) < 0) {
	// this failed. complain
	return BINDERY_ERROR_IOERROR;
    }

    // it all worked like a charm. return positive status
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
    char query[SQL_MAX_QUERY_LENGTH + 1];

    // build the queries and execute them
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM object");
    bindio_do_query (query);
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM prop");
    bindio_do_query (query);
    snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM value");
    bindio_do_query (query);
}
#endif /* WITH_MYSQL */
