// pgAPI.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__PGAPI_H__)
#define __PGAPI_H__

#include "SQLAPI.h"

// API header(s)
#include <libpq-fe.h>
#include <libpq-fs.h>

extern void AddPostgreSQLSupport(const SAConnection *pCon);
extern void ReleasePostgreSQLSupport();

typedef PGconn* (*PQconnectStart_t)(const char *conninfo);
typedef PostgresPollingStatusType (*PQconnectPoll_t)(PGconn *conn);
typedef PGconn* (*PQconnectdb_t)(const char *conninfo);
typedef PGconn* (*PQsetdbLogin_t)(const char *pghost, const char *pgport,								const char *pgoptions, const char *pgtty,
											const char *dbName,
									 const char *login, const char *pwd);
typedef void (*PQfinish_t)(PGconn *conn);
typedef PQconninfoOption* (*PQconndefaults_t)(void);
typedef void (*PQconninfoFree_t)(PQconninfoOption *connOptions);
typedef int (*PQresetStart_t)(PGconn *conn);
typedef PostgresPollingStatusType (*PQresetPoll_t)(PGconn *conn);
typedef void (*PQreset_t)(PGconn *conn);
typedef int (*PQrequestCancel_t)(PGconn *conn);
typedef PGcancel* (*PQgetCancel_t)(PGconn *conn);
typedef void (*PQfreeCancel_t)(PGcancel *cancel);
typedef int	(*PQcancel_t)(PGcancel *cancel, char *errbuf, int errbufsize);
typedef int (*PQserverVersion_t)(const PGconn *conn);
typedef char* (*PQdb_t)(const PGconn *conn);
typedef char* (*PQuser_t)(const PGconn *conn);
typedef char* (*PQpass_t)(const PGconn *conn);
typedef char* (*PQhost_t)(const PGconn *conn);
typedef char* (*PQport_t)(const PGconn *conn);
typedef char* (*PQtty_t)(const PGconn *conn);
typedef char* (*PQoptions_t)(const PGconn *conn);
typedef ConnStatusType (*PQstatus_t)(const PGconn *conn);
typedef char* (*PQerrorMessage_t)(const PGconn *conn);
typedef int (*PQsocket_t)(const PGconn *conn);
typedef int (*PQbackendPID_t)(const PGconn *conn);
typedef int (*PQclientEncoding_t)(const PGconn *conn);
typedef int (*PQsetClientEncoding_t)(PGconn *conn, const char *encoding);
#ifdef USE_SSL
typedef SSL* (*PQgetssl_t)(PGconn *conn);
#endif
typedef void (*PQtrace_t)(PGconn *conn, FILE *debug_port);
typedef void (*PQuntrace_t)(PGconn *conn);
typedef PQnoticeProcessor (*PQsetNoticeProcessor_t)(PGconn *conn, PQnoticeProcessor proc, void *arg);
typedef PGresult* (*PQexec_t)(PGconn *conn, const char *query);
typedef PGnotify* (*PQnotifies_t)(PGconn *conn);
typedef int (*PQsendQuery_t)(PGconn *conn, const char *query);
typedef PGresult* (*PQgetResult_t)(PGconn *conn);
typedef int (*PQisBusy_t)(PGconn *conn);
typedef int (*PQconsumeInput_t)(PGconn *conn);
typedef int (*PQgetline_t)(PGconn *conn, char *string, int length);
typedef int (*PQputline_t)(PGconn *conn, const char *string);
typedef int (*PQgetlineAsync_t)(PGconn *conn, char *buffer, int bufsize);
typedef int (*PQputnbytes_t)(PGconn *conn, const char *buffer, int nbytes);
typedef int (*PQendcopy_t)(PGconn *conn);
typedef int (*PQsetnonblocking_t)(PGconn *conn, int arg);
typedef int (*PQisnonblocking_t)(const PGconn *conn);
typedef int (*PQflush_t)(PGconn *conn);
typedef PGresult* (*PQfn_t)(PGconn *conn, int fnid,
									  int *result_buf,
									  int *result_len,
									  int result_is_int,
									  const PQArgBlock *args,
									  int nargs);
typedef ExecStatusType (*PQresultStatus_t)(const PGresult *res);
typedef char* (*PQresStatus_t)(ExecStatusType status);
typedef char* (*PQresultErrorMessage_t)(const PGresult *res);
typedef int (*PQntuples_t)(const PGresult *res);
typedef int (*PQnfields_t)(const PGresult *res);
typedef int (*PQbinaryTuples_t)(const PGresult *res);
typedef char* (*PQfname_t)(const PGresult *res, int field_num);
typedef int (*PQfnumber_t)(const PGresult *res, const char *field_name);
typedef int	(*PQfformat_t)(const PGresult *res, int field_num);
typedef Oid (*PQftype_t)(const PGresult *res, int field_num);
typedef int (*PQfsize_t)(const PGresult *res, int field_num);
typedef int (*PQfmod_t)(const PGresult *res, int field_num);
typedef char* (*PQcmdStatus_t)(PGresult *res);
typedef char* (*PQoidStatus_t)(const PGresult *res);		/* old and ugly */
typedef Oid (*PQoidValue_t)(const PGresult *res);		/* new and improved */
typedef char* (*PQcmdTuples_t)(PGresult *res);
typedef char* (*PQgetvalue_t)(const PGresult *res, int tup_num, int field_num);
typedef int (*PQgetlength_t)(const PGresult *res, int tup_num, int field_num);
typedef int (*PQgetisnull_t)(const PGresult *res, int tup_num, int field_num);
typedef void (*PQclear_t)(PGresult *res);
typedef PGresult* (*PQmakeEmptyPGresult_t)(PGconn *conn, ExecStatusType status);
typedef void (*PQprint_t)(FILE *fout,		/* output stream */
									const PGresult *res,
									const PQprintOpt *ps);		/* option structure */
typedef void (*PQdisplayTuples_t)(const PGresult *res,
											FILE *fp,	/* where to send the
														 * output */
											int fillAlign,		/* pad the fields with
																 * spaces */
											const char *fieldSep,		/* field separator */
											int printHeader,	/* display headers? */
											int quiet);
typedef void (*PQprintTuples_t)(const PGresult *res,
								FILE *fout,	/* output stream */
										  int printAttName,		/* print attribute names */
										  int terseOutput,		/* delimiter bars */
										  int width);	/* width of column, if
														 * 0, use variable width */
typedef int (*lo_open_t)(PGconn *conn, Oid lobjId, int mode);
typedef int (*lo_close_t)(PGconn *conn, int fd);
typedef int (*lo_read_t)(PGconn *conn, int fd, char *buf, size_t len);
typedef int (*lo_write_t)(PGconn *conn, int fd, char *buf, size_t len);
typedef int (*lo_lseek_t)(PGconn *conn, int fd, int offset, int whence);
typedef Oid (*lo_creat_t)(PGconn *conn, int mode);
typedef int (*lo_tell_t)(PGconn *conn, int fd);
typedef int (*lo_unlink_t)(PGconn *conn, Oid lobjId);
typedef Oid (*lo_import_t)(PGconn *conn, const char *filename);
typedef int (*lo_export_t)(PGconn *conn, Oid lobjId, const char *filename);
typedef int (*PQmblen_t)(const unsigned char *s, int encoding);
typedef int (*PQenv2encoding_t)(void);

// new
typedef PGVerbosity (*PQsetErrorVerbosity_t)(PGconn *conn, PGVerbosity verbosity);
typedef char* (*PQresultErrorField_t)(const PGresult *res, int fieldcode);

// escape
typedef size_t (*PQescapeStringConn_t)(PGconn *conn,
				   char *to, const char *from, size_t length,
				   int *error);
typedef unsigned char* (*PQescapeByteaConn_t)(PGconn *conn,
				  const unsigned char *from, size_t from_length,
				  size_t *to_length);
typedef unsigned char* (*PQunescapeBytea_t)(const unsigned char *strtext,
				size_t *retbuflen);

/* These forms are deprecated! */
typedef size_t (*PQescapeString_t)(char *to, const char *from, size_t length);
typedef char* (*PQescapeBytea_t)(const unsigned char *from, size_t from_length,
			  size_t *to_length);

typedef void (*PQfreemem_t)(void *ptr);

typedef int	(*PQputCopyData_t)(PGconn *conn, const char *buffer, int nbytes);
typedef int	(*PQputCopyEnd_t)(PGconn *conn, const char *errormsg);
typedef int (*PQgetCopyData_t)(PGconn *conn, char **buffer, int async);

typedef PGPing (*PQping_t)(const char *conninfo);
typedef PGPing (*PQpingParams_t)(const char **keywords,
			 const char **values, int expand_dbname);


// API declarations
class SQLAPI_API pgAPI : public saAPI
{
public:
	pgAPI();

	PQconnectStart_t PQconnectStart;
	PQconnectPoll_t PQconnectPoll;
	PQconnectdb_t PQconnectdb;
	PQsetdbLogin_t PQsetdbLogin;
	PQfinish_t PQfinish;
	PQconndefaults_t PQconndefaults;
	PQconninfoFree_t PQconninfoFree;
	PQresetStart_t PQresetStart;
	PQresetPoll_t PQresetPoll;
	PQreset_t PQreset;
	PQrequestCancel_t PQrequestCancel;
	PQgetCancel_t PQgetCancel;
	PQfreeCancel_t PQfreeCancel;
	PQcancel_t PQcancel;
	PQserverVersion_t PQserverVersion;
	PQdb_t PQdb;
	PQuser_t PQuser;
	PQpass_t PQpass;
	PQhost_t PQhost;
	PQport_t PQport;
	PQtty_t PQtty;
	PQoptions_t PQoptions;
	PQstatus_t PQstatus;
	PQerrorMessage_t PQerrorMessage;
	PQsocket_t PQsocket;
	PQbackendPID_t PQbackendPID;
	PQclientEncoding_t PQclientEncoding;
	PQsetClientEncoding_t PQsetClientEncoding;
#ifdef USE_SSL
	PQgetssl_t PQgetssl;
#endif
	PQtrace_t PQtrace;
	PQuntrace_t PQuntrace;
	PQsetNoticeProcessor_t PQsetNoticeProcessor;
	PQexec_t PQexec;
	PQnotifies_t PQnotifies;
	PQsendQuery_t PQsendQuery;
	PQgetResult_t PQgetResult;
	PQisBusy_t PQisBusy;
	PQconsumeInput_t PQconsumeInput;
	PQgetline_t PQgetline;
	PQputline_t PQputline;
	PQgetlineAsync_t PQgetlineAsync;
	PQputnbytes_t PQputnbytes;
	PQendcopy_t PQendcopy;
	PQsetnonblocking_t PQsetnonblocking;
	PQisnonblocking_t PQisnonblocking;
	PQflush_t PQflush;
	PQfn_t PQfn;
	PQresultStatus_t PQresultStatus;
	PQresStatus_t PQresStatus;
	PQresultErrorMessage_t PQresultErrorMessage;
	PQntuples_t PQntuples;
	PQnfields_t PQnfields;
	PQbinaryTuples_t PQbinaryTuples;
	PQfname_t PQfname;
	PQfnumber_t PQfnumber;
	PQfformat_t PQfformat;
	PQftype_t PQftype;
	PQfsize_t PQfsize;
	PQfmod_t PQfmod;
	PQcmdStatus_t PQcmdStatus;
	PQoidStatus_t PQoidStatus;
	PQoidValue_t PQoidValue;
	PQcmdTuples_t PQcmdTuples;
	PQgetvalue_t PQgetvalue;
	PQgetlength_t PQgetlength;
	PQgetisnull_t PQgetisnull;
	PQclear_t PQclear;
	PQmakeEmptyPGresult_t PQmakeEmptyPGresult;
	PQprint_t PQprint;
	PQdisplayTuples_t PQdisplayTuples;
	PQprintTuples_t PQprintTuples;
	lo_open_t lo_open;
	lo_close_t lo_close;
	lo_read_t lo_read;
	lo_write_t lo_write;
	lo_lseek_t lo_lseek;
	lo_creat_t lo_creat;
	lo_tell_t lo_tell;
	lo_unlink_t lo_unlink;
	lo_import_t lo_import;
	lo_export_t lo_export;
	PQmblen_t PQmblen;
	PQenv2encoding_t PQenv2encoding;

	// new 3.7.12
	PQsetErrorVerbosity_t PQsetErrorVerbosity;
	PQresultErrorField_t PQresultErrorField;

	// escape
	PQescapeStringConn_t PQescapeStringConn;
	PQescapeByteaConn_t PQescapeByteaConn;
	PQunescapeBytea_t PQunescapeBytea;

	// These forms are deprecated!
	PQescapeString_t PQescapeString;
	PQescapeBytea_t PQescapeBytea;

	PQfreemem_t PQfreemem;

	PQputCopyData_t PQputCopyData;
	PQputCopyEnd_t PQputCopyEnd;
	PQgetCopyData_t PQgetCopyData;

	PQping_t PQping;
	PQpingParams_t PQpingParams;
};

class SQLAPI_API pgConnectionHandles : public saConnectionHandles
{
public:
	pgConnectionHandles();

	PGconn *conn;	// PostgreSQL connection struct
};

class SQLAPI_API pgCommandHandles : public saCommandHandles
{
public:
	pgCommandHandles();

	PGresult *res; // PostgreSQL result struct
};

extern pgAPI g_pgAPI;

#endif // !defined(__PGAPI_H__)
