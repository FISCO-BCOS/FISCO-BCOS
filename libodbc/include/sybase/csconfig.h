/*
**	Sybase Open Client/Server 
**	Confidential Property of Sybase, Inc.
**	Copyright  Sybase, Inc. 1992 - 2005
**	All rights reserved
*/

#ifndef __CSCONFIG_H__
#define __CSCONFIG_H__

/*
** This file will try to hide any interface differences found on various
** platform/compiler combinations. Any datatype or define that is
** dependant on a particular platform/compiler should have its definition
** here.
*/

/*****************************************************************************
**
** Function interface definitions.
**
*****************************************************************************/

/*
** Define the scoping rules that will be used in all libraries. These defines
** exist to support different compiler-specific keywords describing the
** calling convention of a C function.
**
** CS_PUBLIC		Interface define for functions which are called
**			outside of a library or DLL.
**
** CS_INTERNAL		Interface define for functions which are only called
**			within a library or DLL.
**
** CS_VARARGS		Interface define for functions with variable argument
**			lists which are called outside of a library or DLL.
**
** CS_STATIC		Interface define for functions which are only called
**			within a C module.
*/

/*
** We need to check to value of some compiler #defined macros
** but some compilers will give warnings for trying to use the
** value of a macro which has not been defined -- to avoid these
** warnings we define 'SYB' versions of the macros we need here
*/
#if defined(_MSC_VER)
#define SYB_MSC_VER _MSC_VER
#else /* _MSC_VER */
#define SYB_MSC_VER 0
#endif /* _MSC_VER */
#if defined(__STDC__)
#define SYB__STDC__ 1
#else /* __STDC__ */
#define SYB__STDC__ 0
#endif /* __STDC__ */

#if defined(_CPROTO_)

/*
** The _CPROTO_ ifdef allows users to have the cproto tool generate
** prototype definitions that include the scoping rules. We typedef all of
** the interface defines to allow the cproto tool to preserve the names
** (instead of replacing them with the defined values).
*/
typedef void		CS_PUBLIC;
typedef void		CS_INTERNAL;
typedef void            CS_VARARGS;
typedef void            CS_STATIC;

#else /* defined(_CPROTO_) */

#if ((SYB_MSC_VER >= 800)  || defined(__BORLANDC__))

/*
** Defines for Windows NT calling conventions.
*/
#define CS_PUBLIC	__stdcall
#define CS_INTERNAL     CS_PUBLIC
#define CS_VARARGS      __stdcall
#define CS_STATIC       static

#else /* ((SYB_MSC_VER >= 800) || defined(__BORLANDC__)) */

/*
** Default defines for all other platform/compilers.
*/
#define CS_PUBLIC		
#define CS_INTERNAL 
#define CS_VARARGS
#define CS_STATIC       static

#endif /* ((SYB_MSC_VER >= 800) || defined(__BORLANDC__)) */

#endif /* defined(_CPROTO_) */

/*
** Define wrappers to insure that all function prototypes defined in the
** Open Client and Open Server header files have the correct C function
** linkage when used with C++ compilers.
*/
#if defined(__cplusplus)
#define CS_START_EXTERN_C	extern "C" {
#define CS_END_EXTERN_C		}
#else
#define CS_START_EXTERN_C		
#define CS_END_EXTERN_C		
#endif /* defined(__cplusplus) */

/*
** PROTOTYPE macro which produces the prototype
*/
#ifndef PROTOTYPE
# define PROTOTYPE(s) s
#endif /* PROTOTYPE */

/*
** Define whether the compiler supports the "const" keyword.
*/
#if (SYB__STDC__) || defined(__cplusplus)
# define CS_CONST	const
#else
# define CS_CONST
#endif

/*
** Define CS_ELLIPSE.  It is used in function pointer prototypes where
** the arguments aren't known.  Win3 uses ,... for CS_ELLIPSE, too.
*/
#define CS_ELLIPSE              , ...

/*****************************************************************************
**
** Datatype definitions.
**
*****************************************************************************/

/*
** Certain Sybase C datatypes must be defined as 4 bytes in size. The
** following insures that all platforms (including 64 bit machines) use
** the correct C native types.
*/
#if defined( __alpha) || defined(SYB_LP64) || defined(SYB_LLP64) || defined(_AIX)
typedef	int		CS_INT;
typedef int		CS_RETCODE;
typedef	int		CS_BOOL;
typedef	unsigned int	CS_UINT;
#else
typedef	long		CS_INT;
typedef long		CS_RETCODE;
typedef	long		CS_BOOL;
typedef	unsigned long	CS_UINT;
#endif

typedef void		CS_VOID;
typedef void		CS_THRDRES;

typedef double		CS_FLOAT;	/* 8 byte float type */

/*
** Define the number of bits that a CS_BYTE can hold.
*/
#define CS_BITS_PER_BYTE	8

#endif /* __CSCONFIG_H__ */

