/* COPYRIGHT (C) GUPTA TECHNOLOGIES, LLC 1984-2004 */
/*
INTERFACE TO
  SQL
*/
/*
REVISION HISTORY
  12/04/91 GTI release 5.0.0
  12/08/91 GTI release 5.0.1
  04/01/92 GTI release 5.0.2
  08/28/92 GTI release 5.1.0
  11/02/92 GTI release 5.1.1
  02/12/93 GTI release 5.1.2
  04/02/93 GTI release 5.2.0
  05/03/93 GTI release 5.2.0
  06/15/93 GTI release (null)
  06/15/93 GTI release 5.2.A
  06/30/93 GTI release 5.1.3
  08/04/93 GTI release 5.1.4
  01/19/94 GTI release 5.2.C
  03/22/94 GTI release 5.2.0
  04/18/93 GTI release 6.0.A
  01/14/94 GTI release 6.0.B
  05/11/94 GTI release 6.0.C
  09/27/94 GTI release 6.0.0
  04/12/95 GTI release 6.0.1
  08/02/95 GTI release 6.1.0
  11/14/96 GTI release 6.5.0
  10/20/97 GTI release 7.0.0
  10/23/98 GTI release 7.5.0
  11/10/00 GTI release 7.6.0
  08/22/01 RWS release 8.0.0
  10/22/02 GTI release 8.1.0
  12/20/02 GTI release 8.5.0
  01/18/05 RWS release 9.0.1
DESCRIPTION
  This file contains structure	definitions  and  defined  constants  used  to
  interface with SQLBASE.  For a more  complete  definition  see  "SQLBASE:  C
  Programmer's Guide".
*/

#ifndef SQL
#define SQL

#ifdef __cplusplus
extern "C" {		/* Assume C declarations for C++ */
#endif	/* __cplusplus */

#ifndef SBSTDCALL
#ifdef __GNUC__
	#define SBSTDCALL  /* stdcall */
#else
	#define SBSTDCALL __stdcall
#endif
#endif

/* VERSION NUMBER */
#define   SQLVERS   90001		/* version number */
#define   SQLAPIV   00102		/* API version number since 3.6 */
/* DEFINED CONSTANTS */

/*-------------------------------------------------------------------------
  For 32-bit target systems, such as NetWare 386, Windows/NT, and OS/2 2.x,
  redefine some 16-bit-oriented constructs, such as far, near, pascal, etc.
  These redefinitions assume the compiler being used supports the flat
  memory model.
  ------------------------------------------------------------------------- */

# undef far
# undef near
# undef pascal
# undef cdecl

# define far
# define near
# define pascal
# define cdecl
# ifndef CDECL
#   define CDECL __cdecl
# endif
# define PTR *

/* FETCH RETURN CODES */
#define   FETRTRU   1			/* data truncated */
#define   FETRSIN   2			/* signed number fetched */
#define   FETRDNN   3			/* data is not numeric */
#define   FETRNOF   4			/* numeric overflow */
#define   FETRDTN   5			/* data type not supported */
#define   FETRDND   6			/* data is not in date format */
#define   FETRNUL   7			/* data is null */
/* DATABASE DATA TYPES */
#define   SQLDCHR   1			/* character data type */
#define   SQLDNUM   2			/* numeric data type */
#define   SQLDDAT   3			/* date-time data type */
#define   SQLDLON   4			/* long data type */
#define   SQLDDTE   5			/* date (only) data type */
#define   SQLDTIM   6			/* time (only) data type */
#define   SQLDHDL   7			/* sql handle data type */
#define   SQLDBOO   8			/* boolean data type */
#define   SQLDDTM   8			/* maximum data type */
/* PROGRAM DATA TYPES */
#define   SQLPBUF   1			/* buffer */
#define   SQLPSTR   2			/* string (zero terminated) */
#define   SQLPUCH   3			/* unsigned char */
#define   SQLPSCH   4			/* char */
#define   SQLPUIN   5			/* unsigned int */
#define   SQLPSIN   6			/* int */
#define   SQLPULO   7			/* unsigned long */
#define   SQLPSLO   8			/* long */
#define   SQLPFLT   9			/* float */
#define   SQLPDOU   10			/* double */
#define   SQLPNUM   11			/* SQLBASE internal numeric format */
#define   SQLPDAT   12			/* SQLBASE internal datetime format */
#define   SQLPUPD   13			/* unsigned packed decimal */
#define   SQLPSPD   14			/* signed packed decimal */
#define   SQLPDTE   15			/* date only format */
#define   SQLPTIM   16			/* time only format */
#define   SQLPUSH   17			/* unsigned short */
#define   SQLPSSH   18			/* short */
#define   SQLPNST   19			/* numeric string */
#define   SQLPNBU   20			/* numeric buffer */
#define   SQLPEBC   21			/* EBCDIC buffer format */
#define   SQLPLON   22			/* long text string */
#define   SQLPLBI   23			/* long binary buffer */
#define   SQLPLVR   24			/* char\long varchar > 254 */
#define   SQLPULL   25			/* unsigned long long */
#define   SQLPSLL   26			/* long long */
#define   SQLPDTM   26			/* data type maximum */
/* EXTERNAL DATA TYPES */
#define   SQLEINT   1			/* INTEGER */
#define   SQLESMA   2			/* SMALLINT */
#define   SQLEFLO   3			/* FLOAT */
#define   SQLECHR   4			/* CHAR */
#define   SQLEVAR   5			/* VARCHAR */
#define   SQLELON   6			/* LONGVAR */
#define   SQLEDEC   7			/* DECIMAL */
#define   SQLEDAT   8			/* DATE */
#define   SQLETIM   9			/* TIME */
#define   SQLETMS   10			/* TIMESTAMP */
#define   SQLEMON   11			/* MONEY */
#define   SQLEDOU   12			/* DOUBLE */
#define   SQLEGPH   13			/* GRAPHIC */
#define   SQLEVGP   14			/* VAR GRAPHIC */
#define   SQLELGP   15			/* LONG VAR GRAPHIC */
#define   SQLEBIN   16			/* BINARY */
#define   SQLEVBI   17			/* VAR BINARY */
#define   SQLELBI   18			/* LONG BINARY */
#define   SQLEBOO   19			/* BOOLEAN */
#define   SQLELCH   20			/* CHAR > 254 */
#define   SQLELVR   21			/* VARCHAR > 254 */

/* SET and GET PARAMETER TYPES */
/*	Global parameters
	------------------ */
#define   SQLPDDB   1			/* default database name */
#define   SQLPDUS   2			/* default user name */
#define   SQLPDPW   3			/* default password */
#define   SQLPGBC   4			/* global cursor value */
#define   SQLPLRD   5			/* local result set directory */
#define   SQLPDBM   6			/* db mode - see below */
#define   SQLPDBD   7			/* dbdir */
#define   SQLPCPG   8			/* code page information */
#define   SQLPNIE   9			/* null indicator error */
#define   SQLPCPT   10			/* connect pass thru to backend */
#define   SQLPTPD  11			/* temp dir */
#define   SQLPDTR  12			/* distributed transaction mode */
#define   SQLPPSW  15			/* server password */
#define   SQLPOOJ  16			/* oracle outer join */
#define   SQLPNPF  17			/* net prefix */
#define   SQLPNLG  18			/* net log */
#define   SQLPNCT  19			/* net check type */
#define   SQLPNCK  20			/* net check */
#define   SQLPLCK  22			/* locks */
#define   SQLPINT  25			/* interrupt */
#define   SQLPERF  27			/*  error file */
#define   SQLPDIO  28			/*  direct I/O */
#define   SQLPSWR  29			/*  default write */
#define   SQLPCTY  31			/*  country */
#define   SQLPCSD  32			/*  commit server daemon */
#define   SQLPCSR  33			/*  commit server */
#define   SQLPCCK  36			/*  client check */
#define   SQLPCTS  37			/*  characterset */
#define   SQLPCGR  38			/*  cache group */
#define   SQLPAIO  39			/*  asyncio */
#define   SQLPANL  40			/*  apply net log */
#define   SQLPGRS  41			/*  get reentracy state */
#define   SQLPSTF  42			/*  set SQLTrace flags */
#define   SQLPCLG  43			/*  set commit-order logging */

/*	Server specific parameters
	-------------------------- */

#define   SQLPHEP   1001		/* HEAP size for TSR executables */
#define   SQLPCAC   1002		/* CACHE size in Kbytes */
#define   SQLPBRN   1003		/* brand of database */
#define   SQLPVER   1004		/* release version (ex. "4.0.J") */
#define   SQLPPRF   1005		/* server profiling */
#define   SQLPPDB   1006		/* partitioned database */
#define   SQLPGCM   1007		/* group commit count */
#define   SQLPGCD   1008		/* group commit delay ticks */
#define   SQLPDLK   1009		/* number of deadlocks */
#define   SQLPCTL   1010		/* command time limit */
#define   SQLPAPT   1011		/* process timer activated */
#define   SQLPOSR   1012		/* OS sample rate */
#define   SQLPAWS   1013		/* OS Averaging window size */
#define   SQLPWKL   1014		/* Work Limit */
#define   SQLPWKA   1015		/* Work Space allocation */
#define   SQLPUSR   1016		/* Number of users */
#define   SQLPTMO   1017		/* time out */
#define   SQLPTSS   1018		/* thread stack size */
#define   SQLPTHM   1019		/* thread mode */
#define   SQLPSTC   1020		/* sortcache size in kilobytes */
#define   SQLPSIL   1021		/* silent mode */
#define   SQLPSPF   1022		/* server prefix */
#define   SQLPSVN   1024		/* server name */
#define   SQLPROM   1025		/* read-only mode (0 or 1) */
#define   SQLPSTA   1026		/* enable stats gathering */
#define   SQLPCSV   1027		/* commit server */
#define   SQLPTTP   1028		/* trace for 2PC */
#define   SQLPAJS   1029		/* ANSI join syntax */
#define   SQLPSDIR  1030		/* server module directory */
#define   SQLPSINI  1031		/* server ini file full path name */
#define   SQLPBLD   1032		/* server build number	*/

/*	Database specific parameters
	---------------------------- */

#define   SQLPDBN   2001		/* database name */
#define   SQLPDDR   2002		/* database directory */
#define   SQLPLDR   2003		/* log directory */
#define   SQLPLFS   2004		/* log file size in Kbytes */
#define   SQLPCTI   2005		/* checkpoint time interval in mins */
#define   SQLPLBM   2006		/* log backup mode? (0 or 1) */
#define   SQLPPLF   2007		/* Pre-allocate log files? (0 or 1) */
#define   SQLPTSL   2008		/* transaction span limit */
#define   SQLPROT   2009		/* read-only transactions (0, 1, 2) */
#define   SQLPHFS   2010		/* history file size in Kbytes */
#define   SQLPREC   2011		/* recovery */
#define   SQLPEXE   2012		/* name of executable */
#define   SQLPNLB   2013		/* next log to backup */
#define   SQLPROD   2014		/* read-only database (0 or 1) */
#define   SQLPEXS   2015		/* database file extension size */
#define   SQLPPAR   2016		/* partitioned database (0 or 1) */
#define   SQLPNDB   2017		/* NEWDB */
#define   SQLPLGF   2018		/* log file offset */
#define   SQLPDTL   2019		/* command timelimit */
#define   SQLPSMN   2020		/* show main db */
#define   SQLPCVC   2021		/* catalog version counter */
#define   SQLPDBS   2022		/* database block size */
#define   SQLPUED   2023		/* update external dictionary */
#define   SQLPCINI  2024		/* client ini file full path name */

/*	Cursor specific parameters
	-------------------------- */

#define   SQLPISO   3001		/* isolation level (SQLILRR etc..) */
#define   SQLPWTO   3002		/* lock wait timeout in seconds */
#define   SQLPPCX   3003		/* preserve context (0 or 1) */
#define   SQLPFRS   3004		/* front end result sets */
#define   SQLPLDV   3005		/* load version (ex. "3.6.22") */
#define   SQLPAUT   3006		/* autocommit */
#define   SQLPRTO   3007		/* rollback trans on lock timeout */
#define   SQLPSCR   3008		/* scroll mode (0 or 1) */
#define   SQLPRES   3009		/* restriction mode (0 or 1) */
#define   SQLPFT    3010		/* fetch through */
#define   SQLPNPB   3011		/* no pre-build in RL mode */
#define   SQLPPWD   3012		/* current password */
#define   SQLPDB2   3013		/* DB2 compatibility mode */
#define   SQLPREF   3014		/* referential integrity checking */
#define   SQLPBLK   3015		/* bulk-execute mode */
#define   SQLPOBL   3016		/* optimized bulk-execute mode */
#define   SQLPLFF   3017		/* LONG data allowed in FERS */
#define   SQLPDIS   3018		/* When to return Describe info */
#define   SQLPCMP   3019		/* Compress messages sent to server */
#define   SQLPCHS   3020		/* chained cmd has SELECT (0 or 1) */
#define   SQLPOPL   3021		/* optimizer level */
#define   SQLPRID   3022		/* ROWID */
#define   SQLPEMT   3023		/* Error Message Tokens */
#define   SQLPCLN   3024		/* client name */
#define   SQLPLSS   3025		/* last compiled SQL statement */
#define   SQLPEXP   3026		/* explain query plan */
#define   SQLPCXP   3027		/* cost of execution plan */
#define   SQLPOCL   3028		/* optimizercostlevel */
#define   SQLPTST   3029		 /* distributed transaction status */
#define   SQLP2PP   3030		/* 2-phase protocol (SQL2STD, etc.) */
/*	defined for Load/Unload parsed parameters - cursor specific */
#define   SQLPCLI   3031		/* ON CLIENT option */
#define   SQLPFNM   3032		/* load/unload file name */
#define   SQLPOVR   3033		/* file OVERWRITE flag */
#define   SQLPTFN   3034		/* A Temporary file name */
#define   SQLPTRC   3035		/* Trace stored procedures */
#define   SQLPTRF   3036		/* Tracefile for stored procedures */
#define   SQLPCTF   3037		/* control file flag */

#define   SQLPMID   3038		/* mail id */
#define   SQLPAID   3039		/* adapter id */
#define   SQLPNID   3040		/* network id */
#define   SQLPUID   3041		/* user application id */
#define   SQLPCIS   3042		/* client identification strings */
#define   SQLPIMB   3043		/* input message buffer size */
#define   SQLPOMB   3044		/* output message buffer size */
#define   SQLPWFC   3045		/* which fetchable command */
#define   SQLPRFE   3046		/* Return on First Error-bulk insert */
#define   SQLPCUN   3047		/* Current cursor user name */
#define   SQLPOFF   3048		/* Optimize First Fetch */
#define   SQLPUSC   3049		/* Use Specified Cursor for ref */
#define   SQLPPDG   3050		/* Plan Debug		*/

/*   Application Specific ie. applicable to all cursors that
     belong to the same application  (3700 -  3799) */

#define   SQLPCCB   3700		/* Connect Closure Behaviour */
#define   SQLPTTV   3701		/* Thread Timeout Value */

/*	Static attributes
	-------------------------- */

#define   SQLPFAT   4000		/* first attribute */
#define   SQLPBRS   4001		/* back end result sets */
#define   SQLPMUL   4002		/* multi-user */
#define   SQLPDMO   4003		/* demonstration version */
#define   SQLPLOC   4004		/* local version of database */
#define   SQLPFPT   4005		/* 1st participant */
#define   SQLPLAT   4006		/* last attribute */
#define   SQLPCAP   4007		/* API capability level */
#define   SQLPSCL   4008		/* server capability level */
#define   SQLPRUN   4009		/* runtime version */

/*	Server specific parameters
	---------------------------- */

#define   SQLPPLV   5001		/* print level */
#define   SQLPALG   5002		/* activity log */
#define   SQLPTMS   5003		/* time stamp */
#define   SQLPPTH   5004		/* path name seperator */
#define   SQLPTMZ   5005		/* time zone */
#define   SQLPTCO   5006		/* time colon only */

/*	SQL Server & Router/Gateway specific parameters
	------------------------------ */
#define   SQLPESTR  5101		/* get server error # and string */
#define   SQLPMSTR  5102		/* get server msg# and string */
#define   SQLPMAPC  5103		/* MapGTICursors */
#define   SQLPUPRE  5104		/* get user prefix */
#define   SQLPORID  5105		/* Oracle RowID */
#define   SQLPERRM  5106		/* error mapping */
#define   SQLPRTS   5107		/* SQL Server - Return Status */
#define   SQLPSAUT  5108		/* SQL Server - Autocommit */
#define   SQLPROW   5109		/* SQL Server - Command Type */
#define   SQLPEHC   5110		/* SQL Server - Enhanced Cursors */
#define   SQLPGFS   5111		/* SQL Server - Get Fetch Status */
#define   SQLPLBUF  5112		/* Longbuffer setting */
#define   SQLPDPH   5113		/* SQL Server - DBProcess handle */
#define   SQLPCKE   5114		/* SQL Server - CHECK EXISTS */
#define   SQLPWTX   5115		/* SQL Server - DBWRITETEXT */
#define   SQLPYLD   5116		/* SQL Server - YieldOnServerCall */
#define   SQLPOBN   5117		/* ODBC Router - backend brand */
#define   SQLPOBV   5118		/* ODBC Router - backend version */
#define   SQLPODN   5119		/* ODBC Router - driver name */
#define   SQLPODV   5120		/* ODBC Router - driver version */
#define   SQLPOCV   5121		/* ODBC Router - ODBC version */
#define   SQLPRSYS  5122  /* DRDA - EXEC SQL CONNECT TO remote system name */
#define   SQLPLAB   5123  /* DB2 - return label information if exists */
#define   SQLPCID   5124  /* DB2 - Set Current SQLID default */
#define   SQLPNUMST 5125		/* AS/400 Number of Statements */
#define   SQLPBNDRW 5126		/* Oracle- bind SQLPBUF as RAW */
#define   SQLPNLS   5127		/* Informix - NLS database */

#define   SQLPFRW   5200		/* fetchrow */
#define   SQLPBRW   5201		/* buffrow */

/* Sybase System 10 parameters (reserved 5220 - 5250)
   ------------------------------------------------------------ */

#define   SQLPNESTR 5220       /* SYB - get next error from client */
#define   SQLPNMSTR 5221       /* SYB - get next error from server */
#define   SQLPCESTR 5222       /* SYB - get client message count */
#define   SQLPCMSTR 5223       /* SYB - get server message count */
#define   SQLPTXT   5224       /* SYB - allow bind for text, image */
#define   SQLPEMC   5225       /* SYB - enable multiple connections */

/*	ODBC specific parameters - Refer to ODBC spec for definition
	------------------------------------------------------------ */

#define   SQLP_ACTIVE_CONNECTIONS	  5500
#define   SQLP_ACTIVE_STATEMENTS	  5501
#define   SQLP_DATA_SOURCE_NAME 	  5502
#define   SQLP_DRIVER_HDBC		  5503
#define   SQLP_DRIVER_HENV		  5504
#define   SQLP_DRIVER_HSTMT		  5505
#define   SQLP_DRIVER_NAME		  5506
#define   SQLP_DRIVER_VER		  5507
#define   SQLP_FETCH_DIRECTION		  5508
#define   SQLP_ODBC_API_CONFORMANCE	  5509
#define   SQLP_ODBC_VER 		  5510
#define   SQLP_ROW_UPDATES		  5511
#define   SQLP_ODBC_SAG_CLI_CONFORMANCE   5512
#define   SQLP_SERVER_NAME		  5513
#define   SQLP_SEARCH_PATTERN_ESCAPE	  5514
#define   SQLP_ODBC_SQL_CONFORMANCE	  5515
#define   SQLP_DATABASE_NAME		  5516
#define   SQLP_DBMS_NAME		  5517
#define   SQLP_DBMS_VER 		  5518
#define   SQLP_ACCESSIBLE_TABLES	  5519
#define   SQLP_ACCESSIBLE_PROCEDURES	  5520
#define   SQLP_PROCEDURES		  5521
#define   SQLP_CONCAT_NULL_BEHAVIOUR	  5522
#define   SQLP_CURSOR_COMMIT_BEHAVIOUR	  5523
#define   SQLP_CURSOR_ROLLBACK_BEHAVIOUR  5524
#define   SQLP_DATA_SOURCE_READ_ONLY	  5525
#define   SQLP_DEFAULT_TXN_ISOLATION	  5526
#define   SQLP_EXPRESSIONS_IN_ORDERBY	  5527
#define   SQLP_IDENTIFIER_CASE		  5528
#define   SQLP_IDENTIFIER_QUOTE_CHAR	  5529
#define   SQLP_MAX_COLUMN_NAME_LEN	  5530
#define   SQLP_MAX_CURSOR_NAME_LEN	  5531
#define   SQLP_MAX_OWNER_NAME_LEN	  5532
#define   SQLP_MAX_PROCEDURE_NAME_LEN	  5533
#define   SQLP_MAX_QUALIFIER_NAME_LEN	  5534
#define   SQLP_MAX_TABLE_NAME_LEN	  5535
#define   SQLP_MULT_RESULT_SETS 	  5536
#define   SQLP_MULTIPLE_ACTIVE_TXN	  5537
#define   SQLP_OUTER_JOINS		  5538
#define   SQLP_OWNER_TERM		  5539
#define   SQLP_PROCEDURE_TERM		  5540
#define   SQLP_QUALIFIER_NAME_SEPARATOR   5541
#define   SQLP_QUALIFIER_TERM		  5542
#define   SQLP_SCROLL_CONCURRENCY	  5543
#define   SQLP_SCROLL_OPTIONS		  5544
#define   SQLP_TABLE_TERM		  5545
#define   SQLP_TXN_CAPABLE		  5546
#define   SQLP_USER_NAME		  5547
#define   SQLP_CONVERT_FUNCTIONS	  5548
#define   SQLP_NUMERIC_FUNCTIONS	  5549
#define   SQLP_STRING_FUNCTIONS 	  5550
#define   SQLP_SYSTEM_FUNCTIONS 	  5551
#define   SQLP_TIMEDATE_FUNCTIONS	  5552
#define   SQLP_CONVERT_BIGINT		  5553
#define   SQLP_CONVERT_BINARY		  5554
#define   SQLP_CONVERT_BIT		  5555
#define   SQLP_CONVERT_CHAR		  5556
#define   SQLP_CONVERT_DATE		  5557
#define   SQLP_CONVERT_DECIMAL		  5558
#define   SQLP_CONVERT_DOUBLE		  5559
#define   SQLP_CONVERT_FLOAT		  5560
#define   SQLP_CONVERT_INTEGER		  5561
#define   SQLP_CONVERT_LONGVARCHAR	  5562
#define   SQLP_CONVERT_NUMERIC		  5563
#define   SQLP_CONVERT_REAL		  5564
#define   SQLP_CONVERT_SMALLINT 	  5565
#define   SQLP_CONVERT_TIME		  5566
#define   SQLP_CONVERT_TIMESTAMP	  5567
#define   SQLP_CONVERT_TINYINT		  5568
#define   SQLP_CONVERT_VARBINARY	  5569
#define   SQLP_CONVERT_VARCHAR		  5570
#define   SQLP_CONVERT_LONGVARBINARY	  5571
#define   SQLP_TXN_ISOLATION_OPTION	  5572
#define   SQLP_ODBC_SQL_OPT_IEF 	  5573

/*** ODBC SDK 1.0 Additions ***/
#define   SQLP_CORRELATION_NAME 	  5574
#define   SQLP_NON_NULLABLE_COLUMNS	  5575

/*** ODBC SDK 2.0 Additions ***/
#define   SQLP_DRIVER_HLIB		  5576
#define   SQLP_DRIVER_ODBC_VER		  5577
#define   SQLP_LOCK_TYPES		  5578
#define   SQLP_POS_OPERATIONS		  5579
#define   SQLP_POSITIONED_STATEMENTS	  5580
#define   SQLP_GETDATA_EXTENSIONS	  5581
#define   SQLP_BOOKMARK_PERSISTENCE	  5582
#define   SQLP_STATIC_SENSITIVITY	  5583
#define   SQLP_FILE_USAGE		  5584
#define   SQLP_NULL_COLLATION		  5585
#define   SQLP_ALTER_TABLE		  5586
#define   SQLP_COLUMN_ALIAS		  5587
#define   SQLP_GROUP_BY 		  5588
#define   SQLP_KEYWORDS 		  5589
#define   SQLP_ORDER_BY_COLUMNS_IN_SELECT 5590
#define   SQLP_OWNER_USAGE		  5591
#define   SQLP_QUALIFIER_USAGE		  5592
#define   SQLP_QUOTED_IDENTIFIER_CASE	  5593
#define   SQLP_SPECIAL_CHARACTERS	  5594
#define   SQLP_SUBQUERIES		  5595
#define   SQLP_UNION			  5596
#define   SQLP_MAX_COLUMNS_IN_GROUP_BY	  5597
#define   SQLP_MAX_COLUMNS_IN_INDEX	  5598
#define   SQLP_MAX_COLUMNS_IN_ORDER_BY	  5599
#define   SQLP_MAX_COLUMNS_IN_SELECT	  5600
#define   SQLP_MAX_COLUMNS_IN_TABLE	  5601
#define   SQLP_MAX_INDEX_SIZE		  5602
#define   SQLP_MAX_ROW_SIZE_INCLUDES_LONG 5603
#define   SQLP_MAX_ROW_SIZE		  5604
#define   SQLP_MAX_STATEMENT_LEN	  5605
#define   SQLP_MAX_TABLES_IN_SELECT	  5606
#define   SQLP_MAX_USER_NAME_LEN	  5607
#define   SQLP_MAX_CHAR_LITERAL_LEN	  5608
#define   SQLP_TIMEDATE_ADD_INTERVALS	  5609
#define   SQLP_TIMEDATE_DIFF_INTERVALS	  5610
#define   SQLP_NEED_LONG_DATA_LEN	  5611
#define   SQLP_MAX_BINARY_LITERAL_LEN	  5612
#define   SQLP_LIKE_ESCAPE_CLAUSE	  5613
#define   SQLP_QUALIFIER_LOCATION	  5614

#define   SQLP_GET_TYPE_INFO		  5699


/*	The following parmeters in the range 6000 - 7000 are reserved for
	SQLBase INTERNAL use.
*/

#define  SQLP000    6000		/* for internal use only */
#define  SQLP999    6999		/* for internal use only */
#define  SQLPITP  0x4000		/* INTERNAL USE ONLY */
#define  SQLPITC  0x8000		/* INTERNAL USE ONLY */

/* end of SET and GET PARAMETER TYPES */


/* defines for ON, OFF, DEFAULT parameter values */

#define   SQLVOFF    0			/* parameter should be OFF */
#define   SQLVON     1			/* parameter should be ON */
#define   SQLVDFL    2			/* parameter should default */

/* defines for SQLPDBM (db mode) */

#define   SQLMDBL    1			/* DB Local */
#define   SQLMRTR    2			/* DB Router */
#define   SQLMCOM    3			/* DB Combo */

/* defines for database brands */

#define   SQLBSQB    1			/* SQLBASE */
#define   SQLBDB2    2			/* DB2 */
#define   SQLBDBM    3			/* IBM OS/2 Database Manager */
#define   SQLBORA    4			/* Oracle */
#define   SQLBIGW    5			/* Informix */
#define   SQLBNTW    6			/* Netware SQL */
#define   SQLBAS4    7			/* IBM AS/400 SQL/400 */
#define   SQLBSYB    8			/* Sybase SQL Server */
#define   SQLBDBC    9			/* Teradata DBC Machines */
#define   SQLBALB   10			/* HP Allbase */
#define   SQLBRDB   11			/* DEC's RDB */
#define   SQLBTDM   12			/* Tandem's Nonstop SQL */
#define   SQLBSDS   13			/* IBM SQL/DS */
#define   SQLBSES   14			/* SNI SESAM */
#define   SQLBING   15			/* Ingres */
#define   SQLBSQL   16			/* SQL Access */
#define   SQLBDBA   17			/* DBase */
#define   SQLBDB4   18			/* SNI DDB4 */
#define   SQLBFUJ   19			/* Fujitsu RDBII */
#define   SQLBSUP   20			/* Cincom SUPRA */
#define   SQLB204   21			/* CCA Model 204 */
#define   SQLBDAL   22			/* Apple DAL interface */
#define   SQLBSHR   23			/* Teradata ShareBase */
#define   SQLBIOL   24			/* Informix On-Line */
#define   SQLBEDA   25			/* EDA/SQL */
#define   SQLBUDS   26			/* SNI UDS */
#define   SQLBMIM   27			/* Nocom Mimer */
#define   SQLBOR7   28			/* Oracle version 7 */
#define   SQLBIOS   29			/* Ingres OpenSQL */
#define   SQLBIOD   30			/* Ingres OpenSQL with date support */
#define   SQLBODB   31			/* ODBC Router */
#define   SQLBS10   32			/* SYBASE System 10 */
#define   SQLBSE6   33			/* Informix SE version 6 */
#define   SQLBOL6   34			/* Informix On-Line version 6 */
#define   SQLBNSE   35			/* Informix SE NLS version 6 */
#define   SQLBNOL   36			/* Informix On-Line NLS version 6 */
#define   SQLBSE7   37			/* Informix SE version 7 */
#define   SQLBOL7   38			/* Informix On-Line version 7 */
#define   SQLBETA   39			/* Entire Access, ADABAS */
#define   SQLBI12   40			/* Ingres CA-OpenIngres 1.2 */
#define   SQLBAPP   99			/* SQLHost App Services */

/* SIZES */
#define   SQLSNUM   12			/* numeric program buffer size */
#define   SQLSDAT   12   		/* date-time program buffer size */
#define   SQLSCDA   26			/* character date-time size */
#define   SQLSDTE   SQLSDAT		/* date (only) program buffer size */
#define   SQLSCDE   10			/* character date (only) size */
#define   SQLSRID   40			/* size of ROWID */
#define   SQLSTIM   SQLSDAT		/* time (only) program buffer size */
#define   SQLSCTI   15			/* character time (only) size */
#define   SQLSFEM   100L		/* file extension size (multi-user) */
#define   SQLSFES   20L 		/* file extension size (single-user) */
#define   SQLSTEX   5L			/* table extent size */


/* Two-phase Commit Protocols */

typedef   int		  SQLT2PP;		/* type: 2-phase commit protocol */

#define   SQL2MIN   SQL2STD		/* minimum protocol value */

#define   SQL2STD   ((SQLT2PP) 1)	/* standard 2pc protocol */
#define   SQL2PRA   ((SQLT2PP) 2)	/* presumed-abort 2pc protocol */
#define   SQL2PRC   ((SQLT2PP) 3)	/* presumed-commit 2pc protocol */
#define   SQL2DEF   SQL2STD		/* default is standard */

#define   SQL2MAX   SQL2PRC		/* maximum protocol value */

/* Two-phase Commit Votes */

typedef   int		  SQLT2PV;	/* type: 2-phase commit vote */

#define   SQLVMIN   SQLVCMT		/* minimum vote value */

#define   SQLVCMT   ((SQLT2PV) 1)	/* Vote Commit */
#define   SQLVRBK   ((SQLT2PV) 2)	/* Vote Rollback */
#define   SQLVRO    ((SQLT2PV) 3)	/* Vote ReadOnly */

#define   SQLVMAX   SQLVRO		/* maximum vote value */

/* defines for distributed transaction status */

typedef   int	  SQLTTST;		/* distributed transaction state */

#define   SQLSCMT  ((SQLTTST) 1)	/* transaction state = COMMITted */
#define   SQLSRBK  ((SQLTTST) 2)	/* transaction state = ROLLBACKed */
#define   SQLSUNK  ((SQLTTST) 3)	/* transaction state = UNKNOWN */


/* NULL POINTER */
#define   SQLNPTR   (ubyte1 PTR)0	/* null pointer */
/* RESULT COMMAND TYPES */
#define   SQLTSEL   1			/* select */
#define   SQLTINS   2			/* insert */
#define   SQLTCTB   3			/* create table */
#define   SQLTUPD   4			/* update */
#define   SQLTDEL   5			/* delete */
#define   SQLTCIN   6			/* create index */
#define   SQLTDIN   7			/* drop index */
#define   SQLTDTB   8			/* drop table */
#define   SQLTCMT   9			/* commit */
#define   SQLTRBK   10			/* rollback */
#define   SQLTACO   11			/* add column */
#define   SQLTDCO   12			/* drop column */
#define   SQLTRTB   13			/* rename table */
#define   SQLTRCO   14			/* rename column */
#define   SQLTMCO   15			/* modify column */
#define   SQLTGRP   16			/* grant privilege on table */
#define   SQLTGRD   17			/* grant dba */
#define   SQLTGRC   18			/* grant connect */
#define   SQLTGRR   19			/* grant resource */
#define   SQLTREP   20			/* revoke privilege on table */
#define   SQLTRED   21			/* revoke dba */
#define   SQLTREC   22			/* revoke connect */
#define   SQLTRER   23			/* revoke resource */
#define   SQLTCOM   24			/* comment on */
#define   SQLTWAI   25			/* wait */
#define   SQLTPOS   26			/* post */
#define   SQLTCSY   27			/* create synonym */
#define   SQLTDSY   28			/* drop synonym */
#define   SQLTCVW   29			/* create view */
#define   SQLTDVW   30			/* drop view */
#define   SQLTRCT   31			/* row count */
#define   SQLTAPW   32			/* alter password */
#define   SQLTLAB   33			/* label on */
#define   SQLTCHN   34			/* chained command */
#define   SQLTRPT   35			/* repair table */
#define   SQLTSVP   36			/* savepoint */
#define   SQLTRBS   37			/* rollback to savepoint */
#define   SQLTUDS   38			/* update statistics */
#define   SQLTCDB   39			/* check database */
#define   SQLTFRN   40			/* foreign DBMS commands */
#define   SQLTAPK   41			/* add primary key */
#define   SQLTAFK   42			/* add foreign key */
#define   SQLTDPK   43			/* drop primary key */
#define   SQLTDFK   44			/* drop foreign key */
/* SERVER COMMAND TYPES */
#define   SQLTCDA   45			/* create dbarea */
#define   SQLTADA   46			/* alter  dbarea */
#define   SQLTDDA   47			/* delete dbarea */
#define   SQLTCSG   48			/* create stogroup */
#define   SQLTASG   49			/* alter  stogroup */
#define   SQLTDSG   50			/* delete stogroup */
#define   SQLTCRD   51			/* create database */
#define   SQLTADB   52			/* alter  database */
#define   SQLTDDB   53			/* delete database */
#define   SQLTSDS   54			/* set default stogroup */
#define   SQLTIND   55			/* install database */
#define   SQLTDED   56			/* de-install database */
/* END OF SERVER COMMAND TYPES */

#define   SQLTARU   57			/* add RI user error */
#define   SQLTDRU   58			/* drop RI user error */
#define   SQLTMRU   59			/* modify RI user error */
#define   SQLTSCL   60			/* set client */
#define   SQLTCKT   61			/* check table */
#define   SQLTCKI   62			/* check index */
#define   SQLTOPL   63			/* PL/SQL Stored Procedure */
#define   SQLTBGT   64			/* BEGIN TRANSACTION */
#define   SQLTPRT   65			/* PREPARE TRANSACTION */
#define   SQLTCXN   66			/* COMMIT TRANSACTION */
#define   SQLTRXN   67			/* ROLLBACK TRANSACTION */
#define   SQLTENT   68			/* END TRANSACTION */

/* COMMIT SERVER COMMAND TYPES */
#define   SQLTCBT   69			/* begin transaction */
#define   SQLTCCT   70			/* commit transaction */
#define   SQLTCET   71			/* end	 transaction */
#define   SQLTCPT   72			/* prepare transaction */
#define   SQLTCRT   73			/* rollback transaction */
#define   SQLTCST   74			/* status transaction */
#define   SQLTCRX   75			/* reduce transaction */
#define   SQLTCSD   76			/* start daemon */
#define   SQLTCTD   77			/* stop daemon */
#define   SQLTCRA   78			/* resolve all transactions */
#define   SQLTCRO   79			/* resolve one transaction */
#define   SQLTCOT   80			/* orphan a transaction */
#define   SQLTCFL   81			/* CREATE FAILURE */
#define   SQLTDFL   82			/* DELETE FAILURE */
#define   SQLTSTN   83			/* SET TRACETWOPC ON */
#define   SQLTSTF   84			/* SET TRACETWOPC OFF */
#define   SQLTUNL   85			/* Unload command */
#define   SQLTLDP   86			/* load command */
#define   SQLTPRO   87			/* stored procedure */
#define   SQLTGEP   88			/* grant  execute privilege */
#define   SQLTREE   89			/* revoke execute privilege */
#define   SQLTTGC   90			/* create trigger */
#define   SQLTTGD   91			/* drop trigger */
#define   SQLTVNC   92			/* create event */
#define   SQLTVND   93			/* drop event */
#define   SQLTSTR   94			/* start audit */
#define   SQLTAUD   95			/* audit message */
#define   SQLTSTP   96			/* stop audit */
#define   SQLTACM   97			/* Alter CoMmand */
#define   SQLTXDL   98			/* lock database */
#define   SQLTXDU   99			/* unlock database */
#define   SQLTCEF  100			/* create external function */
#define   SQLTDEF  101			/* drop external function */
#define   SQLTDBT  102			/* DBATTRIBUTE */
#define   SQLTATG  103			/* ALTER TRIGGER */
#define   SQLTAEF  104			/* alter external function */
#define   SQLTADS  105			/* alter database security */
#define   SQLTAEK  106			/* alter exportkey */
#define   SQLTASP  107			/* alter server */

/* DEFAULTS */
#define   SQLDCGP   30			/* CACHEGROUP, cache page allocation group */
#define   SQLDCRT   5			/* CONNECTRETRY, seconds for connect timeout */
#define   SQLDCPT   10			/* CONNECTPAUSETICKS, ticks for pausing */
#define   SQLDDGH   10000l		/* HEAP, DBGATEWY heap size */
#define   SQLDDLH   145000l		/* HEAP, DBLOCAL heap size */
#define   SQLDDRH   20000l		/* HEAP, DBROUTER heap size */
#define   SQLDNTN   71			/* INTERRUPT, interrupt number */
#define   SQLDNBS   30000		/* NETBUFFER, size DBXROUTR network buffer */
#define   SQLDRET   3			/* RETRY, number of connect retries */
#define   SQLDSVS   0X8000		/* STACKSIZE, DBSERVER stack */
#define   SQLDSMS   0X8000		/* STACKSIZE, DBSIM stack */
#define   SQLDSRS   7000		/* STACKSIZE, DBSIM w/router stack */
#define   SQLDTMZ   0l			/* TIMEZONE */
#define   SQLDSVU   128 		/* USERS, DBSERVER users */
#define   SQLDSMU   3			/* USERS, DBSIM users */
#define   SQLDCLI   1024000L		/* checkpoint log interval in bytes */
#define   SQLDCTI   1			/* checkpt time interval in minutes */
#define   SQLDWSA   1000		/* cursor work space allocation */
#define   SQLDPRE   5			/* decimal precision */
#define   SQLDSCA   0			/* decimal scale */
#define   SQLDIML   2000		/* input message buffer length */
#define   SQLDPRI   10			/* integer precision */
#define   SQLDSUL   6			/* length of system user name */
#define   SQLDLBS   20480		/* log buffer size in bytes */
#define   SQLDLFS   1024000L		/* log file size in bytes */
#define   SQLDSLS   15			/* maximum # of large server stacks */
#define   SQLDHFS   1000		/* maximum history file size */
#define   SQLDLPM   20000		/* maximum number of rollback log pages */
#define   SQLDNES   100 		/* normal file extension size */
#define   SQLDOML   1000		/* output message buffer length */
#define   SQLDPES   1024		/* partitioned file extension size */
#define   SQLDPUB   "PUBLIC"		/* public user name */
#define   SQLDLPT   16000		/* rollback log page threshold */
#define   SQLDPRS   5			/* smallint precision */
#define   SQLDRTO   5			/* default response time out */
#define   SQLDSUN   "SYSADM"		/* system default username */
#define   SQLDESC   "$$"		/* connect escape sequence */
#define   SQLDSUN   "SYSADM"		/* system default username */
#define   SQLDNTG   16                  /* number of triggers allowed per event/time/scope */
                                        /* Maximum triggers for an event is * 4 */

/* MAXIMUM SIZES */
#define   SQLMSBNL  18			/* short bind name length */
#define   SQLMBNL   36			/* bind name length */
#define   SQLMLBNL  64			/* long bind name length */
#define   SQLMBSL   32000		/* max length Backend string literal */
#define   SQLMCG1   32767		/* cache group pages */
#define   SQLMCKF   16			/* concatenated key fields */
#define   SQLMCLL   255 		/* clientlimit */
#define   SQLMCLN   12			/* maximum client name size */
#define   SQLMCLP   128 		/* commmand line parameter length */
#define   SQLMCMT   106 		/* max command types */
#define   SQLMCNM   SQLMBNL		/* max referential constraint name */
#define   SQLMCOH   255 		/* column heading string */
#define   SQLMCST   400 		/* max connect string length */
#define   SQLMDBA   10			/* number of databases accessed */
#define   SQLMDFN   25			/* database file name */
#define   SQLMPSS   25			/* process status string */
#define   SQLMDMO   750 		/* maximum DB size for demos (Kbytes) */
#define   SQLMSDNM  8			/* database name CAM_1..4_VERSION */
#define   SQLMDNM   16			/* database name */
#define   SQLMDVL   254 		/* data value length */
#define   SQLMERR   255 		/* error message length */
#define   SQLMETX   3000		/* error text length */
/* ------------------------------------------------------------------------- *
 * We are reverting back to the previous value of 128 for SQLMFNL because it *
 * is resulting in unnecessary complications in the cdm, aic size etc.	     *
 * ------------------------------------------------------------------------- */
#define   SQLMFNL   128 	 /* filename length */

/* ------------------------------------------------------------------------- *
 * Note : We are defining a new constant called SQLRFNL which is used to     *
 * create a filename of restricted length (128) instead of SQLMFNL. It is    *
 * required because the areas table created by main.ini uses a pathname of   *
 * 128 bytes								     *
 * ------------------------------------------------------------------------- */
#define   SQLRFNL   128
#define   SQLMFQN   3			/* number of fields in fully qualified column name */
#define   SQLMFRD   40			/* maximum size of foreign result set directory */
#define   SQLMGCM   32767		/* maximum group commit count */
#define   SQLMICO   255 		/* installed cache page owners */
#define   SQLMICU   255 		/* installed cursors */
#define   SQLMIDB   255 		/* installed databases */
#define   SQLMILK   32767		/* installed locks */
#define   SQLMINL   2000		/* input length */
#define   SQLMIPG   1000000		/* installed pages */
#define   SQLMIPR   800 		/* installed processes */
#define   SQLMITR   800 		/* installed transactions */
#define   SQLMJTB   17			/* joined tables */
#define   SQLMTAC   17*16       /* tables each tab can have subsel */
#define   SQLMLID   32			/* long identifiers */
#define   SQLMNBF   60000		/* network buffer size */
#define   SQLMNCO   254 		/* number of columns per row */
#define   SQLMUCO   253 		/* number of user columns available */
#define   SQLMNPF   3			/* NETPREFIX size */
#define   SQLMOUL   1000		/* output length */
#define   SQLMPAL   255 		/* max path string length */
#define   SQLMSVP   1024 		/* max serverpath (inside ini client section)  string length */
#define   SQLMPFS   21			/* max platform string length */
#define   SQLMPKL   254 		/* max primary key length */
#define   SQLMPRE   15			/* maximum decimal precision */
#define   SQLMPTL   4			/* maximum print level */
#define   SQLMPWD   128 		/* maximum password length */
#define   SQLMCTL   43200		/* max query timelimit (12 hours) */
#define   SQLMRBB   8192		/* maximum rollback log buffer */
#define   SQLMRCB   20480		/* maximum recovery log buffer */
#define   SQLMRET   1000		/* retry count */
#define   SQLMRFH   4			/* maximum # remote file handles */
#define   SQLMROB   8192		/* max size of restore output buffer */
#define   SQLMSES   16			/* number of sessions */
#define   SQLMSID   8			/* short identifiers */
#define   SQLMSLI   255 		/* max number of select list exprs. */
#define   SQLMSNM   8			/* server name */
#define   SQLMSRL   32			/* max length of SQL reserved word */
#define   SQLMSVN   199 		/* maximum server names */
#define   SQLMTFS   10			/* maximum temporary file size */
#define   SQLMTMO   200 		/* maximum timeout */
#define   SQLMTSS   256 		/* text string space size */
#define   SQLMUSR   128 		/* maximum username length */
#define   SQLMVER   8			/* max version string (nn.nn.nn) */
#define   SQLMXER   255 		/* Extended error message length */
#define   SQLMXFS   2147483648UL	/* max file size in bytes */
#define   SQLMXLF   2097152		/* max log file size in KB */
#define   SQLMTHM   2			/* maximum thread mode value */
#define   SQLMPKL   254 		/* max primary key length */
#define   SQLMGTI   250 		/* max global transaction-id length */
#define   SQLMAUF   32			/* max concurrent audit files */
#define   SQLMPNM   8			/* max protocol name length */
#define   SQLMOSR   255 		/* max OS sample rate */
#define   SQLMAWS   255 		/* max OS Averaging window size */
#define   SQLMMID   32			/* max length identificatin strings */

/* MINIMUMS */
#define   SQLMCG0   1			/* cache group pages */
#define   SQLMEXS   1024		/* partitioned file extension size */
#define   SQLMLFE   100000L		/* minimum log file extension size */
#define   SQLMLFS   100000L		/* minimum log file size */
#define   SQLMPAG   15			/* minimum pages (cache) */
#define   SQLMITM   1			/* minimum thread mode value */

/* typedefs */
#if defined(WIN32) || defined(SYSFWNT)
    #ifndef SQLFI64
    #define SQLFI64
    typedef   __int64        b8;		/* 64-bits signed */
    typedef   unsigned __int64 ub8;		/* 64-bits unsigned */
    #endif

#else
    #ifndef SQLFI64
    #define SQLFI64
    typedef struct {
        unsigned long LoPart;
        unsigned long HiPart;
    } ub8;
    typedef struct {
        unsigned long LoPart;
        long          HiPart;
    } b8;
    #endif

#endif

typedef   unsigned char     ubyte1;
typedef   unsigned short    ubyte2;
typedef   unsigned long     ubyte4;
typedef   ub8               ubyte8;
typedef 	   ubyte1     byte1;
typedef 	   short     byte2;
typedef 	   long      byte4;
typedef   b8                 byte8;
typedef   unsigned char PTR ubyte1p;
typedef int (SBSTDCALL *SQLTPFP)(void);

typedef   ubyte1    SQLTARC;		/* remote connection architecture */
typedef   ubyte1    SQLTBNL;		/* bind name length */
typedef   ubyte1    SQLTBNN;		/* bind number */
typedef   ubyte1p   SQLTBNP;		/* bind name pointer */
typedef   byte2     SQLTNUL;		/* null indicator */
typedef   ubyte1    SQLTBOO;		/* boolean data type */
typedef   ubyte1    SQLTCDL;		/* column data length */
typedef   ubyte1    SQLTCHL;		/* column header length */
typedef   ubyte1    SQLTCHO;		/* check option */
typedef   ubyte1p   SQLTCHP;		/* column header pointer */
typedef   ubyte2    SQLTCLL;		/* column data length(long) */
typedef   ubyte1    SQLTCTY;		/* command type */
typedef   ubyte2    SQLTCUR;		/* cursor number */
typedef   ubyte2    SQLTDAL;		/* data length */
typedef   ubyte1p   SQLTDAP;		/* data pointer */
typedef   byte2     SQLTDAY;		/* number of days */
typedef   ubyte1    SQLTDDL;		/* database data length */
typedef   ubyte1    SQLTDDT;		/* database data type */
typedef   ubyte2    SQLTDEDL;		/* database extended data length */
typedef   ubyte2    SQLTDPT;		/* database parameter type */
typedef   ubyte4    SQLTDPV;		/* database parameter value */
typedef   ubyte2    SQLTEPO;		/* error position */
typedef   ubyte2    SQLTFAT;		/* file attribute */
typedef   ubyte2    SQLTFLD;		/* SELECT statement field number */
typedef   byte2     SQLTFLG;		/* flag field */
typedef   ubyte4    SQLTFLH;		/* file handle */
typedef   byte2     SQLTFMD;		/* file mode */
typedef   ubyte2    SQLTFNL;		/* file name length */
typedef   ubyte1p   SQLTFNP;		/* file name pointer */
typedef   ubyte1    SQLTFSC;		/* fetch status code */
typedef   ubyte1p   SQLTILV;		/* isolation level string */
typedef   ubyte1    SQLTLBL;		/* label information length */
typedef   ubyte1p   SQLTLBP;		/* label infromation pointer */
typedef   byte8     SQLTLLI;		/* long long integer */
typedef   byte4     SQLTLNG;		/* long size */
typedef   ubyte4    SQLTLSI;		/* long size */
typedef   ubyte2    SQLTMSZ;		/* message size */
typedef   ubyte1    SQLTNBV;		/* number of bind variables */
typedef   ubyte2    SQLTNCU;		/* number of cursors */
typedef   ubyte1    SQLTNML;		/* number length */
typedef   ubyte1p   SQLTNMP;		/* number pointer */
typedef   ubyte2    SQLTNPG;		/* number of pages */
typedef   ubyte4    SQLTLNPG;		/* number of pages */
typedef   ubyte1    SQLTNSI;		/* number of select items */
typedef   ubyte1    SQLTPCX;		/* preserve context flag */
typedef   ubyte1    SQLTPDL;		/* program data length */
typedef   ubyte1    SQLTPDT;		/* program data type */
typedef   ubyte4    SQLTPGN;		/* page number */
typedef   ubyte2    SQLTPNM;		/* process number */
typedef   ubyte1    SQLTPRE;		/* precision */
typedef   ubyte2    SQLTPTY;		/* set/get parameter type */
typedef   ubyte1    SQLTRBF;		/* roll back flag */
typedef   byte2     SQLTRCD;		/* return codes */
typedef   ubyte1    SQLTRCF;		/* recovery flag */
typedef   ubyte2    SQLTRFM;		/* rollforward mode */
typedef   byte4     SQLTROW;		/* number of rows */
typedef   ubyte1    SQLTSCA;		/* scale */
typedef   ubyte1    SQLTSLC;		/* select list column */
typedef   ubyte2    SQLTSTC;		/* statistics counter */
typedef   ubyte2    SQLTSVH;		/* server handle */
typedef   ubyte2    SQLTSVN;		/* server number */
typedef   byte2     SQLTTIV;		/* wait timeout value */
typedef   byte2     SQLTWNC;		/* whence */
typedef   ubyte2    SQLTWSI;		/* work size */
typedef   ubyte2    SQLTBIR;		/* bulk insert error row number */
typedef   ubyte1p   SQLTDIS;		/* Describe info indicator */
typedef   byte4     SQLTXER;		/* extended error # */
typedef   ubyte4    SQLTPID;		/* client process id */
typedef   ubyte4    SQLTMOD;		/* mode flag */
typedef   ubyte4    SQLTCON;		/* connection handle */

/* defines for isolation level string */
#define   SQLILRR   "RR"		/* Repeatable Read isolation */
#define   SQLILCS   "CS"		/* Cursor Stability isolation */
#define   SQLILRO   "RO"		/* Read-Only isolation */
#define   SQLILRL   "RL"		/* Release Locks isolation */
/* defines for isolation level flags*/
#define   SQLFIRR   0x01		/* Repeatable Read isolation flag */
#define   SQLFICS   0x02		/* Cursor Stability isolation flag */
#define   SQLFIRO   0x04		/* Read-Only isolation flag */
#define   SQLFIRL   0x08		/* Release Locks isolation flag */
/* defines for SQLROF rollforward mode parameter */
#define   SQLMEOL   1			/* rollforward to end of log */
#define   SQLMEOB   2			/* rollforward to end of backup */
#define   SQLMTIM   3			/* rollforward to specified time */
/* defines for when to collect Describe information */
#define   SQLDELY   0			/* get Describe info after sqlcom */
#define   SQLDDLD   1			/* get Describe info after sqlexe */
#define   SQLDNVR   2			/* never get any Describe info */
/* defines for SQLETX() and SQLTEM(): error text type parameters */
#define   SQLXMSG   1			/* retrieve error message text */
#define   SQLXREA   2			/* retrieve error message reason */
#define   SQLXREM   4			/* retrieve error message remedy */

/* defines for extended directory open function */
#define   SQLANRM   0x00		/* normal - no restrictions */
#define   SQLARDO   0x01		/* read only */
#define   SQLAHDN   0x02		/* hidden file */
#define   SQLASYS   0x04		/* system file */
#define   SQLAVOL   0x08		/* volume label */
#define   SQLADIR   0x10		/* directory */
#define   SQLAARC   0x20		/* archive bit */
#define   SQLAFDL   0x100		/* files and directories */
#define   SQLAFIL   0x200		/* files only */

/* defines for state of cursor */
#define   SQLCIDL    0	 /* idle cursor */
#define   SQLCECM    1	 /* executing compile */
#define   SQLCCCM    2	 /* completed compile */
#define   SQLCEXE    3	 /* executing command */
#define   SQLCCXE    4	 /* completed command */
#define   SQLCEFT    5	 /* executing fetch */
#define   SQLCCFT    6	 /* completed fetch */
/* SYSTEM DEFINED TYPEDEF'S -- FOR SYSTEM USE ONLY */

typedef   ubyte2 SQLTMSL;		/* message length */
typedef   byte1 far* SQLTMSP;		/* message pointer */

/*
DESCRIPTION
  This structure is used to receive system information from the
  backend.  Structure elements must be arranged so that the structure
  layout is the same in packed or padded compilation modes.  For
  now, this means longs in the front, ints in the middle, and chars at
  the end of the structure.
*/

struct	  sysdefx
	  {
	  SQLTPGN   syslpt;		/* log page threshold */
	  SQLTPGN   syslpm;		/* log page maximum */
	  ubyte4    syshep;		/* heap size */
	  SQLTNPG   sysncp;		/* number of cache pages */
	  SQLTTIV   systiv;		/* wait timeout value in seconds */
	  ubyte1    sysiso[3];		/* isolation level */
	  ubyte1    sysjou;		/* journal */
	  ubyte1    syslog;		/* log */
	  ubyte1    sysrec;		/* recovery */
	  ubyte1    systyp;		/* system type */
	  };
typedef   struct    sysdefx sysdef;
typedef   struct    sysdefx SQLTSYS;
#define   SYSSIZ    sizeof(sysdef)

/*
DESCRIPTION:
  This structure is used as a parameter to the SQLGDI function.  After a
  a compile, all relevant information for a given Select column can be
  obtained in this structure.
  Note:
     Please note that, originally, gdichb was the first element of the
     gdidefx structure.  It has been moved further down because a column
     heading can be greater than 31 bytes.  A bug was reported complaining
     that the column heading was not being returned correctly since the
     maximum length of a column heading is 46. This can now be returned
     since the size of the buffer (gdichb) has been changed to 47.
     Also, the length field (gdichl) has also been moved down to go with
     the column heading buffer (gdichb).
     The original gdichb and gdichl fields have been renamed to gdifl1 and
     gdifl2.
*/
struct	  gdidefx
	  {
	  ubyte1   gdifl1[31];		/* filler reserved for future use */
	  ubyte1   gdifl2;		/* filler reserved for future use */
	  ubyte1   gdilbb[31];		/* label buffer */
	  SQLTLBL  gdilbl;		/* label info length */
	  SQLTSLC  gdicol;		/* select column number */
	  SQLTDDT  gdiddt;		/* database data type */
	  SQLTDEDL gdiddl;		/* database extended data length */
	  SQLTDDT  gdiedt;		/* external data type */
	  SQLTDEDL gdiedl;		/* external extended data length */
	  SQLTPRE  gdipre;		/* decimal precision */
	  SQLTSCA  gdisca;		/* decimal scale */
	  byte2    gdinul;		/* null indicator */
	  ubyte1   gdichb[47];		/* column heading buffer */
	  SQLTCHL  gdichl;		/* column heading length */
	  byte1    gdifil[2];		/* for future use */
	  };
typedef   struct    gdidefx gdidef;
typedef   struct gdidefx SQLTGDI;
typedef   struct gdidefx* SQLTPGD;
#define   GDISIZ    sizeof(gdidef)

/*
DESCRIPTION
  This structure is used when passing binary data to and from external
  functions. Since binary data can contains nulls as part of the data
  we cannot look for a string terminator. Hence this structure is used
  to provide a pointer to the binary data and the length of the data.
*/

struct	  binaryx
	  {
	  long	   binary_len;
	  char*    binary_ptr;
	  };

#ifndef _INC_FSTREAM
typedef   struct   binaryx binary;
#endif
typedef   struct   binaryx BINARY;
typedef   struct   binaryx *lpbinary;
typedef   struct   binaryx *LPBINARY;

#define   BINARYSIZ sizeof(BINARY)

#define   BINARY_GET_LENGTH(x)		(x.binary_len)
#define   BINARY_GET_BUFFER(x)		(x.binary_ptr)

#define   BINARY_SET_LENGTH(x,y)	(x.binary_len=y)
#define   BINARY_SET_BUFFER(x,y)	(x.binary_ptr=y)

/*
  The following datatypes are analogous to the SqlWindows datatypes
  NUMBER and DATETIME. They are used to pass the Sqlbase internal number
  and datatime datatypes to external functions
*/

struct	datetimex
	{
	char	  datetime_len;
	char	  datetime_value[12];
	};

typedef struct	datetimex datetime;

#define DATETIMESIZ sizeof(datetime)

#define DATETIME_IS_NULL(x)	(x.datetime_len == 0)
#define DATETIME_SET_NULL(x)	(x.datetime_len = 0)

struct	numberx
	{
	char	  number_len;
	char	  number_value[12];
	};

typedef struct	numberx number;

#define NUMBERSIZ sizeof(number)

#define NUMBER_IS_NULL(x)     (x.number_len == 0)
#define NUMBER_SET_NULL(x)    (x.number_len = 0)


/* system types */
#define   SYSTSGL   1			/* single user */
#define   SYSTMUL   2			/* multi-user */
#define   SYSTDB2   3			/* DB2 */
#define   SYSTDMO   4			/* demo */
#define   SYSTGWY   5			/* SQLNetwork Gateway */
#define   SYSTRTR   6			/* SQLNetwork Router */
#define   SYSTSHAS  7			/* SQLNetwork SQLHost App Services */

/*-------------------------------------------------------------------------
  SQL API calling convention:

  For 32-bit systems, the calling convention used depends on the
  target platform:

  - For NetWare and Windows/NT, the __stdcall calling convention is used.
    If __stdcall is not supported by your compiler, then you will need
    to define it to be something equivalent to	__stdcall.
  ------------------------------------------------------------------------- */

# define SQLTAPI byte2 SBSTDCALL	/* Use __stdcall */

#ifndef SQL_PROTO
#ifndef NLINT_ARGS			/* argument checking enabled */

/* SQL FUNCTION PROTOTYPES */

SQLTAPI sqlarf(SQLTCUR	   cur	   , SQLTFNP	 fnp	 ,
	       SQLTFNL	   fnl	   , SQLTCHO	 cho	 );
SQLTAPI sqlbbr(SQLTCUR	   cur	   , SQLTXER PTR errnum  ,
	       SQLTDAP	   errbuf  , SQLTDAL PTR buflen  ,
	       SQLTBIR PTR errrow  , SQLTRBF PTR rbf	 ,
	       SQLTBIR	   errseq  );
SQLTAPI sqlbdb(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
	       SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
	       SQLTBOO	   over    );
SQLTAPI sqlbef(SQLTCUR	   cur	   );
SQLTAPI sqlber(SQLTCUR	   cur	   , SQLTRCD PTR rcd	 ,
	       SQLTBIR PTR errrow  , SQLTRBF PTR rbf	 ,
	       SQLTBIR	   errseq  );
SQLTAPI sqlbkp(SQLTCUR	   cur	   , SQLTBOO	 defalt  ,
	       SQLTBOO	   overwrt , SQLTFNP	 bkfname ,
	       SQLTFNL	   bkfnlen );
SQLTAPI sqlbld(SQLTCUR	   cur	   , SQLTBNP	 bnp	 ,
	       SQLTBNL	   bnl	   );
SQLTAPI sqlblf(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
	       SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
	       SQLTBOO	   over    );
SQLTAPI sqlblk(SQLTCUR	   cur	   , SQLTFLG	 blkflg  );
SQLTAPI sqlbln(SQLTCUR	   cur	   , SQLTBNN	 bnn	 );
SQLTAPI sqlbna(SQLTCUR	   cur	   , SQLTBNP	 bnp	 ,
	       SQLTBNL	   bnl	   , SQLTDAP	 dap	 ,
	       SQLTDAL	   dal	   , SQLTSCA	 sca	 ,
	       SQLTPDT	   pdt	   , SQLTNUL	 nli	 );
SQLTAPI sqlbnd(SQLTCUR	   cur	   , SQLTBNP	 bnp	 ,
	       SQLTBNL	   bnl	   , SQLTDAP	 dap	 ,
	       SQLTDAL	   dal	   , SQLTSCA	 sca	 ,
	       SQLTPDT	   pdt	   );
SQLTAPI sqlbnn(SQLTCUR	   cur	   , SQLTBNN	 bnn	 ,
	       SQLTDAP	   dap	   , SQLTDAL	 dal	 ,
	       SQLTSCA	   sca	   , SQLTPDT	 pdt	 );
SQLTAPI sqlbnu(SQLTCUR	   cur	   , SQLTBNN	 bnn	 ,
	       SQLTDAP	   dap	   , SQLTDAL	 dal	 ,
	       SQLTSCA	   sca	   , SQLTPDT	 pdt	 ,
	       SQLTNUL	   nli	   );
SQLTAPI sqlbss(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
	       SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
	       SQLTBOO	   over    );
SQLTAPI sqlcan(SQLTCUR	   cur	   );
SQLTAPI sqlcbv(SQLTCUR	   cur	   );
SQLTAPI sqlcch(SQLTCON PTR hConp   , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  , SQLTMOD	 fType	 );
SQLTAPI sqlcdr(SQLTSVH	   shandle,  SQLTCUR	 cur	 );
SQLTAPI sqlcex(SQLTCUR	   cur	   , SQLTDAP	 dap	 ,
	       SQLTDAL	   dal	   );
SQLTAPI sqlclf(SQLTSVH	   cur	   , SQLTDAP	 logfile ,
	       SQLTFMD	   startflag);
SQLTAPI sqlcmt(SQLTCUR	   cur	   );
SQLTAPI sqlcnc(SQLTCUR PTR curp    , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  );
SQLTAPI sqlcnr(SQLTCUR PTR curp    , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  );
SQLTAPI sqlcom(SQLTCUR	   cur	   , SQLTDAP	 cmdp	 ,
	       SQLTDAL	   cmdl    );
SQLTAPI sqlcon(SQLTCUR PTR curp    , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  , SQLTWSI	 cursiz  ,
	       SQLTNPG	   pages   , SQLTRCF	 recovr  ,
	       SQLTDAL	   outsize , SQLTDAL	 insize  );
SQLTAPI sqlcpy(SQLTCUR	   fcur    , SQLTDAP	 selp	 ,
	       SQLTDAL	   sell    , SQLTCUR	 tcur	 ,
	       SQLTDAP	   isrtp   , SQLTDAL	 isrtl	 );
SQLTAPI sqlcre(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  );
SQLTAPI sqlcrf(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel );
SQLTAPI sqlcrs(SQLTCUR	   cur	   , SQLTDAP	 rsp	 ,
	       SQLTDAL	   rsl	   );
SQLTAPI sqlcsv(SQLTSVH PTR shandlep, SQLTDAP	 serverid,
	       SQLTDAP	   password);
SQLTAPI sqlcty(SQLTCUR	   cur	   , SQLTCTY PTR cty	 );
SQLTAPI sqldbn(SQLTDAP	   serverid, SQLTDAP	 buffer  ,
	       SQLTDAL	   length  );
SQLTAPI sqldch(SQLTCON	   hCon    );
SQLTAPI sqlded(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  );
SQLTAPI sqldel(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  );
SQLTAPI sqldes(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTDDT PTR ddt	   , SQLTDDL PTR ddl	 ,
	       SQLTCHP	   chp	   , SQLTCHL PTR chlp	 ,
	       SQLTPRE PTR prep    , SQLTSCA PTR scap	 );
SQLTAPI sqldid(SQLTDAP	   dbname  , SQLTDAL	 dbnamel );
SQLTAPI sqldii(SQLTCUR	   cur	   , SQLTSLC	 ivn	 ,
	       SQLTDAP	   inp	   , SQLTCHL*	 inlp	 );
SQLTAPI sqldin(SQLTDAP	   dbnamp  , SQLTDAL	 dbnaml  );
SQLTAPI sqldir(SQLTSVN	   srvno   , SQLTDAP	 buffer  ,
	       SQLTDAL	   length  );
SQLTAPI sqldis(SQLTCUR	   cur	   );
SQLTAPI sqldon(void);
SQLTAPI sqldox(SQLTSVH	   shandle , SQLTDAP	 dirnamep,
	       SQLTFAT	   fattr   );
SQLTAPI sqldrc(SQLTSVH	   cur	   );
SQLTAPI sqldro(SQLTSVH	   shandle , SQLTDAP	 dirname );
SQLTAPI sqldrr(SQLTSVH	   shandle , SQLTDAP	 filename);
SQLTAPI sqldrs(SQLTCUR	   cur	   , SQLTDAP	 rsp	 ,
	       SQLTDAL	   rsl	   );
SQLTAPI sqldsc(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTDDT PTR edt	   , SQLTDDL PTR edl	 ,
	       SQLTCHP	   chp	   , SQLTCHL PTR chlp	 ,
	       SQLTPRE PTR prep    , SQLTSCA PTR scap	 );
SQLTAPI sqldst(SQLTCUR	   cur	   , SQLTDAP	 cnp	 ,
	       SQLTDAL	   cnl	   );
SQLTAPI sqldsv(SQLTSVH	   shandle );
SQLTAPI sqlebk(SQLTCUR	   cur	   );
SQLTAPI sqlefb(SQLTCUR	   cur	   );
SQLTAPI sqlelo(SQLTCUR	   cur	   );
SQLTAPI sqlenl(SQLTCON	   hCon	   , SQLTDAP	 p1, SQLTDAL    l1,
               SQLTDAP	   p2      , SQLTDAL*    l2_p	   );
SQLTAPI sqlenr(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel );
SQLTAPI sqlepo(SQLTCUR	   cur	   , SQLTEPO PTR epo	 );
SQLTAPI sqlerf(SQLTCUR	   cur	   );
SQLTAPI sqlerr(SQLTRCD	   error   , SQLTDAP	 msg	 );
SQLTAPI sqlers(SQLTCUR	   cur	   );
SQLTAPI sqletx(SQLTRCD	   error   , SQLTPTY	 msgtyp  ,
	       SQLTDAP	   bfp	   , SQLTDAL	 bfl	 ,
	       SQLTDAL PTR txtlen  );
SQLTAPI sqlexe(SQLTCUR	   cur	   );
SQLTAPI sqlexp(SQLTCUR	   cur	   , SQLTDAP	 buffer  ,
	       SQLTDAL	   length  );
SQLTAPI sqlfbk(SQLTCUR	   cur	   );
SQLTAPI sqlfer(SQLTRCD	   error   , SQLTDAP	 msg	 );
SQLTAPI sqlfet(SQLTCUR	   cur	   );
SQLTAPI sqlfgt(SQLTSVH	   cur	   , SQLTDAP	 srvfile ,
	       SQLTDAP	   lclfile );
SQLTAPI sqlfpt(SQLTSVH	   cur	   , SQLTDAP	 srvfile ,
	       SQLTDAP	   lclfile );
SQLTAPI sqlfqn(SQLTCUR	   cur	   , SQLTFLD	 field	 ,
	       SQLTDAP	   nameptr , SQLTDAL PTR namelen );
SQLTAPI sqlgbi(SQLTCUR	   cur	   , SQLTCUR PTR pcur	 ,
	       SQLTPNM PTR ppnm  );
SQLTAPI sqlgdi(SQLTCUR	   cur	   , SQLTPGD	 gdi	 );
SQLTAPI sqlget(SQLTCUR	   cur	   , SQLTPTY	 parm	 ,
	       SQLTDAP	   p	   , SQLTDAL PTR l	 );
SQLTAPI sqlgfi(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTCDL PTR cvl	   , SQLTFSC PTR fsc	 );
SQLTAPI sqlgls(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTLSI PTR size    );
SQLTAPI sqlgnl(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTLNG PTR lognum  );
SQLTAPI sqlgnr(SQLTCUR	   cur	   , SQLTDAP	 tbnam	 ,
	       SQLTDAL	   tbnaml  , SQLTROW PTR rows	 );
SQLTAPI sqlgsi(SQLTSVH	   shandle , SQLTFLG	 infoflags,
	       SQLTDAP	   buffer  , SQLTDAL	 buflen  ,
	       SQLTDAL PTR rbuflen );
SQLTAPI sqlgwo(SQLTCON	 hCon	   , SQLTDAP	 p	 ,
               SQLTDAL PTR l	   );
SQLTAPI sqlidb(SQLTCUR	   cur	   );
SQLTAPI sqlims(SQLTCUR	   cur	   , SQLTDAL	 insize  );
SQLTAPI sqlind(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  );
SQLTAPI sqlini(SQLTPFP	   callback);
SQLTAPI sqliniEx(SQLTDAP iniPath, SQLTDAL l);
SQLTAPI sqlins(SQLTSVN	   srvno   , SQLTDAP	 dbnamp  ,
	       SQLTDAL	   dbnaml  , SQLTFLG	 createflag,
	       SQLTFLG	   overwrite);
SQLTAPI sqliqx(SQLTCON	 hCon, SQLTDAP	   p,
               SQLTDAL   l,    SQLTBOO PTR b   );
SQLTAPI sqllab(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTCHP	   lbp	   , SQLTCHL PTR lblp	 );
SQLTAPI sqlldp(SQLTCUR	   cur	   , SQLTDAP	 cmdp	 ,
	       SQLTDAL	   cmdl    );
SQLTAPI sqllsk(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTLSI	   pos	   );
SQLTAPI sqlmcl(SQLTSVH	   shandle , SQLTFLH	 fd	 );
SQLTAPI sqlmdl(SQLTSVH	   shandle , SQLTDAP	 filename);
SQLTAPI sqlmls(SQLTSVH	   shandle , SQLTFLH	 fd	 ,
	       SQLTLLI	   offset  , SQLTWNC	 whence  ,
	       SQLTLLI PTR roffset );
SQLTAPI sqlmop(SQLTSVH	   shandle , SQLTFLH PTR fdp	 ,
	       SQLTDAP	   filename, SQLTFMD	 openmode);
SQLTAPI sqlmrd(SQLTSVH	   shandle , SQLTFLH	 fd	 ,
	       SQLTDAP	   buffer  , SQLTDAL	 len	 ,
	       SQLTDAL PTR rlen    );
SQLTAPI sqlmsk(SQLTSVH	   shandle , SQLTFLH	 fd	 ,
	       SQLTLNG	   offset  , SQLTWNC	 whence  ,
	       SQLTLNG PTR roffset );
SQLTAPI sqlmwr(SQLTSVH	   shandle , SQLTFLH	 fd	 ,
	       SQLTDAP	   buffer  , SQLTDAL	 len	 ,
	       SQLTDAL PTR rlen    );
SQLTAPI sqlnbv(SQLTCUR	   cur	   , SQLTNBV PTR nbv	 );
SQLTAPI sqlnii(SQLTCUR	   cur	   , SQLTNSI PTR nii	 );
SQLTAPI sqlnrr(SQLTCUR	   cur	   , SQLTROW PTR rcountp );
SQLTAPI sqlnsi(SQLTCUR	   cur	   , SQLTNSI PTR nsi	 );
SQLTAPI sqloms(SQLTCUR	   cur	   , SQLTDAL	 outsize );
SQLTAPI sqlopc(SQLTCUR PTR curp    , SQLTCON	 hCon	 ,
	       SQLTMOD	   fType   );
SQLTAPI sqlprs(SQLTCUR	   cur	   , SQLTROW	 row	 );
SQLTAPI sqlrbf(SQLTCUR	   cur	   , SQLTRBF PTR rbf	 );
SQLTAPI sqlrbk(SQLTCUR	   cur	   );
SQLTAPI sqlrcd(SQLTCUR	   cur	   , SQLTRCD PTR rcd	 );
SQLTAPI sqlrdb(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
	       SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
	       SQLTBOO	   over    );
SQLTAPI sqlrdc(SQLTCUR	   cur	   , SQLTDAP	 bufp	 ,
	       SQLTDAL	   bufl    , SQLTDAL PTR readl	 );
SQLTAPI sqlrel(SQLTCUR	   cur	   );
SQLTAPI sqlres(SQLTCUR PTR curptr  , SQLTFNP	 bkfname ,
	       SQLTFNL	   bkfnlen , SQLTSVN	 bkfserv ,
	       SQLTBOO	   overwrt , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnlen  , SQLTSVN	 dbserv  );
SQLTAPI sqlret(SQLTCUR	   cur	   , SQLTDAP	 cnp	 ,
	       SQLTDAL	   cnl	   );
SQLTAPI sqlrlf(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
	       SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
	       SQLTBOO	   over    );
SQLTAPI sqlrlo(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTDAP	   bufp    , SQLTDAL	 bufl	 ,
	       SQLTDAL PTR readl   );
SQLTAPI sqlrof(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTRFM	 mode	 ,
	       SQLTDAP	   datetime, SQLTDAL	 datetimel);
SQLTAPI sqlrow(SQLTCUR	   cur	   , SQLTROW PTR row	 );
SQLTAPI sqlrrd(SQLTCUR	   cur	   );
SQLTAPI sqlrrs(SQLTCUR	   cur	   , SQLTDAP	 rsp	 ,
	       SQLTDAL	   rsl	   );
SQLTAPI sqlrsi(SQLTSVH	   shandle );
SQLTAPI sqlrss(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
	       SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
	       SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
	       SQLTBOO	   over    );
SQLTAPI sqlsab(SQLTSVH	   shandle , SQLTPNM	 pnum	 );
SQLTAPI sqlsap(SQLTSVN	   srvno   , SQLTDAP	 password,
	       SQLTPNM	   pnum    );
SQLTAPI sqlscl(SQLTCUR	   cur	   , SQLTDAP	 namp	 ,
	       SQLTDAL	   naml    );
SQLTAPI sqlscn(SQLTCUR	   cur	   , SQLTDAP	 namp	 ,
	       SQLTDAL	   naml    );
SQLTAPI sqlscp(SQLTNPG	   pages   );
SQLTAPI sqlsdn(SQLTDAP	   dbnamp  , SQLTDAL	 dbnaml  );
SQLTAPI sqlsds(SQLTSVH	   shandle,  SQLTFLG shutdownflg);
SQLTAPI sqlsdx(SQLTSVH	   shandle,  SQLTDAP	 dbnamp,
	       SQLTDAL	   dbnaml  , SQLTFLG	 shutdownflg);
SQLTAPI sqlset(SQLTCUR	   cur	   , SQLTPTY	 parm	 ,
	       SQLTDAP	   p	   , SQLTDAL	 l	 );
SQLTAPI sqlsil(SQLTCUR	   cur	   , SQLTILV	 isolation);
SQLTAPI sqlslp(SQLTCUR	   cur	   , SQLTNPG	 lpt	 ,
	       SQLTNPG	   lpm	   );
SQLTAPI sqlspr(SQLTCUR	   cur	   );
SQLTAPI sqlsrf(SQLTCUR	   cur	   , SQLTDAP	 fnp	 ,
	       SQLTDAL	   fnl	   );
SQLTAPI sqlsrs(SQLTCUR	   cur	   );
SQLTAPI sqlssb(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
	       SQLTPDT	   pdt	   , SQLTDAP	 pbp	 ,
	       SQLTPDL	   pdl	   , SQLTSCA	 sca	 ,
	       SQLTCDL PTR pcv	   , SQLTFSC PTR pfc	 );
SQLTAPI sqlsss(SQLTCUR	   cur	   , SQLTDAL	 size	 );
SQLTAPI sqlsta(SQLTCUR	   cur	   , SQLTSTC PTR svr	 ,
	       SQLTSTC PTR svw	   , SQLTSTC PTR spr	 ,
	       SQLTSTC PTR spw	   );
SQLTAPI sqlstm(SQLTSVH	   shandle );
SQLTAPI sqlsto(SQLTCUR	   cur	   , SQLTDAP	 cnp	 ,
	       SQLTDAL	   cnl	   , SQLTDAP	 ctp	 ,
	       SQLTDAL	   ctl	   );
SQLTAPI sqlstr(SQLTCUR	   cur	   );
SQLTAPI sqlsxt(SQLTSVN	   srvno   , SQLTDAP	 password);
SQLTAPI sqlsys(SQLTCUR	   cur	   , SQLTSYS PTR sys	 );
SQLTAPI sqltec(SQLTRCD	   rcd	   , SQLTRCD PTR np	 );
SQLTAPI sqltem(SQLTCUR	   cur	   , SQLTXER PTR xer	 ,
	       SQLTPTY	   msgtyp  , SQLTDAP	 bfp	 ,
	       SQLTDAL	   bfl	   , SQLTDAL PTR txtlen  );
SQLTAPI sqltio(SQLTCUR	   cur	   , SQLTTIV	 _timeout);
SQLTAPI sqlunl(SQLTCUR	   cur	   , SQLTDAP	 cmdp	 ,
	       SQLTDAL	   cmdl    );
SQLTAPI sqlurs(SQLTCUR	   cur	   );
SQLTAPI sqlwdc(SQLTCUR	   cur	   , SQLTDAP	 bufp	 ,
	       SQLTDAL	   bufl    );
SQLTAPI sqlwlo(SQLTCUR	   cur	   , SQLTDAP	 bufp	 ,
	       SQLTDAL	   bufl    );
SQLTAPI sqlxad(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
	       SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
	       SQLTNML	   nl2	   );
SQLTAPI sqlxcn(SQLTNMP	   op	   , SQLTDAP	 ip	 ,
	       SQLTDAL	   il	   );
SQLTAPI sqlxda(SQLTNMP	   op	   , SQLTNMP	 dp	 ,
	       SQLTNML	   dl	   , SQLTDAY	 days	 );
SQLTAPI sqlxdp(SQLTDAP	   op	   , SQLTDAL	 ol	 ,
	       SQLTNMP	   ip	   , SQLTNML	 il	 ,
	       SQLTDAP	   pp	   , SQLTDAL	 pl	 );
SQLTAPI sqlxdv(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
	       SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
	       SQLTNML	   nl2	   );
SQLTAPI sqlxer(SQLTCUR	   cur	   , SQLTXER PTR errnum,
	       SQLTDAP	   errbuf  , SQLTDAL PTR buflen  );
SQLTAPI sqlxml(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
	       SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
	       SQLTNML	   nl2	   );
SQLTAPI sqlxnp(SQLTDAP	   outp    , SQLTDAL	 outl	 ,
	       SQLTNMP	   isnp    , SQLTNML	 isnl	 ,
	       SQLTDAP	   picp    , SQLTDAL	 picl	 );
SQLTAPI sqlxpd(SQLTNMP	   op	   , SQLTNML PTR olp	 ,
	       SQLTDAP	   ip	   , SQLTDAP	 pp	 ,
	       SQLTDAL	   pl	   );
SQLTAPI sqlxsb(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
	       SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
	       SQLTNML	   nl2	   );

#else

SQLTAPI sqlarf();
SQLTAPI sqlbbr();
SQLTAPI sqlbdb();
SQLTAPI sqlbef();
SQLTAPI sqlber();
SQLTAPI sqlbkp();
SQLTAPI sqlbld();
SQLTAPI sqlblf();
SQLTAPI sqlblk();
SQLTAPI sqlbln();
SQLTAPI sqlbna();
SQLTAPI sqlbnd();
SQLTAPI sqlbnn();
SQLTAPI sqlbnu();
SQLTAPI sqlbss();
SQLTAPI sqlcan();
SQLTAPI sqlcbv();
SQLTAPI sqlcch();
SQLTAPI sqlcdr();
SQLTAPI sqlcex();
SQLTAPI sqlclf();
SQLTAPI sqlcmt();
SQLTAPI sqlcnc();
SQLTAPI sqlcnr();
SQLTAPI sqlcom();
SQLTAPI sqlcon();
SQLTAPI sqlcpy();
SQLTAPI sqlcre();
SQLTAPI sqlcrf();
SQLTAPI sqlcrs();
SQLTAPI sqlcsv();
SQLTAPI sqlcty();
SQLTAPI sqldbn();
SQLTAPI sqldch();
SQLTAPI sqlded();
SQLTAPI sqldel();
SQLTAPI sqldes();
SQLTAPI sqldid();
SQLTAPI sqldii();
SQLTAPI sqldin();
SQLTAPI sqldir();
SQLTAPI sqldis();
SQLTAPI sqldon();
SQLTAPI sqldox();
SQLTAPI sqldrc();
SQLTAPI sqldro();
SQLTAPI sqldrr();
SQLTAPI sqldrs();
SQLTAPI sqldsc();
SQLTAPI sqldst();
SQLTAPI sqldsv();
SQLTAPI sqlebk();
SQLTAPI sqlefb();
SQLTAPI sqlelo();
SQLTAPI sqlenr();
SQLTAPI sqlepo();
SQLTAPI sqlerf();
SQLTAPI sqlerr();
SQLTAPI sqlers();
SQLTAPI sqletx();
SQLTAPI sqlexe();
SQLTAPI sqlexp();
SQLTAPI sqlfbk();
SQLTAPI sqlfer();
SQLTAPI sqlfet();
SQLTAPI sqlfgt();
SQLTAPI sqlfpt();
SQLTAPI sqlfqn();
SQLTAPI sqlgbi();
SQLTAPI sqlgdi();
SQLTAPI sqlget();
SQLTAPI sqlgfi();
SQLTAPI sqlgls();
SQLTAPI sqlgnl();
SQLTAPI sqlgnr();
SQLTAPI sqlgsi();
SQLTAPI sqlgwo();
SQLTAPI sqlidb();
SQLTAPI sqlims();
SQLTAPI sqlind();
SQLTAPI sqlini();
SQLTAPI sqliniEx();
SQLTAPI sqlins();
SQLTAPI sqliqx();
SQLTAPI sqllab();
SQLTAPI sqlldp();
SQLTAPI sqllsk();
SQLTAPI sqlmcl();
SQLTAPI sqlmdl();
SQLTAPI sqlmls();
SQLTAPI sqlmop();
SQLTAPI sqlmrd();
SQLTAPI sqlmsk();
SQLTAPI sqlmwr();
SQLTAPI sqlnbv();
SQLTAPI sqlnii();
SQLTAPI sqlnrr();
SQLTAPI sqlnsi();
SQLTAPI sqloms();
SQLTAPI sqlopc();
SQLTAPI sqlprs();
SQLTAPI sqlrbf();
SQLTAPI sqlrbk();
SQLTAPI sqlrcd();
SQLTAPI sqlrdb();
SQLTAPI sqlrdc();
SQLTAPI sqlrel();
SQLTAPI sqlres();
SQLTAPI sqlret();
SQLTAPI sqlrlf();
SQLTAPI sqlrlo();
SQLTAPI sqlrof();
SQLTAPI sqlrow();
SQLTAPI sqlrrd();
SQLTAPI sqlrrs();
SQLTAPI sqlrsi();
SQLTAPI sqlrss();
SQLTAPI sqlsab();
SQLTAPI sqlsap();
SQLTAPI sqlscl();
SQLTAPI sqlscn();
SQLTAPI sqlscp();
SQLTAPI sqlsdn();
SQLTAPI sqlsds();
SQLTAPI sqlsdx();
SQLTAPI sqlset();
SQLTAPI sqlsil();
SQLTAPI sqlslp();
SQLTAPI sqlspr();
SQLTAPI sqlsrf();
SQLTAPI sqlsrs();
SQLTAPI sqlssb();
SQLTAPI sqlsss();
SQLTAPI sqlsta();
SQLTAPI sqlstm();
SQLTAPI sqlsto();
SQLTAPI sqlstr();
SQLTAPI sqlsxt();
SQLTAPI sqlsys();
SQLTAPI sqltec();
SQLTAPI sqltem();
SQLTAPI sqltio();
SQLTAPI sqlunl();
SQLTAPI sqlurs();
SQLTAPI sqlwdc();
SQLTAPI sqlwlo();
SQLTAPI sqlxad();
SQLTAPI sqlxcn();
SQLTAPI sqlxda();
SQLTAPI sqlxdp();
SQLTAPI sqlxdv();
SQLTAPI sqlxer();
SQLTAPI sqlxml();
SQLTAPI sqlxnp();
SQLTAPI sqlxpd();
SQLTAPI sqlxsb();

#endif
#endif

#define SQLF000 0   /* not a function */
#define SQLFINI 1   /* SQL INItialize applications use of the database */
#define SQLFDON 2   /* SQL DONe using database */
#define SQLFCON 3   /* SQL CONnect to a cursor/database */
#define SQLFDIS 4   /* SQL DISconnect from a cursor/database */
#define SQLFCOM 5   /* SQL COMpile a SQL command */
#define SQLFEXE 6   /* SQL EXEcute an SQL command */
#define SQLFCEX 7   /* SQL Compile and EXecute a SQL command */
#define SQLFCMT 8   /* SQL CoMmiT a transaction to the database */
#define SQLFDES 9   /* SQL DEScribe the items of a select statement */
#define SQLFGFI 10  /* SQL Get Fetch Information */
#define SQLFFBK 11  /* SQL FETch previous row from SELECT */
#define SQLFFET 12  /* SQL FETch next row from SELECT */
#define SQLFEFB 13  /* SQL Enable Fetch Backwards */
#define SQLFPRS 14  /* SQL Position in Result Set */
#define SQLFURS 15  /* SQL Undo Result Set */
#define SQLFNBV 16  /* SQL get Number of Bind Variables */
#define SQLFBND 17  /* SQL BiNd Data variables.  This function is supercede */
#define SQLFBNN 18  /* SQL BiNd Numerics */
#define SQLFBLN 19  /* SQL Bind Long Number */
#define SQLFBLD 20  /* SQL Bind Long Data variables */
#define SQLFSRS 21  /* SQL Start Restriction Set processing */
#define SQLFRRS 22  /* SQL Restart Restriction Set processing */
#define SQLFCRS 23  /* SQL Close Restriction Set */
#define SQLFDRS 24  /* SQL Drop Restriction Set */
#define SQLFARF 25  /* SQL Apply Roll Forward journal */
#define SQLFERF 26  /* SQL End RollForward recovery (no longer supported) */
#define SQLFSRF 27  /* SQL Start Roll Forward journal */
#define SQLFSTO 28  /* SQL STOre a compiled SQL command */
#define SQLFRET 29  /* SQL RETrieve a compiled SQL command */
#define SQLFDST 30  /* SQL Drop a STored command */
#define SQLFCTY 31  /* SQL get Command TYpe */
#define SQLFEPO 32  /* SQL get Error POsition */
#define SQLFGNR 33  /* SQL Get Number of Rows */
#define SQLFNSI 34  /* SQL get Number of Select Items */
#define SQLFRBF 35  /* SQL get Roll Back Flag */
#define SQLFRCD 36  /* SQL get Return CoDe */
#define SQLFROW 37  /* SQL get number of ROWs */
#define SQLFSCN 38  /* SQL Set Cursor Name */
#define SQLFSIL 39  /* SQL Set Isolation Level */
#define SQLFSLP 40  /* SQL Set Log Parameters */
#define SQLFSSB 41  /* SQL Set Select Buffer */
#define SQLFSSS 42  /* SQL Set SortSpace */
#define SQLFRLO 43  /* SQL Read LOng */
#define SQLFWLO 44  /* SQL Write LOng */
#define SQLFLSK 45  /* SQL Long SeeK */
#define SQLFGLS 46  /* SQL Get Long Size */
#define SQLFELO 47  /* SQL End Long Operation */
#define SQLFRBK 48  /* SQL RollBacK a transaction from the database */
#define SQLFERR 49  /* SQL get ERRor message */
#define SQLFCPY 50  /* SQL CoPY */
#define SQLFIDB 51  /* SQL Initialize DataBase */
#define SQLFSYS 52  /* SQL SYSTEM */
#define SQLFSTA 53  /* SQL STAtistics */
#define SQLFR02 54  /* SQL RESERVED */
#define SQLFXAD 55  /* SQL eXtra ADd */
#define SQLFXCN 56  /* SQL eXtra Character to NUmber */
#define SQLFXDA 57  /* SQL eXtra Date Add */
#define SQLFXDP 58  /* SQL eXtra convert SQLBASE Date to Picture */
#define SQLFXDV 59  /* SQL eXtra DiVide */
#define SQLFXML 60  /* SQL eXtra MuLtiply */
#define SQLFXNP 61  /* SQL eXtra convert SQLBASE Numeric to Picture */
#define SQLFXPD 62  /* SQL eXtra convert Picture to SQLBASE Date. */
#define SQLFXSB 63  /* SQL eXtra SuBtract */
#define SQLFINS 64  /* SQL INStall database (no longer supported) */
#define SQLFDIN 65  /* SQL DeINstall database (no longer supported) */
#define SQLFDIR 66  /* SQL DIRectory of databases */
#define SQLFTIO 67  /* SQL TImeOut */
#define SQLFFQN 68  /* SQL get Fully Qualified column Name */
#define SQLFEXP 69  /* SQL EXexcution Plan */
#define SQLFFER 70  /* SQL Full ERror message */
#define SQLFBKP 71  /* SQL BacKuP */
#define SQLFRDC 72  /* SQL Read Database Chunk */
#define SQLFEBK 73  /* SQL End network online BacKup */
#define SQLFRES 74  /* SQL REStore from backup */
#define SQLFWDC 75  /* SQL Write Database Chunk */
#define SQLFRRD 76  /* SQL Recover Restored Database */
#define SQLFERS 77  /* SQL End network ReStore */
#define SQLFNRR 78  /* SQL Number of Rows in Result set */
#define SQLFSTR 79  /* SQL STart Restriction mode */
#define SQLFSPR 80  /* SQL StoP Restriction mode */
#define SQLFCNC 81  /* SQL CoNneCt: the sequel. */
#define SQLFCNR 82  /* SQL Connect with No Recovery */
#define SQLFOMS 83  /* SQL set Output Message Size */
#define SQLFIMS 84  /* SQL set Input Message Size */
#define SQLFSCP 85  /* SQL Set Cache Pages */
#define SQLFDSC 86  /* SQL DeSCribe item of SELECT with external type */
#define SQLFLAB 87  /* SQL get LABel information */
#define SQLFCBV 88  /* SQL Clear Bind Variables */
#define SQLFGET 89  /* SQL GET database parameter */
#define SQLFSET 90  /* SQL SET database parameter */
#define SQLFTEC 91  /* SQL Translate Error Code */
#define SQLFBDB 92  /* SQL Backup DataBase */
#define SQLFBEF 93  /* SQL Bulk Execute Flush */
#define SQLFBER 94  /* SQL get Bulk Execute Returned code */
#define SQLFBLF 95  /* SQL Backup Log Files */
#define SQLFBLK 96  /* SQL set BuLK insert mode */
#define SQLFBSS 97  /* SQL Backup SnapShot */
#define SQLFCAN 98  /* SQL CaNceL command */
#define SQLFCLF 99  /* SQL Change server activity LogFile */
#define SQLFCRE 100 /* SQL CREate database */
#define SQLFCRF 101 /* SQL Continue RollForward recovery */
#define SQLFCSV 102 /* SQL Connect to SerVer */
#define SQLFDBN 103 /* SQL Directory By Name */
#define SQLFDED 104 /* SQL DeINstall database */
#define SQLFDEL 105 /* SQL DELete database */
#define SQLFDID 106 /* SQL DeInstall database and Delete database files */
#define SQLFDRC 107 /* SQL DiRectory Close */
#define SQLFDRO 108 /* SQL DiRectory Open */
#define SQLFDRR 109 /* SQL DiRectory Read */
#define SQLFDSV 110 /* SQL Disconnect from SerVer */
#define SQLFENR 111 /* SQL ENd Rollforward recovery */
#define SQLFFGT 112 /* SQL File GeT */
#define SQLFFPT 113 /* SQL File Put */
#define SQLFGNL 114 /* SQL Get Next Log for rollforward */
#define SQLFGSI 115 /* SQL Get Server Information */
#define SQLFIND 116 /* SQL INstall Database */
#define SQLFMCL 117 /* SQL reMote CLose server file */
#define SQLFMDL 118 /* SQL reMote DeLete file or directory on remote server */
#define SQLFMOP 119 /* SQL reMote OPen file on server */
#define SQLFMRD 120 /* SQL reMote ReaD from file on server */
#define SQLFMSK 121 /* SQL reMote SeeK into file on server */
#define SQLFMWR 122 /* SQL reMote WRite to file on server */
#define SQLFRDB 123 /* SQL Restore DataBase */
#define SQLFREL 124 /* SQL RELease log */
#define SQLFRLF 125 /* SQL Restore Log Files */
#define SQLFROF 126 /* SQL ROllForward recovery */
#define SQLFRSS 127 /* SQL Restore SnapShot */
#define SQLFSAB 128 /* SQL Server ABort process */
#define SQLFSAP 129 /* SQL Server Abort Process */
#define SQLFSDN 130 /* SQL ShutDowN database */
#define SQLFSTM 131 /* SQL Server TerMinate */
#define SQLFBBR 132 /* SQL get Bulk Execute Backend error # and message */
#define SQLFBNA 133 /* SQL BiNd Data variables by name */
#define SQLFBNU 134 /* SQL BiNd Numerical bind variable */
#define SQLFGDI 135 /* SQL Get Descriptor info for a given Select column. */
#define SQLFSXT 136 /* SQL Server eXiT (no longer supported) */
#define SQLFXER 137 /* SQL get backend (Extended) error number and Message */
#define SQLFETX 138 /* SQL get Error message TeXt */
#define SQLFTEM 139 /* SQL get Tokenized Error Message */
#define SQLFSCL 140 /* SQL Set CLient name */
#define SQLFLDP 141 /* SQL LoaD oPeration */
#define SQLFUNL 142 /* SQL UNLoad command */
#define SQLFGBI 143 /* SQL Get Backend cursor Information */
#define SQLFNII 144 /* SQL get Number of Into variable Information */
#define SQLFDII 145 /* SQL Describe Into variable Information */
#define SQLFSDS 146 /* SQL Shutdown/enable server */
#define SQLFSDX 147 /* SQL Shutdown/enable Database Extended */
#define SQLFCDR 148 /* SQL Cancel Database Request */
#define SQLFDOX 149 /* SQL Directory Open Extended */
#define SQLFRSI 150 /* SQL Reset Statistical Information */
#define SQLFCCH 151 /* SQL Create Connection Handle */
#define SQLFOPC 152 /* SQL OPen Cursor */
#define SQLFDCH 153 /* SQL Destroy Connection Handle */
#define SQLFENL 154 /* SQL MTS Enlist */
#define SQLFGWO 155 /* SQL MTS Get Whereabouts Object */
#define SQLFMLS 156 /* SQL reMote Long SeeK into file on server */
#define SQLFIQX 157 /* SQL MTS Query a transaction's status */

#define SQLFMINAPI  SQLFINI		/* changes these when API entry */
#define SQLFMAXAPI  SQLFGWO		/* points change */

/*
 * The following default value macros are obsolete and are not used by
 * the SQLBase code anymore.
 */
#if 0
#define   SQLDNPG   500 		/* CACHE, number of cache pages */
#define   SQLDNSCPG  500		/* SORTCACHE, num of sortcache pages */
#define   SQLMISCPG 1000000		/* installed sort cache pages */
#define   SQLMSCPAG 0			/* minimum sort cache pages */
#define   SQLMSRS   3000		/* minimum sort space */
#endif


#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* !SQL */
