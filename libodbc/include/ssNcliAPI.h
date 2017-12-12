// SQL Server Native API
//
//////////////////////////////////////////////////////////////////////

#if !defined(__SSNCLIAPI_H__)
#define __SSNCLIAPI_H__

#include "SQLAPI.h"

// MSVC++ 6.0 doesn't have this
#ifdef SQLAPI_WINDOWS
#ifndef DBROWCOUNT
#ifdef _WIN64
typedef LONGLONG	DBROWCOUNT;
#else
typedef LONG		DBROWCOUNT;
#endif
#endif
#ifndef DBCOUNTITEM
#ifdef _WIN64
typedef ULONGLONG	DBCOUNTITEM;
#else
typedef ULONG		DBCOUNTITEM;
#endif
#endif
#ifndef DBORDINAL
#ifdef _WIN64
typedef ULONGLONG	DBORDINAL;
#else
typedef ULONG		DBORDINAL;
#endif
#endif
#ifndef DB_UPARAMS
#ifdef _WIN64
typedef ULONGLONG	DB_UPARAMS;
#else
typedef ULONG		DB_UPARAMS;
#endif
#endif
#ifndef SQLLEN
#ifdef _WIN64
typedef INT64           SQLLEN;
#else
#define SQLLEN          SQLINTEGER
#endif
#endif
#ifndef SQLULEN
#ifdef _WIN64
typedef UINT64          SQLULEN;
#else
#define SQLULEN         SQLUINTEGER
#endif
#endif
#ifndef SQLSETPOSIROW
#ifdef _WIN64
typedef UINT64          SQLSETPOSIROW;
#else
#define SQLSETPOSIROW   SQLUSMALLINT
#endif
#endif
#endif

// API header(s)
#include <sql.h>
#include <sqlext.h>
#ifdef SQLAPI_WINDOWS
#include <sqlncli.h>
#else
#include <msodbcsql.h>
#endif

extern void AddNCliSupport(const SAConnection *pCon);
extern void ReleaseNCliSupport();

typedef SQLRETURN  (SQL_API *SQLAllocHandle_t)(SQLSMALLINT HandleType,
           SQLHANDLE InputHandle, SQLHANDLE *OutputHandle);
typedef SQLRETURN  (SQL_API *SQLBindCol_t)(
	SQLHSTMT StatementHandle, 
	SQLUSMALLINT ColumnNumber,
	SQLSMALLINT TargetType, 
	SQLPOINTER TargetValue,
	SQLLEN BufferLength, 
	SQLLEN *StrLen_or_Ind);
typedef SQLRETURN (SQL_API *SQLBindParameter_t)(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       ipar,
    SQLSMALLINT        fParamType,
    SQLSMALLINT        fCType,
    SQLSMALLINT        fSqlType,
    SQLULEN			   cbColDef,
    SQLSMALLINT        ibScale,
    SQLPOINTER         rgbValue,
    SQLLEN		       cbValueMax,
    SQLLEN			  *pcbValue);
typedef SQLRETURN	(SQL_API	*SQLBulkOperations_t)(
	SQLHSTMT			StatementHandle,
	SQLSMALLINT			Operation);
typedef SQLRETURN (SQL_API *SQLBrowseConnectW_t)(
    SQLHDBC            hdbc,
    SQLWCHAR        *szConnStrIn,
    SQLSMALLINT        cbConnStrIn,
    SQLWCHAR        *szConnStrOut,
    SQLSMALLINT        cbConnStrOutMax,
    SQLSMALLINT    *pcbConnStrOut);
typedef SQLRETURN  (SQL_API *SQLCancel_t)(SQLHSTMT StatementHandle);
typedef SQLRETURN  (SQL_API *SQLCloseCursor_t)(SQLHSTMT StatementHandle);
#if defined(_WIN64) || defined(SA_64BIT)
typedef SQLRETURN (SQL_API *SQLColAttributeW_t)(
	SQLHSTMT		hstmt,
	SQLUSMALLINT	iCol,
	SQLUSMALLINT	iField,
	SQLPOINTER		pCharAttr,
	SQLSMALLINT		cbCharAttrMax,	
	SQLSMALLINT		*pcbCharAttr,
	SQLLEN			*pNumAttr);	
#else
typedef SQLRETURN (SQL_API *SQLColAttributeW_t)(
	SQLHSTMT		hstmt,
	SQLUSMALLINT	iCol,
	SQLUSMALLINT	iField,
	SQLPOINTER		pCharAttr,
	SQLSMALLINT		cbCharAttrMax,	
	SQLSMALLINT		*pcbCharAttr,
	SQLPOINTER		pNumAttr);	
#endif
typedef SQLRETURN (SQL_API *SQLColumnPrivilegesW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLWCHAR        *szColumnName,
    SQLSMALLINT        cbColumnName);
typedef SQLRETURN (SQL_API *SQLColumnsW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLWCHAR        *szColumnName,
    SQLSMALLINT        cbColumnName);
typedef SQLRETURN (SQL_API *SQLConnectW_t)(
    SQLHDBC            hdbc,
    SQLWCHAR        *szDSN,
    SQLSMALLINT        cbDSN,
    SQLWCHAR        *szUID,
    SQLSMALLINT        cbUID,
    SQLWCHAR        *szAuthStr,
    SQLSMALLINT        cbAuthStr);
typedef SQLRETURN  (SQL_API *SQLCopyDesc_t)(SQLHDESC SourceDescHandle,
           SQLHDESC TargetDescHandle);
typedef SQLRETURN  (SQL_API *SQLDescribeColW_t)(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       icol,
    SQLWCHAR        *szColName,
    SQLSMALLINT        cbColNameMax,
    SQLSMALLINT    *pcbColName,
    SQLSMALLINT    *pfSqlType,
    SQLULEN       *pcbColDef,
    SQLSMALLINT    *pibScale,
    SQLSMALLINT    *pfNullable);
typedef SQLRETURN (SQL_API *SQLDescribeParam_t)(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       ipar,
    SQLSMALLINT 	  *pfSqlType,
    SQLULEN		 	  *pcbParamDef,
    SQLSMALLINT 	  *pibScale,
    SQLSMALLINT 	  *pfNullable);
typedef SQLRETURN  (SQL_API *SQLDisconnect_t)(SQLHDBC ConnectionHandle);
typedef SQLRETURN (SQL_API *SQLDriverConnectW_t)(
    SQLHDBC            hdbc,
    SQLHWND            hwnd,
    SQLWCHAR        *szConnStrIn,
    SQLSMALLINT        cbConnStrIn,
    SQLWCHAR        *szConnStrOut,
    SQLSMALLINT        cbConnStrOutMax,
    SQLSMALLINT    *pcbConnStrOut,
    SQLUSMALLINT       fDriverCompletion);
typedef SQLRETURN  (SQL_API *SQLEndTran_t)(SQLSMALLINT HandleType, SQLHANDLE Handle,
           SQLSMALLINT CompletionType);
typedef SQLRETURN (SQL_API *SQLExecDirectW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szSqlStr,
    SQLINTEGER         cbSqlStr);
typedef SQLRETURN  (SQL_API *SQLExecute_t)(SQLHSTMT StatementHandle);
typedef SQLRETURN (SQL_API *SQLExtendedFetch_t)(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       fFetchType,
    SQLLEN			   irow,
    SQLULEN		 	  *pcrow,
    SQLUSMALLINT 	  *rgfRowStatus);
typedef SQLRETURN  (SQL_API *SQLFetch_t)(SQLHSTMT StatementHandle);
typedef SQLRETURN  (SQL_API *SQLFetchScroll_t)(
	SQLHSTMT		StatementHandle,
    SQLSMALLINT		FetchOrientation,
	SQLLEN			FetchOffset);
typedef SQLRETURN  (SQL_API *SQLFreeHandle_t)(SQLSMALLINT HandleType, SQLHANDLE Handle);
typedef SQLRETURN  (SQL_API *SQLFreeStmt_t)(SQLHSTMT StatementHandle,
           SQLUSMALLINT Option);
typedef SQLRETURN (SQL_API *SQLForeignKeysW_t)(
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
typedef SQLRETURN  (SQL_API *SQLGetConnectAttrW_t)(
    SQLHDBC            hdbc,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax,
    SQLINTEGER     *pcbValue);
typedef SQLRETURN (SQL_API *SQLGetConnectOptionW_t)(
    SQLHDBC            hdbc,
    SQLUSMALLINT       fOption,
    SQLPOINTER         pvParam);
typedef SQLRETURN (SQL_API *SQLGetCursorNameW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCursor,
    SQLSMALLINT        cbCursorMax,
    SQLSMALLINT    *pcbCursor);
typedef SQLRETURN  (SQL_API *SQLGetData_t)(
	SQLHSTMT		StatementHandle,
    SQLUSMALLINT	ColumnNumber,
	SQLSMALLINT		TargetType,
	SQLPOINTER		TargetValue,
	SQLLEN			BufferLength,
	SQLLEN			*StrLen_or_Ind);
typedef SQLRETURN (SQL_API *SQLGetDescFieldW_t)(
    SQLHDESC           hdesc,
    SQLSMALLINT        iRecord,
    SQLSMALLINT        iField,
    SQLPOINTER         rgbValue,
    SQLINTEGER		   cbValueMax,
    SQLINTEGER     *pcbValue);
typedef SQLRETURN (SQL_API *SQLGetDescRecW_t)(
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
typedef SQLRETURN (SQL_API *SQLGetDiagFieldW_t)(
    SQLSMALLINT        fHandleType,
    SQLHANDLE          handle,
    SQLSMALLINT        iRecord,
    SQLSMALLINT        fDiagField,
    SQLPOINTER         rgbDiagInfo,
    SQLSMALLINT        cbDiagInfoMax,
    SQLSMALLINT    *pcbDiagInfo);
typedef SQLRETURN (SQL_API *SQLGetDiagRecW_t)(
    SQLSMALLINT        fHandleType,
    SQLHANDLE          handle,
    SQLSMALLINT        iRecord,
    SQLWCHAR        *szSqlState,
    SQLINTEGER     *pfNativeError,
    SQLWCHAR        *szErrorMsg,
    SQLSMALLINT        cbErrorMsgMax,
    SQLSMALLINT    *pcbErrorMsg);
typedef SQLRETURN  (SQL_API *SQLGetEnvAttr_t)(
	SQLHENV EnvironmentHandle,
	SQLINTEGER Attribute,
	SQLPOINTER Value,
	SQLINTEGER BufferLength,
	SQLINTEGER *StringLength);
typedef SQLRETURN  (SQL_API *SQLGetFunctions_t)(
	SQLHDBC ConnectionHandle,
	SQLUSMALLINT FunctionId,
	SQLUSMALLINT *Supported);
typedef SQLRETURN  (SQL_API *SQLGetInfoW_t)(
    SQLHDBC            hdbc,
    SQLUSMALLINT       fInfoType,
    SQLPOINTER         rgbInfoValue,
    SQLSMALLINT        cbInfoValueMax,
    SQLSMALLINT    *pcbInfoValue);
typedef SQLRETURN (SQL_API	*SQLGetTypeInfoW_t)(
	SQLHSTMT			StatementHandle,
	SQLSMALLINT			DataType);
typedef SQLRETURN  (SQL_API *SQLGetStmtAttrW_t)(
    SQLHSTMT           hstmt,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax,
    SQLINTEGER     *pcbValue);
typedef SQLRETURN (SQL_API *SQLMoreResults_t)(
    SQLHSTMT           hstmt);
typedef SQLRETURN (SQL_API *SQLNativeSqlW_t)(
    SQLHDBC            hdbc,
    SQLWCHAR        *szSqlStrIn,
    SQLINTEGER         cbSqlStrIn,
    SQLWCHAR        *szSqlStr,
    SQLINTEGER         cbSqlStrMax,
    SQLINTEGER     *pcbSqlStr);
typedef SQLRETURN (SQL_API *SQLNumParams_t)(
    SQLHSTMT           hstmt,
    SQLSMALLINT 	  *pcpar);
typedef SQLRETURN  (SQL_API *SQLNumResultCols_t)(
	SQLHSTMT StatementHandle,
	SQLSMALLINT *ColumnCount);
typedef SQLRETURN  (SQL_API *SQLParamData_t)(
	SQLHSTMT StatementHandle,
	SQLPOINTER *Value);
typedef SQLRETURN (SQL_API *SQLParamOptions_t)(
    SQLHSTMT          hstmt,
    SQLULEN		      crow,
    SQLULEN			  *pirow);
typedef SQLRETURN  (SQL_API *SQLPrepareW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szSqlStr,
    SQLINTEGER         cbSqlStr);
typedef SQLRETURN (SQL_API *SQLPrimaryKeysW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName);
typedef SQLRETURN (SQL_API *SQLProcedureColumnsW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szProcName,
    SQLSMALLINT        cbProcName,
    SQLWCHAR        *szColumnName,
    SQLSMALLINT        cbColumnName);
typedef SQLRETURN (SQL_API *SQLProceduresW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szProcName,
    SQLSMALLINT        cbProcName);
typedef SQLRETURN  (SQL_API *SQLPutData_t)(
	SQLHSTMT	StatementHandle,
    SQLPOINTER	Data,
	SQLLEN		StrLen_or_Ind);
typedef SQLRETURN  (SQL_API *SQLRowCount_t)(
	SQLHSTMT	StatementHandle, 
	SQLLEN		*RowCount);
typedef SQLRETURN  (SQL_API *SQLSetConnectAttrW_t)(
    SQLHDBC            hdbc,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValue);
typedef SQLRETURN  (SQL_API *SQLSetConnectOptionW_t)(
    SQLHDBC            hdbc,
    SQLUSMALLINT       fOption,
    SQLULEN            vParam);
typedef SQLRETURN (SQL_API *SQLSetCursorNameW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCursor,
    SQLSMALLINT        cbCursor);
typedef SQLRETURN  (SQL_API *SQLSetDescFieldW_t)(
	SQLHDESC DescriptorHandle,
	SQLSMALLINT RecNumber, 
	SQLSMALLINT FieldIdentifier,
    SQLPOINTER Value, 
	SQLINTEGER BufferLength);
typedef SQLRETURN  (SQL_API *SQLSetDescRec_t)(
	SQLHDESC	DescriptorHandle,
	SQLSMALLINT RecNumber,
	SQLSMALLINT Type,
	SQLSMALLINT SubType,
	SQLLEN		Length,
	SQLSMALLINT Precision,
	SQLSMALLINT Scale,
	SQLPOINTER	Data,
	SQLLEN		*StringLength,
	SQLLEN		*Indicator);
typedef SQLRETURN  (SQL_API *SQLSetEnvAttr_t)(SQLHENV EnvironmentHandle,
           SQLINTEGER Attribute, SQLPOINTER Value,
           SQLINTEGER StringLength);
typedef SQLRETURN (SQL_API *SQLSetPos_t)(
    SQLHSTMT           hstmt,
    SQLSETPOSIROW      irow,
    SQLUSMALLINT       fOption,
    SQLUSMALLINT       fLock);
typedef SQLRETURN (SQL_API *SQLSetScrollOptions_t)(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       fConcurrency,
    SQLLEN		       crowKeyset,
    SQLUSMALLINT       crowRowset);
typedef SQLRETURN  (SQL_API *SQLSetStmtAttrW_t)(
    SQLHSTMT           hstmt,
    SQLINTEGER         fAttribute,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax);
typedef SQLRETURN  (SQL_API *SQLSpecialColumnsW_t)(
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
typedef SQLRETURN  (SQL_API *SQLStatisticsW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLUSMALLINT       fUnique,
    SQLUSMALLINT       fAccuracy);
typedef SQLRETURN (SQL_API *SQLTablePrivilegesW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName);
typedef SQLRETURN  (SQL_API *SQLTablesW_t)(
    SQLHSTMT           hstmt,
    SQLWCHAR        *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR        *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR        *szTableName,
    SQLSMALLINT        cbTableName,
    SQLWCHAR        *szTableType,
    SQLSMALLINT        cbTableType);

#ifdef SQLAPI_WINDOWS
typedef HANDLE (_stdcall *OpenSqlFilestream_t) (
           LPCWSTR                        FilestreamPath,
           SQL_FILESTREAM_DESIRED_ACCESS  DesiredAccess,
           ULONG                          OpenOptions,
           LPBYTE                         FilestreamTransactionContext,
           SSIZE_T                        FilestreamTransactionContextLength,
           PLARGE_INTEGER                 AllocationSize);
#endif

class SQLAPI_API ssNCliAPI : public saAPI
{
public:
	ssNCliAPI();

	SQLAllocHandle_t		SQLAllocHandle;		// 3.0
	SQLBindCol_t			SQLBindCol;			// 1.0
	SQLBindParameter_t		SQLBindParameter;	// 2.0
	SQLBulkOperations_t		SQLBulkOperations;	// 3.0
	SQLBrowseConnectW_t		SQLBrowseConnectW;	// 3.0
	SQLCancel_t				SQLCancel;			// 1.0
	SQLCloseCursor_t		SQLCloseCursor;		// 3.0
	SQLColAttributeW_t		SQLColAttributeW;
	SQLColumnPrivilegesW_t	SQLColumnPrivilegesW;
	SQLColumnsW_t			SQLColumnsW;
	SQLConnectW_t			SQLConnectW;
	SQLCopyDesc_t			SQLCopyDesc;		// 3.0
	SQLDescribeColW_t		SQLDescribeColW;	// 1.0
	SQLDescribeParam_t		SQLDescribeParam;	// 1.0
	SQLDisconnect_t			SQLDisconnect;		// 1.0
	SQLDriverConnectW_t		SQLDriverConnectW;	// 1.0
	SQLEndTran_t			SQLEndTran;			// 3.0
	SQLExecDirectW_t		SQLExecDirectW;
	SQLExecute_t			SQLExecute;			// 1.0
	SQLExtendedFetch_t		SQLExtendedFetch;	// 1.0
	SQLFetch_t				SQLFetch;			// 1.0
	SQLFetchScroll_t		SQLFetchScroll;		// 1.0
	SQLForeignKeysW_t		SQLForeignKeysW;
	SQLFreeHandle_t			SQLFreeHandle;		// 3.0
	SQLFreeStmt_t			SQLFreeStmt;		// 1.0
	SQLGetConnectAttrW_t	SQLGetConnectAttrW;	// 3.0
	SQLGetConnectOptionW_t	SQLGetConnectOptionW;
	SQLGetCursorNameW_t		SQLGetCursorNameW;
	SQLGetData_t			SQLGetData;			// 1.0
	SQLGetDescFieldW_t		SQLGetDescFieldW;
	SQLGetDescRecW_t		SQLGetDescRecW;
	SQLGetDiagFieldW_t		SQLGetDiagFieldW;
	SQLGetDiagRecW_t		SQLGetDiagRecW;		// 3.0
	SQLGetEnvAttr_t			SQLGetEnvAttr;		// 3.0
	SQLGetFunctions_t		SQLGetFunctions;	// 1.0
	SQLGetInfoW_t			SQLGetInfoW;		// 1.0
	SQLGetStmtAttrW_t		SQLGetStmtAttrW;	// 3.0
	SQLGetTypeInfoW_t		SQLGetTypeInfoW;
	SQLMoreResults_t		SQLMoreResults;		// 1.0
	SQLNativeSqlW_t			SQLNativeSqlW;
	SQLNumParams_t			SQLNumParams;		// 1.0
	SQLNumResultCols_t		SQLNumResultCols;	// 1.0
	SQLParamData_t			SQLParamData;		// 1.0
	SQLParamOptions_t		SQLParamOptions;	// 1.0
	SQLPrepareW_t			SQLPrepareW;		// 1.0
	SQLPrimaryKeysW_t		SQLPrimaryKeysW;
	SQLProcedureColumnsW_t	SQLProcedureColumnsW;// 1.0
	SQLProceduresW_t		SQLProceduresW;		// 1.0
	SQLPutData_t			SQLPutData;			// 1.0
	SQLRowCount_t			SQLRowCount;		// 1.0
	SQLSetConnectAttrW_t	SQLSetConnectAttrW;	// 3.0
	SQLSetConnectOptionW_t	SQLSetConnectOptionW;// 1.0
	SQLSetCursorNameW_t		SQLSetCursorNameW;	// 1.0
	SQLSetDescFieldW_t		SQLSetDescFieldW;	// 3.0
	SQLSetDescRec_t			SQLSetDescRec;		// 3.0
	SQLSetEnvAttr_t			SQLSetEnvAttr;		// 3.0
	SQLSetPos_t				SQLSetPos;			// 1.0
	SQLSetScrollOptions_t	SQLSetScrollOptions;// 1.0
	SQLSetStmtAttrW_t		SQLSetStmtAttrW;	// 3.0
	SQLSpecialColumnsW_t	SQLSpecialColumnsW;	// 1.0
	SQLStatisticsW_t		SQLStatisticsW;		// 1.0
	SQLTablePrivilegesW_t	SQLTablePrivilegesW;// 1.0
	SQLTablesW_t			SQLTablesW;			// 1.0

#ifdef SQLAPI_WINDOWS
	OpenSqlFilestream_t		OpenSqlFilestream;
#endif
};

class SQLAPI_API ssNCliConnectionHandles : public saConnectionHandles
{
public:
	ssNCliConnectionHandles();

	SQLHENV	m_hevn;
	SQLHDBC	m_hdbc;
};

class SQLAPI_API ssNCliCommandHandles : public saCommandHandles
{
public:
	ssNCliCommandHandles();

	SQLHSTMT	m_hstmt;
};

extern ssNCliAPI g_ssNCliAPI;

#endif // !defined(__SSNCLIAPI_H__)
