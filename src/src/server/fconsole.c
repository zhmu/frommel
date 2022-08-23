/*
 * Frommel 2.0 - File Server Console
 *
 * (c) 2001 Rink Springer
 * 
 */
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bindery.h"
#include "config.h"
#include "conn.h"
#include "fs.h"
#include "ncp.h"
#include "net.h"
#include "misc.h"
#include "sap.h"
#include "trustee.h"

#define MAX_LINE_LEN	512

CONNECTION* conn[65535];
char* config_file = NULL;
int   down_flag = 0;

int	volno = 0;
char    path[FS_MAX_VOL_PATH_LEN];

int trust_check_rights (int c, int dh, char* path, uint16 right) { return 0; }
void down_event (int i) { };

/*
 * parse_options (int argc, char** argv)
 *
 * This will parse all command line options passed to the program.
 *
 */
void
parse_options (int argc, char** argv) {
    char ch;
    char* ptr;

    frommel_options = 0;
    while ((ch = getopt (argc, argv, "rhl:d:")) != -1) {
	switch (ch) {
		case 'h': // help
			  fprintf (stderr, "Frommel %s - (c) 2001 Rink Springer\n\n", FROMMEL_VERSION);
			  fprintf (stderr, "Usuage: bindutil [-h]\n\n");
			  fprintf (stderr, "        -h			Show this help\n");
	  		  exit (1);
	}
    }
}

/*
 * resolve_bindery_right (char c)
 *
 * This will resolve the charachter [c] to a number which resembles the
 * bindery right. It will return -1 on failure or otherwise the number.
 *
 */
int
resolve_bindery_right (char c) {
    // figure it out
    switch (tolower (c)) {
	case 'a': // anyone
		  return 0;
	case 'l': // logged-in
		  return 1;
	case 'o': // object
		  return 2;
	case 's': // supervisor
		  return 3;
	case 'n': // netware only
		  return 4;
    }

    // what's this?
    return -1;
}

/*
 * resolve_errorcode (int errcode)
 *
 * This will resolve error code [errcode] to something human-readable.
 * 
 */
char*
resolve_errorcode (int errcode) {
    // figure it out
    switch (errcode) {
				case 0: return "no error";
	case BINDERY_ERROR_BADPASSWORD: return "bad password";
       case BINDERY_ERROR_NOSUCHMEMBER: return "no such member";
	 case BINDERY_ERROR_PROPEXISTS: return "property exists";
	   case BINDERY_ERROR_SECURITY: return "security error";
       case BINDERY_ERROR_NOSUCHOBJECT: return "no such object";
     case BINDERY_ERROR_NOSUCHPROPERTY: return "no such property";
     	    case BINDERY_ERROR_IOERROR: return "IO/general error";
    }

    // what's this?
    return "unknown error";
}

/*
 * resolve_obj_type (char* arg)
 *
 * This will resolve an object's type string to the netware type value, or
 * -1 if the type is unknown.
 *
 */
int
resolve_obj_type (char* arg) {
    // could it be an user object?
    if (!strcasecmp (arg, "user")) {
	// yes, it's an user
	return 1;
    } else if (!strcasecmp (arg, "group")) {
	// it's a group
	return 2;
    } else if (!strcasecmp (arg, "server")) {
	// it's a server
	return 4;
    }

    // what's this?
    return -1;
}

/*
 * resolve_rights (uint16 rights, char* dest)
 *
 * This will resolve rights [rights] into a human-readable form in [dest].
 * [dest] must be at least 11 bytes long.
 *
 */
void
resolve_rights (uint16 rights, char* dest) {
    strcpy (dest, "[");
    strcat (dest, (rights &   TRUST_RIGHT_SUPERVISORY) ? "S" : " ");
    strcat (dest, (rights & 	     TRUST_RIGHT_READ) ? "R" : " ");
    strcat (dest, (rights & 	    TRUST_RIGHT_WRITE) ? "W" : " ");
    strcat (dest, (rights &        TRUST_RIGHT_CREATE) ? "C" : " ");
    strcat (dest, (rights & 	    TRUST_RIGHT_ERASE) ? "E" : " ");
    strcat (dest, (rights &        TRUST_RIGHT_MODIFY) ? "M" : " ");
    strcat (dest, (rights & 	 TRUST_RIGHT_FILESCAN) ? "F" : " ");
    strcat (dest, (rights & TRUST_RIGHT_ACCESSCONTROL) ? "A" : " ");
    strcat (dest, "]");
}

/*
 * handle_line (char* line)
 *
 * This will handle line [line].
 *
 */
void
handle_line (char* line) {
    char*		arg;
    char*		arg2;
    char*		arg3;
    char*		arg4;
    char*		ptr;
    int	  		i, j, type;
    uint8		errcode;
    char		tmp[FS_MAX_VOL_PATH_LEN];
    char		tmpath[FS_MAX_VOL_PATH_LEN];
    char		realpath[FS_MAX_VOL_PATH_LEN];
    TRUST_DSKRECORD	rec;
    TRUST_RECORD	trustee;
    FILE*		f;
    struct stat		st;
    DIR*		dir;
    struct dirent*	dent;
    BINDERY_OBJ		obj;

    // grab the very first space
    if ((arg = strchr (line, ' ')) != NULL) {
	// we got it. now, turn it into a NULL and increment the pointer so
	// we have the argument
	*arg = 0; arg++;
    } else {
	// we have no spaces. let [arg] point to the terminating zero, then
	arg = strchr (line, 0);
    }

    // do we need to dump something ?
    if (!strcasecmp (line, "dump")) {
	// yeah... could it be the bindery?
	if (!strcasecmp (arg, "bindery")) {
	    // yes. do it
	    printf ("Dumping bindery\n---------------\n");
	    bindio_dump();
	    printf ("---------------\n");
	    return;
	}

	// we don't know what to dump. complain
	printf ("Can't dump that\n");
	return;
    }

    // need to list something?
    if (!strcasecmp (line, "list")) {
	// yes... could it be the volumes?
	if (!strcasecmp (arg, "volumes")) {
	    // yes. do it
	    printf ("Dumping volumes\n---------------\n");

	    // list 'em all
	    for (i = 0; i < FS_MAX_VOLS; i++) {
		// is this volume in use?
		if ((volume[i].flags & FS_VOL_USED)) {
		    // yes. show the information
		    printf ("Volume #%02u: Name '%s', UNIX path '%s'\n", i, volume[i].name, volume[i].path);
		}
	    }

	    // get out of here
	    return;
	}

	// need to list the trustees?
	if (!strcasecmp (arg, "trustees")) {
	    // yes. build the trustee's filename
	    snprintf (tmp, FS_MAX_VOL_PATH_LEN, "%s/%s/%s", volume[volno].path, path, TRUST_SYSFILE);

	    // can we open this file?
	    if ((f = fopen (tmp, "rb")) == NULL) {
		// no. there are no trustees for this path, then
		printf ("No trustees for this path\n");
		return;
	    }

	    // show a nice heading
	    printf ("Trustee list\n------------\n");

	    // walk through the entire trustee datafile
	    while (fread (&rec, sizeof (TRUST_DSKRECORD), 1, f)) {
		// convert the structure
		trustee.obj_id = (rec.obj_id[0] << 24) | (rec.obj_id[1] << 16) |
				 (rec.obj_id[2] <<  8) | (rec.obj_id[3]);
		trustee.rights = (rec.rights[1] <<  8) | (rec.rights[0]);

		// get the object's name
		if (bindio_scan_object_by_id (trustee.obj_id, NULL, &obj) < 0) {
		    // this failed. use a question mark
		    strcpy ((char*)obj.name, "?");
		}

		// resolve the rights
		resolve_rights (trustee.rights, (char*)tmp);

		// show the trustee
		printf ("%s %s\n", tmp, obj.name);
	    }

	    // show a nice footer
	    printf ("------------\n");

	    // close the trustee file
	    return;
	}

	// we don't know what to list. complain
	printf ("Can't list that\n");
	return;
    }

    // need to change the path?
    if (!strcasecmp (line, "cd")) {
	// yeah. make a copy of the path first
	strcpy (tmpath, path);

	// do we need to go one path down ?
	if (!strcmp (arg, "..")) {
	    // yes. scan the path from the right side until the slash
	    if ((ptr = strrchr (path, '/')) != NULL) {
		// all set. just turn this pointer in a null
		*ptr = 0;
	    } else {
		// we can't go down. inform the user
		printf ("Can't go below the root directory\n");
		return;
	    }
	} else {
	    // add the path
	    strcat (path, "/"); strcat (path, arg);
	}

	// build the real path
	snprintf (realpath, FS_MAX_VOL_PATH_LEN, "%s/%s", volume[volno].path, path);

	// does this path exist?
	if (stat (realpath, &st) < 0) {
	    // no. restore the old path and complain
	    strcpy (path, tmpath);
	    printf ("Sorry, but that path doesn't exist\n");
	    return;
	}

	// we did it
	return;
    }

    // need to show the files?
    if ((!strcasecmp (line, "dir")) || (!strcmp (line, "ls"))) {
	// yes. build the real path
	snprintf (realpath, FS_MAX_VOL_PATH_LEN, "%s/%s", volume[volno].path, path);

	// open this directory
	if ((dir = opendir (realpath)) == NULL) {
	    // this failed. complain
	    printf ("Cannot open directory\n");
	    return;
	}

	// show a nice header	
	printf ("Directory contents\n------------------\n");	

	// list all files
	while ((dent = readdir (dir)) != NULL) {
	    // we've got an entry. show it
	    printf ("%s\n", dent->d_name);
	}

	// show a nice footer
	printf ("------------------\n");

	// close the directory
	closedir (dir);

	return;
    }

    // need to create something?
    if (!strcasecmp (line, "create")) {
	// yes. grab the second argument
	if ((arg2 = strchr (arg, ' ')) == NULL) {
	    // there is none. complain
	    printf ("Not enough arguments\n");
	    return;
	}

	// isolate the second argument
	*arg2 = 0; arg2++;

	// grab the third argument
	if ((arg3 = strchr (arg2, ' ')) == NULL) {
	    // there is none. complain
	    printf ("Not enough arguments\n");
	    return;
	}

	// isolate the second argument
	*arg3 = 0; arg3++;

	// grab the fourth argument
	if ((arg4 = strchr (arg3, ' ')) == NULL) {
	    // there is none. complain
	    printf ("Not enough arguments\n");
	    return;
	}

	// isolate the second argument
	*arg4 = 0; arg4++;

	// need to create an object?
	if (!strcasecmp (arg, "object")) {
	    // yes. figure out what kind of object this would be
	    if ((type = resolve_obj_type (arg2)) < 0) {
		// what's this?
		printf ("Unknown object type '%s'\n", arg2);
		return;
	    }

	    // is the access argument a neat 2 byte string?
	    if (strlen (arg4) != 2) {
		// no. complain
		printf ("Invalid rights mask '%s'\n", arg4);
		return;
	    }

	    // try to resolve the first right
	    if ((i = resolve_bindery_right (arg4[0])) < 0) {
		// this is unknown. 
		printf ("Invalid rights mask '%c'\n", arg4[0]);
		return;
	    }
	    if ((j = resolve_bindery_right (arg4[1])) < 0) {
		// this is unknown. 
		printf ("Invalid rights mask '%c'\n", arg4[1]);
		return;
	    }

	    // combine the masks
	    i = (i << 4) + j;

	    // add the object
	    if (bindio_add_object (arg2, type, 0, i, &errcode) < 0) {
		// this failed. complain
		printf ("Failure: %s\n", resolve_errorcode (errcode));
		return;
	    }

	    // we've done our thing
	    return;
	} else if (!strcasecmp (arg, "property")) {
	    // we need to create a property. scan for the object name

	}

	// unknown type, complain
	printf ("Type '%s' is not known\n", arg);
	return;
    }

    // need to show the help?
    if (!strcasecmp (line, "help")) {
	// yes. is a command given?
	if (!strlen (arg)) {
	    // yes. show the information
	    printf ("valid commands: dump, list, cd, dir, ls, create, help\n");
	    printf ("use help [command] for more information\n");
	} else {
	    // give the help
	    if (!strcasecmp (arg, "dump")) {
		printf ("dump {bindery} -- this will dump a certain structure\n");
	    } else if (!strcasecmp (arg, "list")) {
		printf ("list {volumes | trustees} -- this will list information\n");
	    } else if (!strcasecmp (arg, "cd")) {
		printf ("cd [path] -- this will change the current directory\n");
	    } else if ((!strcasecmp (arg, "dir")) || (!strcasecmp (arg, "ls"))) {
		printf ("dir / ls -- this will show all files in the current directory\n");
	    } else if (!strcasecmp (arg, "create")) {
		printf ("create object {user | group | server | ...} [name] {security}  -- this will create an object\n");
		printf ("create property {user | group | server | ...} [name] [objname] [security]  -- this will create a property\n");
	    } else if (!strcasecmp (arg, "quit")) {
		printf ("quit -- this will leave the console\n");
	    } else {
		printf ("this function isn't known to us. please use 'help' for a listing of all functions\n");
	    }
	}

	// we've done our thing
	return;
    }

    // do we need to quit?
    if ((!strcasecmp (line, "quit")) || (feof (stdin))) {
	// yeah. farewell
	printf ("OK, seeya!\n");
	exit (0);
    }

    // this is unsupported...
    printf ("Unsupported command\n");
}

/*
 * main (int argc,char** argv)
 *
 * This is the main code
 * 
 */
int
main (int argc,char** argv) {
    int seq = 0, i, j;
    TRUST_RECORD rec;
    char  tmp[FS_MAX_VOL_PATH_LEN];
    char  line[MAX_LINE_LEN + 1];

    // seed the random number generator
    srandomdev();

    // handle all options
    parse_options (argc, argv);

    // initialize the filesystem
    fs_init();

    // parse the configuration file
    parse_config (config_file, 0);

    // try to open the bindery datafiles
    if (!bindio_open ("r+b") < 0) {
	// this failed. complain
	fprintf (stderr, "[FATAL] Cannot open bindery files in '%s'\n", conf_binderypath);
	exit (1);
    }

    // do this forever
    strcpy (path, "");
    while (1) {
	// build a nice path
	j = 0; bzero (tmp, FS_MAX_VOL_PATH_LEN);
	for (i = 0; i < strlen (path); i++) {
	    // is the first char a '/'?
	    if (((i == 0) && (path[0] != '/')) || (i > 0))
		// no, it's not. add the charachter, in uppercase
		tmp[j++] = toupper (path[i]);
	}

	// out of input?
	if (feof (stdin)) {
	    // yes. leave
	    printf ("OK, seeya\n");
	    exit (0);
	}

	// get the input
	fprintf (stderr, "[%s:%s] > ", volume[volno].name, tmp);
	fgets (line, MAX_LINE_LEN, stdin); line[strlen (line) - 1] = 0;

	// handle the line
	handle_line (line);
    }
}
