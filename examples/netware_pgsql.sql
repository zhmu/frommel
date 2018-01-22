#
# netware_pgsql.sql
#
# This SQL file will create the tables for PostgreSQL. Use it like
#
# ~/frommel$ psql -Uroot netware < netware_pgsql.sql
#
# This, of course, assumes database 'netware' has already been created.
#
DROP TABLE object;
DROP TABLE prop;
DROP TABLE value;

CREATE TABLE object (
	id SERIAL NOT NULL PRIMARY KEY,
	type INTEGER NOT NULL,
	name VARCHAR(48) NOT NULL,
	flags INTEGER NOT NULL,
	security INTEGER NOT NULL
);

CREATE TABLE prop (
	id SERIAL NOT NULL PRIMARY KEY,
	name VARCHAR(15) NOT NULL,
	flags INTEGER NOT NULL,
	security INTEGER NOT NULL,
	objectid INTEGER NOT NULL,
	valueid BIGINT NOT NULL
);

CREATE TABLE value (
	id SERIAL NOT NULL PRIMARY KEY,
	propid BIGINT NOT NULL,
	nextid BIGINT NOT NULL,
	size INTEGER NOT NULL,
	objectid BIGINT NOT NULL,
	data OID
);
