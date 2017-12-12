/****************************************************************************
 *
 *                               IBM CORP.
 *
 *                           PROPRIETARY DATA
 *
 * Licensed Material - Property Of IBM
 *
 * "Restricted Materials of IBM"
 *
 * IBM Informix Client SDK
 *
 * Copyright IBM Corporation 2012
 *
** INFXSQL.H - This is the the include file for IBM Informix-CLI
**             applications with sqlcli.h, sqlcli1.h and SQLEXT.H
**
*********************************************************************/

#ifndef __INFXSQL
#define __INFXSQL


#ifdef __cplusplus
extern "C" {                    /* Assume C declarations for C++   */
#endif  /* __cplusplus */

#ifndef ODBCVER
#define ODBCVER 0x0300
#endif

#define FAR

/* environment specific definitions */
#ifndef EXPORT
#define EXPORT   
#endif

#if !defined SQL_API_RC
   #define SQL_API_RC      int
   #define SQL_STRUCTURE   struct
   #define PSQL_API_FN     *
#if defined(WIN32) || defined(WIN64)
   #define SQL_API_FN  __stdcall
#else
   #define SQL_API_FN
#endif /* WIN32 || WIN64 */
   #define SQL_POINTER
   #define SQL_API_INTR
#endif /* !SQL_API_RC */

#define SQL_API SQL_API_FN

#ifndef RC_INVOKED

/*
 * The following are provided to enhance portability and compatibility
 * with ODBC taken from sqlcli.h, sqlcli1.h and sqlsystm.h
 */
typedef char            sqlint8;
typedef unsigned char   sqluint8;
typedef short           sqlint16;
typedef unsigned short  sqluint16;
typedef int             sqlint32;
typedef unsigned int    sqluint32;

typedef           signed   char         SCHAR;
typedef           unsigned char         UCHAR;
typedef           short    int          SWORD;
typedef  unsigned short                 USHORT;
typedef  signed   short                 SSHORT;
typedef  unsigned short    int          UWORD;

typedef                 sqlint32      SDWORD;
typedef                 unsigned long ULONG;
typedef                 sqluint32     UDWORD;
typedef                 signed long   SLONG;

typedef                    double       LDOUBLE;
typedef                    double       SDOUBLE;
typedef                    float        SFLOAT;
typedef  unsigned          char         SQLDATE;
typedef  unsigned          char         SQLTIME;
typedef  unsigned          char         SQLTIMESTAMP;
typedef  unsigned          char         SQLDECIMAL;
typedef  unsigned          char         SQLNUMERIC;

typedef  UCHAR             SQLCHAR;
typedef  UCHAR             SQLVARCHAR;
typedef  SCHAR             SQLSCHAR;
typedef  SDWORD            SQLINTEGER;
typedef  UDWORD            SQLUINTEGER;
typedef  SWORD             SQLSMALLINT;
typedef  SDOUBLE           SQLDOUBLE;
typedef  SDOUBLE           SQLFLOAT;
typedef  SFLOAT            SQLREAL;

typedef void FAR *         PTR;
typedef  PTR               SQLPOINTER;
typedef  UWORD             SQLUSMALLINT;

typedef void FAR *         HENV;
typedef void FAR *         HDBC;
typedef void FAR *         HSTMT;

typedef signed short       RETCODE;

/* 64-bit Length Defines */
#define SQLLEN          SQLINTEGER
#define SQLULEN         SQLUINTEGER
#define SQLSETPOSIROW   SQLUSMALLINT

/* Windows/NT specific DataTypes and defines */
#if !defined(WIN32) && !defined(WIN64)

#define CALLBACK
#define PASCAL
#define _cdecl
#define TRUE             1
#define FALSE            0
#define VOID             void
#ifndef BOOL
#define BOOL int
#endif

typedef int              HWND;
typedef unsigned int     UINT;
typedef VOID            *HANDLE;
typedef char            *LPSTR;
typedef const char      *LPCSTR;
typedef char            *LPWSTR;
typedef char             WCHAR;
typedef SQLUINTEGER      DWORD; 
typedef unsigned short   WORD;
typedef unsigned char    BYTE;
typedef BYTE            *LPBYTE;
typedef SQLINTEGER       LONG;
typedef VOID            *LPVOID;   
typedef VOID            *PVOID;
typedef VOID            *HMODULE;
typedef int              GLOBALHANDLE;
typedef int            (*FARPROC)(void);
typedef VOID            *HINSTANCE;
typedef unsigned int     WPARAM;
typedef SQLUINTEGER      LPARAM; 
typedef VOID            *HKEY;
typedef VOID            *PHKEY;
typedef char             CHAR;
typedef BOOL            *LPBOOL;
typedef DWORD           *LPDWORD;
typedef const char      *LPCWSTR; 
typedef char             TCHAR;
typedef char             VCHAR;
typedef TCHAR           *LPTSTR;
typedef const TCHAR     *LPCTSTR;

#endif /*!WIN32 */


typedef  SQLSMALLINT       SQLRETURN;

typedef  void *            SQLHANDLE;
typedef  SQLHANDLE         SQLHENV;
typedef  SQLHANDLE         SQLHDBC;
typedef  SQLHANDLE         SQLHSTMT;
typedef  SQLHANDLE         SQLHDESC;
typedef  SQLPOINTER        SQLHWND;



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
        SQL_IS_YEAR = 1,
        SQL_IS_MONTH = 2,
        SQL_IS_DAY = 3,
        SQL_IS_HOUR = 4,
        SQL_IS_MINUTE = 5,
        SQL_IS_SECOND = 6,
        SQL_IS_YEAR_TO_MONTH = 7,
        SQL_IS_DAY_TO_HOUR = 8,
        SQL_IS_DAY_TO_MINUTE = 9,
        SQL_IS_DAY_TO_SECOND = 10,
        SQL_IS_HOUR_TO_MINUTE = 11,
        SQL_IS_HOUR_TO_SECOND = 12,
        SQL_IS_MINUTE_TO_SECOND = 13
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

#define SQL_BIGINT_TYPE  long long
#define SQL_BIGUINT_TYPE unsigned long long
typedef SQL_BIGINT_TYPE   SQLBIGINT;
typedef SQL_BIGUINT_TYPE  SQLUBIGINT;

typedef SQLUINTEGER     BOOKMARK;


#ifdef UCS2
typedef unsigned short SQLWCHAR;
#else 
#ifdef UTF8
typedef char SQLWCHAR;
#else 
typedef wchar_t SQLWCHAR;
#endif /* !UTF-8  and !UCS-2 */
#endif  /* UCS-2 */

#define  SQL_C_WCHAR         SQL_WCHAR
#ifdef UNICODE
#define SQL_C_TCHAR     SQL_C_WCHAR
typedef SQLWCHAR        SQLTCHAR;
#else
#define SQL_C_TCHAR     SQL_C_CHAR
typedef SQLCHAR         SQLTCHAR;
#endif

/* Special length values  */
#define  SQL_NULL_DATA        -1
#define  SQL_DATA_AT_EXEC     -2
#define  SQL_NTS              -3      /* NTS = Null Terminated String    */
#define  SQL_NTSL             -3L     /* NTS = Null Terminated String    */

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

/* Special length values  */
#define  SQL_NULL_DATA        -1
#define  SQL_DATA_AT_EXEC     -2
#define  SQL_NTS              -3      /* NTS = Null Terminated String    */
#define  SQL_NTSL             -3L     /* NTS = Null Terminated String    */

/* generally useful constants */
#define  SQL_MAX_MESSAGE_LENGTH   512 /* message buffer size             */
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

/*
 * Environment attributes; note SQL_CONNECTTYPE, SQL_SYNC_POINT are also
 * environment attributes that are settable at the connection level
 */

#define SQL_ATTR_OUTPUT_NTS          10001

/* Options for SQLGetStmtOption/SQLSetStmtOption */

#define SQL_ATTR_AUTO_IPD               10001
#define SQL_ATTR_APP_ROW_DESC           10010
#define SQL_ATTR_APP_PARAM_DESC         10011
#define SQL_ATTR_IMP_ROW_DESC           10012
#define SQL_ATTR_IMP_PARAM_DESC         10013
#define SQL_ATTR_METADATA_ID            10014
#define SQL_ATTR_CURSOR_SCROLLABLE      (-1)
#define SQL_ATTR_CURSOR_SENSITIVITY     (-2)

/* SQL_ATTR_CURSOR_SCROLLABLE values */
#define SQL_NONSCROLLABLE                       0
#define SQL_SCROLLABLE                          1

/* SQL_ATTR_CURSOR_SCROLLABLE values */
#define SQL_NONSCROLLABLE                       0
#define SQL_SCROLLABLE                          1

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
#define SQL_DESC_CARDINALITY            1040
#define SQL_DESC_CARDINALITY_PTR        1043

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

/* SQLGetTypeInfo define */
#define  SQL_ALL_TYPES                0

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

/* CLI attribute/option values */
#define SQL_FALSE               0
#define SQL_TRUE                1

/*
 * NULL status defines; these are used in SQLColAttributes, SQLDescribeCol,
 * to describe the nullability of a column in a table.
 */

#define  SQL_NO_NULLS         0
#define  SQL_NULLABLE         1
#define  SQL_NULLABLE_UNKNOWN 2

/*
 * SQLColAttribute defines for SQL_COLUMN_SEARCHABLE condition.
 */
#define  SQL_PRED_NONE         0
#define  SQL_PRED_CHAR         1
#define  SQL_PRED_BASIC        2


/* values of UNNAMED field in descriptor used in SQLColAttribute */
#define SQL_NAMED             0
#define SQL_UNNAMED           1

/* values of ALLOC_TYPE field in descriptor */
#define SQL_DESC_ALLOC_AUTO 1
#define SQL_DESC_ALLOC_USER 2

/* SQLFreeStmt option values  */
#define  SQL_CLOSE               0
#define  SQL_DROP                1
#define  SQL_UNBIND              2
#define  SQL_RESET_PARAMS        3

/* SQLDataSources "fDirection" values, also used on SQLExtendedFetch() */
/* See sqlext.h for additional SQLExtendedFetch fetch direction defines */
#define  SQL_FETCH_NEXT              1
#define  SQL_FETCH_FIRST             2

/* OTHER CODES USED FOR FETCHORIENTATION IN SQLFETCHSCROLL() */
#define SQL_FETCH_LAST      3
#define SQL_FETCH_PRIOR     4
#define SQL_FETCH_ABSOLUTE  5
#define SQL_FETCH_RELATIVE  6

/* SQLTransact option values  */
#define  SQL_COMMIT              0
#define  SQL_ROLLBACK            1

/* NULL handle defines    */
#define  SQL_NULL_HENV                0L
#define  SQL_NULL_HDBC                0L
#define  SQL_NULL_HSTMT               0L
#define  SQL_NULL_HDESC               0L
#define  SQL_NULL_HANDLE              0L

/* Column types and scopes in SQLSpecialColumns */

#define SQL_SCOPE_CURROW             0
#define SQL_SCOPE_TRANSACTION        1
#define SQL_SCOPE_SESSION            2

/* Defines for SQLStatistics */
#define SQL_INDEX_UNIQUE             0
#define SQL_INDEX_ALL                1

/* Defines for SQLStatistics (returned in the result set) */
#define SQL_INDEX_CLUSTERED          1
#define SQL_INDEX_HASHED             2
#define SQL_INDEX_OTHER              3

/* Defines for SQLSpecialColumns (returned in the result set) */
#define SQL_PC_UNKNOWN               0
#define SQL_PC_NON_PSEUDO            1
#define SQL_PC_PSEUDO                2

/* Reserved value for the IdentifierType argument of SQLSpecialColumns() */
#define SQL_ROW_IDENTIFIER  1

/* SQLGetFunction defines  - supported functions */
#define  SQL_API_SQLALLOCCONNECT        1
#define  SQL_API_SQLALLOCENV            2
#define  SQL_API_SQLALLOCSTMT           3
#define  SQL_API_SQLBINDCOL             4
#define  SQL_API_SQLBINDPARAM           1002
#define  SQL_API_SQLCANCEL              5
#define  SQL_API_SQLCONNECT             7
#define  SQL_API_SQLCOPYDESC            1004
#define  SQL_API_SQLDESCRIBECOL         8
#define  SQL_API_SQLDISCONNECT          9
#define  SQL_API_SQLERROR              10
#define  SQL_API_SQLEXECDIRECT         11
#define  SQL_API_SQLEXECUTE            12
#define  SQL_API_SQLFETCH              13
#define  SQL_API_SQLFREECONNECT        14
#define  SQL_API_SQLFREEENV            15
#define  SQL_API_SQLFREESTMT           16
#define  SQL_API_SQLGETCURSORNAME      17
#define  SQL_API_SQLNUMRESULTCOLS      18
#define  SQL_API_SQLPREPARE            19
#define  SQL_API_SQLROWCOUNT           20
#define  SQL_API_SQLSETCURSORNAME      21
#define  SQL_API_SQLSETDESCFIELD       1017
#define  SQL_API_SQLSETDESCREC         1018
#define  SQL_API_SQLSETENVATTR         1019
#define  SQL_API_SQLSETPARAM           22
#define  SQL_API_SQLTRANSACT           23

#define  SQL_API_SQLCOLUMNS            40
#define  SQL_API_SQLGETCONNECTOPTION   42
#define  SQL_API_SQLGETDATA            43
#define  SQL_API_SQLGETDATAINTERNAL    174
#define  SQL_API_SQLGETDESCFIELD       1008
#define  SQL_API_SQLGETDESCREC         1009
#define  SQL_API_SQLGETDIAGFIELD       1010
#define  SQL_API_SQLGETDIAGREC         1011
#define  SQL_API_SQLGETENVATTR         1012
#define  SQL_API_SQLGETFUNCTIONS       44
#define  SQL_API_SQLGETINFO            45
#define  SQL_API_SQLGETSTMTOPTION      46
#define  SQL_API_SQLGETTYPEINFO        47
#define  SQL_API_SQLPARAMDATA          48
#define  SQL_API_SQLPUTDATA            49
#define  SQL_API_SQLSETCONNECTOPTION   50
#define  SQL_API_SQLSETSTMTOPTION      51
#define  SQL_API_SQLSPECIALCOLUMNS     52
#define  SQL_API_SQLSTATISTICS         53
#define  SQL_API_SQLTABLES             54
#define  SQL_API_SQLDATASOURCES        57
#define  SQL_API_SQLSETCONNECTATTR     1016
#define  SQL_API_SQLSETSTMTATTR        1020

#define  SQL_API_SQLBINDFILETOCOL      1250
#define  SQL_API_SQLBINDFILETOPARAM    1251
#define  SQL_API_SQLSETCOLATTRIBUTES   1252
#define  SQL_API_SQLGETSQLCA           1253
#define  SQL_API_SQLSETCONNECTION      1254
#define  SQL_API_SQLGETDATALINKATTR    1255
#define  SQL_API_SQLBUILDDATALINK      1256
#define  SQL_API_SQLNEXTRESULT         1257
#define  SQL_API_SQLEXTENDEDPREPARE    1296
#define  SQL_API_SQLEXTENDEDBIND       1297
#define  SQL_API_SQLEXTENDEDDESCRIBE   1298

#define  SQL_API_SQLFETCHSCROLL        1021
#define  SQL_API_SQLGETLENGTH          1022
#define  SQL_API_SQLGETPOSITION        1023
#define  SQL_API_SQLGETSUBSTRING       1024


#define  SQL_API_SQLALLOCHANDLE        1001
#define  SQL_API_SQLFREEHANDLE         1006
#define  SQL_API_SQLCLOSECURSOR        1003
#define  SQL_API_SQLENDTRAN            1005
#define  SQL_API_SQLCOLATTRIBUTE       6
#define  SQL_API_SQLGETSTMTATTR        1014
#define  SQL_API_SQLGETCONNECTATTR     1007

/* Information requested by SQLGetInfo() */
#define SQL_MAX_DRIVER_CONNECTIONS           0
#define SQL_MAXIMUM_DRIVER_CONNECTIONS          SQL_MAX_DRIVER_CONNECTIONS
#define SQL_MAX_CONCURRENT_ACTIVITIES        1
#define SQL_MAXIMUM_CONCURRENT_ACTIVITIES       SQL_MAX_CONCURRENT_ACTIVITIES
#define SQL_ATTR_ANSI_APP                   115

/* SQLGetInfo defines  - Info Type */
#define  SQL_DATA_SOURCE_NAME                  2
#define  SQL_FETCH_DIRECTION                   8
#define  SQL_SERVER_NAME                      13
#define  SQL_SEARCH_PATTERN_ESCAPE            14
#define  SQL_DBMS_NAME                        17
#define  SQL_DBMS_VER                         18
#define  SQL_ACCESSIBLE_TABLES                19
#define  SQL_ACCESSIBLE_PROCEDURES            20
#define  SQL_CURSOR_COMMIT_BEHAVIOR           23
#define  SQL_DATA_SOURCE_READ_ONLY            25
#define  SQL_DEFAULT_TXN_ISOLATION            26
#define  SQL_IDENTIFIER_CASE                  28
#define  SQL_IDENTIFIER_QUOTE_CHAR            29
#define  SQL_MAX_COLUMN_NAME_LEN              30
#define  SQL_MAXIMUM_COLUMN_NAME_LENGTH       SQL_MAX_COLUMN_NAME_LEN
#define  SQL_MAX_CURSOR_NAME_LEN              31
#define  SQL_MAXIMUM_CURSOR_NAME_LENGTH       SQL_MAX_CURSOR_NAME_LEN
#define  SQL_MAX_TABLE_NAME_LEN               35
#define  SQL_SCROLL_CONCURRENCY               43
#define  SQL_TXN_CAPABLE                      46
#define  SQL_TRANSACTION_CAPABLE              SQL_TXN_CAPABLE
#define  SQL_USER_NAME                        47
#define  SQL_TXN_ISOLATION_OPTION             72
#define  SQL_TRANSACTION_ISOLATION_OPTION     SQL_TXN_ISOLATION_OPTION
#define  SQL_GETDATA_EXTENSIONS               81
#define  SQL_NULL_COLLATION                   85
#define  SQL_ALTER_TABLE                      86
#define  SQL_ORDER_BY_COLUMNS_IN_SELECT       90
#define  SQL_SPECIAL_CHARACTERS               94
#define  SQL_MAX_COLUMNS_IN_GROUP_BY          97
#define  SQL_MAXIMUM_COLUMNS_IN_GROUP_BY      SQL_MAX_COLUMNS_IN_GROUP_BY
#define  SQL_MAX_COLUMNS_IN_INDEX             98
#define  SQL_MAXIMUM_COLUMNS_IN_INDEX         SQL_MAX_COLUMNS_IN_INDEX
#define  SQL_MAX_COLUMNS_IN_ORDER_BY          99
#define  SQL_MAXIMUM_COLUMNS_IN_ORDER_BY      SQL_MAX_COLUMNS_IN_ORDER_BY
#define  SQL_MAX_COLUMNS_IN_SELECT           100
#define  SQL_MAXIMUM_COLUMNS_IN_SELECT       SQL_MAX_COLUMNS_IN_SELECT
#define  SQL_MAX_COLUMNS_IN_TABLE            101
#define  SQL_MAX_INDEX_SIZE                  102
#define  SQL_MAXIMUM_INDEX_SIZE              SQL_MAX_INDEX_SIZE
#define  SQL_MAX_ROW_SIZE                    104
#define  SQL_MAXIMUM_ROW_SIZE                SQL_MAX_ROW_SIZE
#define  SQL_MAX_STATEMENT_LEN               105
#define  SQL_MAXIMUM_STATEMENT_LENGTH        SQL_MAX_STATEMENT_LEN
#define  SQL_MAX_TABLES_IN_SELECT            106
#define  SQL_MAXIMUM_TABLES_IN_SELECT        SQL_MAX_TABLES_IN_SELECT
#define  SQL_MAX_USER_NAME_LEN               107
#define  SQL_MAXIMUM_USER_NAME_LENGTH        SQL_MAX_USER_NAME_LEN
#define  SQL_MAX_SCHEMA_NAME_LEN             SQL_MAX_OWNER_NAME_LEN
#define  SQL_MAXIMUM_SCHEMA_NAME_LENGTH      SQL_MAX_SCHEMA_NAME_LEN
#define  SQL_MAX_CATALOG_NAME_LEN            SQL_MAX_QUALIFIER_NAME_LEN
#define  SQL_MAXIMUM_CATALOG_NAME_LENGTH     SQL_MAX_CATALOG_NAME_LEN
#define  SQL_OJ_CAPABILITIES                115
#define  SQL_OUTER_JOIN_CAPABILITIES                SQL_OJ_CAPABILITIES
#define  SQL_XOPEN_CLI_YEAR               10000
#define  SQL_CURSOR_SENSITIVITY           10001
#define  SQL_DESCRIBE_PARAMETER           10002
#define  SQL_CATALOG_NAME                 10003
#define  SQL_COLLATION_SEQ                10004
#define  SQL_MAX_IDENTIFIER_LEN           10005
#define  SQL_MAXIMUM_IDENTIFIER_LENGTH    SQL_MAX_IDENTIFIER_LEN
#define  SQL_INTEGRITY                       73
#define  SQL_DATABASE_CODEPAGE               2519
#define  SQL_APPLICATION_CODEPAGE            2520
#define  SQL_CONNECT_CODEPAGE                2521
#define  SQL_ATTR_DB2_APPLICATION_ID         2532
#define  SQL_ATTR_DB2_APPLICATION_HANDLE     2533
#define  SQL_ATTR_HANDLE_XA_ASSOCIATED       2535
#define  SQL_DB2_DRIVER_VER                  2550
#define  SQL_ATTR_XML_DECLARATION            2552
#define  SQL_ATTR_CURRENT_IMPLICIT_XMLPARSE_OPTION    2553
#define  SQL_ATTR_XQUERY_STATEMENT           2557
#define  SQL_DB2_DRIVER_TYPE                 2567
#define  SQL_INPUT_CHAR_CONVFACTOR           2581
#define  SQL_OUTPUT_CHAR_CONVFACTOR          2582
#define  SQL_ATTR_REPLACE_QUOTED_LITERALS    2586
#define  SQL_ATTR_REPORT_TIMESTAMP_TRUNC_AS_WARN    2587


#define SQL_INFO_LAST                         114
#define SQL_INFO_DRIVER_START                 1000

/* SQL_ALTER_TABLE bitmasks */
#define SQL_AT_ADD_COLUMN                       0x00000001L
#define SQL_AT_DROP_COLUMN                      0x00000002L
#define SQL_AT_ADD_CONSTRAINT                   0x00000008L

/* Bitmasks for SQL_ASYNC_MODE */

#define	SQL_AM_NONE		0
#define	SQL_AM_CONNECTION	1
#define	SQL_AM_STATEMENT	2

/* SQL_CURSOR_COMMIT_BEHAVIOR and SQL_CURSOR_ROLLBACK_BEHAVIOR values */

#define SQL_CB_DELETE                 0x0000
#define SQL_CB_CLOSE                  0x0001
#define SQL_CB_PRESERVE               0x0002

/* SQL_FETCH_DIRECTION masks */

#define  SQL_FD_FETCH_NEXT            0x00000001L
#define  SQL_FD_FETCH_FIRST           0x00000002L
#define  SQL_FD_FETCH_LAST            0x00000004L
#define  SQL_FD_FETCH_PRIOR           0x00000008L
#define  SQL_FD_FETCH_ABSOLUTE        0x00000010L
#define  SQL_FD_FETCH_RELATIVE        0x00000020L
#define  SQL_FD_FETCH_RESUME          0x00000040L

/* SQL_GETDATA_EXTENSIONS values */

#define SQL_GD_ANY_COLUMN             0x00000001L
#define SQL_GD_ANY_ORDER              0x00000002L

/* SQL_IDENTIFIER_CASE values */

#define SQL_IC_UPPER                  0x0001
#define SQL_IC_LOWER                  0x0002
#define SQL_IC_SENSITIVE              0x0003
#define SQL_IC_MIXED                  0x0004

/* SQL_OJ_CAPABILITIES values */

#define SQL_OJ_LEFT                   0x00000001L
#define SQL_OJ_RIGHT                  0x00000002L
#define SQL_OJ_FULL                   0x00000004L
#define SQL_OJ_NESTED                 0x00000008L
#define SQL_OJ_NOT_ORDERED            0x00000010L
#define SQL_OJ_INNER                  0x00000020L
#define SQL_OJ_ALL_COMPARISON_OPS     0x00000040L

/* SQL_TXN_CAPABLE values */

#define SQL_TC_NONE                   0x0000
#define SQL_TC_DML                    0x0001
#define SQL_TC_ALL                    0x0002
#define SQL_TC_DDL_COMMIT             0x0003
#define SQL_TC_DDL_IGNORE             0x0004

/* SQL_SCROLL_CONCURRENCY masks */

#define SQL_SCCO_READ_ONLY            0x00000001L
#define SQL_SCCO_LOCK                 0x00000002L
#define SQL_SCCO_OPT_ROWVER           0x00000004L
#define SQL_SCCO_OPT_VALUES           0x00000008L

/* SQL_TXN_ISOLATION_OPTION masks */
#define SQL_TXN_READ_UNCOMMITTED            0x00000001L
#define SQL_TRANSACTION_READ_UNCOMMITTED        SQL_TXN_READ_UNCOMMITTED
#define SQL_TXN_READ_COMMITTED              0x00000002L
#define SQL_TRANSACTION_READ_COMMITTED          SQL_TXN_READ_COMMITTED
#define SQL_TXN_REPEATABLE_READ             0x00000004L
#define SQL_TRANSACTION_REPEATABLE_READ         SQL_TXN_REPEATABLE_READ
#define SQL_TXN_SERIALIZABLE                0x00000008L
#define SQL_TRANSACTION_SERIALIZABLE            SQL_TXN_SERIALIZABLE
#define SQL_TXN_NOCOMMIT                    0x00000020L
#define SQL_TRANSACTION_NOCOMMIT                SQL_TXN_NOCOMMIT
#define SQL_TXN_IDS_CURSOR_STABILITY        0x00000040L
#define SQL_TRANSACTION_IDS_CURSOR_STABILITY    SQL_TXN_IDS_CURSOR_STABILITY
#define SQL_TXN_IDS_LAST_COMMITTED          0x00000080L
#define SQL_TRANSACTION_IDS_LAST_COMMITTED      SQL_TXN_IDS_LAST_COMMITTED

#define SQL_TXN_LAST_COMMITTED              0x00000010L
#define SQL_TRANSACTION_LAST_COMMITTED          SQL_TXN_LAST_COMMITTED

/* SQL_NULL_COLLATION values */
#define SQL_NC_HIGH                         0
#define SQL_NC_LOW                          1

/* API Prototypes  */

SQLRETURN SQL_API_FN  SQLAllocConnect  (SQLHENV           henv,
                                        SQLHDBC     FAR   *phdbc);


SQLRETURN SQL_API_FN  SQLAllocEnv      (SQLHENV     FAR   *phenv);

SQLRETURN SQL_API_FN  SQLAllocStmt     (SQLHDBC           hdbc,
                                        SQLHSTMT    FAR   *phstmt);

SQLRETURN SQL_API_FN SQLAllocHandle    (SQLSMALLINT fHandleType,
                                        SQLHANDLE hInput,
                                        SQLHANDLE * phOutput);

SQLRETURN SQL_API_FN  SQLBindCol       (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      icol,
                                        SQLSMALLINT       fCType,
                                        SQLPOINTER        rgbValue,
                                        SQLLEN            cbValueMax,
                                        SQLLEN      FAR   *pcbValue);

SQLRETURN SQL_API_FN  SQLCancel        (SQLHSTMT          hstmt);

SQLRETURN  SQL_API_FN SQLBindParam     (SQLHSTMT StatementHandle,
                                        SQLUSMALLINT ParameterNumber,
                                        SQLSMALLINT ValueType,
                                        SQLSMALLINT ParameterType,
                                        SQLULEN LengthPrecision,
                                        SQLSMALLINT ParameterScale,
                                        SQLPOINTER ParameterValue,
                                        SQLLEN *StrLen_or_Ind);

SQLRETURN SQL_API_FN SQLCloseCursor    (SQLHSTMT hStmt);

SQLRETURN SQL_API_FN  SQLColAttribute  (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      icol,
                                        SQLUSMALLINT      fDescType,
                                        SQLPOINTER        rgbDesc,
                                        SQLSMALLINT       cbDescMax,
                                        SQLSMALLINT FAR   *pcbDesc,
                                        SQLPOINTER        pfDesc);


SQLRETURN SQL_API_FN  SQLColumns       (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szCatalogName,
                                        SQLSMALLINT       cbCatalogName,
                                        SQLCHAR     FAR   *szSchemaName,
                                        SQLSMALLINT       cbSchemaName,
                                        SQLCHAR     FAR   *szTableName,
                                        SQLSMALLINT       cbTableName,
                                        SQLCHAR     FAR   *szColumnName,
                                        SQLSMALLINT       cbColumnName);

SQLRETURN SQL_API_FN  SQLConnect       (SQLHDBC           hdbc,
                                        SQLCHAR     FAR   *szDSN,
                                        SQLSMALLINT       cbDSN,
                                        SQLCHAR     FAR   *szUID,
                                        SQLSMALLINT       cbUID,
                                        SQLCHAR     FAR   *szAuthStr,
                                        SQLSMALLINT       cbAuthStr);

SQLRETURN  SQL_API_FN SQLCopyDesc      (SQLHDESC hDescSource,
                                        SQLHDESC hDescTarget);

SQLRETURN SQL_API_FN  SQLDataSources   (SQLHENV           henv,
                                        SQLUSMALLINT      fDirection,
                                        SQLCHAR     FAR   *szDSN,
                                        SQLSMALLINT       cbDSNMax,
                                        SQLSMALLINT FAR   *pcbDSN,
                                        SQLCHAR     FAR   *szDescription,
                                        SQLSMALLINT       cbDescriptionMax,
                                        SQLSMALLINT FAR   *pcbDescription);

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

SQLRETURN SQL_API_FN SQLEndTran        (SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle,
                                        SQLSMALLINT fType);

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


SQLRETURN  SQL_API SQLFetchScroll      (SQLHSTMT StatementHandle,
                                        SQLSMALLINT FetchOrientation,
                                        SQLLEN      FetchOffset);

SQLRETURN SQL_API_FN  SQLFreeConnect   (SQLHDBC           hdbc);

SQLRETURN SQL_API_FN  SQLFreeEnv       (SQLHENV           henv);

SQLRETURN SQL_API_FN  SQLFreeStmt      (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      fOption);

SQLRETURN SQL_API_FN SQLFreeHandle     (SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle);

SQLRETURN  SQL_API SQLGetConnectAttr   (SQLHDBC ConnectionHandle,
                                        SQLINTEGER Attribute,
                                        SQLPOINTER Value,
                                        SQLINTEGER BufferLength,
                                        SQLINTEGER *StringLength);

SQLRETURN SQL_API_FN  SQLGetConnectOption (
                                        SQLHDBC           hdbc,
                                        SQLUSMALLINT      fOption,
                                        SQLPOINTER        pvParam);

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

SQLRETURN SQL_API_FN SQLGetDiagField(   SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle,
                                        SQLSMALLINT iRecNumber,
                                        SQLSMALLINT fDiagIdentifier,
                                        SQLPOINTER pDiagInfo,
                                        SQLSMALLINT cbDiagInfoMax,
                                        SQLSMALLINT * pcbDiagInfo );


SQLRETURN SQL_API_FN SQLGetDiagRec(     SQLSMALLINT fHandleType,
                                        SQLHANDLE hHandle,
                                        SQLSMALLINT iRecNumber,
                                        SQLCHAR * pszSqlState,
                                        SQLINTEGER * pfNativeError,
                                        SQLCHAR * pszErrorMsg,
                                        SQLSMALLINT cbErrorMsgMax,
                                        SQLSMALLINT * pcbErrorMsg );

SQLRETURN SQL_API_FN SQLGetEnvAttr     (SQLHENV           henv,
                                        SQLINTEGER        Attribute,
                                        SQLPOINTER        Value,
                                        SQLINTEGER        BufferLength,
                                        SQLINTEGER  FAR   *StringLength);

SQLRETURN SQL_API_FN  SQLGetFunctions  (SQLHDBC           hdbc,
                                        SQLUSMALLINT      fFunction,
                                        SQLUSMALLINT FAR  *pfExists);


SQLRETURN SQL_API_FN  SQLGetInfo       (SQLHDBC           hdbc,
                                        SQLUSMALLINT      fInfoType,
                                        SQLPOINTER        rgbInfoValue,
                                        SQLSMALLINT       cbInfoValueMax,
                                        SQLSMALLINT FAR   *pcbInfoValue);

SQLRETURN  SQL_API SQLGetStmtAttr      (SQLHSTMT StatementHandle,
                                        SQLINTEGER Attribute,
                                        SQLPOINTER Value,
                                        SQLINTEGER BufferLength,
                                        SQLINTEGER *StringLength);

SQLRETURN SQL_API_FN  SQLGetStmtOption (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      fOption,
                                        SQLPOINTER        pvParam);

SQLRETURN SQL_API_FN  SQLGetTypeInfo   (SQLHSTMT          hstmt,
                                        SQLSMALLINT       fSqlType);


SQLRETURN SQL_API_FN  SQLNumResultCols (SQLHSTMT          hstmt,
                                        SQLSMALLINT FAR   *pccol);


SQLRETURN SQL_API_FN  SQLParamData     (SQLHSTMT          hstmt,
                                        SQLPOINTER  FAR   *prgbValue);


SQLRETURN SQL_API_FN  SQLPrepare       (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szSqlStr,
                                        SQLINTEGER        cbSqlStr);

SQLRETURN SQL_API_FN  SQLPutData       (SQLHSTMT          hstmt,
                                        SQLPOINTER        rgbValue,
                                        SQLLEN            cbValue);

SQLRETURN SQL_API_FN  SQLRowCount      (SQLHSTMT          hstmt,
                                        SQLLEN      FAR   *pcrow);


SQLRETURN SQL_API_FN  SQLSetConnectAttr(
                                        SQLHDBC           hdbc,
                                        SQLINTEGER        fOption,
                                        SQLPOINTER        pvParam,
                                        SQLINTEGER        fStrLen);

SQLRETURN SQL_API_FN  SQLSetConnectOption(
                                        SQLHDBC           hdbc,
                                        SQLUSMALLINT      fOption,
                                        ULONG            vParam);

SQLRETURN SQL_API_FN  SQLSetCursorName (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szCursor,
                                        SQLSMALLINT       cbCursor);


SQLRETURN  SQL_API_FN SQLSetDescField  (SQLHDESC DescriptorHandle,
                                        SQLSMALLINT RecNumber,
                                        SQLSMALLINT FieldIdentifier,
                                        SQLPOINTER Value,
                                        SQLINTEGER BufferLength);

SQLRETURN  SQL_API_FN SQLSetDescRec    (SQLHDESC DescriptorHandle,
                                        SQLSMALLINT RecNumber,
                                        SQLSMALLINT Type,
                                        SQLSMALLINT SubType,
                                        SQLLEN Length,
                                        SQLSMALLINT Precision,
                                        SQLSMALLINT Scale,
                                        SQLPOINTER Data,
                                        SQLLEN *StringLength,
                                        SQLLEN *Indicator);

SQLRETURN SQL_API_FN SQLSetEnvAttr     (SQLHENV           henv,
                                        SQLINTEGER        Attribute,
                                        SQLPOINTER        Value,
                                        SQLINTEGER        StringLength);


SQLRETURN SQL_API_FN  SQLSetParam      (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      ipar,
                                        SQLSMALLINT       fCType,
                                        SQLSMALLINT       fSqlType,
                                        SQLULEN           cbParamDef,
                                        SQLSMALLINT       ibScale,
                                        SQLPOINTER        rgbValue,
                                        SQLLEN      FAR   *pcbValue);

SQLRETURN SQL_API_FN  SQLSetStmtAttr   (SQLHSTMT          hstmt,
                                        SQLINTEGER        fOption,
                                        SQLPOINTER        pvParam,
                                        SQLINTEGER       fStrLen);


SQLRETURN SQL_API_FN  SQLSetStmtOption (SQLHSTMT          hstmt,
                                        SQLUSMALLINT      fOption,
                                        SQLULEN           vParam);

SQLRETURN SQL_API_FN  SQLSpecialColumns(SQLHSTMT          hstmt,
                                        SQLUSMALLINT      fColType,
                                        SQLCHAR     FAR   *szCatalogName,
                                        SQLSMALLINT       cbCatalogName,
                                        SQLCHAR     FAR   *szSchemaName,
                                        SQLSMALLINT       cbSchemaName,
                                        SQLCHAR     FAR   *szTableName,
                                        SQLSMALLINT       cbTableName,
                                        SQLUSMALLINT      fScope,
                                        SQLUSMALLINT      fNullable);

SQLRETURN SQL_API_FN  SQLStatistics    (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szCatalogName,
                                        SQLSMALLINT       cbCatalogName,
                                        SQLCHAR     FAR   *szSchemaName,
                                        SQLSMALLINT       cbSchemaName,
                                        SQLCHAR     FAR   *szTableName,
                                        SQLSMALLINT       cbTableName,
                                        SQLUSMALLINT      fUnique,

                                        SQLUSMALLINT      fAccuracy);

SQLRETURN SQL_API_FN  SQLTables        (SQLHSTMT          hstmt,
                                        SQLCHAR     FAR   *szCatalogName,
                                        SQLSMALLINT       cbCatalogName,
                                        SQLCHAR     FAR   *szSchemaName,
                                        SQLSMALLINT       cbSchemaName,
                                        SQLCHAR     FAR   *szTableName,
                                        SQLSMALLINT       cbTableName,
                                        SQLCHAR     FAR   *szTableType,
                                        SQLSMALLINT       cbTableType);

SQLRETURN SQL_API_FN  SQLTransact      (SQLHENV           henv,
                                        SQLHDBC           hdbc,
                                        SQLUSMALLINT      fType);
#endif



#ifdef __cplusplus
}                                    /* End of extern "C" { */
#endif  /* __cplusplus */

#define GUID TAGGUID
typedef struct _TAGGUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[ 8 ];
} TAGGUID;

#define __SQL
#include <sqlext.h>

#undef GUID

/* UNICODE versions */

#if defined(_WIN64) || defined(ODBC64)
SQLRETURN SQL_API SQLColAttributeW(
    SQLHSTMT        hstmt,
    SQLUSMALLINT    iCol,
    SQLUSMALLINT    iField,
    SQLPOINTER      pCharAttr,
    SQLSMALLINT     cbCharAttrMax,
    SQLSMALLINT     *pcbCharAttr,
    SQLLEN          *pNumAttr);
#else
    SQLRETURN SQL_API SQLColAttributeW(
    SQLHSTMT        hstmt,
    SQLUSMALLINT    iCol,
    SQLUSMALLINT    iField,
    SQLPOINTER      pCharAttr,
    SQLSMALLINT     cbCharAttrMax,
    SQLSMALLINT  *pcbCharAttr,
    SQLPOINTER      pNumAttr);
#endif

SQLRETURN SQL_API SQLColAttributesW(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       icol,
    SQLUSMALLINT       fDescType,
    SQLPOINTER         rgbDesc,
    SQLSMALLINT        cbDescMax,
    SQLSMALLINT        *pcbDesc,
    SQLLEN             *pfDesc);

SQLRETURN SQL_API SQLConnectW(
    SQLHDBC            hdbc,
    SQLWCHAR        *szDSN,
    SQLSMALLINT        cbDSN,
    SQLWCHAR        *szUID,
    SQLSMALLINT        cbUID,
    SQLWCHAR        *szAuthStr,
    SQLSMALLINT        cbAuthStr);

SQLRETURN SQL_API SQLConnectWInt(
    SQLHDBC            hdbc,
    SQLWCHAR        *szDSN,
    SQLSMALLINT        cbDSN,
    SQLWCHAR        *szUID,
    SQLSMALLINT        cbUID,
    SQLWCHAR        *szAuthStr,
    SQLSMALLINT        cbAuthStr);


SQLRETURN SQL_API SQLDescribeColW(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       icol,
    SQLWCHAR        *szColName,
    SQLSMALLINT        cbColNameMax,
    SQLSMALLINT    *pcbColName,
    SQLSMALLINT    *pfSqlType,
    SQLULEN        *pcbColDef,
    SQLSMALLINT    *pibScale,
    SQLSMALLINT    *pfNullable);


SQLRETURN SQL_API SQLErrorW(
    SQLHENV            henv,
    SQLHDBC            hdbc,
    SQLHSTMT           hstmt,
    SQLWCHAR        *szSqlState,
    SQLINTEGER     *pfNativeError,
    SQLWCHAR        *szErrorMsg,
    SQLSMALLINT        cbErrorMsgMax,
    SQLSMALLINT    *pcbErrorMsg);

SQLRETURN SQL_API SQLExecDirectW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szSqlStr,
    SQLINTEGER         cbSqlStr);

SQLRETURN SQL_API SQLGetConnectAttrW(
    SQLHDBC            hdbc,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax,
    SQLINTEGER     *pcbValue);

SQLRETURN SQL_API SQLGetCursorNameW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCursor,
    SQLSMALLINT        cbCursorMax,
    SQLSMALLINT    *pcbCursor);

SQLRETURN  SQL_API SQLSetDescFieldW(SQLHDESC DescriptorHandle,
                                   SQLSMALLINT RecNumber,
                                   SQLSMALLINT FieldIdentifier,
                                   SQLPOINTER Value,
                                   SQLINTEGER BufferLength);



SQLRETURN SQL_API SQLGetDescFieldW(
    SQLHDESC           hdesc,
    SQLSMALLINT        iRecord,
    SQLSMALLINT        iField,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax,
    SQLINTEGER     *pcbValue);

SQLRETURN SQL_API SQLGetDescRecW(
    SQLHDESC           hdesc,
    SQLSMALLINT        iRecord,
    SQLWCHAR        *szName,
    SQLSMALLINT        cbNameMax,
    SQLSMALLINT    *pcbName,
    SQLSMALLINT    *pfType,
    SQLSMALLINT    *pfSubType,
    SQLLEN         *pLength,
    SQLSMALLINT    *pPrecision,
    SQLSMALLINT    *pScale,
    SQLSMALLINT    *pNullable);

SQLRETURN SQL_API SQLGetDiagFieldW(
    SQLSMALLINT        fHandleType,
    SQLHANDLE          handle,
    SQLSMALLINT        iRecord,
    SQLSMALLINT        fDiagField,
    SQLPOINTER         rgbDiagInfo,
    SQLSMALLINT        cbDiagInfoMax,
    SQLSMALLINT    *pcbDiagInfo);

SQLRETURN SQL_API SQLGetDiagRecW(
    SQLSMALLINT        fHandleType,
    SQLHANDLE          handle,
    SQLSMALLINT        iRecord,
    SQLWCHAR        *szSqlState,
    SQLINTEGER     *pfNativeError,
    SQLWCHAR        *szErrorMsg,
    SQLSMALLINT        cbErrorMsgMax,
    SQLSMALLINT    *pcbErrorMsg);

SQLRETURN SQL_API_FN SQLGetEnvAttrW(
    SQLHENV    hEnv,
    SQLINTEGER fAttribute,
    SQLPOINTER pParam,
    SQLINTEGER cbParamMax,
    SQLINTEGER * pcbParam );

SQLRETURN SQL_API SQLPrepareW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szSqlStr,
    SQLINTEGER         cbSqlStr);

SQLRETURN SQL_API_FN SQLExtendedPrepareW( SQLHSTMT      hStmt,
                                          SQLWCHAR *    pszSqlStrIn,
                                          SQLINTEGER    cbSqlStr,
                                          SQLINTEGER    cPars,
                                          SQLSMALLINT   sStmtType,
                                          SQLINTEGER    cStmtAttrs,
                                          SQLINTEGER *  piStmtAttr,
                                          SQLINTEGER *  pvParams );

SQLRETURN SQL_API SQLSetConnectAttrW(
    SQLHDBC            hdbc,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValue);

SQLRETURN SQL_API SQLSetCursorNameW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCursor,
    SQLSMALLINT        cbCursor);

SQLRETURN SQL_API_FN  SQLSetEnvAttrW(
    SQLHENV            hEnv,
    SQLINTEGER         fAttribute,
    SQLPOINTER         pParam,
    SQLINTEGER         cbParam );

SQLRETURN SQL_API SQLColumnsW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLWCHAR        *szColumnName,
    SQLSMALLINT        cbColumnName);

SQLRETURN SQL_API SQLGetInfoW(
    SQLHDBC            hdbc,
    SQLUSMALLINT       fInfoType,
    SQLPOINTER         rgbInfoValue,
    SQLSMALLINT        cbInfoValueMax,
    SQLSMALLINT    *pcbInfoValue);

SQLRETURN SQL_API_FN SQLGetConnectOptionW(
    SQLHDBC hDbc,
    SQLUSMALLINT fOptionIn,
    SQLPOINTER pvParam );

SQLRETURN SQL_API_FN SQLSetConnectOptionW(
    SQLHDBC hDbc,
    SQLUSMALLINT fOptionIn,
    SQLULEN vParam );

SQLRETURN SQL_API_FN  SQLGetTypeInfoW(
    SQLHSTMT           hstmt,
    SQLSMALLINT        fSqlType);

SQLRETURN SQL_API SQLSpecialColumnsW(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       fColType,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLUSMALLINT       fScope,
    SQLUSMALLINT       fNullable);

SQLRETURN SQL_API SQLStatisticsW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLUSMALLINT       fUnique,
    SQLUSMALLINT       fAccuracy);

SQLRETURN SQL_API SQLTablesW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLWCHAR        *szTableType,
    SQLSMALLINT        cbTableType);



SQLRETURN SQL_API SQLDataSourcesW(
    SQLHENV            henv,
    SQLUSMALLINT       fDirection,
    SQLWCHAR        *szDSN,
    SQLSMALLINT        cbDSNMax,
    SQLSMALLINT    *pcbDSN,
    SQLWCHAR        *szDescription,
    SQLSMALLINT        cbDescriptionMax,
    SQLSMALLINT    *pcbDescription);




SQLRETURN SQL_API SQLDriverConnectW(
    SQLHDBC            hdbc,
    SQLHWND            hwnd,
    SQLWCHAR        *szConnStrIn,
    SQLSMALLINT        cbConnStrIn,
    SQLWCHAR        *szConnStrOut,
    SQLSMALLINT        cbConnStrOutMax,
    SQLSMALLINT    *pcbConnStrOut,
    SQLUSMALLINT       fDriverCompletion);


SQLRETURN SQL_API SQLBrowseConnectW(
    SQLHDBC            hdbc,
    SQLWCHAR        *szConnStrIn,
    SQLSMALLINT        cbConnStrIn,
    SQLWCHAR        *szConnStrOut,
    SQLSMALLINT        cbConnStrOutMax,
    SQLSMALLINT    *pcbConnStrOut);

SQLRETURN SQL_API SQLColumnPrivilegesW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLWCHAR        *szColumnName,
    SQLSMALLINT        cbColumnName);

SQLRETURN SQL_API SQLGetStmtAttrW(
    SQLHSTMT           hstmt,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax,
    SQLINTEGER     *pcbValue);

SQLRETURN SQL_API SQLSetStmtAttrW(
    SQLHSTMT           hstmt,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax);

SQLRETURN SQL_API SQLForeignKeysW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szPkCatalogName,
    SQLSMALLINT        cbPkCatalogName,
    SQLWCHAR        *szPkSchemaName,
    SQLSMALLINT        cbPkSchemaName,
    SQLWCHAR        *szPkTableName,
    SQLSMALLINT        cbPkTableName,
    SQLWCHAR        *szFkCatalogName,
    SQLSMALLINT        cbFkCatalogName,
    SQLWCHAR        *szFkSchemaName,
    SQLSMALLINT        cbFkSchemaName,
    SQLWCHAR        *szFkTableName,
    SQLSMALLINT        cbFkTableName);


SQLRETURN SQL_API SQLNativeSqlW(
    SQLHDBC            hdbc,
    SQLWCHAR        *szSqlStrIn,
    SQLINTEGER         cbSqlStrIn,
    SQLWCHAR        *szSqlStr,
    SQLINTEGER         cbSqlStrMax,
    SQLINTEGER     *pcbSqlStr);


SQLRETURN SQL_API SQLPrimaryKeysW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName);

SQLRETURN SQL_API SQLProcedureColumnsW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szProcName,
    SQLSMALLINT        cbProcName,
    SQLWCHAR        *szColumnName,
    SQLSMALLINT        cbColumnName);

SQLRETURN SQL_API SQLProceduresW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szProcName,
    SQLSMALLINT        cbProcName);


SQLRETURN SQL_API SQLTablePrivilegesW(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName);

SQLRETURN SQL_API SQLDriversW(
    SQLHENV            henv,
    SQLUSMALLINT       fDirection,
    SQLWCHAR        *szDriverDesc,
    SQLSMALLINT        cbDriverDescMax,
    SQLSMALLINT    *pcbDriverDesc,
    SQLWCHAR        *szDriverAttributes,
    SQLSMALLINT        cbDrvrAttrMax,
    SQLSMALLINT    *pcbDrvrAttr);

#endif /* __INFXSQL */
