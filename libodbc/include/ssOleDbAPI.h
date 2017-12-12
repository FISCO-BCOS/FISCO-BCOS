// ssOleDbAPI.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__SSOLEDBAPI_H__)
#define __SSOLEDBAPI_H__

#include "SQLAPI.h"

// API header(s)
#include <oledb.h>

extern void AddSSOleDbSupport();
extern void ReleaseSSOleDbSupport();

// API declarations
class ssOleDbAPI : public saAPI
{
public:
	ssOleDbAPI();
};

class SQLAPI_API ssOleDbConnectionHandles : public saConnectionHandles
{
public:
	ssOleDbConnectionHandles();

	IDBInitialize *pIDBInitialize;
	IDBCreateCommand *pIDBCreateCommand;
	ITransactionLocal *pITransactionLocal;
	IDBDataSourceAdmin *pIDBDataSourceAdmin;
};

class SQLAPI_API ssOleDbCommandHandles : public saCommandHandles
{
public:
	ssOleDbCommandHandles();

	ICommandText *pICommandText;
	IMultipleResults *pIMultipleResults;
	IRowset *pIRowset;
};

extern ssOleDbAPI g_ssOleDbAPI;

#endif // !defined(__SSOLEDBAPI_H__)
