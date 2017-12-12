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
 * (c)  Copyright IBM Corporation 1997, 2011. All rights reserved.
 *
** INFXCLI.H - This is the the main include for IBM Informix-CLI
**             applications.
**
** Preconditions:  
**                for Windows environment:
**                        #include "windows.h"
**
*********************************************************************/

#ifndef __INFXCLI_H
#define __INFXCLI_H

#ifdef __cplusplus
extern "C" {                /* Assume C declarations for C++   */
#endif  /* __cplusplus */

/*
**     include sql.h and sqlext.h
*/
#ifdef _WINDOWS_
#include "sql.h"
#include "sqlext.h"
#else
#include <stdlib.h>
#include "infxsql.h"
#endif /*_WINDOWS_*/

#define SQL_TXN_LAST_COMMITTED               0x00000010L
#define SQL_TRANSACTION_LAST_COMMITTED       SQL_TXN_LAST_COMMITTED

/* For extended errors */

#define SQL_DIAG_ISAM_ERROR        13
#define SQL_DIAG_XA_ERROR          14

/* START -- Q+E Software's SQLSetStmtOption extensions (1040 to 1139) */
/* defines here for backwards compatibility */

#define SQL_STMTOPT_START                       1040

/* Get the rowid for the last row inserted */
#define SQL_GET_ROWID                           (SQL_STMTOPT_START+8)

/* Get the value for the serial column in the last row inserted */
#define SQL_GET_SERIAL_VALUE                    (SQL_STMTOPT_START+9)

/* END -- Q+E Software's SQLSetStmtOption extensions (1040 to 1139) */

/*
**    Informix extensions
*/

/* Informix Column Attributes Flags Definitions */
#define FDNULLABLE      0x0001          /* null allowed in field */
#define FDDISTINCT      0x0002          /* distinct of all */
#define FDDISTLVARCHAR  0x0004          /* distinct of SQLLVARCHAR */
#define FDDISTBOOLEAN   0x0008          /* distinct of SQLBOOL */
#define FDDISTSIMP      0x0010          /* distinct of simple type */
#define FDCSTTYPE       0x0020          /* constructor type */
#define FDNAMED         0x0040          /* named row type */

#define ISNULLABLE( flags )       ( flags & FDNULLABLE ? 1 : 0)
#define ISDISTINCT( flags )       ( flags & FDDISTINCT ? 1 : 0)

/* Informix Type Estensions */
#define SQL_INFX_UDT_FIXED                      -100
#define SQL_INFX_UDT_VARYING                    -101
#define SQL_INFX_UDT_BLOB                       -102
#define SQL_INFX_UDT_CLOB                       -103
#define SQL_INFX_UDT_LVARCHAR                   -104
#define SQL_INFX_RC_ROW                         -105
#define SQL_INFX_RC_COLLECTION                  -106
#define SQL_INFX_RC_LIST                        -107
#define SQL_INFX_RC_SET                         -108
#define SQL_INFX_RC_MULTISET                    -109
#define SQL_INFX_UNSUPPORTED                    -110
#define SQL_INFX_C_SMARTLOB_LOCATOR             -111
#define SQL_INFX_QUALIFIER                      -112
#define SQL_INFX_DECIMAL                        -113
#define SQL_INFX_BIGINT                         -114

typedef void * HINFX_RC;		/* row & collection handle */

/* Informix Connect Attributes Extensions */
#define SQL_OPT_LONGID                           2251
#define SQL_INFX_ATTR_LONGID                     SQL_OPT_LONGID
#define SQL_INFX_ATTR_LEAVE_TRAILING_SPACES      2252
#define SQL_INFX_ATTR_DEFAULT_UDT_FETCH_TYPE     2253
#define SQL_INFX_ATTR_ENABLE_SCROLL_CURSORS      2254
#define SQL_ENABLE_INSERT_CURSOR                 2255
#define SQL_INFX_ATTR_ENABLE_INSERT_CURSORS      SQL_ENABLE_INSERT_CURSOR
#define SQL_INFX_ATTR_OPTIMIZE_AUTOCOMMIT        2256
#define SQL_INFX_ATTR_ODBC_TYPES_ONLY            2257
#define SQL_INFX_ATTR_FETCH_BUFFER_SIZE          2258
#define SQL_INFX_ATTR_OPTOFC                     2259
#define SQL_INFX_ATTR_OPTMSG                     2260
#define SQL_INFX_ATTR_REPORT_KEYSET_CURSORS      2261
#define SQL_INFX_ATTR_LO_AUTOMATIC               2262
#define SQL_INFX_ATTR_AUTO_FREE                  2263
#define SQL_INFX_ATTR_DEFERRED_PREPARE           2265

#define SQL_INFX_ATTR_PAM_FUNCTION               2266  /* void pamCallback
                                                                   (int msgStyle,
                                                                    void *responseBuf, 
                                                                    int responseBufLen, 
                                                                    int *responseLenPtr,
                                                                    void *challengeBuf,
                                                                    int challengeBufLen,
                                                                    int *challengeLenPtr) */
#define SQL_INFX_ATTR_PAM_RESPONSE_BUF           2267  /* SQLPOINTER */
#define SQL_INFX_ATTR_PAM_RESPONSE_BUF_LEN       2268  /* SQLINTEGER */
#define SQL_INFX_ATTR_PAM_RESPONSE_LEN_PTR       2269  /* SQLPOINTER */
#define SQL_INFX_ATTR_PAM_CHALLENGE_BUF          2270  /* SQLPOINTER */
#define SQL_INFX_ATTR_PAM_CHALLENGE_BUF_LEN      2271  /* SQLINTEGER */
#define SQL_INFX_ATTR_PAM_CHALLENGE_LEN_PTR      2272  /* SQLINTEGER * - number of bytes in challenge */
#define SQL_INFX_ATTR_DELIMIDENT                 2273  /* As of now this attribute is only being used
                                                         in .NET Provider since it is sitting on top
                                                         of ODBC.*/
#define SQL_INFX_ATTR_DBLOCALE			2275
#define SQL_INFX_ATTR_LOCALIZE_DECIMALS  2276
#define SQL_INFX_ATTR_DEFAULT_DECIMAL    2277
#define SQL_INFX_ATTR_SKIP_PARSING       2278
#define SQL_INFX_ATTR_CALL_FROM_DOTNET   2279
#define SQL_INFX_ATTR_LENGTHINCHARFORDIAGRECW        2280
#define SQL_INFX_ATTR_SENDTIMEOUT        2281
#define SQL_INFX_ATTR_RECVTIMEOUT        2282
#define SQL_INFX_ATTR_IDSISAMERRMSG      2283

/*Attributes same as cli*/
#define SQL_ATTR_USE_TRUSTED_CONTEXT               2561

/* Informix Descriptor Extensions */
#define SQL_INFX_ATTR_FLAGS                      1900 /* UDWORD */
#define SQL_INFX_ATTR_EXTENDED_TYPE_CODE         1901 /* UDWORD */
#define SQL_INFX_ATTR_EXTENDED_TYPE_NAME         1902 /* UCHAR ptr */
#define SQL_INFX_ATTR_EXTENDED_TYPE_OWNER        1903 /* UCHAR ptr */
#define SQL_INFX_ATTR_EXTENDED_TYPE_ALIGNMENT    1904 /* UDWORD */
#define SQL_INFX_ATTR_SOURCE_TYPE_CODE           1905 /* UDWORD */

/* Informix Statement Attributes Extensions */
#define SQL_VMB_CHAR_LEN                         2325
#define SQL_INFX_ATTR_VMB_CHAR_LEN               SQL_VMB_CHAR_LEN
#define SQL_INFX_ATTR_MAX_FET_ARR_SIZE           2326

/* Informix fOption, SQL_VMB_CHAR_LEN vParam */
#define SQL_VMB_CHAR_EXACT                       0
#define SQL_VMB_CHAR_ESTIMATE                    1

/* Informix row/collection traversal constants */
#define SQL_INFX_RC_NEXT		1
#define SQL_INFX_RC_PRIOR		2
#define SQL_INFX_RC_FIRST		3
#define SQL_INFX_RC_LAST		4
#define SQL_INFX_RC_ABSOLUTE		5
#define SQL_INFX_RC_RELATIVE		6
#define SQL_INFX_RC_CURRENT		7

/*******************************************************************************
 * Large Object (LO) related structures
 *
 * LO_SPEC: Large object spec structure
 * It is used for creating smartblobs. The user may examin and/or set certain
 * fields of LO_SPEC by using ifx_lo_spec[set|get]_* accessor functions.
 *
 * LO_PTR: Large object pointer structure
 * Identifies the LO and provides ancillary, security-related information.
 *
 * LO_STAT: Large object stat structure
 * It is used in querying attribtes of smartblobs. The user may examin fields
 * herein by using ifx_lo_stat[set|get]_* accessor functions. 
 *
 * These structures are opaque to the user. Accessor functions are provided
 * for these structures.
 ******************************************************************************/

/* Informix GetInfo Extensions to obtain length of LO related structures */
#define SQL_INFX_LO_SPEC_LENGTH                  2250 /* UWORD */
#define SQL_INFX_LO_PTR_LENGTH                   2251 /* UWORD */
#define SQL_INFX_LO_STAT_LENGTH                  2252 /* UWORD */

/******************************************************************************
 * LO Open flags: (see documentation for further explanation)
 *
 * LO_APPEND   - Positions the seek position to end-of-file + 1. By itself,
 *               it is equivalent to write only mode followed by a seek to the
 *               end of large object. Read opeartions will fail.
 *               You can OR the LO_APPEND flag with another access mode.
 * LO_WRONLY   - Only write operations are valid on the data.
 * LO_RDONLY   - Only read operations are valid on the data.
 * LO_RDWR     - Both read and write operations are valid on the data.
 *
 * LO_RANDOM   - If set overrides optimizer decision. Indicates that I/O is
 *               random and that the system should not read-ahead.
 * LO_SEQUENTIAL - If set overrides optimizer decision. Indicates that
 *               reads are sequential in either forward or reverse direction.
 *
 * LO_FORWARD  - Only used for sequential access. Indicates that the sequential
 *               access will be in a forward direction, i.e. from low offset
 *               to higher offset.
 * LO_REVERSE  - Only used for sequential access. Indicates that the sequential
 *               access will be in a reverse direction.
 *
 * LO_BUFFER   - If set overrides optimizer decision. I/O goes through the
 *               buffer pool.
 * LO_NOBUFFER - If set then I/O does not use the buffer pool.
 ******************************************************************************/

#define LO_APPEND       0x1
#define LO_WRONLY       0x2
#define LO_RDONLY       0x4     /* default */
#define LO_RDWR         0x8

#define LO_RANDOM       0x20    /* default is determined by optimizer */
#define LO_SEQUENTIAL   0x40    /* default is determined by optimizer */

#define LO_FORWARD      0x80    /* default */
#define LO_REVERSE      0x100

#define LO_BUFFER       0x200   /* default is determined by optimizer */
#define LO_NOBUFFER     0x400   /* default is determined by optimizer */

#define LO_DIRTY_READ   0x10
#define LO_NODIRTY_READ 0x800

#define LO_LOCKALL	0x1000  /* default */
#define LO_LOCKRANGE	0x2000 
    
/*******************************************************************************
 * LO create-time flags: 
 *
 * Bitmask - Set/Get via ifx_lo_specset_flags() on LO_SPEC.
 ******************************************************************************/
 
#define LO_ATTR_LOG                          0x0001
#define LO_ATTR_NOLOG                        0x0002
#define LO_ATTR_DELAY_LOG                    0x0004
#define LO_ATTR_KEEP_LASTACCESS_TIME         0x0008
#define LO_ATTR_NOKEEP_LASTACCESS_TIME       0x0010
#define LO_ATTR_HIGH_INTEG                   0x0020
#define LO_ATTR_MODERATE_INTEG               0x0040

/*******************************************************************************
 * Symbolic constants for the "lseek" routine
 ******************************************************************************/
 
#define LO_SEEK_SET 0   /* Set curr. pos. to "offset"           */
#define LO_SEEK_CUR 1   /* Set curr. pos. to current + "offset" */
#define LO_SEEK_END 2   /* Set curr. pos. to EOF + "offset"     */
 
/*******************************************************************************
 * Symbolic constants for lo_lock and lo_unlock routines.
******************************************************************************/
	
#define LO_SHARED_MODE		1
#define LO_EXCLUSIVE_MODE	2

/*******************************************************************************
 * Intersolv specific infoTypes for SQLGetInfo
 ******************************************************************************/

#define SQL_RESERVED_WORDS						 1011
#define SQL_PSEUDO_COLUMNS						 1012
#define SQL_FROM_RESERVED_WORDS					 1013
#define SQL_WHERE_CLAUSE_TERMINATORS			 1014
#define SQL_COLUMN_FIRST_CHARS					 1015
#define SQL_COLUMN_MIDDLE_CHARS					 1016
#define SQL_TABLE_FIRST_CHARS					 1018
#define SQL_TABLE_MIDDLE_CHARS					 1019
#define SQL_FAST_SPECIAL_COLUMNS				 1021
#define SQL_ACCESS_CONFLICTS					 1022
#define SQL_LOCKING_SYNTAX						 1023
#define SQL_LOCKING_DURATION					 1024
#define SQL_RECORD_OPERATIONS					 1025
#define SQL_QUALIFIER_SYNTAX					 1026


/* Function for acquiring the xa_switch structure defined by Informix RM */

struct xa_switch_t * _fninfx_xa_switch( void );


/* Function for obtaining the Environment handle associated with an XA 
Connection */

RETCODE IFMX_SQLGetXaHenv(int, HENV *);


/*Function for obtaining the Database handle associated with an XA Connection */

RETCODE IFMX_SQLGetXaHdbc(int, HDBC *);

#ifdef __cplusplus
}                                    /* End of extern "C" { */
#endif  /* __cplusplus */

#endif /* __INFXCLI_H */
