// samisc.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__SAMISC_H__)
#define __SAMISC_H__

#define sa_min(x, y) ((x) < (y)? (x) : (y))
#define sa_max(x, y) ((x) > (y)? (x) : (y))

#if defined(SA_UNICODE)
#define sa_strcpy(x, y) wcscpy(x, y)
#define sa_strlen(x) wcslen(x)
#define sa_strstr(x, y) wcsstr(x, y)
#define sa_strchr(x, y) wcschr(x, y)
#define sa_isspace(x) iswspace(x)
#define sa_isdigit(x) iswdigit(x)
#define sa_isalpha(x) iswalpha(x)
#define sa_strcmp(x, y) wcscmp(x, y)
#define sa_strncmp(x, y, z) wcsncmp(x, y, z)
#define sa_tolower(x) towlower(x)
#define sa_toupper(x) towupper(x)
#define sa_strcoll(x, y) wcscoll(x, y)
#define sa_strpbrk(x, y) wcspbrk(x, y)
#define sa_strrchr(x, y) wcsrchr(x, y)
#define sa_strtol(x, y, z) wcstol(x, y, z)
#define sa_strtoul(x, y, z) wcstoul(x, y, z)
#define sa_strtod(x, y) wcstod(x, y)
#define sa_toi(x) (int)wcstol(x, NULL, 10)
#define sa_tol(x) wcstol(x, NULL, 10)
#define sa_sscanf swscanf
#define sa_printf wprintf
#define sa_scanf wscanf
#define sa_getchar getwchar
#ifdef SQLAPI_WINDOWS
#define sa_snprintf _snwprintf
#define sa_vsnprintf(x, y, z, j) _vsnwprintf(x, y, z, j)
#define sa_stricmp(x, y) _wcsicmp(x, y)
#else
#define sa_vsnprintf(x, y, z, j) vswprintf(x, y, z, j)
#define sa_snprintf swprintf
#define sa_stricmp(x, y) wcscasecmp(x, y)
#endif // ! SQLAPI_WINDOWS
#define sa_csinc(x) (++(x))
#define sa_clen(x) (1)
#else
#define sa_strcpy(x, y) strcpy(x, y)
#define sa_strlen(x) strlen(x)
#define sa_strstr(x, y) strstr(x, y)
#define sa_strchr(x, y) strchr(x, y)
#define sa_isspace(x) isspace((unsigned char)x)
#define sa_isdigit(x) isdigit((unsigned char)x)
#define sa_isalpha(x) isalpha((unsigned char)x)
#define sa_strcmp(x, y) strcmp(x, y)
#define sa_strncmp(x, y, z) strncmp(x, y, z)
#define sa_tolower(x) tolower((unsigned char)x)
#define sa_toupper(x) toupper((unsigned char)x)
#define sa_strcoll(x, y) strcoll(x, y)
#define sa_strpbrk(x, y) strpbrk(x, y)
#define sa_strrchr(x, y) strrchr(x, y)
#define sa_strtol(x, y, z) strtol(x, y, z)
#define sa_strtoul(x, y, z) strtoul(x, y, z)
#define sa_strtod(x, y) strtod(x, y)
#define sa_toi(x) atoi(x)
#define sa_tol(x) atol(x)
#define sa_sscanf sscanf
#define sa_printf printf
#define sa_scanf scanf
#define sa_getchar getchar
#ifdef SQLAPI_WINDOWS
#if defined(__BORLANDC__) && (__BORLANDC__  <= 0x0520)
#define sa_vsnprintf(x, y, z, j) vsprintf(x, z, j)
#else
#define sa_vsnprintf(x, y, z, j) _vsnprintf(x, y, z, j)
#endif
#define sa_snprintf _snprintf
#define sa_stricmp(x, y) _stricmp(x, y)
#else
#define sa_vsnprintf(x, y, z, j) vsnprintf(x, y, z, j)
#define sa_snprintf snprintf
#define sa_stricmp(x, y) strcasecmp(x, y)
#endif // ! SQLAPI_WINDOWS
#define sa_csinc(x) (++(x))
#define sa_clen(x) (1)
#endif

#ifdef SQLAPI_WINDOWS
#ifdef __BORLANDC__
#define _strnicmp strnicmp
#define _stricmp stricmp
#endif
#elif SQLAPI_SCOOSR5
#define _strnicmp strnicmp
#define _stricmp stricmp
#else
#define _strnicmp strncasecmp
#define _stricmp strcasecmp
#endif	// defined(SQLAPI_WINDOWS)

#ifdef SQLAPI_WINDOWS
#include <windows.h>
#else
#if defined(SA_USE_PTHREAD)
#include <pthread.h>
#endif	// defined(SA_USE_PTHREAD)
#endif

class SQLAPI_API SAMutex
{
#ifdef  SQLAPI_WINDOWS
#ifdef SA_USE_MUTEXT_LOCK
	HANDLE m_hMutex;
#else
	CRITICAL_SECTION m_hCriticalSection;
#endif
#endif	// defined(SQLAPI_WINDOWS)

#if defined(SA_USE_PTHREAD)
	pthread_mutex_t m_mutex;	// mutex
	// we need additional machinery
	// to allow portable recursion
	int				m_locks;	// number of times owner locked mutex
	pthread_t		m_owner_thread;	//owner of mutex
	pthread_mutex_t	m_helper_mutex;	// structure access lock
#endif	// defined(SA_USE_PTHREAD)

public:
	SAMutex();
	virtual ~SAMutex();

	void Wait();
	void Release();
};

class SQLAPI_API SACriticalSectionScope
{
	SAMutex *m_pSAMutex;

public:
	SACriticalSectionScope(SAMutex *pSAMutex);
	virtual ~SACriticalSectionScope();
};

#endif // !defined(__SAMISC_H__)
