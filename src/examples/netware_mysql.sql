#
# netware_mysql.sql
#
# This SQL file will create the tables for MySQL. Use it like
#
# ~/frommel$ mysql -u root -p netware < netware_mysql.sql
#
# This, of course, assumes database 'netware' has already been created.
#
DROP TABLE IF EXISTS object;
DROP TABLE IF EXISTS prop;
DROP TABLE IF EXISTS value;

CREATE TABLE object (
	id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT,
	type SMALLINT NOT NULL,
	name VARCHAR(48) NOT NULL,
	flags TINYINT NOT NULL,
	security TINYINT NOT NULL,
	INDEX (name),
	INDEX (type)
);

CREATE TABLE prop (
	id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT,
	name VARCHAR(15) NOT NULL,
	flags TINYINT NOT NULL,
	security TINYINT NOT NULL,
	objectid BIGINT NOT NULL,
	valueid BIGINT NOT NULL,
	INDEX (name)
);

CREATE TABLE value (
	id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT,
	propid BIGINT NOT NULL,
	nextid BIGINT NOT NULL,
	size BIGINT NOT NULL,
	objectid BIGINT NOT NULL,
	data BLOB NOT NULL
);
