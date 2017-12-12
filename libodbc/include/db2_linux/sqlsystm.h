/******************************************************************************
 *
 * Source File Name = SQLSYSTM.H
 *
 * (C) COPYRIGHT International Business Machines Corp. 1993, 1998
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * Function = Include File defining:
 *              Operating System Specific Information
 *
 * Operating System = Linux
 *
 *****************************************************************************/

#if !defined  SQL_H_SQLSYSTM
#define SQL_H_SQLSYSTM          /* Permit duplicate Includes */

#if !defined DB2LINUX
   #define DB2LINUX    1
#endif

/* Operating System Control Parameters */

#if !defined SQL_API_RC
   #define SQL_API_RC      int
   #define SQL_STRUCTURE   struct
   #define PSQL_API_FN     *
   #define SQL_API_FN
   #define SQL_POINTER
   #define SQL_API_INTR
#endif

/******************************************************************************
**
** The SQLOLDCHAR macro may be used to maintain compatibility between
** version 1 applications and version 2 header files.  In version 1, many
** strings were declared as 'unsigned char'.  In keeping with the spirit
** of ANSI C, all character data, structure members and function
** parameters with string semantics have been changed to 'char' in version 2.
** This change may produce type conflicts with some compilers.  Adding
** -DSQLOLDCHAR to the compile command will cause the changed items to
** revert to their version 1 types.  Note that this should be used for
** compatibility purposes only.  New code should be written using plain
** 'char' where indicated in the documentation.
******************************************************************************/

#undef _SQLOLDCHAR
#if defined SQLOLDCHAR
   #define _SQLOLDCHAR     unsigned char
#else
   #define _SQLOLDCHAR     char
#endif

/******************************************************************************
**
** Define fixed size integer types.
**
******************************************************************************/

typedef char            sqlint8;
typedef unsigned char   sqluint8;

typedef short           sqlint16;
typedef unsigned short  sqluint16;


#if defined __LP64__ || defined __PPC64__ || defined __x86_64__ || defined __s390x__
   #define db2Is64bit
#endif

#if defined db2Is64bit || defined DB2_FORCE_INT32_TYPES_TO_INT
   typedef int             sqlint32;
   typedef unsigned int    sqluint32;
#else
   typedef long            sqlint32;
   typedef unsigned long   sqluint32;
#endif

#if !defined SQL_BIGINT_TYPE
   #if defined db2Is64bit
      #define SQL_BIGINT_TYPE long
      #define DB2_CONSTRUCT_BIGINT_CONSTANT(db2BigIntConstantValue) db2BigIntConstantValue##L
   #else
      #define SQL_BIGINT_TYPE long long
      #define DB2_CONSTRUCT_BIGINT_CONSTANT(db2BigIntConstantValue) db2BigIntConstantValue##LL
   #endif
#endif

#if !defined SQL_BIGUINT_TYPE
   #if defined db2Is64bit
      #define SQL_BIGUINT_TYPE unsigned long
   #else
      #define SQL_BIGUINT_TYPE unsigned long long
   #endif
#endif

typedef SQL_BIGINT_TYPE sqlint64;
typedef SQL_BIGUINT_TYPE sqluint64;

/******************************************************************************
**
** The sqlintptr and sqluintptr are defined as integer types large enough
** to contain pointer values on this platform.
**
******************************************************************************/

#if defined db2Is64bit
   typedef sqlint64        sqlintptr;
   typedef sqluint64       sqluintptr;
#else
   typedef sqlint32        sqlintptr;
   typedef sqluint32       sqluintptr;
#endif

#endif /* SQL_H_SQLSYSTM */
