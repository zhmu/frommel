/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 *
 * PostgreSQL database module
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

#ifdef WITH_PGSQL
#include <libpq-fe.h>
#include <libpq/libpq-fs.h>

PGconn* pgsql_conn;

// BINDIO_MODULE is the bindery module we actually use
#define BINDIO_MODULE "binderyio_pgsql.c"

/*
 * PQescapeString (char* out, char* in, int len)
 *
 * This will escape [len] bytes of string [in] to [out].
 *
 * NOTICE: PostgreSQL docs say this in the library, but it's not in mine
 *	   (7.1.3). mod_auth_pgsql also doesn't use it.
 *
 */
void
PQescapeString (char* out, char* in, int len) {
    int i, j = 0;
    uint8 c;

    strcpy (out, "");
    for (i = 0; i < len; i++) {
	// get the char
	c = (uint8)in[i];

	// is it not a slash?
	if (c != '\\') 
	   // quote it (XXX)
	   sprintf (out, "%s\\%03o", out, (unsigned int)c);
	else
	   // add a double slash
	   sprintf (out, "%s\\\\", out);
    }
}

/*
 * bindio_open(char* mode)
 *
 * This will open a link to the PostgreSQL database. It will return zero on
 * success or -1 on failure.
 *
 */
int
bindio_open(char* mode) {
    fprintf (stderr, "[INFO] Bindery module '%s' initializing\n", BINDIO_MODULE);

    // initialize PostgreSQL
    pgsql_conn = PQsetdbLogin ((!strcasecmp (conf_sql_hostname, "localhost")) ? NULL : conf_sql_hostname, NULL, NULL, NULL, conf_sql_database, conf_sql_username, conf_sql_password);
    if (PQstatus (pgsql_conn) != CONNECTION_OK) {
	// this failed. complain
	fprintf (stderr, "[CRIT] Cannot open PostgreSQL link to the database (error '%s')\n", PQerrorMessage (pgsql_conn));
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
    PGresult* res;

    // debug
    DPRINTF (12, LOG_BINDERYIO, "[DEBUG] [SQL] Execute query '%s': ", query);

    // do the query
    res = PQexec (pgsql_conn, query);
    if ((res != NULL) && ((PQresultStatus (res) == PGRES_TUPLES_OK) || (PQresultStatus (res) == PGRES_COMMAND_OK))) {
	// this worked. return ok status
	PQclear (res);
	DPRINTF (12, LOG_BINDERYIO, " success\n");
	return 0;
    }

    // this did not work. complain
    DPRINTF (12, LOG_BINDERYIO, " failure, error '%s'\n", PQerrorMessage (pgsql_conn));
    return -1;
}

/*
 * bindio_do_query_res (char* query)
 *
 * This will execute query [query]. It will return the PostgreSQL result handle
 * on success or NULL on failure.
 *
 */
PGresult*
bindio_do_query_res (char* query) {
    PGresult* res;

    // debug
    DPRINTF (12, LOG_BINDERYIO, "[DEBUG] [SQL] Execute query '%s': ", query);

    // do the query
    res = PQexec (pgsql_conn, query);
    if ((res != NULL) && ((PQresultStatus (res) == PGRES_TUPLES_OK) || (PQresultStatus (res) == PGRES_COMMAND_OK))) {
	// this worked. return ok status
	DPRINTF (12, LOG_BINDERYIO, " success\n");
	return res;
    }

    // this did not work. complain
    if (res != NULL) PQclear (res);
    DPRINTF (12, LOG_BINDERYIO, " failure, error '%s'\n", PQerrorMessage (pgsql_conn));
    return NULL;
}

/*
 * bindio_begin_trans()
 *
 * This will begin a transaction.
 *
 */
void
bindio_begin_trans() {
    if (bindio_do_query ("BEGIN") < 0)
	DPRINTF (11, LOG_BINDERYIO, "[SQL] WARNING: BEGIN TRANSACTION failed\n");
}

/*
 * bindio_end_trans()
 *
 * This will end a transaction.
 *
 */
void
bindio_end_trans() {
    if (bindio_do_query ("COMMIT") < 0)
	DPRINTF (11, LOG_BINDERYIO, "[SQL] WARNING: COMMIT TRANSACTION failed\n");
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
    PGresult *res;

    // query for the information
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. complain
	return -1;
    }

    // got any actual results?
    if (PQntuples (res) != 1) {
	// no. there's no such object, complain
	PQclear (res);
	return -1;
    }
	
    // yay, this worked. do we have a valid structure?
    if (prop) {
	// yes. fill it out
	bzero (prop, sizeof (BINDERY_PROP));
	prop->id = atoi (PQgetvalue (res, 0, 0));
	strncpy ((char*)prop->name, PQgetvalue (res, 0, 1), 15);
	prop->flags = atoi (PQgetvalue (res, 0, 2));
	prop->security = atoi (PQgetvalue (res, 0, 3));
	prop->object_id = atoi (PQgetvalue (res, 0, 4));
	prop->value_id = atoi (PQgetvalue (res, 0, 5));
    }

    // get rid of the result
    PQclear (res);

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
    PGresult *res;

    // query for the information
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. complain
	return -1;
    }

    // got any actual results?
    if (PQntuples (res) != 1) {
	// no. there's no such object, complain
	PQclear (res);
	return -1;
    }

    // yay, this worked. do we have a valid structure?
    if (obj) {
        // yes. fill it out
	bzero (obj, sizeof (BINDERY_OBJ));
	obj->id = atoi (PQgetvalue (res, 0, 0));
	obj->type = atoi (PQgetvalue (res, 0, 1));
	strncpy ((char*)obj->name, PQgetvalue (res, 0, 2), 15);
	obj->flags = atoi (PQgetvalue (res, 0, 3));
	obj->security = atoi (PQgetvalue (res, 0, 4));
	obj->prop_id = 0;
    }

    // free the result
    PQclear (res);

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
    PGresult* obj_res;
    PGresult* prop_res;
    int obj_count, prop_count;

    // walk all objects first
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,type,name,security FROM object");
    obj_res = bindio_do_query_res (query);
    if (obj_res == NULL) {
	// this failed. complain
	DPRINTF (0, LOG_BINDERYIO, "[WARN] Cannot walk object table, skipping object dump\n");
	return;
    }

    // keep fetching
    obj_count = 0;
    while (obj_count < PQntuples (obj_res)) {
	fprintf (stderr, "- Object '%s' (id=0x%x,type=0x%x,security=0x%x)\n", PQgetvalue (obj_res, obj_count, 2), atoi (PQgetvalue (obj_res, obj_count, 0)), atoi (PQgetvalue (obj_res, obj_count, 1)), atoi (PQgetvalue (obj_res, obj_count, 3)));

	// walk the properties, too
	snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT id,name,security,valueid FROM prop WHERE objectid=%u", atoi (PQgetvalue (obj_res, obj_count, 0)));
	prop_res = bindio_do_query_res (query);
	if (prop_res == NULL) {
	    // this failed. complain
	    DPRINTF (0, LOG_BINDERYIO, "[WARN] Cannot walk object '%s', skipping\n", PQgetvalue (obj_res, obj_count, 2));
	} else {
	    // keep fetching
	    prop_count = 0;
	    while (prop_count < PQntuples (prop_res)) {
		printf ("    - Property '%s' (id=0x%x,security=0x%x)\n", PQgetvalue (prop_res, prop_count, 1), atoi (PQgetvalue (prop_res, prop_count, 0)), atoi (PQgetvalue (prop_res, prop_count, 2)));
		prop_count++;
	    }

	    PQclear (prop_res);
	}

	// next object
	obj_count++;
    }

    PQclear (obj_res);
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
    PQescapeString (temp, objname, strlen (objname));

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
    PGresult*   res;
    int	id;

    // quote the object name (XXX)
    PQescapeString (temp, objname, strlen (objname));

    // do we already have this object?
    if (!bindio_scan_object (objname, type, NULL, NULL)) {
	// yes. complain
	if (errcode) { *errcode = BINDERY_ERROR_OBJEXISTS; };
	return -1;
    }

    // there's no such object. create it, then. build the query
    snprintf (query, SQL_MAX_QUERY_LENGTH, "INSERT INTO object VALUES (NEXTVAL('object_id_seq'),%u,'%s',%u,%u)", type, temp, flags, security);

    // execute the query
    if (!bindio_do_query (query)) {
	// this worked. grab the id number
	snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT CURRVAL('object_id_seq')");
	res = bindio_do_query_res (query);
	if (res == NULL) {
	    // this failed. complain
	    if (errcode) *errcode = BINDERY_ERROR_IOERROR;
	    return -1;
	}

	id = atoi (PQgetvalue (res, 0, 0));
	PQclear (res);
	return id;
    }

    // it couldn't be done. return failure status
    if (errcode) *errcode = BINDERY_ERROR_IOERROR;
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
    PQescapeString (temp, propname, strlen (propname));

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
    uint32	value_id, prop_id, oid;
    int		oid_fd;
    PGresult*	res;

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
	bcopy (data, temp, 128);
    } else {
	// no, clear temp out
	bzero (temp, 128);
    }

    // begin a transaction
    bindio_begin_trans();

    // create the object
    oid = lo_creat (pgsql_conn, INV_READ | INV_WRITE);
    if (oid == 0) {
	DPRINTF (11, LOG_BINDERYIO, "[SQL] lo_creat() failed\n");
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // open the object
    oid_fd = lo_open (pgsql_conn, oid, INV_WRITE);

    // write the data
    if (lo_write (pgsql_conn, oid_fd, temp, 128) < 128) {
	// this failed. complain
	DPRINTF (11, LOG_BINDERYIO, "[SQL] lo_write() failed\n");
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // close the object
    lo_close (pgsql_conn, oid_fd);

    // commit the transaction
    bindio_end_trans();

    // add a new property value
    snprintf (query, SQL_MAX_QUERY_LENGTH, "INSERT INTO value VALUES (NEXTVAL ('value_id_seq'),-1,-1,1,'%u','%u')", objid, oid);
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. complain
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // get the value id
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT CURRVAL('value_id_seq')");
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. complain
	if (errcode) *errcode = BINDERY_ERROR_IOERROR;
	return -1;
    }
    value_id = atoi (PQgetvalue (res, 0, 0));

    // quote the property name
    PQescapeString (temp, propname, strlen (propname));

    // add the new property
    snprintf (query, SQL_MAX_QUERY_LENGTH, "INSERT INTO prop VALUES (NEXTVAL ('prop_id_seq'),'%s',%u,%u,%u,%u)", temp, flags, security, objid, value_id);
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. get rid of the bindery value we created first
        snprintf (query, SQL_MAX_QUERY_LENGTH, "DELETE FROM value WHERE id=%u", value_id);
	bindio_do_query (query);

	// complain
	if (errcode) { *errcode = BINDERY_ERROR_IOERROR; };
	return -1;
    }

    // get the property id
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT CURRVAL('prop_id_seq')");
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. complain
	if (errcode) *errcode = BINDERY_ERROR_IOERROR;
	return -1;
    }
    prop_id = atoi (PQgetvalue (res, 0, 0));

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
    PGresult*	res;
    int		oid, oid_fd;

    // build the query
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT propid,nextid,size,data FROM value WHERE id=%u", valid);
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. complain
	return -1;
    }

    // got any actual results?
    if (PQntuples (res) != 1) {
	// no. there's no such object, complain
	PQclear (res);
	return -1;
    }

    // yay, this worked. do we have a valid structure?
    if (val) {
	// yes. fill it out
	bzero (val, sizeof (BINDERY_VAL));
	val->id = valid;
	val->prop_id = atoi (PQgetvalue (res, 0, 0));
	val->next_id = atoi (PQgetvalue (res, 0, 1));
	val->size = atoi (PQgetvalue (res, 0, 2));

	// open the object ID
	oid = atoi (PQgetvalue (res, 0, 3));
	DPRINTF (11, LOG_BINDERYIO, "[SQL] OID = '%s':%u\n", PQgetvalue (res, 0, 3), oid);

	// begin a transaction
	bindio_begin_trans();

	oid_fd = lo_open (pgsql_conn, oid, INV_READ);
	if (oid_fd < 0) {
	    DPRINTF (11, LOG_BINDERYIO, "[SQL] read_prop(): lo_open() failed\n");
	    fprintf (stderr, "[CRIT] (error '%s')\n", PQerrorMessage (pgsql_conn));
	    PQclear (res);
	    bindio_end_trans();
	    return -1;
	}

	if (lo_read (pgsql_conn, oid_fd, val->data, 128) < 128) {
	    // this failed. complain
	    DPRINTF (11, LOG_BINDERYIO, "[SQL] read_prop(): lo_read() failed\n");
	    lo_close (pgsql_conn, oid_fd);
	    PQclear (res);
	    bindio_end_trans();
	    return -1;
	}
	lo_close (pgsql_conn, oid_fd);

	// end the transaction
	bindio_end_trans();
    }

    // free the result
    PQclear (res);

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
    char	   query[SQL_MAX_QUERY_LENGTH + 1];
    char	   temp[SQL_MAX_QUERY_LENGTH + 1];
    int		   oid, oid_fd;
    PGresult*	   res;

    // scan for the property
    if (bindio_scan_prop (objid, propname, &prop) < 0) {
	// it was not found. return an error code
	DPRINTF (11, LOG_BINDERYIO, "[SQL] scan_prop() failed\n");
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
	DPRINTF (11, LOG_BINDERYIO, "[SQL] scan_val() failed\n");
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // quote the data (XXX)
    PQescapeString (temp, dataval, 128);

    // get the value id
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT data FROM value WHERE id=%u", prop.value_id);
    res = bindio_do_query_res (query);
    if (res == NULL) {
	// this failed. complain
	DPRINTF (11, LOG_BINDERYIO, "[SQL] Can't select data value\n");
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }
    oid = atoi (PQgetvalue (res, 0, 0));
    PQclear (res);

    // begin the transaction
    bindio_begin_trans();

    // open the object
    oid_fd = lo_open (pgsql_conn, oid, INV_WRITE);
    if (oid_fd < 0) {
	DPRINTF (11, LOG_BINDERYIO, "[SQL] writeprop(): lo_open() failed\n");
	lo_close (pgsql_conn, oid_fd);
	return BINDERY_ERROR_IOERROR;
    }

    // overwrite the data
    if (lo_write (pgsql_conn, oid_fd, dataval, 128) < 128) {
	// this failed. complain
	DPRINTF (11, LOG_BINDERYIO, "[SQL] writeprop(): lo_write() failed\n");
	lo_close (pgsql_conn, oid_fd);
	return BINDERY_ERROR_IOERROR;
    }

    // it worked
    lo_close (pgsql_conn, oid_fd);

    // end the transaction
    bindio_end_trans();

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
    char	   query[SQL_MAX_QUERY_LENGTH + 1];
    char	   temp[SQL_MAX_QUERY_LENGTH + 1];
    int		   i = 0;

    // quote the object name (XXX)
    PQescapeString (temp, objname, strlen (objname));

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
    PGresult*	   res;

    // grab the property's record
    if (bindio_scan_propid (propid, NULL, &prop) < 0) {
	// there's no such property. complain
	return BINDERY_ERROR_NOSUCHPROPERTY;
    }

    // XXX: multi-segment deletes don't work yet

    // grab the value id
    snprintf (query, SQL_MAX_QUERY_LENGTH, "SELECT data FROM value WHERE id=%u", prop.value_id);
    res = bindio_do_query_res (query);
    if (res != NULL) {
	// good, this worked. zap the Large Object thing
	if (lo_unlink (pgsql_conn, atoi (PQgetvalue (res, 0, 0))) < 0) {
	    DPRINTF (12, LOG_BINDERYIO, "[SQL] Warning: Cannot delete large object\n");
	}
    }

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
