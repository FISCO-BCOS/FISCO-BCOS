/******************************************************************************
 *
 * Source File Name = sqlcli.h
 *
 * (C) COPYRIGHT International Business Machines Corp. 1993, 1999
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * Function = Include File defining:
 *              DB2 CLI Interface - Constants
 *              DB2 CLI Interface - Data Structures
 *              DB2 CLI Interface - Function Prototypes
 *
 * Operating System = Common C Include File
 *
 *****************************************************************************/

#ifndef SQL_H_SQLCLI
#define SQL_H_SQLCLI             /* Permit duplicate Includes */

/* Prevent inclusion of winsock.h in windows.h */
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#define DB2_WINSOCKAPI_
#endif

/* DB2CLI_VER            DB2 Call Level Interface Version Number (0x0210).
 *                       To revert to Version 1.0 definitions,
 *                       issue #define DB2CLI_VER 0x0110 before including
 *                       sqlcli.h and sqlcli1.h
 */

/* If DB2CLI_VER is not defined, assume version 2.10 */
#ifndef DB2CLI_VER
#define DB2CLI_VER 0x0310
#endif

/* ODBC64 should be used instead of CLI_WIN64 for linking with libdb2o.dll */
#ifndef ODBC64
#ifdef CLI_WIN64
#define ODBC64
#endif
#endif

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "sqlsystm.h"                  /* System dependent defines  */

#if defined(DB2NT)
#include <windows.h>
#else
#define FAR
#endif

#define SQL_API SQL_API_FN


/* generally useful constants */
#define  SQL_MAX_MESSAGE_LENGTH   1024 /* message buffer size             */
#define  SQL_MAX_ID_LENGTH        128  /* maximum identifier name size,
                                          e.g. cursor names               */

/* date/time length constants */
#define SQL_DATE_LEN           10
#define SQL_TIME_LEN            8  /* add P+1 if precision is nonzero */
#define SQL_TIMESTAMP_LEN      19  /* add P+1 if precision is nonzero */

/* handle type identifiers */
#define SQL_HANDLE_ENV             1
#define SQL_HANDLE_DBC             2
#define SQL_HANDLE_STMT            3
#define SQL_HANDLE_DESC            4

/* RETCODE values             */
#define  SQL_SUCCESS             0
#define  SQL_SUCCESS_WITH_INFO   1
#define  SQL_NEED_DATA           99
#define  SQL_NO_DATA             100
#define  SQL_STILL_EXECUTING     2
#define  SQL_ERROR               -1
#define  SQL_INVALID_HANDLE      -2

/* test for SQL_SUCCESS or SQL_SUCCESS_WITH_INFO */
#define SQL_SUCCEEDED(rc) (((rc)&(~1))==0)

/* SQLFreeStmt option values  */
#define  SQL_CLOSE               0
#define  SQL_DROP                1
#define  SQL_UNBIND              2
#define  SQL_RESET_PARAMS        3

/* SQLTransact option values  */
#define  SQL_COMMIT              0
#define  SQL_ROLLBACK            1

/* Standard SQL data types */
#define  SQL_UNKNOWN_TYPE        0
#define  SQL_CHAR                1
#define  SQL_NUMERIC             2
#define  SQL_DECIMAL             3
#define  SQL_INTEGER             4
#define  SQL_SMALLINT            5
#define  SQL_FLOAT               6
#define  SQL_REAL                7
#define  SQL_DOUBLE              8
#define  SQL_DATETIME            9
#define  SQL_VARCHAR            12
#define  SQL_WCHAR              (-8)
#define  SQL_WVARCHAR           (-9)
#define  SQL_WLONGVARCHAR       (-10)
#define  SQL_DECFLOAT           (-360)
/* One-parameter shortcuts for date/time data types */
#define SQL_TYPE_DATE      91
#define SQL_TYPE_TIME      92
#define SQL_TYPE_TIMESTAMP 93

/* Statement attribute values for cursor sensitivity */
#define SQL_UNSPECIFIED     0
#define SQL_INSENSITIVE     1
#define SQL_SENSITIVE       2

/* Default conversion code for SQLBindCol(), SQLBindParam() and SQLGetData() */
#define SQL_DEFAULT        99

/* SQLGetData() code indicating that the application row descriptor
 * specifies the data type
 */
#define SQL_ARD_TYPE      (-99)

/* SQL date/time type subcodes */
#define SQL_CODE_DATE       1
#define SQL_CODE_TIME       2
#define SQL_CODE_TIMESTAMP  3

/* SQL extended data types */
#define  SQL_GRAPHIC            -95
#define  SQL_VARGRAPHIC         -96
#define  SQL_LONGVARGRAPHIC     -97
#define  SQL_BLOB               -98
#define  SQL_CLOB               -99
#define  SQL_DBCLOB             -350
#define  SQL_XML                -370
#define  SQL_DATALINK           -400
#define  SQL_USER_DEFINED_TYPE  -450

/* C data type to SQL data type mapping */
#define  SQL_C_DBCHAR         SQL_DBCLOB
#define  SQL_C_DECIMAL_IBM    SQL_DECIMAL
#define  SQL_C_DATALINK       SQL_C_CHAR
#define  SQL_C_PTR            2463
#define  SQL_C_DECIMAL_OLEDB  2514
#define  SQL_C_DECIMAL64      SQL_DECFLOAT
#define  SQL_C_DECIMAL128     -361

/*
 *  locator type identifier
 */

#define SQL_BLOB_LOCATOR       31
#define SQL_CLOB_LOCATOR       41
#define SQL_DBCLOB_LOCATOR    -351

/*
 * C Data Type for the LOB locator types
 */
#define SQL_C_BLOB_LOCATOR     SQL_BLOB_LOCATOR
#define SQL_C_CLOB_LOCATOR     SQL_CLOB_LOCATOR
#define SQL_C_DBCLOB_LOCATOR   SQL_DBCLOB_LOCATOR

/*
 * NULL status defines; these are used in SQLColAttributes, SQLDescribeCol,
 * to describe the nullability of a column in a table.
 */

#define  SQL_NO_NULLS         0
#define  SQL_NULLABLE         1
#define  SQL_NULLABLE_UNKNOWN 2

/* values of UNNAMED field in descriptor used in SQLColAttribute */
#define SQL_NAMED             0
#define SQL_UNNAMED           1

/* values of ALLOC_TYPE field in descriptor */
#define SQL_DESC_ALLOC_AUTO 1
#define SQL_DESC_ALLOC_USER 2

/* values of USER_DEFINED_TYPE_CODE */
#define SQL_TYPE_BASE            0
#define SQL_TYPE_DISTINCT        1
#define SQL_TYPE_STRUCTURED      2
#define SQL_TYPE_REFERENCE       3

/* Special length values  */
#define  SQL_NULL_DATA        -1
#define  SQL_DATA_AT_EXEC     -2
#define  SQL_NTS              -3      /* NTS = Null Terminated String    */
#define  SQL_NTSL             -3L     /* NTS = Null Terminated String    */

/* SQLColAttributes defines */
#define  SQL_COLUMN_SCHEMA_NAME      16
#define  SQL_COLUMN_CATALOG_NAME     17
#define  SQL_COLUMN_DISTINCT_TYPE    1250
#define  SQL_DESC_DISTINCT_TYPE      SQL_COLUMN_DISTINCT_TYPE
#define  SQL_COLUMN_REFERENCE_TYPE   1251
#define  SQL_DESC_REFERENCE_TYPE     SQL_COLUMN_REFERENCE_TYPE
#define  SQL_DESC_STRUCTURED_TYPE    1252
#define  SQL_DESC_USER_TYPE          1253
#define  SQL_DESC_BASE_TYPE          1254
#define  SQL_DESC_KEY_TYPE           1255
#define  SQL_DESC_KEY_MEMBER         1266

/* identifiers of fields in the SQL descriptor */
#define SQL_DESC_COUNT                  1001
#define SQL_DESC_TYPE                   1002
#define SQL_DESC_LENGTH                 1003
#define SQL_DESC_OCTET_LENGTH_PTR       1004
#define SQL_DESC_PRECISION              1005
#define SQL_DESC_SCALE                  1006
#define SQL_DESC_DATETIME_INTERVAL_CODE 1007
#define SQL_DESC_NULLABLE               1008
#define SQL_DESC_INDICATOR_PTR          1009
#define SQL_DESC_DATA_PTR               1010
#define SQL_DESC_NAME                   1011
#define SQL_DESC_UNNAMED                1012
#define SQL_DESC_OCTET_LENGTH           1013
#define SQL_DESC_ALLOC_TYPE             1099
#define SQL_DESC_USER_DEFINED_TYPE_CODE 1098


/* Defines for SQL_DESC_KEY_TYPE */
#define  SQL_KEYTYPE_NONE             0
#define  SQL_KEYTYPE_PRIMARYKEY       1
#define  SQL_KEYTYPE_UNIQUEINDEX      2


/* SQLColAttribute defines for SQL_COLUMN_UPDATABLE condition */
#define  SQL_UPDT_READONLY            0
#define  SQL_UPDT_WRITE               1
#define  SQL_UPDT_READWRITE_UNKNOWN   2

/*
 * SQLColAttribute defines for SQL_COLUMN_SEARCHABLE condition.
 */
#define  SQL_PRED_NONE         0
#define  SQL_PRED_CHAR         1
#define  SQL_PRED_BASIC        2

/* NULL handle defines    */
#define  SQL_NULL_HENV                0L
#define  SQL_NULL_HDBC                0L
#define  SQL_NULL_HSTMT               0L
#define  SQL_NULL_HDESC               0L
#define  SQL_NULL_HANDLE              0L

/* identifiers of fields in the diagnostics area */
#define SQL_DIAG_RETURNCODE        1
#define SQL_DIAG_NUMBER            2
#define SQL_DIAG_ROW_COUNT         3
#define SQL_DIAG_SQLSTATE          4
#define SQL_DIAG_NATIVE            5
#define SQL_DIAG_MESSAGE_TEXT      6
#define SQL_DIAG_DYNAMIC_FUNCTION  7
#define SQL_DIAG_CLASS_ORIGIN      8
#define SQL_DIAG_SUBCLASS_ORIGIN   9
#define SQL_DIAG_CONNECTION_NAME  10
#define SQL_DIAG_SERVER_NAME      11
#define SQL_DIAG_DYNAMIC_FUNCTION_CODE 12

/* dynamic function codes */
#define SQL_DIAG_ALTER_TABLE            4
#define SQL_DIAG_CALL                                   7
#define SQL_DIAG_CREATE_INDEX          (-1)
#define SQL_DIAG_CREATE_TABLE          77
#define SQL_DIAG_CREATE_VIEW           84
#define SQL_DIAG_DELETE_WHERE          19
#define SQL_DIAG_DROP_INDEX            (-2)
#define SQL_DIAG_DROP_TABLE            32
#define SQL_DIAG_DROP_VIEW             36
#define SQL_DIAG_DYNAMIC_DELETE_CURSOR 38
#define SQL_DIAG_DYNAMIC_UPDATE_CURSOR 81
#define SQL_DIAG_GRANT                 48
#define SQL_DIAG_INSERT                50
#define SQL_DIAG_MERGE                128
#define SQL_DIAG_REVOKE                59
#define SQL_DIAG_SELECT_CURSOR         85
#define SQL_DIAG_UNKNOWN_STATEMENT      0
#define SQL_DIAG_UPDATE_WHERE          82

/*
 *  IBM specific SQLGetDiagField values.
 */

#define SQL_DIAG_DEFERRED_PREPARE_ERROR   1279

/* SQL_DIAG_ROW_NUMBER values */
#define SQL_ROW_NO_ROW_NUMBER                   (-1)
#define SQL_ROW_NUMBER_UNKNOWN                  (-2)

/* SQL_DIAG_COLUMN_NUMBER values */
#define SQL_COLUMN_NO_COLUMN_NUMBER             (-1)
#define SQL_COLUMN_NUMBER_UNKNOWN               (-2)

/*
 * The following are provided to enhance portability and compatibility
 * with ODBC
 */

typedef           signed   char         SCHAR;
typedef           unsigned char         UCHAR;

typedef           short    int          SWORD;
typedef  unsigned short                 USHORT;

typedef  signed   short                 SSHORT;
typedef  unsigned short    int          UWORD;

#if defined(DB2NT)
   typedef                 long         SDWORD;
   typedef        unsigned long         ULONG;
   typedef        unsigned long         UDWORD;
   typedef                 long         SLONG;
#else
   typedef                 sqlint32     SDWORD;
   typedef                 sqluint32    ULONG;
   typedef                 sqluint32    UDWORD;
   typedef                 sqlint32     SLONG;
#endif
typedef                    double       SDOUBLE;
typedef                    float        SFLOAT;
typedef  unsigned          char         SQLDATE;
typedef  unsigned          char         SQLTIME;
typedef  unsigned          char         SQLTIMESTAMP;
typedef  unsigned          char         SQLDECIMAL;
typedef  unsigned          char         SQLNUMERIC;



#if defined(WINDOWS)
typedef  long     double                LDOUBLE;
#else
typedef           double                LDOUBLE;
#endif

typedef void FAR *         PTR;
typedef void FAR *         HENV;
typedef void FAR *         HDBC;
typedef void FAR *         HSTMT;

typedef signed short       RETCODE;



/* SQL portable types for C  */
typedef  UCHAR             SQLCHAR;
typedef  UCHAR             SQLVARCHAR;
typedef  SCHAR             SQLSCHAR;
typedef  SDWORD            SQLINTEGER;
typedef  SWORD             SQLSMALLINT;
typedef  SDOUBLE           SQLDOUBLE;
typedef  SDOUBLE           SQLFLOAT;
typedef  SFLOAT            SQLREAL;

typedef  SQLSMALLINT       SQLRETURN;

#if (DB2CLI_VER >= 0x0200)
typedef  UDWORD            SQLUINTEGER;
typedef  UWORD             SQLUSMALLINT;
#else
typedef  SQLINTEGER        SQLUINTEGER;
typedef  SQLSMALLINT       SQLUSMALLINT;
#endif

/* 64-bit Length Defines */
#ifdef ODBC64
typedef sqlint64        SQLLEN;
typedef sqluint64       SQLULEN;
typedef sqluint64       SQLSETPOSIROW;
#else
#define SQLLEN          SQLINTEGER
#define SQLULEN         SQLUINTEGER
#define SQLSETPOSIROW   SQLUSMALLINT
#endif

typedef  PTR               SQLPOINTER;

/*
 * Double Byte Character Set support
 */

/*
 * Do not support SQL_WCHART_CONVERT in UNICODE
 */
#ifdef UNICODE
#undef SQL_WCHART_CONVERT
#endif

#ifdef SQL_WCHART_CONVERT
typedef  wchar_t           SQLDBCHAR;
#else
typedef  unsigned short    SQLDBCHAR;
#endif

#ifdef DB2WIN
typedef  wchar_t           SQLWCHAR;
#else
typedef  unsigned short    SQLWCHAR;
#endif

#ifdef DB2WIN

typedef  SQLINTEGER        SQLHANDLE;
typedef  HENV              SQLHENV;
typedef  HDBC              SQLHDBC;
typedef  HSTMT             SQLHSTMT;
typedef  HWND              SQLHWND;

#else
#if ((defined DB2NT && defined _WIN64) || defined ODBC64)
typedef  void *            SQLHANDLE;
typedef  SQLHANDLE         SQLHENV;
typedef  SQLHANDLE         SQLHDBC;
typedef  SQLHANDLE         SQLHSTMT;
#else

#ifndef __SQLTYPES
typedef  SQLINTEGER        SQLHANDLE;
typedef  SQLINTEGER        SQLHENV;
typedef  SQLINTEGER        SQLHDBC;
typedef  SQLINTEGER        SQLHSTMT;
#endif
#endif

#if defined (DB2NT)
typedef  HWND              SQLHWND;
#else
typedef  SQLPOINTER        SQLHWND;
#endif

#endif
typedef  SQLHANDLE         SQLHDESC;

#ifndef __SQLTYPES

/*
 * SQL_NO_NATIVE_BIGINT_SUPPORT and SQL_BIGINT_TYPE are defined in sqlsystm.h
 *
 */

#if defined(SQL_NO_NATIVE_BIGINT_SUPPORT)
typedef struct  SQLBIGINT
{
        SQLUINTEGER dwLowWord;
        SQLINTEGER  dwHighWord;
} SQLBIGINT;
typedef struct  SQLUBIGINT
{
        SQLUINTEGER dwLowWord;
        SQLUINTEGER dwHighWord;
} SQLUBIGINT;
#elif defined(SQL_BIGINT_TYPE)
typedef SQL_BIGINT_TYPE   SQLBIGINT;
typedef SQL_BIGUINT_TYPE  SQLUBIGINT;
#endif

typedef  struct DATE_STRUCT
  {
    SQLSMALLINT    year;
    SQLUSMALLINT   month;
    SQLUSMALLINT   day;
  } DATE_STRUCT;

typedef DATE_STRUCT SQL_DATE_STRUCT;

typedef  struct TIME_STRUCT
  {
    SQLUSMALLINT   hour;
    SQLUSMALLINT   minute;
    SQLUSMALLINT   second;
  } TIME_STRUCT;

typedef TIME_STRUCT SQL_TIME_STRUCT;

typedef  struct TIMESTAMP_STRUCT
  {
    SQLSMALLINT    year;
    SQLUSMALLINT   month;
    SQLUSMALLINT   day;
    SQLUSMALLINT   hour;
    SQLUSMALLINT   minute;
    SQLUSMALLINT   second;
    SQLUINTEGER    fraction;     /* fraction of a second */
  } TIMESTAMP_STRUCT;


typedef TIMESTAMP_STRUCT SQL_TIMESTAMP_STRUCT;

typedef enum
{
        SQL_IS_YEAR                                             = 1,
        SQL_IS_MONTH                                    = 2,
        SQL_IS_DAY                                              = 3,
        SQL_IS_HOUR                                             = 4,
        SQL_IS_MINUTE                                   = 5,
        SQL_IS_SECOND                                   = 6,
        SQL_IS_YEAR_TO_MONTH                    = 7,
        SQL_IS_DAY_TO_HOUR                              = 8,
        SQL_IS_DAY_TO_MINUTE                    = 9,
        SQL_IS_DAY_TO_SECOND                    = 10,
        SQL_IS_HOUR_TO_MINUTE                   = 11,
        SQL_IS_HOUR_TO_SECOND                   = 12,
        SQL_IS_MINUTE_TO_SECOND                 = 13
} SQLINTERVAL;

typedef struct tagSQL_YEAR_MONTH
{
                SQLUINTEGER             year;
                SQLUINTEGER             month;
} SQL_YEAR_MONTH_STRUCT;

typedef struct tagSQL_DAY_SECOND
{
                SQLUINTEGER             day;
                SQLUINTEGER             hour;
                SQLUINTEGER             minute;
                SQLUINTEGER             second;
                SQLUINTEGER             fraction;
} SQL_DAY_SECOND_STRUCT;

typedef struct tagSQL_INTERVAL_STRUCT
{
        SQLINTERVAL             interval_type;
        SQLSMALLINT             interval_sign;
        union {
                SQL_YEAR_MONTH_STRUCT           year_month;
                SQL_DAY_SECOND_STRUCT           day_second;
        } intval;

} SQL_INTERVAL_STRUCT;

/* Maximum precision (in base 10) of an SQL_C_NUMERIC value */
#define SQL_MAX_C_NUMERIC_PRECISION     38

/* internal representation of numeric data type */
#define SQL_MAX_NUMERIC_LEN             16
typedef struct tagSQL_NUMERIC_STRUCT
{
        SQLCHAR         precision;
        SQLSCHAR        scale;
        SQLCHAR         sign;   /* 1 if positive, 0 if negative */
        SQLCHAR         val[SQL_MAX_NUMERIC_LEN];
} SQL_NUMERIC_STRUCT;

#endif


#define SQL_DECIMAL64_LEN               8
#define SQL_DECIMAL128_LEN              16

typedef struct tagSQLDECIMAL64 {
    union {
              SQLDOUBLE dummy;   /* Dummy member for alignment purposes */
              SQLCHAR   dec64[SQL_DECIMAL64_LEN];
          } udec64;
} SQLDECIMAL64;

typedef struct tagSQLDECIMAL128 {
    union {
              SQLDOUBLE dummy;   /* Dummy member for alignment purposes */
              SQLCHAR   dec128[SQL_DECIMAL128_LEN];
          } udec128;
} SQLDECIMAL128;

/* Core Function Prototypes  */




SQLRETURN SQL_API_FN  SQLAllocConnect  (SQLHENV           henv,
                                        SQLHDBC     FAR   *phdbc);


SQLRETURN SQL_API_FN  SQLAllocEnv      (SQLHENV     FAR   *phenv);

SQLRETURN SQL_API_FN  SQLAllocStmt     (SQLHDBC           hdbc,
                                        SQLHSTMT    FAR   *phstmt);

SQLRETURN SQL_API_FN SQLAllocHandle(    SQLSMALLINT fHandleType,
                                        SQLHANDLE hInput,
                                        SQLHANDLE * phOutput );

SQLRETURN SQL_API_FN  SQLBindCol       (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      icol,
                                        SQLSMALLINT       fCType,
                                        SQLPOINTER        rgbValue,
                                        SQLLEN            cbValueMax,
                                        SQLLEN      FAR   *pcbValue);

SQLRETURN SQL_API_FN  SQLCancel        (SQLHSTMT          hstmt);


#ifdef ODBC64
SQLRETURN SQL_API_FN  SQLColAttribute  (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      icol,
                                        SQLUSMALLINT      fDescType,
                                        SQLPOINTER        rgbDesc,
                                        SQLSMALLINT       cbDescMax,
                                        SQLSMALLINT FAR   *pcbDesc,
                                        SQLLEN            *pfDesc);
#else
SQLRETURN SQL_API_FN  SQLColAttribute  (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      icol,
                                        SQLUSMALLINT      fDescType,
                                        SQLPOINTER        rgbDesc,
                                        SQLSMALLINT       cbDescMax,
                                        SQLSMALLINT FAR   *pcbDesc,
                                        SQLPOINTER        pfDesc);
#endif



SQLRETURN SQL_API_FN  SQLConnect       (SQLHDBC           hdbc,
                                        SQLCHAR     FAR   *szDSN,
                                        SQLSMALLINT       cbDSN,
                                        SQLCHAR     FAR   *szUID,
                                        SQLSMALLINT       cbUID,
                                        SQLCHAR     FAR   *szAuthStr,
                                        SQLSMALLINT       cbAuthStr);

SQLRETURN SQL_API_FN  SQLDescribeCol   (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      icol,
                                        SQLCHAR     FAR   *szColName,
                                        SQLSMALLINT       cbColNameMax,
                                        SQLSMALLINT FAR   *pcbColName,
                                        SQLSMALLINT FAR   *pfSqlType,
                                        SQLULEN     FAR   *pcbColDef,
                                        SQLSMALLINT FAR   *pibScale,
                                        SQLSMALLINT FAR   *pfNullable);

SQLRETURN SQL_API_FN  SQLDisconnect    (SQLHDBC           hdbc);

SQLRETURN SQL_API_FN  SQLError         (SQLHENV           henv,
                                        SQLHDBC           hdbc,
                                        SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szSqlState,
                                        SQLINTEGER  FAR   *pfNativeError,
                                        SQLCHAR     FAR   *szErrorMsg,
                                        SQLSMALLINT       cbErrorMsgMax,
                                        SQLSMALLINT FAR   *pcbErrorMsg);

SQLRETURN SQL_API_FN  SQLExecDirect    (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szSqlStr,
                                        SQLINTEGER        cbSqlStr);

SQLRETURN SQL_API_FN  SQLExecute       (SQLHSTMT          hstmt);

SQLRETURN SQL_API_FN  SQLFetch         (SQLHSTMT          hstmt);

SQLRETURN SQL_API_FN  SQLFreeConnect   (SQLHDBC           hdbc);

SQLRETURN SQL_API_FN  SQLFreeEnv       (SQLHENV           henv);

SQLRETURN SQL_API_FN  SQLFreeStmt      (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      fOption);

SQLRETURN SQL_API_FN SQLCloseCursor(    SQLHSTMT hStmt );

SQLRETURN SQL_API_FN  SQLGetCursorName (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szCursor,
                                        SQLSMALLINT       cbCursorMax,
                                        SQLSMALLINT FAR   *pcbCursor);

SQLRETURN SQL_API_FN  SQLGetData       (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      icol,
                                        SQLSMALLINT       fCType,
                                        SQLPOINTER        rgbValue,
                                        SQLLEN            cbValueMax,
                                        SQLLEN      FAR   *pcbValue);

SQLRETURN SQL_API_FN  SQLNumResultCols (SQLHSTMT          hstmt,
                                        SQLSMALLINT FAR   *pccol);

SQLRETURN SQL_API_FN  SQLPrepare       (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szSqlStr,
                                        SQLINTEGER        cbSqlStr);

SQLRETURN SQL_API_FN  SQLRowCount      (SQLHSTMT          hstmt,
                                        SQLLEN      FAR   *pcrow);

SQLRETURN SQL_API_FN  SQLSetCursorName (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szCursor,
                                        SQLSMALLINT       cbCursor);

SQLRETURN SQL_API_FN  SQLSetParam      (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      ipar,
                                        SQLSMALLINT       fCType,
                                        SQLSMALLINT       fSqlType,
                                        SQLULEN           cbParamDef,
                                        SQLSMALLINT       ibScale,
                                        SQLPOINTER        rgbValue,
                                        SQLLEN      FAR   *pcbValue);

SQLRETURN SQL_API_FN  SQLTransact      (SQLHENV           henv,
                                        SQLHDBC           hdbc,
                                        SQLUSMALLINT      fType);

SQLRETURN SQL_API_FN SQLEndTran(        SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle,
                                        SQLSMALLINT fType );

SQLRETURN SQL_API_FN SQLFreeHandle(     SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle );

SQLRETURN SQL_API_FN SQLGetDiagRec(     SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle,
                                        SQLSMALLINT iRecNumber,
                                        SQLCHAR * pszSqlState,
                                        SQLINTEGER * pfNativeError,
                                        SQLCHAR * pszErrorMsg,
                                        SQLSMALLINT cbErrorMsgMax,
                                        SQLSMALLINT * pcbErrorMsg );

SQLRETURN SQL_API_FN SQLGetDiagField(   SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle,
                                        SQLSMALLINT iRecNumber,
                                        SQLSMALLINT fDiagIdentifier,
                                        SQLPOINTER pDiagInfo,
                                        SQLSMALLINT cbDiagInfoMax,
                                        SQLSMALLINT * pcbDiagInfo );

SQLRETURN  SQL_API_FN SQLCopyDesc(      SQLHDESC hDescSource,
                                        SQLHDESC hDescTarget );

SQLRETURN  SQL_API_FN SQLGetDescField(  SQLHDESC DescriptorHandle,
                                        SQLSMALLINT RecNumber,
                                        SQLSMALLINT FieldIdentifier,
                                        SQLPOINTER Value,
                                        SQLINTEGER BufferLength,
                                        SQLINTEGER *StringLength);

SQLRETURN  SQL_API_FN SQLGetDescRec(    SQLHDESC DescriptorHandle,
                                        SQLSMALLINT RecNumber,
                                        SQLCHAR *Name,
                                        SQLSMALLINT BufferLength,
                                        SQLSMALLINT *StringLength,
                                        SQLSMALLINT *Type,
                                        SQLSMALLINT *SubType,
                                        SQLLEN *Length,
                                        SQLSMALLINT *Precision,
                                        SQLSMALLINT *Scale,
                                        SQLSMALLINT *Nullable);

SQLRETURN  SQL_API_FN SQLSetDescField(  SQLHDESC DescriptorHandle,
                                        SQLSMALLINT RecNumber,
                                        SQLSMALLINT FieldIdentifier,
                                        SQLPOINTER Value,
                                        SQLINTEGER BufferLength);

SQLRETURN  SQL_API_FN SQLSetDescRec(    SQLHDESC DescriptorHandle,
                                        SQLSMALLINT RecNumber,
                                        SQLSMALLINT Type,
                                        SQLSMALLINT SubType,
                                        SQLLEN Length,
                                        SQLSMALLINT Precision,
                                        SQLSMALLINT Scale,
                                        SQLPOINTER Data,
                                        SQLLEN *StringLength,
                                        SQLLEN *Indicator);



#ifdef __cplusplus
}
#endif

/*
 * Include ODBC header files for
 * functions that are not specified in the X/Open Call Level Interface.
 * This is included with permission from Microsoft.
 * Do not modify (i.e. must not add, remove, rearrange) any part of the
 * contents of sqlext.h
 * Note: SQLDrivers is not supported by DB2 CLI.
 */
#ifndef __SQL
#define __SQL
#define ODBCVER 0x0351
#endif
#if !defined(WINDOWS) && !defined(WIN32) &&  !defined(SQLWINT)
typedef SQLWCHAR * LPWSTR;
typedef sqluint32  DWORD;
#endif

#include "sqlext.h"


#ifdef DB2_WINSOCKAPI_
#undef _WINSOCKAPI_
#undef DB2_WINSOCKAPI_
#endif

#endif /* SQL_H_SQLCLI */
