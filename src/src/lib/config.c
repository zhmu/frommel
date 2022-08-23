/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <ctype.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "frommel.h"
#include "fs.h"
#include "ncp.h"
#include "net.h"
#include "link.h"
#include "misc.h"
#include "sap.h"
#include "web.h"

char* conf_fname = NULL;
int   conf_lineno = 1;
char  conf_servername[49];
char  conf_ifname[CONFIG_MAX_IFNAME_LEN];
char  conf_logfile[VOL_MAX_VOL_PATH_LEN];
char  conf_tmpvol[16];
char  conf_tmpath[VOL_MAX_VOL_PATH_LEN];
char  conf_binderypath[VOL_MAX_VOL_PATH_LEN];
int   conf_nwversion = 0;
int   conf_uid = 0;
int   conf_gid = 0;
int   conf_serialno = CONFIG_DEFAULT_SERIALNO;
int   conf_appno = CONFIG_DEFAULT_APPNO;
int   conf_filemode = CONFIG_DEFAULT_FILEMODE;
int   conf_dirmode = CONFIG_DEFAULT_DIRMODE;
int   conf_trusteemode = CONFIG_DEFAULT_TRUSTMODE;
int   conf_sap_interval = CONFIG_DEFAULT_SAPINTERVAL;
int   conf_nofconns = CONFIG_DEFAULT_NOFCONNS;
int   conf_sap_enabled = 0;
int   conf_sap_nearest = 1;
char  conf_sql_hostname[CONFIG_MAX_SQL_LEN];
char  conf_sql_username[CONFIG_MAX_SQL_LEN];
char  conf_sql_password[CONFIG_MAX_SQL_LEN];
char  conf_sql_database[CONFIG_MAX_SQL_LEN];
int   conf_web_enabled = 0;
int   conf_web_port = 1854;
char  conf_web_templatedir[VOL_MAX_VOL_PATH_LEN];
char  conf_web_username[WEB_MAX_VALUE_LEN];
char  conf_web_password[WEB_MAX_VALUE_LEN];
int   conf_link_enabled = 0;
int   conf_link_key = 0;
int   conf_link_nofservers = 0;
LINK_SERVER conf_link_servers[LINK_MAX_SERVERS];

/*
 * conf_lookup_yesno (char* name)
 *
 * This will look up the value of [name]. It can be yes/on/true or no/off/false.
 * It will return 1 if it is true, 0 on false and -1 on failure.
 *
 */
int
conf_lookup_yesno (char* name) {
    // look them all up
    if (!strcasecmp (name, "yes")) return 1;
    if (!strcasecmp (name, "on")) return 1;
    if (!strcasecmp (name, "true")) return 1;
    if (!strcasecmp (name, "no")) return 0;
    if (!strcasecmp (name, "off")) return 0;
    if (!strcasecmp (name, "false")) return 0;

    // what's this?
    return -1;
}

/*
 * conf_lookup_section (char* name)
 *
 * This will return the number of section [name]. This will return any of the
 * CONFIG_SECTION_xxx on success or -1 on failure.
 *
 */
int
conf_lookup_section (char* name) {
    char* ptr;

    // look them all up
    if (!strcasecmp (name, "general")) return CONFIG_SECTION_GENERAL;
    if (!strcasecmp (name, "sap")) return CONFIG_SECTION_SAP;
    if (!strcasecmp (name, "database")) return CONFIG_SECTION_DATABASE;
    if (!strcasecmp (name, "web")) return CONFIG_SECTION_WEB;
    if (!strcasecmp (name, "link")) return CONFIG_SECTION_LINK;

    // does it begin with 'volume:' ?
    if (!strncasecmp (name, "volume:", 7)) {
	// yes. grab the name after the :
	ptr = strchr (name, ':'); ptr++;

	// is this name longer than 16 chars?
	if (strlen (ptr) > 16) {
	    // yes. complain
	    fprintf (stderr, "%s[%u]: a volume name cannot exceed 16 charachters\n", conf_fname, conf_lineno);
	    exit (0xf0);
	}

	// we got a volume. set up the name and return the return code
	strcpy (conf_tmpvol, ptr);
	return CONFIG_SECTION_VOLUME;
    }

    // can't find it
    return -1;
}

/*
 * conf_lookup_general (char* name)
 *
 * This will look up a general name [name]. It will return any of the
 * CONFIG_GENERAL_xxx on success, or -1 on failure.
 *
 */
int
conf_lookup_general (char* name) {
    // look them al up
    if (!strcasecmp (name, "name")) return CONFIG_GENERAL_NAME;
    if (!strcasecmp (name, "version")) return CONFIG_GENERAL_VERSION;
    if (!strcasecmp (name, "debuglevel")) return CONFIG_GENERAL_DEBUGLEVEL;
    if (!strcasecmp (name, "logfile")) return CONFIG_GENERAL_LOGFILE;
    if (!strcasecmp (name, "interface")) return CONFIG_GENERAL_INTERFACE;
    if (!strcasecmp (name, "user")) return CONFIG_GENERAL_USER;
    if (!strcasecmp (name, "group")) return CONFIG_GENERAL_GROUP;
    if (!strcasecmp (name, "serialno")) return CONFIG_GENERAL_SERIALNO;
    if (!strcasecmp (name, "appno")) return CONFIG_GENERAL_APPNO;
    if (!strcasecmp (name, "filemode")) return CONFIG_GENERAL_FILEMODE;
    if (!strcasecmp (name, "dirmode")) return CONFIG_GENERAL_DIRMODE;
    if (!strcasecmp (name, "sap_update_interval")) return CONFIG_GENERAL_SAPINTERVAL;
    if (!strcasecmp (name, "nofconns")) return CONFIG_GENERAL_NOFCONNS;
    if (!strcasecmp (name, "binderypath")) return CONFIG_GENERAL_BINDERYPATH;
    if (!strcasecmp (name, "trusteemode")) return CONFIG_GENERAL_TRUSTEEMODE;

    // can't find it
    return -1;

}

/*
 * conf_handle_general (char* fname)
 *
 * This will handle general line [line].
 *
 */
void
conf_handle_general (char* line) {
    char* name;
    char* value;
    char* ptr;
    char* tmp;
    int   i, j;
    struct passwd* p;
    struct group* g;

    // scan for an equal sign
    if ((ptr = strchr (line, '=')) == NULL) {
	// there's none. complain
	fprintf (stderr, "%s[%u]: '=' expected\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // grab the name
    name = line; *ptr = 0; value = ptr + 1; ptr = name;
    while (*ptr) { if (*ptr == ' ') *ptr = 0; ptr++; }
    ptr = value;
    while (*ptr == ' ') { if (*ptr == ' ') { value++; }; ptr++; }

    // look up the name
    if ((i = conf_lookup_general (name)) < 0) {
	// this failed. complain
	fprintf (stderr, "%s[%u]: unknown option\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // figure out what to do with it
    switch (i) {
	case CONFIG_GENERAL_NAME: // server name
				  if (strlen (value) > 48) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: server name may not exceed 48 charachters\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }

				  // yay, we got it. copy it to the server name
				  strcpy (conf_servername, value);
				  break;
     case CONFIG_GENERAL_VERSION: // server version
			  	  if ((ptr = strchr (value, '.')) == NULL) {
				      // there's no colon. complain
				      fprintf (stderr, "%s[%u]: '.' expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  *ptr = 0; ptr++;

				  // build the version
				  i = strtol (value, &tmp, 10);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  j = strtol (ptr, &tmp, 10);
				  if (tmp == ptr) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }

				  // setup the version number
				  conf_nwversion = (i * 100) + j;
				  break;
  case CONFIG_GENERAL_DEBUGLEVEL: // debugging level
				  debuglevel = strtol (value, &tmp, 10);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
     case CONFIG_GENERAL_LOGFILE: // log file
				  if (strlen (value) > VOL_MAX_VOL_PATH_LEN) {
				      fprintf (stderr, "%s[%u]: a log filename may not exceed %u charachters\n", conf_fname, conf_lineno, VOL_MAX_VOL_PATH_LEN);
				      exit (0xf0);
				  }

				  // copy the filename
				  strcpy (conf_logfile, value);
				  break;
   case CONFIG_GENERAL_INTERFACE: // interface
				  if (strlen (value) > CONFIG_MAX_IFNAME_LEN) {
				      fprintf (stderr, "%s[%u]: a network interfce name may not exceed %u charachters\n", conf_fname, conf_lineno, CONFIG_MAX_IFNAME_LEN);
				      exit (0xf0);
				  }

				  // copy the interface name
				  strcpy (conf_ifname, value);
				  break;
        case CONFIG_GENERAL_USER: // user
				  if ((p = getpwnam (value)) == NULL) {
				      fprintf (stderr, "%s[%u]: user not found\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }

				  // user ID of zero?
				  if (!p->pw_uid) {
				      fprintf (stderr, "%s[%u]: root users are not allowed\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }

				  // store the user id
				  conf_uid = p->pw_uid;
				  break;
        case CONFIG_GENERAL_GROUP: // group
				  if ((g = getgrnam (value)) == NULL) {
				      fprintf (stderr, "%s[%u]: group not found\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }

				  // store the group id
				  conf_gid = g->gr_gid;
				  break;
    case CONFIG_GENERAL_SERIALNO: // serial number
				  conf_serialno = strtol (value, &tmp, 16);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
       case CONFIG_GENERAL_APPNO: // application number
				  conf_appno = strtol (value, &tmp, 16);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
    case CONFIG_GENERAL_FILEMODE: // file mode
				  conf_filemode = strtol (value, &tmp, 8);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
     case CONFIG_GENERAL_DIRMODE: // dir mode
				  conf_dirmode = strtol (value, &tmp, 8);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
 case CONFIG_GENERAL_SAPINTERVAL: // sap update interval
				  conf_sap_interval = strtol (value, &tmp, 10);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
    case CONFIG_GENERAL_NOFCONNS: // number of connections
				  conf_nofconns = strtol (value, &tmp, 10);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
 case CONFIG_GENERAL_BINDERYPATH: // bindery path
				  if (strlen (value) > VOL_MAX_VOL_PATH_LEN) {
				      fprintf (stderr, "%s[%u]: the bindery path may not exceed %u charachters\n", conf_fname, conf_lineno, VOL_MAX_VOL_PATH_LEN);
				      exit (0xf0);
				  }

				  // copy the filename
				  strcpy (conf_binderypath, value);
				  break;
 case CONFIG_GENERAL_TRUSTEEMODE: // dir mode
				  conf_trusteemode = strtol (value, &tmp, 8);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
    }
}

/*
 * conf_lookup_volume (char* name)
 *
 * This will look up a volume name [name]. It will return any of the
 * CONFIG_VOLUME_xxx on success, or -1 on failure.
 *
 */
int
conf_lookup_volume (char* name) {
    // look them al up
    if (!strcasecmp (name, "path")) return CONFIG_VOLUME_PATH;

    // can't find it
    return -1;
}

/*
 * conf_handle_volume (char* fname)
 *
 * This will handle volume line [line].
 *
 */
void
conf_handle_volume (char* line) {
    char* name;
    char* value;
    char* ptr;
    char* tmp;
    int   i, j;

    // scan for an equal sign
    if ((ptr = strchr (line, '=')) == NULL) {
	// there's none. complain
	fprintf (stderr, "%s[%u]: '=' expected\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // grab the name
    name = line; *ptr = 0; value = ptr + 1; ptr = name;
    while (*ptr) { if (*ptr == ' ') *ptr = 0; ptr++; }
    ptr = value;
    while (*ptr == ' ') { if (*ptr == ' ') { value++; }; ptr++; }

    // look up the name
    if ((i = conf_lookup_volume (name)) < 0) {
	// this failed. complain
	fprintf (stderr, "%s[%u]: unknown option\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // figure out what to do with it
    switch (i) {
	 case CONFIG_VOLUME_PATH: // volume name
				  if (strlen (value) > VOL_MAX_VOL_PATH_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: a volume path may not exceed %u charachters\n", conf_fname, conf_lineno, VOL_MAX_VOL_PATH_LEN);
				      exit (0xf0);
				  }

				  // store the path
				  strcpy (conf_tmpath, value);
				  break;
    }
}

/*
 * conf_lookup_sap (char* name)
 *
 * This will look up a sap option [name]. It will return any of the
 * CONFIG_SAP_xxx on success, or -1 on failure.
 *
 */
int
conf_lookup_sap (char* name) {
    // look them al up
    if (!strcasecmp (name, "enabled")) return CONFIG_SAP_ENABLED;
    if (!strcasecmp (name, "nearest")) return CONFIG_SAP_NEAREST;

    // can't find it
    return -1;
}

/*
 * conf_handle_sap (char* fname)
 *
 * This will handle sap line [line].
 *
 */
void
conf_handle_sap (char* line) {
    char* name;
    char* value;
    char* ptr;
    char* tmp;
    int   i, j;

    // scan for an equal sign
    if ((ptr = strchr (line, '=')) == NULL) {
	// there's none. complain
	fprintf (stderr, "%s[%u]: '=' expected\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // grab the name
    name = line; *ptr = 0; value = ptr + 1; ptr = name;
    while (*ptr) { if (*ptr == ' ') *ptr = 0; ptr++; }
    ptr = value;
    while (*ptr == ' ') { if (*ptr == ' ') { value++; }; ptr++; }

    // look up the name
    if ((i = conf_lookup_sap (name)) < 0) {
	// this failed. complain
	fprintf (stderr, "%s[%u]: unknown option\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // figure out what to do with it
    switch (i) {
	 case CONFIG_SAP_ENABLED: // sap enabled state
				  j = conf_lookup_yesno (value);
				  if (j < 0) {
				      // this failed. complain
				      fprintf (stderr, "%s[%u]: 'yes' or 'no' expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  conf_sap_enabled = j;
			 	  break;
	 case CONFIG_SAP_NEAREST: // sap enabled state
				  j = conf_lookup_yesno (value);
				  if (j < 0) {
				      // this failed. complain
				      fprintf (stderr, "%s[%u]: 'yes' or 'no' expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  conf_sap_nearest = j;
			 	  break;
    }
}

/*
 * conf_add_vol ()
 *
 * This will add the volume declared by a [volume:xxx] section.
 *
 */
void
conf_add_vol() {
    int i;

    // if there is no volume name, just leave
    if (conf_tmpvol[0] == 0) return;

    // uppercase the volume name
    for (i = 0; i < strlen (conf_tmpvol); i++)
	conf_tmpvol[i] = toupper (conf_tmpvol[i]);

    // do we already have this volume?
    i = vol_find_volume (conf_tmpvol);
    if (i != -1) {
	// yes. complain
	fprintf (stderr, "[FATAL] Duplicate volume '%s'\n", conf_tmpvol);
	exit (0xf0);
    }

    // add the volume
    if (vol_add_volume (conf_tmpvol, conf_tmpath, 0) < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] Cannot add volume '%s' with base path '%s'\n", conf_tmpvol, conf_tmpath);
	exit (0xf0);
    }
}

/*
 * conf_lookup_database(char* name)
 *
 * This will look up a database name [name]. It will return any of the
 * CONFIG_DATABASEL_xxx on success, or -1 on failure.
 *
 */
int
conf_lookup_database (char* name) {
    // look them al up
    if (!strcasecmp (name, "hostname")) return CONFIG_DATABASE_HOSTNAME;
    if (!strcasecmp (name, "username")) return CONFIG_DATABASE_USERNAME;
    if (!strcasecmp (name, "password")) return CONFIG_DATABASE_PASSWORD;
    if (!strcasecmp (name, "database")) return CONFIG_DATABASE_DATABASE;
    
    // can't find it
    return -1;
}

/*
 * conf_handle_database (char* fname)
 *
 * This will handle database line [line].
 *
 */
void
conf_handle_database (char* line) {
    char* name;
    char* value;
    char* ptr;
    char* tmp;
    int   i, j;

    // scan for an equal sign
    if ((ptr = strchr (line, '=')) == NULL) {
	// there's none. complain
	fprintf (stderr, "%s[%u]: '=' expected\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // grab the name
    name = line; *ptr = 0; value = ptr + 1; ptr = name;
    while (*ptr) { if (*ptr == ' ') *ptr = 0; ptr++; }
    ptr = value;
    while (*ptr == ' ') { if (*ptr == ' ') { value++; }; ptr++; }

    // look up the name
    if ((i = conf_lookup_database (name)) < 0) {
	// this failed. complain
	fprintf (stderr, "%s[%u]: unknown option\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // figure out what to do with it
    switch (i) {
   case CONFIG_DATABASE_HOSTNAME: // database server hostname
				  if (strlen (value) > CONFIG_MAX_SQL_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: a database value may not exceed %u charachters\n", conf_fname, conf_lineno, CONFIG_MAX_SQL_LEN);
				      exit (0xf0);
				  }
				  strcpy (conf_sql_hostname, value);
				  break;
   case CONFIG_DATABASE_USERNAME: // database username
				  if (strlen (value) > CONFIG_MAX_SQL_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: a database value may not exceed %u charachters\n", conf_fname, conf_lineno, CONFIG_MAX_SQL_LEN);
				      exit (0xf0);
				  }
				  strcpy (conf_sql_username, value);
				  break;
   case CONFIG_DATABASE_PASSWORD: // database password
				  if (strlen (value) > CONFIG_MAX_SQL_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: a database value may not exceed %u charachters\n", conf_fname, conf_lineno, CONFIG_MAX_SQL_LEN);
				      exit (0xf0);
				  }
				  strcpy (conf_sql_password, value);
				  break;
   case CONFIG_DATABASE_DATABASE: // database database
				  if (strlen (value) > CONFIG_MAX_SQL_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: a database value may not exceed %u charachters\n", conf_fname, conf_lineno, CONFIG_MAX_SQL_LEN);
				      exit (0xf0);
				  }
				  strcpy (conf_sql_database, value);
				  break;
    }
}

/*
 * conf_lookup_web (char* name)
 *
 * This will look up a web name [name]. It will return any of the
 * CONFIG_WEB_xxx on success, or -1 on failure.
 *
 */
int
conf_lookup_web (char* name) {
    // look them al up
    if (!strcasecmp (name, "enabled")) return CONFIG_WEB_ENABLED;
    if (!strcasecmp (name, "port")) return CONFIG_WEB_PORT;
    if (!strcasecmp (name, "templatedir")) return CONFIG_WEB_TEMPLATEDIR;
    if (!strcasecmp (name, "username")) return CONFIG_WEB_USERNAME;
    if (!strcasecmp (name, "password")) return CONFIG_WEB_PASSWORD;

    // can't find it
    return -1;
}


/*
 * conf_handle_web (char* fname)
 *
 * This will handle web line [line].
 *
 */
void
conf_handle_web (char* line) {
    char* name;
    char* value;
    char* ptr;
    char* tmp;
    int   i, j;

    // scan for an equal sign
    if ((ptr = strchr (line, '=')) == NULL) {
	// there's none. complain
	fprintf (stderr, "%s[%u]: '=' expected\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // grab the name
    name = line; *ptr = 0; value = ptr + 1; ptr = name;
    while (*ptr) { if (*ptr == ' ') *ptr = 0; ptr++; }
    ptr = value;
    while (*ptr == ' ') { if (*ptr == ' ') { value++; }; ptr++; }

    // look up the name
    if ((i = conf_lookup_web (name)) < 0) {
	// this failed. complain
	fprintf (stderr, "%s[%u]: unknown option\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // figure out what to do with it
    switch (i) {
         case CONFIG_WEB_ENABLED: // enabled status
				  j = conf_lookup_yesno (value);
				  if (j < 0) {
				      // this failed. complain
				      fprintf (stderr, "%s[%u]: 'yes' or 'no' expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  conf_web_enabled = j;
				  break;
	    case CONFIG_WEB_PORT: // port number
				  conf_web_port = strtol (value, &tmp, 10);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
     case CONFIG_WEB_TEMPLATEDIR: // template directory
				  if (strlen (value) > VOL_MAX_VOL_PATH_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: a template path may not exceed %u charachters\n", conf_fname, conf_lineno, VOL_MAX_VOL_PATH_LEN);
				      exit (0xf0);
				  }

				  // store the path
				  strcpy (conf_web_templatedir, value);
				  break;
	case CONFIG_WEB_USERNAME: // username
				  if (strlen (value) > WEB_MAX_VALUE_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: an username may not exceed %u charachters\n", conf_fname, conf_lineno, WEB_MAX_VALUE_LEN);
				      exit (0xf0);
				  }

				  // store the username
				  strcpy (conf_web_username, value);
				  break;
	case CONFIG_WEB_PASSWORD: // password
				  if (strlen (value) > WEB_MAX_VALUE_LEN) {
				      // it's too long. complain
				      fprintf (stderr, "%s[%u]: a password may not exceed %u charachters\n", conf_fname, conf_lineno, WEB_MAX_VALUE_LEN);
				      exit (0xf0);
				  }

				  // store the password
				  strcpy (conf_web_password, value);
				  break;
    }
}

/*
 * conf_lookup_link (char* name)
 *
 * This will look up a link name [name]. It will return any of the
 * CONFIG_LINK_xxx on success, or -1 on failure.
 *
 */
int
conf_lookup_link (char* name) {
    // look them al up
    if (!strcasecmp (name, "enabled")) return CONFIG_LINK_ENABLED;
    if (!strcasecmp (name, "key")) return CONFIG_LINK_KEY;

    // can't find it. must be a server entry
    return CONFIG_LINK_SERVER;
}

/*
 * conf_link_addserver (char* name, char* address)
 *
 * This add server [name] with address [address] to the console. This will
 * return zero on success or -1 on failure.
 *
 */
int
conf_link_addserver (char* name, char* address) {
    unsigned long long l;
    char* ptr;
    int i;
  
    // copy the name into the record
    strncpy (conf_link_servers[conf_link_nofservers].servername, name, 48);
    l = strtoull (address, &ptr, 0x10);
    if (*ptr) {
	// this failed. return negative status
	return -1;
    }

    // upercase the seer name
    for (i = 0; i < strlen (conf_link_servers[conf_link_nofservers].servername); i++)
	conf_link_servers[conf_link_nofservers].servername[i] = toupper (conf_link_servers[conf_link_nofservers].servername[i]);

    // yay, this worked. copy the address
    conf_link_servers[conf_link_nofservers].addr[5] = (l & 0xff);
    conf_link_servers[conf_link_nofservers].addr[4] = (l >>   8);
    conf_link_servers[conf_link_nofservers].addr[3] = (l >>  16);
    conf_link_servers[conf_link_nofservers].addr[2] = (l >>  24);
    conf_link_servers[conf_link_nofservers].addr[1] = (l >>  32);
    conf_link_servers[conf_link_nofservers].addr[0] = (l >>  40);

    // increment the server count
    conf_link_nofservers++;

    // it worked
    return 0;
}

/*
 * conf_handle_link (char* fname)
 *
 * This will handle link line [line].
 *
 */
void
conf_handle_link (char* line) {
    char* name;
    char* value;
    char* ptr;
    char* tmp;
    int   i, j;

    // scan for an equal sign
    if ((ptr = strchr (line, '=')) == NULL) {
	// there's none. complain
	fprintf (stderr, "%s[%u]: '=' expected\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // grab the name
    name = line; *ptr = 0; value = ptr + 1; ptr = name;
    while (*ptr) { if (*ptr == ' ') *ptr = 0; ptr++; }
    ptr = value;
    while (*ptr == ' ') { if (*ptr == ' ') { value++; }; ptr++; }

    // look up the name
    if ((i = conf_lookup_link (name)) < 0) {
	// this failed. complain
	fprintf (stderr, "%s[%u]: unknown option\n", conf_fname, conf_lineno);
	exit (0xf0);
    }

    // figure out what to do with it
    switch (i) {
        case CONFIG_LINK_ENABLED: // enabled status
				  j = conf_lookup_yesno (value);
				  if (j < 0) {
				      // this failed. complain
				      fprintf (stderr, "%s[%u]: 'yes' or 'no' expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  conf_link_enabled = j;
				  break;
	    case CONFIG_LINK_KEY: // link key
				  conf_link_key = strtol (value, &tmp, 16);
				  if (tmp == value) {
				      // we need a number.
				      fprintf (stderr, "%s[%u]: number expected\n", conf_fname, conf_lineno);
				      exit (0xf0);
				  }
				  break;
	 case CONFIG_LINK_SERVER: // server. did we run out of them ?
    				  if (conf_link_nofservers == LINK_MAX_SERVERS) {
				      // yes. this failed
				      fprintf (stderr, "%s[%u]: we can't add more servers, %u is the limit. Server '%s' not added'\n", conf_fname, conf_lineno, LINK_MAX_SERVERS, name);
				  } else {
				      // no. add the server
				      if (conf_link_addserver (name, value) < 0) {
				          // this failed. complain
				          fprintf (stderr, "%s[%u]: servers must be added in the form 'name=mac address'\n", conf_fname, conf_lineno);
				          exit (0xf0);
				      }
				  }
				  break;
    }
}

/*
 * parse_config (char* fname, int flags)
 *
 * This will parse configuration file [fname]. If [fname] is NULL, it will
 * use the default. [flags] are the flags about how strict we are.
 *
 */
void
parse_config (char* fname, int flags) {
    FILE*	f;
    char	line[FROMMEL_MAX_CONFIG_LINE];
    char*	ptr;
    int		section = -1;
    int		i;

    // set everything to the defaults
    conf_servername[0] = 0; conf_logfile[0] = 0; conf_ifname[0] = 0;
    conf_binderypath[0] = 0; conf_sql_hostname[0] = 0;
    conf_sql_username[0] = 0; conf_sql_password[0] = 0;
    conf_sql_database[0] = 0; conf_web_templatedir[0] = 0;
    conf_web_username[0] = 0; conf_web_password[0] = 0;

    // need to use the default file?
    if (fname == NULL) {
	// yes. do it
	conf_fname = CONFIG_FILE;
    } else {
	// no. use the name passed
	conf_fname = fname;
    }

    // open the file
    if ((f = fopen (conf_fname, "rt")) == NULL) {
	// this failed. complain
	fprintf (stderr, "[FATAL] Cannot open configuration file '%s'\n", conf_fname);
	exit (0xf0);
    }

    // keep reading lines until we've done all we can
    conf_tmpvol[0] = 0;
    while (fgets (line, FROMMEL_MAX_CONFIG_LINE, f)) {
	// get rid of any newline
	while ((ptr = strchr (line, '\n'))) *ptr = 0;

	// is this line not blank ?
	if (strlen (line)) {
	    // yes. is it commented?
	    if (line[0] != '#') {
		// no. is it a heading?
		if ((ptr = strchr (line, ']')) != NULL) {
		    // yes. get rid of that charachter
		    *ptr = 0;

		    // scan for the '['
		    if ((ptr = strchr (line, '[')) == NULL) {
			// there is none. complain
			fprintf (stderr, "%s[%u]: '[' expected\n", conf_fname, conf_lineno);
			exit (0xf0);
		    }

		    // was the previous section a volume?
		    if (section == CONFIG_SECTION_VOLUME) {
			// yes. add the volume
			conf_add_vol();
		    }

		    // reset the volume name
    		    conf_tmpvol[0] = 0;

		    // look up the section
		    if ((section = conf_lookup_section (line + 1)) < 0) {
			// there's no such section. complain
			fprintf (stderr, "%s[%u]: unknown section\n", conf_fname, conf_lineno);
			exit (0xf0);
		    }
		} else {
		    // it's a line we need to parse. take care of it
		    switch (section) {
			case CONFIG_SECTION_GENERAL: // general section
						     conf_handle_general (line);
						     break;
			 case CONFIG_SECTION_VOLUME: // volume section
						     conf_handle_volume (line);
						     break;
			    case CONFIG_SECTION_SAP: // sap section
						     conf_handle_sap (line);
						     break;
		       case CONFIG_SECTION_DATABASE: // database section
						     conf_handle_database (line);
						     break;
			    case CONFIG_SECTION_WEB: // web section
						     conf_handle_web (line);
						     break;
			   case CONFIG_SECTION_LINK: // link section
						     conf_handle_link (line);
						     break;
					    default: // no section!
						     fprintf (stderr, "%s[%u]: '[' expected\n", conf_fname, conf_lineno);
						     exit (0xf0);
		    }
		}
	    }
	}

	conf_lineno++;
    }

    // was the previous section a volume?
    if (section == CONFIG_SECTION_VOLUME) {
	// yes. add the volume
	conf_add_vol();
    }

    // close the file
    fclose (f);

    // need to verify the volumes?
    if ((flags & CONFIG_FLAG_NOVOLCHECK) == 0) {
	// yes. do we have a SYS volume ?
	i = vol_find_volume ("SYS");
	if (i < 0) {
	    // no. complain
	    fprintf (stderr, "[FATAL] No SYS volume declared, but it is required\n");
	    exit (0xf0);
	}

	// is it the first volume?
	if (i != 0) {
	    // no. complain
	    fprintf (stderr, "[FATAL] The SYS volume must be the first volume declared\n");
	   exit (0xf0);
	} 
    }

    // got a server name?
    if (!strlen (conf_servername)) {
	// no. complain
	fprintf (stderr, "[FATAL] You must set up a server name\n");
	exit (0xf0);
    }

    // is a version given?
    if (!conf_nwversion) {
	// no. complain
	fprintf (stderr, "[FATAL] You must set up a server version number\n");
	exit (0xf0);
    }

    // got an interface name?
    if (!strlen (conf_ifname)) {
	// no. complain
	fprintf (stderr, "[FATAL] You must set up the interface to bind to\n");
	exit (0xf0);
    }

    // got a bindery path?
    if (!strlen (conf_binderypath)) {
	// no. complain
	fprintf (stderr, "[FATAL] You must set up the bindery path\n");
	exit (0xf0);
    }

    // is the web interface enabled?
    if (conf_web_enabled) {
	// yes. is a template path given?
	if (!strlen (conf_web_templatedir)) {
	    // no. complain
	    fprintf (stderr, "[FATAL] You must set up the template path if you intent to use the web interface\n");
	    exit (0xf0);
	}

	// do we have an username and a password?
	if ((!strlen (conf_web_username)) || (!strlen (conf_web_password))) {
	    // no. complain
	    fprintf (stderr, "[FATAL] You must set up an username and password ifyou intend to use the web interface\n");
	    exit (0xf0);
	}
    }

    // convert the server name to uppercase
    for (i = 0; i < strlen (conf_servername); i++)
	conf_servername[i] = toupper (conf_servername[i]);
}
