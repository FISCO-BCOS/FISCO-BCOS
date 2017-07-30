#if !defined(__SQLAPI_H__)
#define __SQLAPI_H__

#ifdef SQLAPI_EXPORTS
#define SQLAPI_API __declspec(dllexport)
#else
#define SQLAPI_API
#endif

#ifdef SQLAPI_DECL_THROW
#define SQLAPI_THROW(x) throw(x)
#else
#define SQLAPI_THROW(x)
#endif

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64) || defined(_WINDOWS) || defined(_WINDOWS_)
#define SQLAPI_WINDOWS
#include <windows.h>
#endif

#if !defined(SQLAPI_WINDOWS) || defined (CYGWIN)
#include <wchar.h>
#include <wctype.h>
#endif

#ifndef SQLAPI_WINDOWS
#include <sys/time.h>
#endif

#ifdef UNDER_CE
#include <types.h>
#else
#include <sys/types.h>
#include <sys/timeb.h>
#endif
#include <time.h>
#include <stdarg.h>
#include <limits.h>

#ifdef SQLAPI_WINDOWS
// 64-bit integer
typedef __int64 sa_int64_t;
typedef unsigned __int64 sa_uint64_t;
// SQLAPI callback naming
#define SQLAPI_CALLBACK __cdecl
#else
// 64-bit integer
typedef long long int sa_int64_t;
typedef unsigned long long int sa_uint64_t;
// SQLAPI callback naming
#define SQLAPI_CALLBACK
#endif // ! SQLAPI_WINDOWS

#ifdef SA_USE_STL
#include <string>
#endif

#ifndef SIZE_MAX
#define SIZE_MAX UINT_MAX
#endif

#define MAX_BATCH_NUM 5000

class ISAClient;
class ISAConnection;
class ISACursor;

class SAMutex;

class SAConnection;
class SACommand;
struct sa_Commands;
class saOptions;
class SAValue;
class SAParam;
class SAField;

class SAException;

class saPlaceHolder;

class SABytes;
class SALongBinary;
class SALongChar;
class SABLob;
class SACLob;
class SAValueRead;

//! \addtogroup enums SQLAPI++ defined enums
//! \{

typedef
	enum eSAClient
{
	//! DBMS client is not specified
	SA_Client_NotSpecified,
	//! ODBC
	SA_ODBC_Client,
	//! Oracle
	SA_Oracle_Client,
	//! Microsoft SQL Server
	SA_SQLServer_Client,
	//! InterBase or Firebird
	SA_InterBase_Client,
	//! SQLBase
	SA_SQLBase_Client,
	//! IBM DB2
	SA_DB2_Client,
	//! Informix
	SA_Informix_Client,
	//! Sybase ASE 
	SA_Sybase_Client,
	//! MySQL
	SA_MySQL_Client,
	//! PostgreSQL
	SA_PostgreSQL_Client,
	//! SQLite
	SA_SQLite_Client,
	_SA_Client_Reserverd = (int)(((unsigned int)(-1))/2)
} SAClient_t;

typedef
	enum eSAErrorClass
{
	//! no error occurred
	SA_No_Error,
	//! user-generated error
	SA_UserGenerated_Error,
	//! the Library-generated error
	SA_Library_Error,
	//! DBMS API error occured
	SA_DBMS_API_Error,
	_SA_ErrorClass_Reserved = (int)(((unsigned int)(-1))/2)
} SAErrorClass_t;

typedef
	enum eSAIsolationLevel
{
	//! the default(unknown) isolation level
	SA_LevelUnknown = -1,
	//! standard ANSI isolation level 0
	SA_ANSILevel0,
	//! standard ANSI isolation level 1
	SA_ANSILevel1,
	//! standard ANSI isolation level 2
	SA_ANSILevel2,
	//! standard ANSI isolation level 3
	SA_ANSILevel3,
	//! isolation level 'Read Uncommitted'
	SA_ReadUncommitted = SA_ANSILevel0,
	//! isolation level 'Read Committed'
	SA_ReadCommitted = SA_ANSILevel1,
	//! isolation level 'Repeatable Read'
	SA_RepeatableRead = SA_ANSILevel2,
	//! isolation level 'Serializable'
	SA_Serializable = SA_ANSILevel3,
	_SA_IsolationLevel_Reserved = (int)(((unsigned int)(-1))/2)
} SAIsolationLevel_t;

typedef
	enum eSAAutoCommit
{
	//! the default(unknown) auto-commit mode
	SA_AutoCommitUnknown = -1,
	//! auto-commit mode is off
	SA_AutoCommitOff,
	//! auto-commit mode is on
	SA_AutoCommitOn,
	_SA_AutoCommit_Reserved = (int)(((unsigned int)(-1))/2)
} SAAutoCommit_t;

typedef
	enum eSADataType
{
	SA_dtUnknown,
	SA_dtBool,
	SA_dtShort,
	SA_dtUShort,
	SA_dtLong,
	SA_dtULong,
	SA_dtDouble,
	SA_dtNumeric,
	SA_dtDateTime,
	SA_dtInterval,
	SA_dtString,
	SA_dtBytes,
	SA_dtLongBinary,
	SA_dtLongChar,
	SA_dtBLob,
	SA_dtCLob,
	SA_dtCursor,
	SA_dtSpecificToDBMS,
	_SA_DataType_Reserved = (int)(((unsigned int)(-1))/2)
} SADataType_t;

typedef
	enum eSACommandType
{
	//! command type is not defined
	SA_CmdUnknown,
	//! command is an SQL statement (with or without parameters)
	SA_CmdSQLStmt,
	//! command is a raw SQL statement and not interpreted by SQLAPI++
	SA_CmdSQLStmtRaw,
	//! command is a stored procedure or a function
	SA_CmdStoredProc,
	_SA_Cmd_Reserved = (int)(((unsigned int)(-1))/2)
} SACommandType_t;

typedef
	enum eSAParamDirType
{
	SA_ParamInput,
	SA_ParamInputOutput,
	SA_ParamOutput,
	SA_ParamReturn,
	_SA_ParamDirType_Reserved = (int)(((unsigned int)(-1))/2)
} SAParamDirType_t;

typedef
	enum eSALongOrLobReaderModes
{
	SA_LongOrLobReaderDefault,
	SA_LongOrLobReaderManual,
	_SA_LongOrLobReaderModes_Reserved = (int)(((unsigned int)(-1))/2)
} SALongOrLobReaderModes_t;

typedef
	enum eSAPieceType
{
	SA_FirstPiece = 1,
	SA_NextPiece = 2,
	SA_LastPiece = 3,
	SA_OnePiece = 4,
	_SA_Reserved_PieceType = (int)(((unsigned int)(-1))/2)
} SAPieceType_t;

typedef
	enum eSAConnectionHandlerType
{
	//! The handles is called after DBMS connection structures is allocated
	SA_PreConnectHandler,
	//! The handles is called after DBMS connection is esteblished
	SA_PostConnectHandler
} SAConnectionHandlerType_t;

//! \}

//! \addtogroup typedefs SQLAPI++ defined types
//! \{

//! Callback for exception pre-handling
typedef bool (SQLAPI_CALLBACK *PreHandleException_t)(SAException &x);

//! Long or LOB writer callback, use for parameter binding
typedef size_t (SQLAPI_CALLBACK *saLongOrLobWriter_t)(SAPieceType_t &ePieceType, void *pBuf, size_t nLen, void *pAddlData);

//! Long or LOB reader callback, use for field fetching
typedef void (SQLAPI_CALLBACK *saLongOrLobReader_t)(SAPieceType_t ePieceType, void *pBuf, size_t nLen, size_t nBlobSize, void *pAddlData);

//! DBMS connection handling callback
typedef void (SQLAPI_CALLBACK *saConnectionHandler_t)(SAConnection &con, SAConnectionHandlerType_t eHandlerType);

//! \}

typedef void (SQLAPI_CALLBACK *EnumCursors_t)(ISACursor *, void *);

class SQLAPI_API saAPI
{
protected:
	saAPI();
public:
	virtual ~saAPI();
};

class SQLAPI_API saConnectionHandles
{
protected:
	saConnectionHandles();
public:
	virtual ~saConnectionHandles();
};

class SQLAPI_API saCommandHandles
{
protected:
	saCommandHandles();
public:
	virtual ~saCommandHandles();
};

#if defined(SA_UNICODE)
#define _TSA(x)      L ## x
typedef wchar_t SAChar;
#else
typedef char SAChar;
#define _TSA(x)      x
#endif

struct SAStringData;

//! Provides support for manipulating character values
class SQLAPI_API SAString
{
public:
	// Constructors
	//! Constructs an empty SAString
	SAString();
	//! Copy constructor
	SAString(const SAString &stringSrc);
	//! Initializes SAString from a single character
	SAString(SAChar ch, size_t nRepeat = 1);
	//! Initializes SAString from an ANSI (multibyte) string (converts to SAChar)
	SAString(const char *lpsz);
	//! Initializes SAString from a UNICODE string (converts to SAChar)
	SAString(const wchar_t *lpsz);
	//! Initializes SAString from subset of characters from an ANSI (multibyte) string (converts to SAChar)
	SAString(const char *lpch, size_t nLength);
	//! Initializes SAString from subset of characters from a UNICODE string (converts to SAChar)
	SAString(const wchar_t *lpch, size_t nLength);
	//! Initializes SAString from unsigned characters (converts to SAChar)
	SAString(const unsigned char *psz);
	//! Special constructor for binary data (no converion to SAChar)
	SAString(const void *pBuffer, size_t nLengthInBytes);

	// Attributes & Operations

	//! Get the data length (in characters).
	size_t GetLength() const;
	//! True if zero length
	bool IsEmpty() const;
	//! Clear contents to empty
	void Empty();

	//! Return pointer to const string
	operator const SAChar *() const;

	// overloaded assignment

	//! Ref-counted copy from another SAString
	const SAString &operator =(const SAString &sSrc);
	//! Set string content to single character
	const SAString &operator=(SAChar ch);
#ifdef SA_UNICODE
	const SAString &operator=(char ch);
#endif
	//! Copy string content from ANSI (multibyte) string (converts to SAChar)
	const SAString &operator=(const char *lpsz);
	//! Copy string content from UNICODE string (converts to SAChar)
	const SAString &operator=(const wchar_t *lpsz);
	//! Copy string content from unsigned chars
	const SAString &operator=(const unsigned char *psz);

	// string concatenation

	//! Concatenate from another SAString
	const SAString &operator+=(const SAString &string);
	//! Concatenate a single character
	const SAString &operator+=(SAChar ch);
#ifdef SA_UNICODE
	// concatenate an ANSI character after converting it to SAChar
	const SAString &operator+=(char ch);
#endif
	//! Concatenate from a SAChar string
	const SAString &operator+=(const SAChar *lpsz);

	friend SAString SQLAPI_API operator+(const SAString &string1, const SAString &string2);
	friend SAString SQLAPI_API operator+(const SAString &string, SAChar ch);
	friend SAString SQLAPI_API operator+(SAChar ch, const SAString &string);
#ifdef SA_UNICODE
	friend SAString SQLAPI_API operator+(const SAString &string, char ch);
	friend SAString SQLAPI_API operator+(char ch, const SAString &string);
#endif
	friend SAString SQLAPI_API operator+(const SAString &string, const SAChar *lpsz);
	friend SAString SQLAPI_API operator+(const SAChar *lpsz, const SAString &string);

	// string comparison

	//! Straight character comparison
	int Compare(const SAChar *lpsz) const;
	//! Compare ignoring case
	int CompareNoCase(const SAChar *lpsz) const;
#ifndef UNDER_CE
	//! NLS aware comparison, case sensitive
	int Collate(const SAChar *lpsz) const;
#endif

	//! Convert the object to an uppercase
	void MakeUpper();
	//! Convert the object to an lowercase
	void MakeLower();

	// simple sub-string extraction

	//! Return all characters starting at zero-based nFirst
	SAString Mid(size_t nFirst) const;
	//! Return nCount characters starting at zero-based nFirst
	SAString Mid(size_t nFirst, size_t nCount) const;
	//! Return first nCount characters in string
	SAString Left(size_t nCount) const;
	//! Return nCount characters from end of string
	SAString Right(size_t nCount) const;

	// trimming whitespace (either side)

	//! Remove whitespace starting from right edge
	void TrimRight();
	//! Remove whitespace starting from left side
	void TrimLeft();

	// trimming anything (either side)

	//! Remove continuous occurrences of chTarget starting from right
	void TrimRight(SAChar chTarget);
	//! Remove continuous occcurrences of characters in passed string,
	// starting from right
	void TrimRight(const SAChar *lpszTargets);
	//! Remove continuous occurrences of chTarget starting from left
	void TrimLeft(SAChar chTarget);
	//! Remove continuous occcurrences of characters in
	// passed string, starting from left
	void TrimLeft(const SAChar *lpszTargets);

	// advanced manipulation

	// replace occurrences of substring lpszOld with lpszNew;
	// empty lpszNew removes instances of lpszOld
	size_t Replace(const SAChar *lpszOld, const SAChar *lpszNew);
	// insert character at zero-based index; concatenates
	// if index is past end of string
	size_t Insert(size_t nIndex, SAChar ch);
	// insert substring at zero-based index; concatenates
	// if index is past end of string
	size_t Insert(size_t nIndex, const SAChar *pstr);
	// delete nCount characters starting at zero-based index
	size_t Delete(size_t nIndex, size_t nCount = 1);

	// searching

	// find character starting at left, SIZ if not found
	size_t Find(SAChar ch) const;
	// find character starting at right
	size_t ReverseFind(SAChar ch) const;
	// find character starting at zero-based index and going right
	size_t Find(SAChar ch, size_t nStart) const;
	// find first instance of any character in passed string
	size_t FindOneOf(const SAChar *lpszCharSet) const;
	// find first instance of substring
	size_t Find(const SAChar *lpszSub) const;
	// find first instance of substring starting at zero-based index
	size_t Find(const SAChar *lpszSub, size_t nStart) const;

	// simple formatting

	// printf-like formatting using passed string
	void Format(const SAChar *lpszFormat, ...);
	// printf-like formatting using variable arguments parameter
	void FormatV(const SAChar *, va_list argList);

	// Access to string implementation buffer as "C" character array

	// get pointer to modifiable buffer at least as long as nMinBufLength
	SAChar *GetBuffer(size_t nMinBufLength);
	// release buffer, setting length to nNewLength (or to first null if SIZE_MAX)
	void ReleaseBuffer(size_t nNewLength = SIZE_MAX);

	// Use LockBuffer/UnlockBuffer to turn refcounting off

	// turn refcounting off
	SAChar *LockBuffer();
	// turn refcounting back on
	void UnlockBuffer();

	// Special buffer access routines to manipulate binary data

	// get binary data length (in bytes)
	size_t GetBinaryLength() const;
	// return pointer to const binary data buffer
	operator const void *() const;
	// get pointer to modifiable binary data buffer at least as long as nMinBufLengthInBytes
	void *GetBinaryBuffer(size_t nMinBufLengthInBytes);
	// release buffer, setting length to nNewLength (or to first nul if -1)
	void ReleaseBinaryBuffer(size_t nNewLengthInBytes);

	// return pointer to const Unicode string, convert if needed
	const wchar_t *GetWideChars() const;
	// get string length (in Unicode characters)
	size_t GetWideCharsLength() const;
	// return pointer to const multibyte string, convert if needed
	const char *GetMultiByteChars() const;
	// get string length (in multibyte characters)
	size_t GetMultiByteCharsLength() const;

	// Special conversion functions (multibyte <-> Unicode)
#ifdef SA_UNICODE
	// return pointer to const UTF8 string
	const char *GetUTF8Chars() const;
	// get string length (in UTF8 characters)
	size_t GetUTF8CharsLength() const;
	// assing UTF8 data
	void SetUTF8Chars(const char* szSrc, size_t nSrcLen = SIZE_MAX);
#endif	// SA_UNICODE
	// return pointer to const UTF16 string
	const void *GetUTF16Chars() const;
	// get string length (in UTF16 characters)
	size_t GetUTF16CharsLength() const;
	// assing UTF16 data
	void SetUTF16Chars(const void* szSrc, size_t nSrcLen = SIZE_MAX);

	// Implementation
public:
	~SAString();

protected:
	SAChar *m_pchData;	// pointer to ref counted string data

	// implementation helpers
	SAStringData *GetData() const;
	void Init();
	void AllocBuffer(size_t nLen);
#ifdef SA_UNICODE
	void AssignBinaryCopy(size_t nSrcLenInBytes, const void *pSrcData);
	void ConcatBinaryCopy(size_t nSrc1LenInBytes, const void *pSrc1Data, size_t nSrc2LenInBytes, const void *pSrc2Data);
	void ConcatBinaryInPlace(size_t nSrcLen, const void *pData);
#endif	// SA_UNICODE
	void AssignCopy(size_t nSrcLen, const SAChar *lpszSrcData);
	void ConcatCopy(size_t nSrc1Len, const SAChar *lpszSrc1Data, size_t nSrc2Len, const SAChar *lpszSrc2Data);
	void ConcatInPlace(size_t nSrcLen, const SAChar *lpszSrcData);

	void CopyBeforeWrite();
	void AllocBeforeWrite(size_t nLen);
	void Release();
	static void Release(SAStringData *pData);
	static size_t SafeStrlen(const SAChar *lpsz);
	static void FreeData(SAStringData *pData);

#ifdef SA_USE_STL
public:
	SAString(const std::string &stringSrc);
	SAString(const std::wstring &stringSrc);
	const SAString &operator=(const std::string &stringSrc);
	const SAString &operator=(const std::wstring &stringSrc);
	const SAString &operator+=(const std::string &stringSrc);
	const SAString &operator+=(const std::wstring &stringSrc);
#endif
};

// Compare helpers
bool SQLAPI_API operator==(const SAString &s1, const SAString &s2);
bool SQLAPI_API operator==(const SAString &s1, const SAChar *s2);
bool SQLAPI_API operator==(const SAChar *s1, const SAString &s2);
bool SQLAPI_API operator!=(const SAString &s1, const SAString &s2);
bool SQLAPI_API operator!=(const SAString &s1, const SAChar *s2);
bool SQLAPI_API operator!=(const SAChar *s1, const SAString &s2);
bool SQLAPI_API operator<(const SAString &s1, const SAString &s2);
bool SQLAPI_API operator<(const SAString &s1, const SAChar *s2);
bool SQLAPI_API operator<(const SAChar *s1, const SAString &s2);
bool SQLAPI_API operator>(const SAString &s1, const SAString &s2);
bool SQLAPI_API operator>(const SAString &s1, const SAChar *s2);
bool SQLAPI_API operator>(const SAChar *s1, const SAString &s2);
bool SQLAPI_API operator<=(const SAString &s1, const SAString &s2);
bool SQLAPI_API operator<=(const SAString &s1, const SAChar *s2);
bool SQLAPI_API operator<=(const SAChar *s1, const SAString &s2);
bool SQLAPI_API operator>=(const SAString &s1, const SAString &s2);
bool SQLAPI_API operator>=(const SAString &s1, const SAChar *s2);
bool SQLAPI_API operator>=(const SAChar *s1, const SAString &s2);


class SQLAPI_API SANull
{
};

#define SA_NUMERIC_MANTISSA_SIZE 32
class SQLAPI_API SANumeric
{
	void InitZero();
	bool setFromPlainString(const SAChar *sVal);
	bool setFromExpString(const SAString &sVal);

public:
	SANumeric();	// default constructor, initializes to zero

	SANumeric(double dVal);			// initializes from double
	SANumeric &operator=(double);	// reinitializes from double
	operator double() const;		// converts to double

	SANumeric(sa_int64_t iVal);			// initializes from 64-bit integer
	SANumeric(sa_uint64_t iVal);
	SANumeric &operator=(sa_int64_t);	// reinitializes from 64-bit integer
	SANumeric &operator=(sa_uint64_t);
	operator sa_int64_t() const;		// converts to 64-bit integer
	operator sa_uint64_t() const;

	SANumeric &operator=(const SAChar *sVal);	// reinitializes from string
	operator SAString() const;	// converts to string

public:
	unsigned char precision;	// the maximum number of digits in base 10
	unsigned char scale;		// the number of digits to the right of the decimal point
	unsigned char sign;			// the sign: 1 for positive numbers, 0 for negative numbers

	// a number stored as SA_NUMERIC_MANTISSA_SIZE-byte scaled integer, with the least-significant byte on the left
	unsigned char val[SA_NUMERIC_MANTISSA_SIZE];
};

class SQLAPI_API SAInterval
{
public:
	SAInterval();
	SAInterval(double dVal);
	SAInterval(long nDays, int nHours, int nMins, int nSecs);

	double GetTotalDays() const;
	double GetTotalHours() const;
	double GetTotalMinutes() const;
	double GetTotalSeconds() const;

	long GetDays() const;
	long GetHours() const;
	long GetMinutes() const;
	long GetSeconds() const;

	SAInterval& operator=(double dVal);

	SAInterval operator+(const SAInterval& interval) const;
	SAInterval operator-(const SAInterval& interval) const;
	SAInterval& operator+=(const SAInterval interval);
	SAInterval& operator-=(const SAInterval interval);
	SAInterval operator-() const;

	operator double() const;

	operator SAString() const;

	void SetInterval(long nDays, int nHours, int nMins, int nSecs);

private:
	double m_interval;
};

//! Provides support for manipulating date/time values
class SQLAPI_API SADateTime
{
	friend class SAValueRead;

	static int m_saMonthDays[13];

protected:
	static bool DateFromTm(
		unsigned short wYear, unsigned short wMonth, unsigned short wDay,
		unsigned short wHour, unsigned short wMinute, unsigned short wSecond,
		unsigned int nNanoSecond,
		double &dtDest);
	static bool TmFromDate(
		double dtSrc,
	struct tm &tmDest, unsigned int &nNanoSecond);

protected:
    void Init_Tm();
    struct tm m_tm;
    unsigned int m_nFraction;   // 0..999999999
    void setSADateTime(int nYear, int nMonth, int nDay, int nHour, int nMinute, int nSecond);
public:
    SADateTime();
    SADateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec);
    SADateTime(char* dateTime);
    SADateTime(const struct tm &tmValue);
    SADateTime(double dt);
#ifndef UNDER_CE
	SADateTime(const struct timeb &tmbValue);
	SADateTime(const struct timeval &tmvValue);
#endif
#ifdef SQLAPI_WINDOWS
	SADateTime(SYSTEMTIME &st);
#endif
	SADateTime(const SADateTime &other);

	operator struct tm &();
	operator struct tm() const;
	operator double() const;
	operator SAString() const;

	int GetYear() const;		// year, f.ex., 1999, 2000
	int GetMonth() const;		// 1..12
	int GetDay() const;			// 1..31
	int GetHour() const;		// 0..23
	int GetMinute() const;		// 0..59
	int GetSecond() const;		// 0..59
	int GetDayOfWeek() const;	// 1..7, 1=Sunday, 2=Monday, and so on
	int GetDayOfYear() const;	// 1..366, where January 1 = 1

	unsigned int &Fraction();
	unsigned int Fraction() const;

#ifndef UNDER_CE
	void GetTimeValue(struct timeb &tmv);
	void GetTimeValue(struct timeval &tmv);
#endif
#ifdef SQLAPI_WINDOWS
	void GetTimeValue(SYSTEMTIME &st);
#endif

	//! Return the current date/time value
	static SADateTime SQLAPI_CALLBACK currentDateTime();
	static SADateTime SQLAPI_CALLBACK currentDateTimeWithFraction();

	SADateTime operator+(SAInterval interval) const;
	SADateTime operator-(SAInterval interval) const;
	SADateTime& operator+=(SAInterval interval);
	SADateTime& operator-=(SAInterval interval);

	SAInterval operator-(const SADateTime& dt) const;
};

class SQLAPI_API SAPos
{
	friend class SACommand;

	SAString m_sName;

public:
	SAPos(int nByID);
	SAPos(const SAString& sByName);
};

class SQLAPI_API saOptions
{
	int		m_nOptionCount;
	SAParam	**m_ppOptions;

private:
	// disable copy constructor
	saOptions(const saOptions &);
	// disable assignment operator
	saOptions &operator = (const saOptions &);

public:
	saOptions();
	virtual ~saOptions();

	SAString &operator[](const SAString &sOptionName);
	SAString operator[](const SAString &sOptionName) const;
};

//! Represents an unique session with a data source
class SQLAPI_API SAConnection
{
	friend class SACommand;
	friend class SAField;
	friend class SAParam;
	friend class ISAConnection;
	friend class Iora7Connection;
	friend class Iora8Connection;

private:
	// disable copy constructor
	SAConnection(const SAConnection &);
	// disable assignment operator
	SAConnection &operator = (const SAConnection &);

	SAClient_t m_eSAClient;
	ISAConnection *m_pISAConnection;
	SAMutex *m_pCommandsMutex;
	sa_Commands *m_pCommands;

	SAIsolationLevel_t m_eIsolationLevel;
	SAAutoCommit_t m_eAutoCommit;

	saOptions m_Options;

	int	nReserved;

protected:
	void EnumCursors(EnumCursors_t fn, void *pAddlData);
	void RegisterCommand(SACommand *pCommand);
	void UnRegisterCommand(SACommand *pCommand);
	ISACursor *GetISACursor(SACommand *pCommand);

public:
	SAConnection();
	virtual ~SAConnection();

	void setClient(SAClient_t eSAClient) SQLAPI_THROW(SAException);
	SAClient_t Client() const;
	long ClientVersion() const SQLAPI_THROW(SAException);
	long ServerVersion() const SQLAPI_THROW(SAException);
	SAString ServerVersionString() const SQLAPI_THROW(SAException);

	bool isConnected() const;
	bool isAlive() const;
	void Connect(
		const SAString &sDBString,
		const SAString &sUserID,
		const SAString &sPassword,
		SAClient_t eSAClient = SA_Client_NotSpecified,
		saConnectionHandler_t fHandler = NULL) SQLAPI_THROW(SAException);
	void Disconnect() SQLAPI_THROW(SAException);
	void Destroy();
	void Reset();

	void setIsolationLevel(SAIsolationLevel_t eIsolationLevel) SQLAPI_THROW(SAException);
	SAIsolationLevel_t IsolationLevel() const;
	void setAutoCommit(SAAutoCommit_t eAutoCommit) SQLAPI_THROW(SAException);
	SAAutoCommit_t AutoCommit() const;
	void Commit() SQLAPI_THROW(SAException);
	void Rollback() SQLAPI_THROW(SAException);

	SAString &setOption(const SAString &sOptionName);
	SAString Option(const SAString &sOptionName) const;

	saAPI *NativeAPI() const SQLAPI_THROW(SAException);
	saConnectionHandles *NativeHandles() SQLAPI_THROW(SAException);
};

// SAConnection options (common for at least several DBMS-es)
// Worksattion ID
#define SACON_OPTION_WSID		_TSA("WSID")
// Application Name
#define SACON_OPTION_APPNAME	_TSA("APPNAME")

//! Defines a specific command that you intend to execute against a data source.
class SQLAPI_API SACommand
{
	friend class SAConnection;
	friend class IibCursor;
	friend class IsybCursor;
	friend class IssDBLibCursor;
	friend class IsbCursor;
	friend class ImyCursor;
	friend class IpgCursor;
	friend class Iora8Connection;
	friend class Iora8Cursor;
	friend class Iora7Connection;
	friend class Iora7Cursor;
	//friend class SAValue;

private:
	// disable copy constructor
	SACommand(const SACommand &);
	// disable assignment operator
	SACommand &operator = (const SACommand &);

	SAConnection	*m_pConnection;

	SACommandType_t	m_eCmdType;
	SAString		m_sCmd;
	bool			m_bPrepared;
	bool			m_bExecuted;
	bool			m_bFieldsDescribed;
	bool			m_bSelectBuffersSet;

	bool			m_bParamsKnown;
	int				m_nPlaceHolderCount;
	saPlaceHolder	**m_ppPlaceHolders;
	int				m_nParamCount;
	SAParam			**m_ppParams;
	int				m_nMaxParamID;
	SAParam			**m_ppParamsID;
	int				m_nCurParamID;
	SAString		m_sCurParamName;

	int				m_nFieldCount;
	SAField			**m_ppFields;

	saOptions		m_Options;

	int	nReserved;

	void Init();

	static int CompareIdentifier(
		const SAString &sIdentifier1,
		const SAString &sIdentifier2);
	SAParam &CreateParam(
		const SAString &sName,
		SADataType_t eParamType,
		int nNativeType,
		size_t nParamSize,
		int	nParamPrecision,
		int	nParamScale,
		SAParamDirType_t eDirType,
		const SAString &sFullName,
		size_t nStart,	// param position in SQL statement
		size_t nEnd);	// param end position in SQL statemen
	void GetParamsSP();
	void UnDescribeParams();
	void ParseInputMarkers(
		SAString &sCmd,
		bool *pbSpacesInCmd);

	void DescribeFields() SQLAPI_THROW(SAException);
	void CreateField(
		const SAString &sName,
		SADataType_t eFieldType,
		int nNativeType,
		size_t nFieldSize,
		int nFieldPrecision,
		int nFieldScale,
		bool bFieldRequired);
	void DestroyFields();

	// parses sql statement and create bind parameters array if any (In)
	// also cancels previous stsement if any
	void ParseCmd(
		const SAString &sSQL,
		SACommandType_t eCmdType);

	void UnSetCommandText();
	void UnPrepare();
	void UnExecute();

public:
	//! Construct command with no associated connection and SQL
	SACommand();
	//! Construct command based on the given connection and SQL
	SACommand(
		SAConnection *pConnection,
		const SAString &sCmd = SAString(),
		SACommandType_t eCmdType = SA_CmdUnknown);

	virtual ~SACommand();

	SAConnection *Connection() const;
	void setConnection(SAConnection *pConnection);

	virtual void Open() SQLAPI_THROW(SAException);
	virtual bool isOpened();
	virtual bool isExecuted();
	virtual void Close() SQLAPI_THROW(SAException);
	virtual void Destroy();
	virtual void Reset();

	void setCommandText(
		const SAString &sSQL,
		SACommandType_t eCmdType = SA_CmdUnknown);
	SAString CommandText() const;
	SACommandType_t CommandType() const;
	virtual void Prepare() SQLAPI_THROW(SAException);
	virtual void Execute() SQLAPI_THROW(SAException);
	virtual void BatchExecute(char **pValues, int paramNum, int recNum) SQLAPI_THROW(SAException);
	bool isResultSet() SQLAPI_THROW(SAException);
	long RowsAffected() SQLAPI_THROW(SAException);	// returns number of rows affected by last DML operation
	bool FetchNext() SQLAPI_THROW(SAException);	// returns true if new row is fetched
	bool FetchPrior() SQLAPI_THROW(SAException);
	bool FetchFirst() SQLAPI_THROW(SAException);
	bool FetchLast() SQLAPI_THROW(SAException);
	void Cancel() SQLAPI_THROW(SAException);

	SAParam &CreateParam(
		const SAString &sName,
		SADataType_t eParamType,
		SAParamDirType_t eDirType = SA_ParamInput);
	SAParam &CreateParam(
		const SAString &sName,
		SADataType_t eParamType,
		int nNativeType,
		size_t nParamSize,
		int	nParamPrecision,
		int	nParamScale,
		SAParamDirType_t eDirType);
	void DestroyParams();
	int ParamCount();
	SAParam &ParamByIndex(int i);	// zero based index of C array
	SAParam &Param(int nParamByID);	// id in SQL statement, not in C array
	SAParam &Param(const SAString& sParamByName);
	SACommand &operator << (const SAPos &pos);
	SACommand &operator << (const SANull &null);
	SACommand &operator << (bool Value);
	SACommand &operator << (short Value);
	SACommand &operator << (unsigned short Value);
	SACommand &operator << (long Value);
	SACommand &operator << (unsigned long Value);
	SACommand &operator << (double Value);
	SACommand &operator << (const SANumeric &Value);
	SACommand &operator << (sa_int64_t Value);
	SACommand &operator << (sa_uint64_t Value);
	SACommand &operator << (const SADateTime &Value);
	SACommand &operator << (const SAChar *Value);	// special overload for string constants
	SACommand &operator << (const SAString &Value);
	SACommand &operator << (const SABytes &Value);
	SACommand &operator << (const SALongBinary &Value);
	SACommand &operator << (const SALongChar &Value);
	SACommand &operator << (const SABLob &Value);
	SACommand &operator << (const SACLob &Value);
	SACommand &operator << (const SAValueRead &Value);

	int FieldCount() SQLAPI_THROW(SAException);
	SAField &Field(int nField) SQLAPI_THROW(SAException);	// 1-based field number in result set
	SAField &Field(const SAString &sField) SQLAPI_THROW(SAException);
	SAField &operator[](int nField) SQLAPI_THROW(SAException);	// 1-based field number in result set
	SAField &operator[](const SAString &sField) SQLAPI_THROW(SAException);

	SAString &setOption(const SAString &sOptionName);
	SAString Option(const SAString &sOptionName) const;

	saCommandHandles *NativeHandles() SQLAPI_THROW(SAException);

	void setBatchExceptionPreHandler(PreHandleException_t fnHandler);
};

// SACommand options (common for at least several DBMS-es)
// Prefertching rows
#define SACMD_PREFETCH_ROWS		_TSA("PreFetchRows")
// Using scrollable cursor
#define SACMD_SCROLLABLE		_TSA("Scrollable")

class SQLAPI_API SAValueRead
{
	friend class ISACursor;
	friend class IibCursor;
	friend class Iora7Cursor;
	friend class Iora8Cursor;
	friend class IsbCursor;
	friend class IodbcCursor;
	friend class IssDBLibCursor;
	friend class IssOleDbCursor;
	friend class Idb2Cursor;
	friend class IinfCursor;
	friend class IsybCursor;
	friend class ImyCursor;
	friend class IpgCursor;
	friend class Isl3Cursor;
	friend class IssNCliCursor;

protected:
	SALongOrLobReaderModes_t m_eReaderMode;

	saLongOrLobReader_t	m_fnReader;
	size_t m_nReaderWantedPieceSize;
	void *m_pReaderAddlData;

	unsigned char *m_pReaderBuf;
	size_t m_nReaderAlloc;
	size_t m_nExpectedSizeMax;
	size_t m_nReaderRead;
	size_t m_nPieceSize;

	size_t PrepareReader(
		size_t nExpectedSizeMax,	// to optimaze buf allocation for internal buffer, 0 = unknown
		size_t nCallerMaxSize,	// max Piece that can be proceeced by API
		unsigned char *&pBuf,
		saLongOrLobReader_t fnReader,
		size_t nReaderWantedPieceSize,
		void *pReaderAddlData,
		bool bAddSpaceForNull = false);
	void InvokeReader(
		SAPieceType_t ePieceType,
		unsigned char *&pBuf,
		size_t nPieceLen);
	SAString asLongOrLob() const;

protected:
	SADataType_t	m_eDataType;

	// null indicator
	bool	*m_pbNull;
	// scalar types
	void	*m_pScalar;
	// an exact numeric value with a fixed precision and scale
	SANumeric	*m_pNumeric;
	// Date time
	SADateTime	*m_pDateTime;
	// Time interval
	SAInterval	*m_pInterval;
	// variable length types (string, bytes, Longs and Lobs)
	SAString	*m_pString;
	// Cursor
	SACommand	*m_pCursor;
private:
	// null indicator
	bool	m_bInternalNull;
	// scalar types
	union uScalars
	{
		bool m_Bool;
		short m_Short;
		unsigned short m_uShort;
		long m_Long;
		unsigned long m_uLong;
		double m_Double;
	} m_InternalScalar;
	SANumeric	m_InternalNumeric;
	SADateTime	m_InternalDateTime;
	SAInterval	m_InternalInterval;
	// variable length types (string, bytes, Longs and Lobs)
	SAString	m_InternalString;
	// Cursor
	SACommand	m_InternalCursor;

public:
	SAValueRead(SADataType_t eDataType);
	SAValueRead(const SAValueRead &vr);
	virtual ~SAValueRead();

	SAValueRead &operator =(const SAValueRead &vr);

public:
	SADataType_t DataType() const;

	// Null type
	bool isNull() const;

	// scalar types
	bool asBool() const;
	short asShort() const;
	unsigned short asUShort() const;
	long asLong() const;
	unsigned long asULong() const;
	double asDouble() const;

	// numeric
	SANumeric asNumeric() const;

	// date/time
	SADateTime asDateTime() const;

	// Interval
	SAInterval asInterval() const;

	// variable length types
	SAString asString() const;
	SAString asBytes() const;
	SAString asLongBinary() const;
	SAString asLongChar() const;
	SAString asBLob() const;
	SAString asCLob() const;

	// cursor
	SACommand *asCursor() const;

	void setLongOrLobReaderMode(SALongOrLobReaderModes_t eMode);
	SALongOrLobReaderModes_t LongOrLobReaderMode() const;

	// operators for quick accessing values
	// do not inline to prevent varnings
	operator bool() const;
	operator short() const;
	operator unsigned short() const;
	operator long() const;
	operator unsigned long() const;
	operator double() const;
	operator SANumeric() const;
	operator SADateTime() const;
	operator SAInterval() const;
	operator SAString() const;
	operator SACommand *() const;

	// data and indicator storage
public:
	void setIndicatorStorage(bool *pStorage);
	void setDataStorage(void *pStorage, SADataType_t eDataType);
};

class SQLAPI_API SAValue : public SAValueRead
{
	friend class ISACursor;
	friend class IibCursor;
	friend class IssDBLibCursor;
	friend class Iora7Cursor;
	friend class Iora8Cursor;
	friend class IsbCursor;
	friend class IodbcCursor;
	friend class Idb2Cursor;
	friend class IinfCursor;
	friend class IsybCursor;
	friend class ImyCursor;
	friend class IpgCursor;
	friend class Isl3Cursor;
	friend class IssNCliCursor;

private:
	bool m_bInternalUseDefault;

protected:
	bool *m_pbUseDefault;

	saLongOrLobWriter_t	m_fnWriter;
	size_t m_nWriterSize;
	void *m_pWriterAddlData;
	void *m_pWriterBuf;
	size_t m_nWriterAlloc;
	size_t m_nWriterWritten;

	size_t InvokeWriter(
		SAPieceType_t &ePieceType,
		size_t nCallerMaxSize,
		void *&pBuf);

public:
	SAValue(SADataType_t eDataType);
	virtual ~SAValue();

	// Sets NULL value
	void setAsNull();
	// Sets a flag to use default value
	void setAsDefault();
	// queries if "default value" flag is set
	bool isDefault() const;

	void setAsUnknown();

	// scalar types
	bool &setAsBool();
	short &setAsShort();
	unsigned short &setAsUShort();
	long &setAsLong();
	unsigned long &setAsULong();
	double &setAsDouble();

	// numeric
	SANumeric &setAsNumeric();

	// date/time
	SADateTime &setAsDateTime();

	// interval
	SAInterval &setAsInterval();

	// variable length types
	SAString &setAsString();
	SAString &setAsBytes();
	SAString &setAsLongBinary(
		saLongOrLobWriter_t fnWriter = NULL,
		size_t nWriterSize = 0, void *pAddlData = NULL);
	SAString &setAsLongChar(
		saLongOrLobWriter_t fnWriter = NULL,
		size_t nWriterSize = 0, void *pAddlData = NULL);
	SAString &setAsBLob(
		saLongOrLobWriter_t fnWriter = NULL,
		size_t nWriterSize = 0, void *pAddlData = NULL);
	SAString &setAsCLob(
		saLongOrLobWriter_t fnWriter = NULL,
		size_t nWriterSize = 0, void *pAddlData = NULL);

	// cursor
	SACommand *&setAsCursor();

	// special set function(s)
	SAValueRead &setAsValueRead();
};

class SQLAPI_API SAParam : public SAValue
{
	friend class SACommand;
	friend class saPlaceHolder;
	friend class saOptions;

	SACommand *m_pCommand;

	SAString m_sName;
	SADataType_t m_eParamType;
	int m_nNativeType;
	size_t m_nParamSize;
	int m_nParamPrecision;
	int m_nParamScale;
	SAParamDirType_t m_eDirType;

	saOptions m_Options;

private:
	// disable copy constructor
	SAParam(const SAParam &);
	// disable assignment operator
	SAParam &operator = (const SAParam &);

protected:
	SAParam(
		SACommand *pCommand,
		const SAString &sName,
		SADataType_t eParamType,
		int nNativeType,
		size_t nParamSize,
		int	nParamPrecision,
		int	nParamScale,
		SAParamDirType_t eDirType);
	virtual ~SAParam();

public:
	const SAString &Name() const;
	SADataType_t ParamType() const;
	void setParamType(SADataType_t eParamType);
	int ParamNativeType() const;
	void setParamNativeType(int nNativeType);
	size_t ParamSize() const;
	void setParamSize(size_t nParamSize);
	SAParamDirType_t ParamDirType() const;
	void setParamDirType(SAParamDirType_t eParamDirType);
	int ParamPrecision() const;
	void setParamPrecision(int nParamPrecision);
	int ParamScale() const;
	void setParamScale(int nParamScale);

	void ReadLongOrLob(
		saLongOrLobReader_t fnReader,
		size_t nReaderWantedSize,
		void *pAddlData);

	SAString &setOption(const SAString &sOptionName);
	SAString Option(const SAString &sOptionName) const;
};

class SQLAPI_API saPlaceHolder
{
	friend class SACommand;

	SAString m_sFullName;
	size_t m_nStart;
	size_t m_nEnd;

	SAParam	*m_pParamRef;

private:
	saPlaceHolder(
		const SAString &sFullName,
		size_t nStart,
		size_t nEnd,
		SAParam *pParamRef);
	virtual ~saPlaceHolder();

public:
	const SAString &getFullName() const;
	size_t& getStart();
	size_t& getEnd();
	SAParam *getParam() const;
};

class SQLAPI_API SABytes : public SAString
{
public:
	SABytes(const SAString &sData);
};

class SQLAPI_API SALongOrLob : public SAString
{
	friend class SACommand;

protected:
	saLongOrLobWriter_t	m_fnWriter;
	size_t m_nWriterPieceSize;
	void *m_pAddlData;

	SALongOrLob(const SAString &sData);
	SALongOrLob(
		saLongOrLobWriter_t fnWriter,
		size_t nWriterPieceSize,
		void *pAddlData);
};

class SQLAPI_API SALongBinary : public SALongOrLob
{
public:
	SALongBinary(const SAString &sData);
	SALongBinary(
		saLongOrLobWriter_t fnWriter,
		size_t nWriterPieceSize,
		void *pAddlData);
};

class SQLAPI_API SALongChar : public SALongOrLob
{
public:
	SALongChar(const SAString &sData);
	SALongChar(
		saLongOrLobWriter_t fnWriter,
		size_t nWriterPieceSize,
		void *pAddlData);
};

class SQLAPI_API SABLob : public SALongOrLob
{
public:
	SABLob(const SAString &sData);
	SABLob(
		saLongOrLobWriter_t fnWriter,
		size_t nWriterPieceSize,
		void *pAddlData);
};

class SQLAPI_API SACLob : public SALongOrLob
{
public:
	SACLob(const SAString &sData);
	SACLob(
		saLongOrLobWriter_t fnWriter,
		unsigned int nWriterPieceSize,
		void *pAddlData);
};

class SQLAPI_API SAField : public SAValueRead
{
	friend class SACommand;

	SACommand *m_pCommand;

	// as reported by describe API
	int					m_nPos;	// 1-based
	SAString			m_sName;
	SADataType_t		m_eFieldType;
	int					m_nNativeType;
	size_t				m_nFieldSize;
	int					m_nFieldPrecision;
	int					m_nFieldScale;
	bool				m_bFieldRequired;

	saOptions m_Options;

private:
	// disable copy constructor
	SAField(const SAField &);
	// disable assignment operator
	SAField &operator = (const SAField &);

protected:
	SAField(
		SACommand *pCommand,
		int nPos,	// 1-based
		const SAString &sName,
		SADataType_t eFieldType,
		int nNativeType,
		size_t nFieldSize,
		int nFieldPrecision,
		int nFieldScale,
		bool bFieldRequired);
	virtual ~SAField();

public:
	int Pos() const;
	const SAString &Name() const;
	SADataType_t FieldType() const;
	int FieldNativeType() const;
	size_t FieldSize() const;
	int FieldPrecision() const;
	int FieldScale() const;
	bool isFieldRequired() const;

	// !!!
	void setFieldSize(int nSize);
	void setFieldType(SADataType_t eType);

	void ReadLongOrLob(
		saLongOrLobReader_t fnReader,
		size_t nReaderWantedSize,
		void *pAddlData);

	SAString &setOption(const SAString &sOptionName);
	SAString Option(const SAString &sOptionName) const;
};

class SQLAPI_API SAException
#ifdef SQLAPI_EXCEPTION_DELIVERED_FROM
	: public SQLAPI_EXCEPTION_DELIVERED_FROM
#endif
{
	friend class SAConnection;
	friend class SACommand;
	friend class SAStoredProc;
	friend class Iora7Connection;
	friend class Iora8Connection;
	friend class IibConnection;
	friend class IodbcConnection;
	friend class IsbConnection;
	friend class Iss6Connection;

public:
	SAException(
		SAErrorClass_t eError,
		int nNativeError,
		int nErrPos,
		const SAString &sMsg);
	SAException(
		SAErrorClass_t eError,
		int nNativeError,
		int nErrPos,
		const SAChar *lpszFormat, ...);

	SAException(const SAException &other);

public:
	virtual ~SAException();

	static void SQLAPI_CALLBACK throwUserException(
		int nUserCode, const SAChar *lpszFormat, ...);

	SAErrorClass_t ErrClass() const;
	int ErrNativeCode() const;
	int ErrPos() const;
	SAString ErrText() const;

#ifdef SQLAPI_EXCEPTION_HAS_CUSTOM_MEMBERS
	SQLAPI_EXCEPTION_HAS_CUSTOM_MEMBERS
#endif

protected:
	SAErrorClass_t	m_eErrorClass;
	int				m_nNativeError;
	int				m_nErrPos;
	SAString		m_sMsg;

	int	nReserved;
};

class SQLAPI_API SAUserException : public SAException
{
	friend class SAException;

protected:
	SAUserException(int nUserCode, const SAString &sMsg);

public:
	virtual ~SAUserException();
};

typedef	enum eSATraceInfo
{
	SA_Trace_None = 0,
	SA_Trace_QueryText = 1, //! trace real DBMS API query text SQLAPI++ sends
} SATraceInfo_t;

//! Callback for tracing
typedef void (SQLAPI_CALLBACK *SATraceFunction_t)(
	SATraceInfo_t traceInfo, //! tracing into label
	SAConnection* pCon, //! related SAConnection or NULL
	SACommand* pCmd, //! related SACommand or NULL
	const SAChar* szTraceInfo, //! tracing text
	void* pData); //! user provided data

//! Global SQLAPI++ settings
class SQLAPI_API SAGlobals
{
public:
	static char * SQLAPI_CALLBACK setlocale(int category, const char *locale);

	static int GetVersionMajor();
	static int GetVersionMinor();
	static int GetVersionBuild();

	static bool& UnloadAPI();

	static const SAChar* ClientToString(SAClient_t eSAClient);
	static SAClient_t StringToClient(const SAChar* szClientName);

	static void SetTraceFunction(SATraceInfo_t traceInfo, SATraceFunction_t traceFunc, void* pData);
};

#define SQLAPI_VER_MAJOR	4
#define SQLAPI_VER_MINOR	1
#define SQLAPI_VER_BUILD	2

#endif // !defined(__SQLAPI_H__)


