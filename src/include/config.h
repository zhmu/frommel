/*
 * Frommel 2.0
 *
 * (c) 2001 Rink Springer
 * 
 */
#include "defs.h"
#include "fs.h"
#include "link.h"
#include "misc.h"
#include "volume.h"

#ifndef __CONFIG_H__
#define __CONFIG_H__

// CONFIG_FILE is the default configuration file
#define CONFIG_FILE		"/etc/frommel.conf"

// PACKET_BUFSIZE is the size of read/write packets we want.
#define PACKET_BUFSIZE		2048

// PACKET_MAXLEN is the maximum length of an IPX packet
#define PACKET_MAXLEN		2048

// SAP_MAX_SERVICES is the maximum number of services the stand-alone SAP
// daemon will advertise
#define SAP_MAX_SERVICES	16

// if ALLOW_CONSOLE_COMMANDS is defined, the server will support special
// commands in the form of SEND #command TO CONSOLE from clients. Only
// supervisor (equivalent) users can use this.
#define ALLOW_CONSOLE_COMMANDS

// SERVER_DOWN_TIMEOUT is the amount of seconds the server will wait before
// it downs itself (when a down server command is given)
#define SERVER_DOWN_TIMEOUT	30

// SQL_MAX_QUERY_LENGTH is the maximum length of a SQL query string
#define SQL_MAX_QUERY_LENGTH	8192

// WEB_MAX_PACKET_LEN is the maximum packet length for WEB service packets
#define WEB_MAX_PACKET_LEN	4096

// WEB_MAX_xxx_LEN are the maximum lengths we allow
#define    WEB_MAX_LINE_LEN	4096
#define    WEB_MAX_VALUE_LEN	1024

// WEB_MAX_CONNS is the maximum number of connections we will allow
#define WEB_MAX_CONNS		4

// CONN_TIMEOUT_LENGTH is the time (in seconds) we will wait we send watchdog
// connections to a seemingly idle connection.
#define CONN_TIMEOUT_LENGTH	300

// SAP_TIMEOUT_LENGTH is the time (in seconds) we will wait before we drop
// a seemingly dead server from the list.
#define SAP_TIMEOUT_LENGTH	150

// ----- PLEASE DO NOT CHANGE ANYTHING BELOW THIS ----------------------------

// FS_DESC_xxx are the file server description strings
#define FS_DESC_COMPANY		"Frommel"
#define FS_DESC_REVISION	"Version "FROMMEL_VERSION
#define FS_DESC_REVDATE		"28 Aug 2001"
#define FS_DESC_COPYRIGHT	"(c) 2001 Rink Springer"

// FROMMEL_VERSION is the version number of our daemon
#define FROMMEL_VERSION		"2.0a"

// CONFIG_FLAG_xxx are the flags for the configuration file reader
#define CONFIG_FLAG_NOVOLCHECK	1

// CONFIG_SECTION_xxx are the configuration file sections
#define CONFIG_SECTION_GENERAL		0
#define CONFIG_SECTION_VOLUME		1
#define CONFIG_SECTION_SAP		2
#define CONFIG_SECTION_DATABASE		3
#define CONFIG_SECTION_WEB		4
#define CONFIG_SECTION_LINK		5

// CONFIG_GENERAL_xxx are the general configuration options
#define CONFIG_GENERAL_NAME		0
#define CONFIG_GENERAL_VERSION		1
#define CONFIG_GENERAL_DEBUGLEVEL	2
#define CONFIG_GENERAL_LOGFILE		3
#define CONFIG_GENERAL_INTERFACE	4
#define CONFIG_GENERAL_USER		5
#define CONFIG_GENERAL_GROUP		6
#define CONFIG_GENERAL_SERIALNO		7
#define CONFIG_GENERAL_APPNO		8
#define CONFIG_GENERAL_FILEMODE		9
#define CONFIG_GENERAL_DIRMODE		10
#define CONFIG_GENERAL_SAPINTERVAL	11
#define CONFIG_GENERAL_NOFCONNS		12
#define CONFIG_GENERAL_BINDERYPATH	13
#define CONFIG_GENERAL_TRUSTEEMODE	14

// CONFIG_VOLUME_xxx are the volume configuration options
#define CONFIG_VOLUME_PATH		0

// CONFIG_SAP_xxx are the SAP options
#define CONFIG_SAP_ENABLED		0
#define CONFIG_SAP_NEAREST		1

// CONFIG_DATABASE_xxx are the database options
#define CONFIG_DATABASE_HOSTNAME	0	
#define CONFIG_DATABASE_USERNAME	1
#define CONFIG_DATABASE_PASSWORD	2
#define CONFIG_DATABASE_DATABASE	3

// CONFIG_WEB_xxx are the web options
#define CONFIG_WEB_ENABLED		0
#define CONFIG_WEB_PORT			1
#define CONFIG_WEB_TEMPLATEDIR		2
#define CONFIG_WEB_USERNAME		3
#define CONFIG_WEB_PASSWORD		4

// CONFIG_LINK_xxx are the link options
#define CONFIG_LINK_ENABLED		0
#define CONFIG_LINK_KEY			1
#define CONFIG_LINK_SERVER		2

// CONFIG_MAX_IFNAME_LEN is the maximum length of the interface to bind to
#define CONFIG_MAX_IFNAME_LEN		8

// CONFIG_MAX_SQL_LEN is the maximum length of any SQL configuration string
#define CONFIG_MAX_SQL_LEN		64

// CONFIG_DEFAULT_xxx are the default values
#define CONFIG_DEFAULT_SERIALNO		0xdeadc0de
#define CONFIG_DEFAULT_APPNO		0xbabe
#define CONFIG_DEFAULT_FILEMODE		0x180		// rw-------
#define CONFIG_DEFAULT_DIRMODE		0x1c0		// rwx------
#define CONFIG_DEFAULT_SAPINTERVAL	30
#define CONFIG_DEFAULT_NOFCONNS		25
#define CONFIG_DEFAULT_TRUSTMODE	0x180		// rw-------

extern char conf_servername[49];
extern char conf_logfile[VOL_MAX_VOL_PATH_LEN];
extern char conf_binderypath[VOL_MAX_VOL_PATH_LEN];
extern char conf_ifname[CONFIG_MAX_IFNAME_LEN];
extern char conf_sql_hostname[CONFIG_MAX_SQL_LEN];
extern char conf_sql_username[CONFIG_MAX_SQL_LEN];
extern char conf_sql_password[CONFIG_MAX_SQL_LEN];
extern char conf_sql_database[CONFIG_MAX_SQL_LEN];
extern int  conf_nwversion;
extern int  conf_uid, conf_gid;
extern int  conf_serialno, conf_appno;
extern int  conf_filemode, conf_dirmode, conf_trusteemode;
extern int  conf_sap_interval;
extern int  conf_nofconns;
extern int  conf_sap_enabled;
extern int  conf_sap_nearest;
extern int  conf_web_enabled;
extern int  conf_web_port;
extern char conf_web_templatedir[VOL_MAX_VOL_PATH_LEN];
extern char conf_web_username[WEB_MAX_VALUE_LEN];
extern char conf_web_password[WEB_MAX_VALUE_LEN];
extern int  conf_link_enabled;
extern int  conf_link_key;
extern int  conf_link_nofservers;
extern LINK_SERVER conf_link_servers[LINK_MAX_SERVERS];

void parse_config (char*, int);

#endif
