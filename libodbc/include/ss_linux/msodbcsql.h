#ifndef __msodbcsql_h__
#define __msodbcsql_h__

/*
 *-----------------------------------------------------------------------------
 * File:        msodbcsql.h
 *
 * Copyright:   Copyright (c) Microsoft Corporation
 *
 * Contents:    This SDK is not supported under any Microsoft standard support 
 *              program or service. The information is provided AS IS without 
 *              warranty of any kind. Microsoft disclaims all implied 
 *              warranties including, without limitation, any implied 
 *              warranties of merchantability or of fitness for a particular 
 *              purpose. The entire risk arising out of the use of this SDK 
 *              remains with you. In no event shall Microsoft, its authors, or 
 *              anyone else involved in the creation, production, or delivery 
 *              of this SDK be liable for any damages whatsoever (including, 
 *              without limitation, damages for loss of business profits, 
 *              business interruption, loss of business information, or other 
 *              pecuniary loss) arising out of the use of or inability to use 
 *              this SDK, even if Microsoft has been advised of the possibility 
 *              of such damages.
 *
 *-----------------------------------------------------------------------------
 */

#if !defined(SQLODBC_VER)
#define SQLODBC_VER 1100
#endif

#if SQLODBC_VER >= 1100

#define SQLODBC_PRODUCT_NAME_FULL_VER_ANSI      "Microsoft ODBC Driver 11 for SQL Server"
#define SQLODBC_PRODUCT_NAME_FULL_ANSI          "Microsoft ODBC Driver for SQL Server"
#define SQLODBC_PRODUCT_NAME_SHORT_VER_ANSI     "ODBC Driver 11 for SQL Server"
#define SQLODBC_PRODUCT_NAME_SHORT_ANSI         "ODBC Driver for SQL Server"

#endif /* SQLODBC_VER >= 1100 */

#define SQLODBC_PRODUCT_NAME_FULL_VER           SQLODBC_PRODUCT_NAME_FULL_VER_ANSI
#define SQLODBC_PRODUCT_NAME_FULL               SQLODBC_PRODUCT_NAME_FULL_ANSI
#define SQLODBC_PRODUCT_NAME_SHORT_VER          SQLODBC_PRODUCT_NAME_SHORT_VER_ANSI
#define SQLODBC_PRODUCT_NAME_SHORT              SQLODBC_PRODUCT_NAME_SHORT_ANSI

#define SQLODBC_DRIVER_NAME                     SQLODBC_PRODUCT_NAME_SHORT_VER

/* max SQL Server identifier length */
#define SQL_MAX_SQLSERVERNAME                       128

/*
 * SQLSetConnectAttr driver specific defines.
 * Microsoft has 1200 thru 1249 reserved for Microsoft ODBC Driver for SQL Server usage.
 * Connection attributes
 */
#define SQL_COPT_SS_BASE                                1200
#define SQL_COPT_SS_INTEGRATED_SECURITY                 (SQL_COPT_SS_BASE+3) /* Force integrated security on login */
#define SQL_COPT_SS_TRANSLATE                           (SQL_COPT_SS_BASE+20) /* Perform code page translation */
#define SQL_COPT_SS_ENCRYPT                             (SQL_COPT_SS_BASE+23) /* Allow strong encryption for data */
#define SQL_COPT_SS_MARS_ENABLED                        (SQL_COPT_SS_BASE+24) /* Multiple active result set per connection */
#define SQL_COPT_SS_TXN_ISOLATION                       (SQL_COPT_SS_BASE+27) /* Used to set/get any driver-specific or ODBC-defined TXN iso level */
#define SQL_COPT_SS_TRUST_SERVER_CERTIFICATE            (SQL_COPT_SS_BASE+28) /* Trust server certificate */

/*
 * SQLSetStmtAttr Microsoft ODBC Driver for SQL Server specific defines.
 * Statement attributes
 */
#define SQL_SOPT_SS_BASE                            1225
#define SQL_SOPT_SS_TEXTPTR_LOGGING                 (SQL_SOPT_SS_BASE+0) /* Text pointer logging */
#define SQL_SOPT_SS_NOBROWSETABLE                   (SQL_SOPT_SS_BASE+3) /* Set NOBROWSETABLE option */
/* Define old names */
#define SQL_TEXTPTR_LOGGING                         SQL_SOPT_SS_TEXTPTR_LOGGING
#define SQL_COPT_SS_BASE_EX                         1240
#define SQL_COPT_SS_WARN_ON_CP_ERROR                (SQL_COPT_SS_BASE_EX+3) /* Issues warning when data from the server had a loss during code page conversion. */
#define SQL_COPT_SS_CONNECTION_DEAD                 (SQL_COPT_SS_BASE_EX+4) /* dbdead SQLGetConnectOption only. It will try to ping the server. Expensive connection check */
#define SQL_COPT_SS_APPLICATION_INTENT              (SQL_COPT_SS_BASE_EX+7) /* Application Intent */
#define SQL_COPT_SS_MULTISUBNET_FAILOVER            (SQL_COPT_SS_BASE_EX+8) /* Multi-subnet Failover */

/*
 * SQLColAttributes driver specific defines.
 * SQLSetDescField/SQLGetDescField driver specific defines.
 * Microsoft has 1200 thru 1249 reserved for Microsoft ODBC Driver for SQL Server usage.
 */
#define SQL_CA_SS_BASE                              1200
#define SQL_CA_SS_COLUMN_SSTYPE                     (SQL_CA_SS_BASE+0)   /*  dbcoltype/dbalttype */
#define SQL_CA_SS_COLUMN_UTYPE                      (SQL_CA_SS_BASE+1)   /*  dbcolutype/dbaltutype */
#define SQL_CA_SS_NUM_ORDERS                        (SQL_CA_SS_BASE+2)   /*  dbnumorders */
#define SQL_CA_SS_COLUMN_ORDER                      (SQL_CA_SS_BASE+3)   /*  dbordercol */
#define SQL_CA_SS_COLUMN_VARYLEN                    (SQL_CA_SS_BASE+4)   /*  dbvarylen */
#define SQL_CA_SS_NUM_COMPUTES                      (SQL_CA_SS_BASE+5)   /*  dbnumcompute */
#define SQL_CA_SS_COMPUTE_ID                        (SQL_CA_SS_BASE+6)   /*  dbnextrow status return */
#define SQL_CA_SS_COMPUTE_BYLIST                    (SQL_CA_SS_BASE+7)   /*  dbbylist */
#define SQL_CA_SS_COLUMN_ID                         (SQL_CA_SS_BASE+8)   /*  dbaltcolid */
#define SQL_CA_SS_COLUMN_OP                         (SQL_CA_SS_BASE+9)   /*  dbaltop */
#define SQL_CA_SS_COLUMN_SIZE                       (SQL_CA_SS_BASE+10)  /*  dbcollen */
#define SQL_CA_SS_COLUMN_HIDDEN                     (SQL_CA_SS_BASE+11)  /*  Column is hidden (FOR BROWSE) */
#define SQL_CA_SS_COLUMN_KEY                        (SQL_CA_SS_BASE+12)  /*  Column is key column (FOR BROWSE) */
#define SQL_CA_SS_COLUMN_COLLATION                  (SQL_CA_SS_BASE+14)  /*  Column collation (only for chars) */
#define SQL_CA_SS_VARIANT_TYPE                      (SQL_CA_SS_BASE+15)
#define SQL_CA_SS_VARIANT_SQL_TYPE                  (SQL_CA_SS_BASE+16)
#define SQL_CA_SS_VARIANT_SERVER_TYPE               (SQL_CA_SS_BASE+17)

/* XML, CLR UDT, and table valued parameter related metadata */
#define SQL_CA_SS_UDT_CATALOG_NAME                  (SQL_CA_SS_BASE+18) /*  UDT catalog name */
#define SQL_CA_SS_UDT_SCHEMA_NAME                   (SQL_CA_SS_BASE+19) /*  UDT schema name */
#define SQL_CA_SS_UDT_TYPE_NAME                     (SQL_CA_SS_BASE+20) /*  UDT type name */
#define SQL_CA_SS_XML_SCHEMACOLLECTION_CATALOG_NAME (SQL_CA_SS_BASE+22) /*  Name of the catalog that contains XML Schema collection */
#define SQL_CA_SS_XML_SCHEMACOLLECTION_SCHEMA_NAME  (SQL_CA_SS_BASE+23) /*  Name of the schema that contains XML Schema collection */
#define SQL_CA_SS_XML_SCHEMACOLLECTION_NAME         (SQL_CA_SS_BASE+24) /*  Name of the XML Schema collection */
#define SQL_CA_SS_CATALOG_NAME                      (SQL_CA_SS_BASE+25) /*  Catalog name */
#define SQL_CA_SS_SCHEMA_NAME                       (SQL_CA_SS_BASE+26) /*  Schema name */
#define SQL_CA_SS_TYPE_NAME                         (SQL_CA_SS_BASE+27) /*  Type name */

/* table valued parameter related metadata */
#define SQL_CA_SS_COLUMN_COMPUTED                   (SQL_CA_SS_BASE+29) /*  column is computed */
#define SQL_CA_SS_COLUMN_IN_UNIQUE_KEY              (SQL_CA_SS_BASE+30) /*  column is part of a unique key */
#define SQL_CA_SS_COLUMN_SORT_ORDER                 (SQL_CA_SS_BASE+31) /*  column sort order */
#define SQL_CA_SS_COLUMN_SORT_ORDINAL               (SQL_CA_SS_BASE+32) /*  column sort ordinal */
#define SQL_CA_SS_COLUMN_HAS_DEFAULT_VALUE          (SQL_CA_SS_BASE+33) /*  column has default value for all rows of the table valued parameter */

/* sparse column related metadata */
#define SQL_CA_SS_IS_COLUMN_SET                     (SQL_CA_SS_BASE+34) /*  column is a column-set column for sparse columns */

/* Legacy datetime related metadata */
#define SQL_CA_SS_SERVER_TYPE                       (SQL_CA_SS_BASE+35) /*  column type to send on the wire for datetime types */

/* Defines for use with SQL_COPT_SS_INTEGRATED_SECURITY - Pre-Connect Option only */
#define SQL_IS_OFF                          0L           /*  Integrated security isn't used */
#define SQL_IS_ON                           1L           /*  Integrated security is used */
#define SQL_IS_DEFAULT                      SQL_IS_OFF
/* Defines for use with SQL_COPT_SS_TRANSLATE */
#define SQL_XL_OFF                          0L           /*  Code page translation is not performed */
#define SQL_XL_ON                           1L           /*  Code page translation is performed */
#define SQL_XL_DEFAULT                      SQL_XL_ON
/* Defines for use with SQL_SOPT_SS_TEXTPTR_LOGGING */
#define SQL_TL_OFF                          0L           /*  No logging on text pointer ops */
#define SQL_TL_ON                           1L           /*  Logging occurs on text pointer ops */
#define SQL_TL_DEFAULT                      SQL_TL_ON
/* Defines for use with SQL_SOPT_SS_NOBROWSETABLE */
#define SQL_NB_OFF                          0L           /*  NO_BROWSETABLE is off */
#define SQL_NB_ON                           1L           /*  NO_BROWSETABLE is on */
#define SQL_NB_DEFAULT                      SQL_NB_OFF
/* SQL_COPT_SS_ENCRYPT */
#define SQL_EN_OFF                          0L
#define SQL_EN_ON                           1L
/* SQL_COPT_SS_TRUST_SERVER_CERTIFICATE */
#define SQL_TRUST_SERVER_CERTIFICATE_NO     0L
#define SQL_TRUST_SERVER_CERTIFICATE_YES    1L
/* SQL_COPT_SS_WARN_ON_CP_ERROR */
#define SQL_WARN_NO                         0L
#define SQL_WARN_YES                        1L
/* SQL_COPT_SS_MARS_ENABLED */
#define SQL_MARS_ENABLED_NO                 0L
#define SQL_MARS_ENABLED_YES                1L
/* SQL_TXN_ISOLATION_OPTION bitmasks */
#define SQL_TXN_SS_SNAPSHOT                 0x00000020L

/* The following are defines for SQL_CA_SS_COLUMN_SORT_ORDER */
#define SQL_SS_ORDER_UNSPECIFIED            0L
#define SQL_SS_DESCENDING_ORDER             1L
#define SQL_SS_ASCENDING_ORDER              2L
#define SQL_SS_ORDER_DEFAULT                SQL_SS_ORDER_UNSPECIFIED

/*
 * Driver specific SQL data type defines.
 * Microsoft has -150 thru -199 reserved for Microsoft ODBC Driver for SQL Server usage.
 */
#define SQL_SS_VARIANT                      (-150)
#define SQL_SS_UDT                          (-151)
#define SQL_SS_XML                          (-152)
#define SQL_SS_TABLE                        (-153)
#define SQL_SS_TIME2                        (-154)
#define SQL_SS_TIMESTAMPOFFSET              (-155)

/* Local types to be used with SQL_CA_SS_SERVER_TYPE */
#define SQL_SS_TYPE_DEFAULT                         0L
#define SQL_SS_TYPE_SMALLDATETIME                   1L
#define SQL_SS_TYPE_DATETIME                        2L

/* Extended C Types range 4000 and above. Range of -100 thru 200 is reserved by Driver Manager. */
#define SQL_C_TYPES_EXTENDED                0x04000L

/*
 * SQL_SS_LENGTH_UNLIMITED is used to describe the max length of
 * VARCHAR(max), VARBINARY(max), NVARCHAR(max), and XML columns
 */
#define SQL_SS_LENGTH_UNLIMITED             0

/*
 * User Data Type definitions.
 * Returned by SQLColAttributes/SQL_CA_SS_COLUMN_UTYPE.
 */
#define SQLudtBINARY                        3
#define SQLudtBIT                           16
#define SQLudtBITN                          0
#define SQLudtCHAR                          1
#define SQLudtDATETIM4                      22
#define SQLudtDATETIME                      12
#define SQLudtDATETIMN                      15
#define SQLudtDECML                         24
#define SQLudtDECMLN                        26
#define SQLudtFLT4                          23
#define SQLudtFLT8                          8
#define SQLudtFLTN                          14
#define SQLudtIMAGE                         20
#define SQLudtINT1                          5
#define SQLudtINT2                          6
#define SQLudtINT4                          7
#define SQLudtINTN                          13
#define SQLudtMONEY                         11
#define SQLudtMONEY4                        21
#define SQLudtMONEYN                        17
#define SQLudtNUM                           10
#define SQLudtNUMN                          25
#define SQLudtSYSNAME                       18
#define SQLudtTEXT                          19
#define SQLudtTIMESTAMP                     80
#define SQLudtUNIQUEIDENTIFIER              0
#define SQLudtVARBINARY                     4
#define SQLudtVARCHAR                       2
#define MIN_USER_DATATYPE                   256
/*
 * Aggregate operator types.
 * Returned by SQLColAttributes/SQL_CA_SS_COLUMN_OP.
 */
#define SQLAOPSTDEV                         0x30    /* Standard deviation */
#define SQLAOPSTDEVP                        0x31    /* Standard deviation population */
#define SQLAOPVAR                           0x32    /* Variance */
#define SQLAOPVARP                          0x33    /* Variance population */
#define SQLAOPCNT                           0x4b    /* Count */
#define SQLAOPSUM                           0x4d    /* Sum */
#define SQLAOPAVG                           0x4f    /* Average */
#define SQLAOPMIN                           0x51    /* Min */
#define SQLAOPMAX                           0x52    /* Max */
#define SQLAOPANY                           0x53    /* Any */
#define SQLAOPNOOP                          0x56    /* None */
/*
 * SQLGetDiagField driver specific defines.
 * Microsoft has -1150 thru -1199 reserved for Microsoft ODBC Driver for SQL Server usage.
 */
#define SQL_DIAG_SS_BASE                    (-1150)
#define SQL_DIAG_SS_MSGSTATE                (SQL_DIAG_SS_BASE)
#define SQL_DIAG_SS_SEVERITY                (SQL_DIAG_SS_BASE-1)
#define SQL_DIAG_SS_SRVNAME                 (SQL_DIAG_SS_BASE-2)
#define SQL_DIAG_SS_PROCNAME                (SQL_DIAG_SS_BASE-3)
#define SQL_DIAG_SS_LINE                    (SQL_DIAG_SS_BASE-4)
/*
 * SQLGetDiagField/SQL_DIAG_DYNAMIC_FUNCTION_CODE driver specific defines.
 * Microsoft has -200 thru -299 reserved for Microsoft ODBC Driver for SQL Server usage.
 */
#define SQL_DIAG_DFC_SS_BASE                (-200)
#define SQL_DIAG_DFC_SS_ALTER_DATABASE      (SQL_DIAG_DFC_SS_BASE-0)
#define SQL_DIAG_DFC_SS_CHECKPOINT          (SQL_DIAG_DFC_SS_BASE-1)
#define SQL_DIAG_DFC_SS_CONDITION           (SQL_DIAG_DFC_SS_BASE-2)
#define SQL_DIAG_DFC_SS_CREATE_DATABASE     (SQL_DIAG_DFC_SS_BASE-3)
#define SQL_DIAG_DFC_SS_CREATE_DEFAULT      (SQL_DIAG_DFC_SS_BASE-4)
#define SQL_DIAG_DFC_SS_CREATE_PROCEDURE    (SQL_DIAG_DFC_SS_BASE-5)
#define SQL_DIAG_DFC_SS_CREATE_RULE         (SQL_DIAG_DFC_SS_BASE-6)
#define SQL_DIAG_DFC_SS_CREATE_TRIGGER      (SQL_DIAG_DFC_SS_BASE-7)
#define SQL_DIAG_DFC_SS_CURSOR_DECLARE      (SQL_DIAG_DFC_SS_BASE-8)
#define SQL_DIAG_DFC_SS_CURSOR_OPEN         (SQL_DIAG_DFC_SS_BASE-9)
#define SQL_DIAG_DFC_SS_CURSOR_FETCH        (SQL_DIAG_DFC_SS_BASE-10)
#define SQL_DIAG_DFC_SS_CURSOR_CLOSE        (SQL_DIAG_DFC_SS_BASE-11)
#define SQL_DIAG_DFC_SS_DEALLOCATE_CURSOR   (SQL_DIAG_DFC_SS_BASE-12)
#define SQL_DIAG_DFC_SS_DBCC                (SQL_DIAG_DFC_SS_BASE-13)
#define SQL_DIAG_DFC_SS_DISK                (SQL_DIAG_DFC_SS_BASE-14)
#define SQL_DIAG_DFC_SS_DROP_DATABASE       (SQL_DIAG_DFC_SS_BASE-15)
#define SQL_DIAG_DFC_SS_DROP_DEFAULT        (SQL_DIAG_DFC_SS_BASE-16)
#define SQL_DIAG_DFC_SS_DROP_PROCEDURE      (SQL_DIAG_DFC_SS_BASE-17)
#define SQL_DIAG_DFC_SS_DROP_RULE           (SQL_DIAG_DFC_SS_BASE-18)
#define SQL_DIAG_DFC_SS_DROP_TRIGGER        (SQL_DIAG_DFC_SS_BASE-19)
#define SQL_DIAG_DFC_SS_DUMP_DATABASE       (SQL_DIAG_DFC_SS_BASE-20)
#define SQL_DIAG_DFC_SS_BACKUP_DATABASE     (SQL_DIAG_DFC_SS_BASE-20)
#define SQL_DIAG_DFC_SS_DUMP_TABLE          (SQL_DIAG_DFC_SS_BASE-21)
#define SQL_DIAG_DFC_SS_DUMP_TRANSACTION    (SQL_DIAG_DFC_SS_BASE-22)
#define SQL_DIAG_DFC_SS_BACKUP_TRANSACTION  (SQL_DIAG_DFC_SS_BASE-22)
#define SQL_DIAG_DFC_SS_GOTO                (SQL_DIAG_DFC_SS_BASE-23)
#define SQL_DIAG_DFC_SS_INSERT_BULK         (SQL_DIAG_DFC_SS_BASE-24)
#define SQL_DIAG_DFC_SS_KILL                (SQL_DIAG_DFC_SS_BASE-25)
#define SQL_DIAG_DFC_SS_LOAD_DATABASE       (SQL_DIAG_DFC_SS_BASE-26)
#define SQL_DIAG_DFC_SS_RESTORE_DATABASE    (SQL_DIAG_DFC_SS_BASE-26)
#define SQL_DIAG_DFC_SS_LOAD_HEADERONLY     (SQL_DIAG_DFC_SS_BASE-27)
#define SQL_DIAG_DFC_SS_RESTORE_HEADERONLY  (SQL_DIAG_DFC_SS_BASE-27)
#define SQL_DIAG_DFC_SS_LOAD_TABLE          (SQL_DIAG_DFC_SS_BASE-28)
#define SQL_DIAG_DFC_SS_LOAD_TRANSACTION    (SQL_DIAG_DFC_SS_BASE-29)
#define SQL_DIAG_DFC_SS_RESTORE_TRANSACTION (SQL_DIAG_DFC_SS_BASE-29)
#define SQL_DIAG_DFC_SS_PRINT               (SQL_DIAG_DFC_SS_BASE-30)
#define SQL_DIAG_DFC_SS_RAISERROR           (SQL_DIAG_DFC_SS_BASE-31)
#define SQL_DIAG_DFC_SS_READTEXT            (SQL_DIAG_DFC_SS_BASE-32)
#define SQL_DIAG_DFC_SS_RECONFIGURE         (SQL_DIAG_DFC_SS_BASE-33)
#define SQL_DIAG_DFC_SS_RETURN              (SQL_DIAG_DFC_SS_BASE-34)
#define SQL_DIAG_DFC_SS_SELECT_INTO         (SQL_DIAG_DFC_SS_BASE-35)
#define SQL_DIAG_DFC_SS_SET                 (SQL_DIAG_DFC_SS_BASE-36)
#define SQL_DIAG_DFC_SS_SET_IDENTITY_INSERT (SQL_DIAG_DFC_SS_BASE-37)
#define SQL_DIAG_DFC_SS_SET_ROW_COUNT       (SQL_DIAG_DFC_SS_BASE-38)
#define SQL_DIAG_DFC_SS_SET_STATISTICS      (SQL_DIAG_DFC_SS_BASE-39)
#define SQL_DIAG_DFC_SS_SET_TEXTSIZE        (SQL_DIAG_DFC_SS_BASE-40)
#define SQL_DIAG_DFC_SS_SETUSER             (SQL_DIAG_DFC_SS_BASE-41)
#define SQL_DIAG_DFC_SS_SHUTDOWN            (SQL_DIAG_DFC_SS_BASE-42)
#define SQL_DIAG_DFC_SS_TRANS_BEGIN         (SQL_DIAG_DFC_SS_BASE-43)
#define SQL_DIAG_DFC_SS_TRANS_COMMIT        (SQL_DIAG_DFC_SS_BASE-44)
#define SQL_DIAG_DFC_SS_TRANS_PREPARE       (SQL_DIAG_DFC_SS_BASE-45)
#define SQL_DIAG_DFC_SS_TRANS_ROLLBACK      (SQL_DIAG_DFC_SS_BASE-46)
#define SQL_DIAG_DFC_SS_TRANS_SAVE          (SQL_DIAG_DFC_SS_BASE-47)
#define SQL_DIAG_DFC_SS_TRUNCATE_TABLE      (SQL_DIAG_DFC_SS_BASE-48)
#define SQL_DIAG_DFC_SS_UPDATE_STATISTICS   (SQL_DIAG_DFC_SS_BASE-49)
#define SQL_DIAG_DFC_SS_UPDATETEXT          (SQL_DIAG_DFC_SS_BASE-50)
#define SQL_DIAG_DFC_SS_USE                 (SQL_DIAG_DFC_SS_BASE-51)
#define SQL_DIAG_DFC_SS_WAITFOR             (SQL_DIAG_DFC_SS_BASE-52)
#define SQL_DIAG_DFC_SS_WRITETEXT           (SQL_DIAG_DFC_SS_BASE-53)
#define SQL_DIAG_DFC_SS_DENY                (SQL_DIAG_DFC_SS_BASE-54)
#define SQL_DIAG_DFC_SS_SET_XCTLVL          (SQL_DIAG_DFC_SS_BASE-55)
#define SQL_DIAG_DFC_SS_MERGE               (SQL_DIAG_DFC_SS_BASE-56)

/* Severity codes for SQL_DIAG_SS_SEVERITY */
#define EX_ANY          0
#define EX_INFO         10
#define EX_MAXISEVERITY EX_INFO
#define EX_MISSING      11
#define EX_TYPE         12
#define EX_DEADLOCK     13
#define EX_PERMIT       14
#define EX_SYNTAX       15
#define EX_USER         16
#define EX_RESOURCE     17
#define EX_INTOK        18
#define MAXUSEVERITY    EX_INTOK
#define EX_LIMIT        19
#define EX_CMDFATAL     20
#define MINFATALERR     EX_CMDFATAL
#define EX_DBFATAL      21
#define EX_TABCORRUPT   22
#define EX_DBCORRUPT    23
#define EX_HARDWARE     24
#define EX_CONTROL      25

#endif /* __msodbcsql_h__ */

