/*
 * This code was created by Peter Harvey (mostly during Christmas 98/99).
 * This code is LGPL. Please ensure that this message remains in future
 * distributions and uses of this code (thats about all I get out of it).
 * - Peter Harvey pharvey@codebydesign.com
 * 
 * This file has been modified for the MonetDB project.  See the file
 * Copyright in this directory for more information.
 */

/**********************************************************************
 * SQLConnect()
 * CLI Compliance: ISO 92
 *
 * Author: Martin van Dinther
 * Date  : 30 aug 2002
 *
 **********************************************************************/

#include <monet_options.h>
#include "ODBCGlobal.h"
#include "ODBCDbc.h"
#include "ODBCUtil.h"
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

SQLRETURN
SQLConnect_(ODBCDbc *dbc, SQLCHAR *szDataSource, SQLSMALLINT nDataSourceLength,
	    SQLCHAR *szUID, SQLSMALLINT nUIDLength, SQLCHAR *szPWD,
	    SQLSMALLINT nPWDLength)
{
	SQLRETURN rc = SQL_SUCCESS;
	char *dsn = NULL;
	char *uid = NULL;
	char *pwd = NULL;
	char *schema = NULL;
	int port = 0;
	char *s;
	Mapi mid;
	static int setlen;
	static opt *set = NULL;

	if (set == NULL) {
		/* one time initialization of configuration options */
		char *cfg;

		if ((setlen = mo_builtin_settings(&set)) == 0) {
			/* Memory allocation error */
			addDbcError(dbc, "HY001", NULL, 0);
			return SQL_ERROR;
		}
		if ((cfg = getenv("MONETDB_CONFIG")) != NULL)
			setlen = mo_add_option(&set, setlen, opt_cmdline,
					       "config", cfg);
		mo_system_config(&set, setlen);
	}

	/* check connection state, should not be connected */
	if (dbc->Connected) {
		/* Connection name in use */
		addDbcError(dbc, "08002", NULL, 0);
		return SQL_ERROR;
	}

	/* convert input string parameters to normal null terminated C strings */
	fixODBCstring(szDataSource, nDataSourceLength, addDbcError, dbc);
	if (nDataSourceLength == 0) {
		szDataSource = (SQLCHAR *) "MonetDB";
		nDataSourceLength = strlen((char *) szDataSource);
	}
	dsn = dupODBCstring(szDataSource, (size_t) nDataSourceLength);
	/* for now we only allow the MonetDB data source */
	if (strcasecmp(dsn, "monetdb") != 0) {
		free(dsn);
		/* Data source name not found and no default driver
		   specified */
		addDbcError(dbc, "IM002", NULL, 0);
		return SQL_ERROR;
	}

	/* we need NULL-terminated strings for uid and password, so we
	   need to make copies */
	fixODBCstring(szUID, nUIDLength, addDbcError, dbc);
	if (nUIDLength == 0) {
		uid = strdup(mo_find_option(set, setlen, "sql_user"));
	} else {
		uid = dupODBCstring(szUID, (size_t) nUIDLength);
	}
	fixODBCstring(szPWD, nPWDLength, addDbcError, dbc);
	if (nPWDLength == 0) {
		pwd = mo_find_option(set, setlen, "sql_passwd");
		if (pwd == NULL)
			pwd = "monetdb";
		pwd = strdup(pwd);
	} else {
		pwd = dupODBCstring(szPWD, (size_t) nPWDLength);
	}

	if ((s = getenv("SQLPORT")) != NULL)
		port = atoi(s);
	if (port == 0) {
		s = mo_find_option(set, setlen, "sql_port");
		port = atoi(s);
	}

	/* TODO: get and use a database name */

	/* Retrieved and checked the arguments.
	   Now try to open a connection with the server */
	/* temporarily hold hostname in s */
	s = mo_find_option(set, setlen, "host");

#ifdef ODBCDEBUG
	ODBCLOG("SQLConnect: DSN=%s UID=%s PWD=%s port=%d\n",
		dsn, uid, pwd, port);
#endif

	/* connect to a server on host via port */
	mid = mapi_connect(s, port, uid, pwd, "sql");
	if (mid == NULL || mapi_error(mid)) {
		/* Client unable to establish connection */
		addDbcError(dbc, "08001", NULL, 0);
		rc = SQL_ERROR;
		/* clean up */
		if (mid)
			mapi_destroy(mid);
		if (uid != NULL)
			free(uid);
		if (pwd != NULL)
			free(pwd);
		if (dsn != NULL)
			free(dsn);
	} else {
		/* store internal information and clean up buffers */
		dbc->Connected = 1;
		dbc->mid = mid;
		if (dbc->DSN != NULL)
			free(dbc->DSN);
		dbc->DSN = dsn;
		if (dbc->UID != NULL)
			free(dbc->UID);
		dbc->UID = uid;
		if (dbc->PWD != NULL)
			free(dbc->PWD);
		dbc->PWD = pwd;
		if (dbc->DBNAME != NULL)
			free(dbc->DBNAME);
		dbc->DBNAME = schema;
		mapi_setAutocommit(mid, dbc->sql_attr_autocommit == SQL_AUTOCOMMIT_ON);
	}

	return rc;
}

SQLRETURN SQL_API
SQLConnect(SQLHDBC hDbc, SQLCHAR *szDataSource, SQLSMALLINT nDataSourceLength,
	   SQLCHAR *szUID, SQLSMALLINT nUIDLength, SQLCHAR *szPWD,
	   SQLSMALLINT nPWDLength)
{
#ifdef ODBCDEBUG
	ODBCLOG("SQLConnect " PTRFMT "\n", PTRFMTCAST hDbc);
#endif

	if (!isValidDbc((ODBCDbc *) hDbc))
		return SQL_INVALID_HANDLE;

	clearDbcErrors((ODBCDbc *) hDbc);

	return SQLConnect_((ODBCDbc *) hDbc, szDataSource, nDataSourceLength,
			   szUID, nUIDLength, szPWD, nPWDLength);
}

#ifdef WITH_WCHAR
SQLRETURN SQL_API
SQLConnectW(SQLHDBC hDbc,
	    SQLWCHAR *szDataSource, SQLSMALLINT nDataSourceLength,
	    SQLWCHAR *szUID, SQLSMALLINT nUIDLength,
	    SQLWCHAR *szPWD, SQLSMALLINT nPWDLength)
{
	SQLCHAR *ds = NULL, *uid = NULL, *pwd = NULL;
	SQLRETURN rc = SQL_ERROR;
	ODBCDbc *dbc = (ODBCDbc *) hDbc;

#ifdef ODBCDEBUG
	ODBCLOG("SQLConnectW " PTRFMT "\n", PTRFMTCAST hDbc);
#endif

	if (!isValidDbc(dbc))
		return SQL_INVALID_HANDLE;

	clearDbcErrors(dbc);

	fixWcharIn(szDataSource, nDataSourceLength, ds, addDbcError, dbc, goto exit);
	fixWcharIn(szUID, nUIDLength, uid, addDbcError, dbc, goto exit);
	fixWcharIn(szPWD, nPWDLength, pwd, addDbcError, dbc, goto exit);

	rc = SQLConnect_(dbc, ds, SQL_NTS, uid, SQL_NTS, pwd, SQL_NTS);

  exit:
	if (ds)
		free(ds);
	if (uid)
		free(uid);
	if (pwd)
		free(pwd);
	return rc;
}
#endif	/* WITH_WCHAR */
