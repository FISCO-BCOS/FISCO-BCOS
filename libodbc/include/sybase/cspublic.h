/*
**	Sybase Open Client/Server 
**	Confidential Property of Sybase, Inc.
**	Copyright  Sybase, Inc. 1992 - 2012
**	All rights reserved
*/
#ifndef __CSPUBLIC_H__
#define __CSPUBLIC_H__

#include <string.h>

/*
** Include the core header files. These files contain the defines and
** data structures that are shared by all libraries.
*/
#ifndef __NO_INCLUDE__
#include	<cstypes.h>
#include	<sqlca.h>
#endif /* __NO_INCLUDE__ */

/*****************************************************************************
**
** Size defines used in client/server applications.
**
*****************************************************************************/

/*
** Defines for the safe string funtions.
*/
#define CS_STRLCPY	cs_strlcpy
#define CS_STRLCAT  	cs_strlcat
#define CS_SNPRINTF  	cs_snprintf

/*
** Define the maximum size of a fully qualified table name.
*/
#define	CS_OBJ_NAME		(CS_INT)((CS_MAX_NAME * 3) + 4)

/*
** The maximum number of bytes in the server message or error message
** stored in the CS_SERVERMSG and CS_CLIENTMSG structures. If the total
** message is longer than this, multiple structures will be passed to the
** application program.
*/
#define CS_MAX_MSG		(CS_INT)1024

/*
** The maximum number of bytes in a Sybase timestamp.
*/
#define CS_TS_SIZE		(CS_INT)8

/*
** The maximum number of bytes in Sybase text pointers.
*/
#define CS_TP_SIZE		(CS_INT)16

/*
** The size of the sqlstate array in the CS_SERVERMSG and CS_CLIENTMSG
** structures. Please note that this size is 2 bytes greater than what the
** standard specifies. Users should look at only the first 6 bytes in
** the array. That last 2 bytes are reserved to insure modulo 4 byte
** structure alignment.
*/
#define CS_SQLSTATE_SIZE	(CS_INT)8

/*****************************************************************************
**
** Defines passed into Open Client/Server APIs.
**
*****************************************************************************/

/*
** Define all the library versions currently supported. 
** If compile flag 'CS_NO_LARGE_IDENTIFIERS' is set, we need to use
** the old values for CS_VERSION_xxx for compatibility reasons.
** The compile flag 'CS_NO_LARGE_IDENTIFIERS' isn't supported anymore
** as of CS_VERSION_155.
** We want the compiler to return errors when the CS_NO_LARGE_IDENTIFIERS
** flag is used with versions >= CS_VERSION_155
*/
#ifdef CS_NO_LARGE_IDENTIFIERS
#define CS_VERSION_100		(CS_INT)112
#define CS_VERSION_110		(CS_INT)1100
#define CS_VERSION_120		CS_VERSION_110
#define CS_VERSION_125		(CS_INT)12500
#define CS_VERSION_150		(CS_INT)15000
#else
#define CS_VERSION_100          (CS_INT)113
#define CS_VERSION_110          (CS_INT)1101
#define CS_VERSION_120          CS_VERSION_110
#define CS_VERSION_125          (CS_INT)12501
#define CS_VERSION_150          (CS_INT)15001
#define CS_VERSION_155		(CS_INT)15501
#define CS_VERSION_157		(CS_INT)15701
#endif /* CS_NO_LARGE_IDENTIFIERS */

#define CS_CURRENT_VERSION	CS_VERSION_157

/*
** Action flags used.
** 	CS_CACHE currently only used by OMI apis
*/
#define CS_GET			(CS_INT)33
#define CS_SET			(CS_INT)34
#define CS_CLEAR		(CS_INT)35
#define CS_INIT			(CS_INT)36
#define CS_STATUS		(CS_INT)37
#define CS_MSGLIMIT		(CS_INT)38
#define	CS_SEND			(CS_INT)39
#define	CS_SUPPORTED		(CS_INT)40
#define CS_CACHE                (CS_INT)41
 
/*
** Bind indicator values. These are preferred when passing data into
** Client Library or Server Library, since they add the appropriate cast.
*/
#define CS_GOODDATA		(CS_SMALLINT)0
#define CS_NULLDATA		(CS_SMALLINT)(-1)

/*
** Define ct_debug() operations.
*/
#define CS_SET_FLAG		(CS_INT)1700
#define CS_CLEAR_FLAG		(CS_INT)1701
#define CS_SET_DBG_FILE		(CS_INT)1702
#define CS_SET_PROTOCOL_FILE	(CS_INT)1703

/*
** Define ct_debug() types of trace information.
*/
#define CS_DBG_ALL		(CS_INT)0x1
#define CS_DBG_ASYNC		(CS_INT)0x2
#define CS_DBG_ERROR		(CS_INT)0x4
#define CS_DBG_MEM		(CS_INT)0x8
#define CS_DBG_PROTOCOL		(CS_INT)0x10
#define CS_DBG_PROTOCOL_STATES	(CS_INT)0x20
#define CS_DBG_API_STATES	(CS_INT)0x40
#define CS_DBG_DIAG		(CS_INT)0x80
#define CS_DBG_NETWORK		(CS_INT)0x100
#define CS_DBG_API_LOGCALL	(CS_INT)0x200
#define	CS_DBG_CTL_INFO		(CS_INT)0x400
#define	CS_DBG_SSL		(CS_INT)0x800

/*
** Cancel types.
*/
#define CS_CANCEL_CURRENT	(CS_INT)6000
#define CS_CANCEL_ALL		(CS_INT)6001
#define CS_CANCEL_ATTN		(CS_INT)6002
#define CS_CANCEL_ABORT_NOTIF	(CS_INT)6003

/*
** Cursor fetch options. 
*/
#define CS_FIRST		(CS_INT)3000
#define CS_NEXT			(CS_INT)3001
#define CS_PREV			(CS_INT)3002
#define CS_LAST			(CS_INT)3003
#define CS_ABSOLUTE		(CS_INT)3004
#define CS_RELATIVE		(CS_INT)3005

/*
** Op codes used in cs_calc().
*/
#define CS_ADD			(CS_INT)1
#define CS_SUB			(CS_INT)2
#define CS_MULT			(CS_INT)3
#define CS_DIV			(CS_INT)4
#define CS_ZERO			(CS_INT)5

/*
** The cs_dt_info() types.
*/
#define CS_MONTH		(CS_INT)7340
#define CS_SHORTMONTH		(CS_INT)7341
#define CS_DAYNAME		(CS_INT)7342
#define CS_DATEORDER		(CS_INT)7343
#define CS_12HOUR		(CS_INT)7344
#define CS_DT_CONVFMT		(CS_INT)7345

/*
** The cs_strcmp() options.
*/
#define CS_COMPARE		(CS_INT)7440
#define CS_SORT			(CS_INT)7441

/*
** Callback types.
*/
#define CS_COMPLETION_CB	(CS_INT)1
#define CS_SERVERMSG_CB		(CS_INT)2
#define CS_CLIENTMSG_CB		(CS_INT)3
#define CS_NOTIF_CB		(CS_INT)4
#define CS_ENCRYPT_CB		(CS_INT)5
#define CS_CHALLENGE_CB		(CS_INT)6
#define CS_DS_LOOKUP_CB		(CS_INT)7
#define CS_SECSESSION_CB	(CS_INT)8
#define	CS_SSLVALIDATE_CB	(CS_INT)9
#define	CS_DONEPROC_CB		(CS_INT)10
#define CS_EXTENDED_ENCRYPT_CB	(CS_INT)11

/*
** To install a signal callback, the type needs to calculated as an
** offset of the operating-system-specific signal number and the
** following define. For example, to install a callback handler for a
** SIGALRM signal, pass (CS_SIGNAL_CB + SIGALRM) to the ct_callback()
** routine.
*/
#define CS_SIGNAL_CB		(CS_INT)100

/*
** Exit and close flags.
*/
#define CS_FORCE_EXIT		(CS_INT)300
#define CS_FORCE_CLOSE		(CS_INT)301

/*
** ct_diag() and cs_diag() type flags.
*/
#define CS_CLIENTMSG_TYPE	(CS_INT)4700
#define CS_SERVERMSG_TYPE	(CS_INT)4701
#define CS_ALLMSG_TYPE		(CS_INT)4702
#define SQLCA_TYPE		(CS_INT)4703
#define SQLCODE_TYPE		(CS_INT)4704
#define SQLSTATE_TYPE		(CS_INT)4705

/*
** Compute info types.
*/
#define CS_COMP_OP		(CS_INT)5350
#define CS_COMP_ID		(CS_INT)5351
#define CS_COMP_COLID		(CS_INT)5352
#define CS_COMP_BYLIST		(CS_INT)5353
#define CS_BYLIST_LEN		(CS_INT)5354

/*
** Compute info operators.
*/
#define CS_OP_SUM		(CS_INT)5370
#define CS_OP_AVG		(CS_INT)5371
#define CS_OP_COUNT		(CS_INT)5372
#define CS_OP_MIN		(CS_INT)5373
#define CS_OP_MAX		(CS_INT)5374

/*
** Browse types.
*/
#define CS_ISBROWSE		(CS_INT)9000
#define CS_TABNUM		(CS_INT)9001
#define CS_TABNAME		(CS_INT)9002

/*
** Result types from ct_results().
*/
#define CS_ROW_RESULT		(CS_INT)4040
#define CS_CURSOR_RESULT	(CS_INT)4041
#define CS_PARAM_RESULT		(CS_INT)4042
#define CS_STATUS_RESULT	(CS_INT)4043
#define CS_MSG_RESULT		(CS_INT)4044
#define CS_COMPUTE_RESULT	(CS_INT)4045
#define CS_CMD_DONE		(CS_INT)4046
#define CS_CMD_SUCCEED		(CS_INT)4047
#define CS_CMD_FAIL		(CS_INT)4048
#define CS_ROWFMT_RESULT	(CS_INT)4049
#define CS_COMPUTEFMT_RESULT	(CS_INT)4050
#define CS_DESCRIBE_RESULT	(CS_INT)4051

/*
** Flags for getting result info using ct_res_info().
*/
#define CS_ROW_COUNT		(CS_INT)800
#define CS_CMD_NUMBER		(CS_INT)801
#define CS_NUM_COMPUTES		(CS_INT)802
#define CS_NUMDATA		(CS_INT)803
#define CS_ORDERBY_COLS		(CS_INT)804
#define CS_NUMORDERCOLS		(CS_INT)805
#define CS_MSGTYPE		(CS_INT)806
#define CS_BROWSE_INFO		(CS_INT)807
#define CS_TRANS_STATE		(CS_INT)808
#define CS_TOKEN_TYPE		(CS_INT)809

/*
** Possible values for CS_TRANS_STATE.
*/
#define CS_TRAN_UNDEFINED	(CS_INT)0
#define CS_TRAN_IN_PROGRESS	(CS_INT)1
#define CS_TRAN_COMPLETED	(CS_INT)2
#define CS_TRAN_FAIL		(CS_INT)3
#define CS_TRAN_STMT_FAIL	(CS_INT)4

/*
** Define the invalid count that the application gets when
** ct_res_info() is called at the wrong time.
*/
#define	CS_NO_COUNT		(CS_INT)-1

/*****************************************************************************
**
** Commands in Open Client/Server APIs.
**
*****************************************************************************/

/*
** ct_command() command types.
*/
#define CS_LANG_CMD		(CS_INT)148
#define CS_RPC_CMD		(CS_INT)149
#define CS_MSG_CMD		(CS_INT)150
#define CS_SEND_DATA_CMD	(CS_INT)151
#define CS_PACKAGE_CMD		(CS_INT)152
#define CS_SEND_BULK_CMD	(CS_INT)153
#define CS_SEND_DATA_NOCMD	(CS_INT)154

/*
** ct_cursor() command types.
*/
#define CS_CURSOR_DECLARE	(CS_INT)700
#define CS_CURSOR_OPEN		(CS_INT)701
#define CS_CURSOR_ROWS		(CS_INT)703
#define CS_CURSOR_UPDATE	(CS_INT)704
#define CS_CURSOR_DELETE	(CS_INT)705
#define CS_CURSOR_CLOSE		(CS_INT)706
#define CS_CURSOR_DEALLOC	(CS_INT)707
#define CS_CURSOR_OPTION	(CS_INT)725

/*
** Open Server-specific cursor command types.
*/
#define CS_CURSOR_FETCH		(CS_INT)708
#define CS_CURSOR_INFO		(CS_INT)709

/*
** ct_dyndesc() command types.
*/
#define CS_ALLOC		(CS_INT)710
#define CS_DEALLOC		(CS_INT)711
#define CS_USE_DESC		(CS_INT)712
#define CS_GETCNT		(CS_INT)713
#define CS_SETCNT		(CS_INT)714
#define CS_GETATTR		(CS_INT)715
#define CS_SETATTR		(CS_INT)716

/*
** ct_dynamic() command types.
*/
#define CS_PREPARE		(CS_INT)717
#define CS_EXECUTE		(CS_INT)718
#define CS_EXEC_IMMEDIATE	(CS_INT)719
#define CS_DESCRIBE_INPUT	(CS_INT)720
#define CS_DESCRIBE_INPUTIN	(CS_INT)320 /* CS_DESCRIBE_INPUT & CS_INPUT */
#define CS_DESCRIBE_OUTPUT	(CS_INT)721
#define CS_DYN_CURSOR_DECLARE	(CS_INT)722


/*
** ct_dynsqlda() arguments and actions
*/
#define CS_SQLDA_SYBASE		(CS_INT)729
#define CS_GET_IN		(CS_INT)730
#define CS_GET_OUT		(CS_INT)731
#define CS_SQLDA_BIND		(CS_INT)732
#define CS_SQLDA_PARAM		(CS_INT)733

/*
** Open Server-specific dynamic command types.
*/
#define CS_PROCNAME		(CS_INT)723
#define CS_ACK			(CS_INT)724


/*
** ct_ds_objinfo() objinfo types.
*/
#define CS_OBJ_CLASSOID		(CS_INT)725
#define CS_OBJ_DNAME		(CS_INT)726
#define CS_OBJ_NUMATTR		(CS_INT)727
#define CS_OBJ_ATRRIBUTE	(CS_INT)728

/*
** Command options
*/
#define CS_RECOMPILE		(CS_INT)188
#define CS_NO_RECOMPILE		(CS_INT)189
#define CS_BULK_INIT		(CS_INT)190
#define CS_BULK_CONT		(CS_INT)191
#define CS_BULK_DATA		(CS_INT)192
#define CS_COLUMN_DATA		(CS_INT)193

/*
** Cursor options.
*/
#define CS_FOR_UPDATE		(CS_INT)0x0001
#define CS_READ_ONLY		(CS_INT)0x0002
#define CS_DYNAMIC		(CS_INT)0x0004	/* Open Server only */
#define CS_RESTORE_OPEN		(CS_INT)0x0008	/* CT-Lib only */
#define CS_MORE			(CS_INT)0x0010
#define CS_END			(CS_INT)0x0020
#define CS_IMPLICIT_CURSOR	(CS_INT)0x0040	/* Implicit, CT-Lib */
#define CS_SCROLL_SENSITIVE	(CS_INT)0x0080	/* Scrollable cursor, reserved */
#define CS_SCROLL_INSENSITIVE	(CS_INT)0x0100	/* Scrollable cursor, as default */
#define CS_SCROLL_SEMISENSITIVE	(CS_INT)0x0200	/* Scrollable cursor */
#define CS_SCROLL_KEYSETDRIVEN	(CS_INT)0x0400	/* Scrollable cursor, reserved */
#define CS_SCROLL_CURSOR	(CS_INT)0x0800  /* "Default", internal */
#define CS_NOSCROLL_INSENSITIVE	(CS_INT)0x1000	/* Non-scroll insensitive */
#define CS_CUR_RELLOCKS_ONCLOSE	(CS_INT)0x2000	/* Release locks on curclose */

/*
** Sybase-defined message ids for CS_MSG_CMDs.
*/
#define CS_MSG_GETLABELS	(CS_INT)6
#define CS_MSG_LABELS		(CS_INT)7
#define CS_MSG_TABLENAME	(CS_INT)8
#define CS_PARSE_TREE		(CS_INT)8710

/*
** Minimum and maximum user-defined message id for CS_MSG_CMDs.
*/
#define CS_USER_MSGID		(CS_INT)32768
#define CS_USER_MAX_MSGID	(CS_INT)65535

/*
** Defines for sp_regwatch registered procedure
*/
#define CS_NOTIFY_ONCE          0x0002 /* one-time notification request. */
#define CS_NOTIFY_ALWAYS        0x0004 /* permanent notification request. */
#define CS_NOTIFY_WAIT          0x0020 /* blocking notification request. */
#define CS_NOTIFY_NOWAIT        0x0040 /* non-blocking notification request. */


/*****************************************************************************
**
** Open Client properties.
**
*****************************************************************************/

/*
** Properties that are used in *_props() functions.
*/
#define CS_USERNAME		(CS_INT)9100
#define CS_PASSWORD		(CS_INT)9101
#define CS_APPNAME		(CS_INT)9102
#define CS_HOSTNAME		(CS_INT)9103
#define CS_LOGIN_STATUS		(CS_INT)9104
#define CS_TDS_VERSION		(CS_INT)9105
#define CS_CHARSETCNV		(CS_INT)9106
#define CS_PACKETSIZE		(CS_INT)9107
#define CS_USERDATA		(CS_INT)9108
#define CS_COMMBLOCK		(CS_INT)9109
#define CS_NETIO		(CS_INT)9110
#define CS_NOINTERRUPT		(CS_INT)9111
#define CS_TEXTLIMIT		(CS_INT)9112
#define CS_HIDDEN_KEYS		(CS_INT)9113
#define CS_VERSION		(CS_INT)9114
#define CS_IFILE		(CS_INT)9115
#define CS_LOGIN_TIMEOUT	(CS_INT)9116
#define CS_TIMEOUT		(CS_INT)9117
#define CS_MAX_CONNECT		(CS_INT)9118
#define CS_MESSAGE_CB		(CS_INT)9119
#define CS_EXPOSE_FMTS		(CS_INT)9120
#define CS_EXTRA_INF		(CS_INT)9121
#define CS_TRANSACTION_NAME	(CS_INT)9122
#define CS_ANSI_BINDS		(CS_INT)9123
#define CS_BULK_LOGIN		(CS_INT)9124
#define CS_LOC_PROP		(CS_INT)9125
#define CS_CUR_STATUS		(CS_INT)9126
#define CS_CUR_ID		(CS_INT)9127
#define CS_CUR_NAME		(CS_INT)9128
#define CS_CUR_ROWCOUNT		(CS_INT)9129
#define CS_PARENT_HANDLE	(CS_INT)9130
#define CS_EED_CMD		(CS_INT)9131
#define CS_DIAG_TIMEOUT		(CS_INT)9132
#define CS_DISABLE_POLL		(CS_INT)9133
#define CS_NOTIF_CMD		(CS_INT)9134
#define CS_SEC_ENCRYPTION	(CS_INT)9135
#define CS_SEC_CHALLENGE	(CS_INT)9136
#define CS_SEC_NEGOTIATE	(CS_INT)9137
#define CS_MEM_POOL		(CS_INT)9138
#define CS_USER_ALLOC		(CS_INT)9139
#define CS_USER_FREE		(CS_INT)9140
#define CS_ENDPOINT		(CS_INT)9141
#define CS_NO_TRUNCATE		(CS_INT)9142
#define CS_CON_STATUS		(CS_INT)9143
#define CS_VER_STRING		(CS_INT)9144
#define CS_ASYNC_NOTIFS		(CS_INT)9145
#define CS_SERVERNAME		(CS_INT)9146
/* For CS_SERVERADDR (9206) see below */
#define CS_THREAD_RESOURCE	(CS_INT)9147
#define CS_NOAPI_CHK		(CS_INT)9148
#define CS_SEC_APPDEFINED	(CS_INT)9149
#define CS_NOCHARSETCNV_REQD	(CS_INT)9150
#define CS_STICKY_BINDS		(CS_INT)9151
#define CS_HAVE_CMD		(CS_INT)9152
#define CS_HAVE_BINDS		(CS_INT)9153
#define CS_HAVE_CUROPEN		(CS_INT)9154
#define CS_EXTERNAL_CONFIG	(CS_INT)9155
#define CS_CONFIG_FILE		(CS_INT)9156
#define CS_CONFIG_BY_SERVERNAME	(CS_INT)9157
 
/*
** Directory Service connection properties
*/
#define CS_DS_CHAIN		(CS_INT)9158
#define CS_DS_EXPANDALIAS	(CS_INT)9159
#define CS_DS_COPY		(CS_INT)9160
#define CS_DS_LOCALSCOPE	(CS_INT)9161
#define CS_DS_PREFERCHAIN	(CS_INT)9162
#define CS_DS_SCOPE		(CS_INT)9163
#define CS_DS_SIZELIMIT		(CS_INT)9164
#define CS_DS_TIMELIMIT		(CS_INT)9165
#define CS_DS_PRINCIPAL		(CS_INT)9166
/* For CS_DS_PASSWORD (9198) see below */
#define CS_DS_REFERRAL		(CS_INT)9167
#define CS_DS_SEARCH		(CS_INT)9168
#define CS_DS_DITBASE		(CS_INT)9169
#define CS_DS_FAILOVER		(CS_INT)9170
#define CS_NET_TRANADDR		(CS_INT)9171
#define CS_DS_PROVIDER 		(CS_INT)9172
#define CS_RETRY_COUNT		(CS_INT)9173
#define CS_LOOP_DELAY 		(CS_INT)9174

/*
** Properties for Security services support 
*/
#define CS_SEC_NETWORKAUTH	(CS_INT)9175
#define CS_SEC_DELEGATION	(CS_INT)9176
#define CS_SEC_MUTUALAUTH	(CS_INT)9177
#define CS_SEC_INTEGRITY	(CS_INT)9178
#define CS_SEC_CONFIDENTIALITY	(CS_INT)9179
#define CS_SEC_CREDTIMEOUT	(CS_INT)9180
#define CS_SEC_SESSTIMEOUT	(CS_INT)9181
#define CS_SEC_DETECTREPLAY	(CS_INT)9182
#define CS_SEC_DETECTSEQ	(CS_INT)9183 
#define CS_SEC_DATAORIGIN	(CS_INT)9184
#define CS_SEC_MECHANISM	(CS_INT)9185
#define CS_SEC_CREDENTIALS	(CS_INT)9186
#define CS_SEC_CHANBIND		(CS_INT)9187
#define	CS_SEC_SERVERPRINCIPAL	(CS_INT)9188
#define CS_SEC_KEYTAB		(CS_INT)9189

/*
** More properties
*/
#define CS_ABORTCHK_INTERVAL	(CS_INT)9190
#define CS_LOGIN_TYPE		(CS_INT)9191
#define CS_CON_KEEPALIVE	(CS_INT)9192
#define CS_CON_TCP_NODELAY	(CS_INT)9193
#define CS_LOGIN_REMOTE_SERVER	(CS_INT)9194
#define CS_LOGIN_REMOTE_PASSWD	(CS_INT)9195

/*
** Property for reverting to behavior of earlier versions
*/
#define CS_BEHAVIOR		(CS_INT)9197

/*
** Property for HA failover 
*/
#define CS_HAFAILOVER        	(CS_INT)9196

/*
** Property for Directory services. (belongs with CS_DS_* above) 
** Added at LDAP implementation time.
*/
#define CS_DS_PASSWORD		(CS_INT)9198

/*
** Property for blklib user data.
*/
#define CS_BLKDATA		(CS_INT)9199

/*
** Properties for SSL
*/
#define CS_PROP_SSL_PROTOVERSION	(CS_INT)9200
#define CS_PROP_SSL_CIPHER		(CS_INT)9201
#define CS_PROP_SSL_LOCALID		(CS_INT)9202
#define CS_PROP_SSL_CA			(CS_INT)9203
#define	CS_PROP_TLS_KEYREGEN		(CS_INT)9205

/*
** Property for connecting to ASE using host and port.
*/
#define CS_SERVERADDR		(CS_INT)9206

/*
** Properties related to use of login redirection and
** Cluster HA extensions.
*/
#define CS_PROP_REDIRECT		(CS_INT)9207
#define CS_PROP_EXTENDEDFAILOVER	(CS_INT)9208
#define CS_DS_RAND_OFFSET		(CS_INT)9209

/*
** Property used to retrieve the ASE database SPID
** that is returned at login time. Note that this value
** may change should migration occur!
*/
#define	CS_PROP_APPLICATION_SPID	(CS_INT)9210

/* 
** For ct_cmd_props(), scrollable cursor.
** CS_CUR_ROWPOSITION is reserved for future use.
*/
#define CS_CUR_TOTALROWS	(CS_INT)9211
#define CS_CUR_ROWPOSITION	(CS_INT)9212

/*
** For extended password encryption
*/
#define CS_SEC_EXTENDED_ENCRYPTION	(CS_INT)9213
#define CS_SEC_NON_ENCRYPTION_RETRY	(CS_INT)9214
 
/*
** For connection migration
*/
#define	CS_PROP_MIGRATABLE	(CS_INT)9215

/*
** For setting sybase home and an alternate libtcl config file location.
*/
#define CS_SYBASE_HOME		(CS_INT)9216
#define CS_LIBTCL_CFG		(CS_INT)9217

/*
** For setting default interfaces file
*/
#define CS_DEFAULT_IFILE	(CS_INT)9218

/*
** To retrieve the CS_TRANADDR ct_connect successfully used to
** establish communication with the server.
*/
#define CS_CONNECTED_ADDR	(CS_INT)9219

/*
** The connection allows partial text/image updates.
*/
#define CS_PARTIAL_TEXT      	(CS_INT)9220

/*
** The connection omits 'writetext' with ct_send_data().
*/
#define CS_SENDDATA_NOCMD      	(CS_INT)9221

/*
** When the application name was set to the executable name, this name
** will be used to parse ocs.cfg for its application specific section.
*/
#define CS_USE_DISCOVERED_APPNAME      	(CS_INT)9222

/*
** When the application name was set to the executable name, this setting
** will indicate how to 'sanitize' that name: all upper case, all lower case
** or no sanitization.
*/
#define CS_SANITIZE_DISC_APPNAME      	(CS_INT)9223

/*
** Allow an application to set the TCP send and receive buffer sizes.
** These are negotiated at the TCP layer and must be set before calling
** ct_connect.
*/
#define CS_TCP_RCVBUF      	(CS_INT)9224
#define CS_TCP_SNDBUF      	(CS_INT)9225
#define CS_TCP_LRGBUF		2146304

/*
** Allow row format information to be suppressed on subsequent execution
** of a prepared statement.
*/
#define CS_CMD_SUPPRESS_FMT	(CS_INT)9226

/*
** CS_DS_SCOPE Values
*/
#define CS_SCOPE_COUNTRY	(CS_INT)1
#define CS_SCOPE_DMD		(CS_INT)2
#define CS_SCOPE_WORLD		(CS_INT)3

/*
** CS_DS_SEARCH Values
*/
#define CS_SEARCH_OBJECT	(CS_INT)1
#define CS_SEARCH_ONE_LEVEL	(CS_INT)2
#define CS_SEARCH_SUBTREE	(CS_INT)3
 
/*
** Possible values for the CS_NETIO property.
*/
#define CS_SYNC_IO		(CS_INT)8111
#define CS_ASYNC_IO		(CS_INT)8112
#define CS_DEFER_IO		(CS_INT)8113

/*
** Possible bit values for the CS_LOGIN_STATUS property.
*/
#define CS_CONSTAT_CONNECTED	(CS_INT)0x1
#define CS_CONSTAT_DEAD		(CS_INT)0x2


/*
** Possible bit values for the CS_CUR_STATUS property.
*/
#define CS_CURSTAT_NONE			(CS_INT)0x0000
#define CS_CURSTAT_DECLARED		(CS_INT)0x0001
#define CS_CURSTAT_OPEN			(CS_INT)0x0002
#define CS_CURSTAT_CLOSED		(CS_INT)0x0004
#define CS_CURSTAT_RDONLY		(CS_INT)0x0008
#define CS_CURSTAT_UPDATABLE		(CS_INT)0x0010
#define CS_CURSTAT_ROWCOUNT		(CS_INT)0x0020
#define CS_CURSTAT_DEALLOC		(CS_INT)0x0040
#define CS_CURSTAT_SCROLLABLE		(CS_INT)0x0080
#define CS_CURSTAT_IMPLICIT		(CS_INT)0x0100
#define CS_CURSTAT_SENSITIVE		(CS_INT)0x0200
#define CS_CURSTAT_INSENSITIVE		(CS_INT)0x0400
#define CS_CURSTAT_SEMISENSITIVE	(CS_INT)0x0800
#define CS_CURSTAT_KEYSETDRIVEN		(CS_INT)0x1000
#define CS_CURSTAT_RELLOCKS_ONCLOSE	(CS_INT)0x2000

/* 
** Possible bit values for implicit cursor status
*/
#define CS_IMPCURSTAT_NONE		(CS_INT)0x0000
#define CS_IMPCURSTAT_DECLARED 		(CS_INT)0x0001
#define CS_IMPCURSTAT_READROWS		(CS_INT)0x0002
#define CS_IMPCURSTAT_CLOSED		(CS_INT)0x0004
#define CS_IMPCURSTAT_SENDSUCCESS	(CS_INT)0x0008
#define CS_IMPCURSTAT_FINALREAD		(CS_INT)0x0010
#define CS_IMPCURSTAT_NOSEND		(CS_INT)0x0020
#define CS_IMPCURSTAT_NOSENDDONE	(CS_INT)0x0040

/*
** Possible values for the CS_TDS_VERSION property.
*/
#define CS_TDS_40		(CS_INT)7360
#define CS_TDS_42		(CS_INT)7361
#define CS_TDS_46		(CS_INT)7362
#define CS_TDS_495		(CS_INT)7363
#define CS_TDS_50		(CS_INT)7364

/*
** Possible values for the CS_BEHAVIOR property.
*/
#define CS_BEHAVIOR_080		(CS_INT)7369 
#define CS_BEHAVIOR_100		(CS_INT)7370
#define CS_BEHAVIOR_110		(CS_INT)7371
#define CS_BEHAVIOR_120		(CS_INT)7372
#define CS_BEHAVIOR_125		(CS_INT)7373

/*
** Possible values for the CS_PROP_SSL_PROTOVERSION property.
**
** If 2.0 handshake is desired with SSL 3.0 or TLS 1.0, or
** CS_SSLVER_20HAND
*/
#define	CS_SSLVER_20		(CS_INT)1
#define	CS_SSLVER_30		(CS_INT)2
#define	CS_SSLVER_TLS1		(CS_INT)3
#define	CS_SSLVER_20HAND	(CS_INT)0x80000000

/*
** Valid values for the CS_SANITIZE_DISC_APPNAME property.
*/
typedef enum cs_sanitize_conversion
{
	CS_CNVRT_NOTHING,
	CS_CNVRT_LOWERCASE,
	CS_CNVRT_UPPERCASE
} CS_SANITIZE_CONVERSION;

/*
** Structure used with the CS_PROP_SSL_LOCALID property.
*/
typedef struct _cs_sslid
{
	CS_CHAR	*identity_file;
	CS_CHAR	*identity_password;
} CS_SSLIDENTITY;

/*
** Structure used to deal with certificates in the validate callback.
*/
typedef struct _cs_sslcertfield
{
	CS_VOID		*value;
	CS_INT		 field_id;
	CS_INT		 length;
} CS_SSLCERT_FIELD;
typedef struct _cs_sslcert
{
	CS_INT			 field_count;
	CS_INT			 extension_count;
	CS_UINT			 start_date;
	CS_UINT			 end_date;
	CS_SSLCERT_FIELD	*fieldptr;
	CS_SSLCERT_FIELD	*extensionptr;
} CS_SSLCERT;

typedef CS_INT (CS_PUBLIC * CS_CERT_CB) (
	CS_VOID *user_data,
	CS_SSLCERT *certptr,
	CS_INT cert_count,
	CS_INT valid
	);

/*
** Defines for valid certificate fields.
*/
#define	CS_SSLFLD_noMatch				0
#define	CS_SSLFLD_md2					1
#define	CS_SSLFLD_md4					2
#define	CS_SSLFLD_md5					3
#define	CS_SSLFLD_sha1					4
#define	CS_SSLFLD_rsaEncryption				5
#define	CS_SSLFLD_md2WithRSA				6
#define	CS_SSLFLD_md4WithRSA				7
#define	CS_SSLFLD_md5WithRSA				8
#define	CS_SSLFLD_sha1WithRSA				9
#define	CS_SSLFLD_dhKeyAgreement			10
#define	CS_SSLFLD_pbeWithMD2AndDES_CBC			11
#define	CS_SSLFLD_pbeWithMD5AndDES_CBC			12
#define	CS_SSLFLD_emailAddress				13
#define	CS_SSLFLD_unstructuredName			14
#define	CS_SSLFLD_contentType				15
#define	CS_SSLFLD_messageDigest				16
#define	CS_SSLFLD_signingTime				17
#define	CS_SSLFLD_counterSignature			18
#define	CS_SSLFLD_challengePassword			19
#define	CS_SSLFLD_unstructuredAddress			20
#define	CS_SSLFLD_extendedCertificateAttributes		21
#define	CS_SSLFLD_commonName				22
#define	CS_SSLFLD_surName				23
#define	CS_SSLFLD_serialNumber				24
#define	CS_SSLFLD_countryName				25
#define	CS_SSLFLD_localityName				26
#define	CS_SSLFLD_stateProvinceName			27
#define	CS_SSLFLD_streetAddress				28
#define	CS_SSLFLD_organizationName			29
#define	CS_SSLFLD_organizationalUnitName		30
#define	CS_SSLFLD_title					31
#define	CS_SSLFLD_description				32
#define	CS_SSLFLD_businessCategory			33
#define	CS_SSLFLD_postalAddress				34
#define	CS_SSLFLD_postalCode				35
#define	CS_SSLFLD_postOfficeBox				36
#define	CS_SSLFLD_physicalDeliveryOfficeName		37
#define	CS_SSLFLD_telephoneNumber			38
#define	CS_SSLFLD_telexNumber				39
#define	CS_SSLFLD_telexTerminalIdentifier		40
#define	CS_SSLFLD_facsimileTelephoneNumber		41
#define	CS_SSLFLD_x_121Address				42
#define	CS_SSLFLD_internationalISDNNumber		43
#define	CS_SSLFLD_registeredAddress			44
#define	CS_SSLFLD_destinationIndicator			45
#define	CS_SSLFLD_preferredDeliveryMethod		46
#define	CS_SSLFLD_presentationAddress			47
#define	CS_SSLFLD_supportedApplicationContext		48
#define	CS_SSLFLD_member				49
#define	CS_SSLFLD_owner					50
#define	CS_SSLFLD_roleOccupant				51
#define	CS_SSLFLD_mysteryPKCS7_PKCS5			52
#define	CS_SSLFLD_netscapeCertType			53
#define	CS_SSLFLD_netscapeBaseURL			54
#define	CS_SSLFLD_netscapeRevocationURL			55
#define	CS_SSLFLD_netscapeCARevocationURL		56
#define	CS_SSLFLD_netscapeCertRenewalURL		57
#define	CS_SSLFLD_netscapeCAPolicyURL			58
#define	CS_SSLFLD_netscapeSSLServerName			59
#define	CS_SSLFLD_netscapeComment			60
#define	CS_SSLFLD_subjectDirectoryAttributes		61
#define	CS_SSLFLD_subjectKeyIdentifier			62
#define	CS_SSLFLD_keyUsage				63
#define	CS_SSLFLD_privateKeyUsagePeriod			64
#define	CS_SSLFLD_subjectAltName			65
#define	CS_SSLFLD_issuerAltName				66
#define	CS_SSLFLD_basicConstraints			67
#define	CS_SSLFLD_crlNumber				68
#define	CS_SSLFLD_crlReason				69
#define	CS_SSLFLD_holdInstructionCode			70
#define	CS_SSLFLD_invalidityDate			71
#define	CS_SSLFLD_deltaCRLIndicator			72
#define	CS_SSLFLD_issuingDistributionPoint		73
#define	CS_SSLFLD_nameConstraints			74
#define	CS_SSLFLD_certificatePolicies			75
#define	CS_SSLFLD_policyMappings			76
#define	CS_SSLFLD_policyConstraints			77
#define	CS_SSLFLD_authorityKeyIdentifier		78
#define	CS_SSLFLD_extendedKeyUsage			79
#define	CS_SSLFLD_pkixSubjectInfoAccess			80
#define	CS_SSLFLD_pkixAuthorityInfoAccess		81
#define	CS_SSLFLD_pkixCPS				82
#define	CS_SSLFLD_pkixUserNotice			83
#define	CS_SSLFLD_pkixKPServerAuth			84
#define	CS_SSLFLD_pkixKPClientAuth			85
#define	CS_SSLFLD_pkixKPCodeSigning			86
#define	CS_SSLFLD_pkixKPEmailProtection			87
#define	CS_SSLFLD_pkixKPIPSECEndSystem			88
#define	CS_SSLFLD_pkixKPIPSECTunnel			89
#define	CS_SSLFLD_pkixKPIPSECUser			90
#define	CS_SSLFLD_pkixKPTimeStamping			91
#define	CS_SSLFLD_netscapeKPStepUp			92
#define	CS_SSLFLD_microsoftKPServerGatedCrypto		93
#define	CS_SSLFLD_sha1WithDSA				94
#define	CS_SSLFLD_dsa					95
#define	CS_SSLFLD_contentInfoData			96
#define	CS_SSLFLD_contentInfoSignedData			97
#define	CS_SSLFLD_contentInfoEnvelopedData		98
#define	CS_SSLFLD_contentInfoSignedAndEnvelopedData	99
#define	CS_SSLFLD_contentInfoDigestedData		100
#define	CS_SSLFLD_contentInfoEncryptedData		101
#define	CS_SSLFLD_keyBag				102
#define	CS_SSLFLD_pkcs8ShroudedKeyBag			103
#define	CS_SSLFLD_certBag				104
#define	CS_SSLFLD_crlBag				105
#define	CS_SSLFLD_secretBag				106
#define	CS_SSLFLD_safeContentBag			107
#define	CS_SSLFLD_pbeWithSHA1And128RC4			108
#define	CS_SSLFLD_pbeWithSHA1And40BitRC4		109
#define	CS_SSLFLD_pbeWithSHA1And3KeyTripleDESCBC	110
#define	CS_SSLFLD_pbeWithSHA1And2KeyTripleDESCBC	111
#define	CS_SSLFLD_pbeWithSHA1And128BitRC2CBC		112
#define	CS_SSLFLD_pbeWithSHA1And40BitRC2CBC		113
#define	CS_SSLFLD_desEDECBC3				114
#define	CS_SSLFLD_rc2CBC				115
#define	CS_SSLFLD_x509Certificate			116
#define	CS_SSLFLD_dhPublicNumber			117
#define	CS_SSLFLD_ecdhPublicNumber			118
#define	CS_SSLFLD_ecdhPrivateNumber			119
#define	CS_SSLFLD_ecPublicKey				120
#define	CS_SSLFLD_ecPrivateKey				121
#define	CS_SSLFLD_ecdsaWithSHA1				122
#define	CS_SSLFLD_ecdsaec163a01				123
#define	CS_SSLFLD_ecdsaec163a02				124
#define	CS_SSLFLD_ecdsaec155b01				125
#define	CS_SSLFLD_ecdsaec163b01				126
#define	CS_SSLFLD_ecdsaec210b01				127
#define	CS_SSLFLD_ecnraec163a01				128
#define	CS_SSLFLD_ecnraec163a02				129
#define	CS_SSLFLD_ecnraec155b01				130
#define	CS_SSLFLD_ecnraec163b01				131
#define	CS_SSLFLD_ecnraec210b01				132
#define	CS_SSLFLD_curve113a01				133
#define	CS_SSLFLD_curve163a01				134
#define	CS_SSLFLD_curve163a02				135
#define	CS_SSLFLD_curve163b01				136

/*
** Define values for SSL/TLS certificate validation checks
*/
#define	CS_SSL_VALID_CERT		0
#define	CS_SSL_INVALID_BADCHAIN		1
#define	CS_SSL_INVALID_EXPCERT		2
#define	CS_SSL_INVALID_INCOMPLETE	3
#define	CS_SSL_INVALID_UNKNOWN		4
#define	CS_SSL_INVALID_UNTRUSTED	5
#define	CS_SSL_INVALID_MISSINGNAME	6
#define	CS_SSL_INVALID_MISMATCHNAME	7
#define	CS_SSL_INVALID_CERT		8

/*****************************************************************************
**
** Open Client/Server options.
**
*****************************************************************************/

/*
** The following is the list of all valid options:
*/
#define CS_OPT_DATEFIRST        (CS_INT)5001    /* Set first day of week */
#define CS_OPT_TEXTSIZE         (CS_INT)5002    /* Text size */
#define CS_OPT_STATS_TIME       (CS_INT)5003    /* Server time statistics */
#define CS_OPT_STATS_IO         (CS_INT)5004    /* Server I/O statistics */
#define CS_OPT_ROWCOUNT         (CS_INT)5005    /* Maximum row count */
#define CS_OPT_NATLANG          (CS_INT)5006    /* National Language */
#define CS_OPT_DATEFORMAT       (CS_INT)5007    /* Date format */
#define CS_OPT_ISOLATION        (CS_INT)5008    /* Transaction isolation
                                                ** level */
#define CS_OPT_AUTHON           (CS_INT)5009    /* Set authority level on */
#define CS_OPT_CHARSET          (CS_INT)5010    /* Character set */
#define CS_OPT_SHOWPLAN         (CS_INT)5013    /* show execution plan    */
#define CS_OPT_NOEXEC           (CS_INT)5014    /* don't execute query */
#define CS_OPT_ARITHIGNORE      (CS_INT)5015    /* ignore arithmetic
                                                ** exceptions */
#define CS_OPT_TRUNCIGNORE      (CS_INT)5016    /* support ANSI null values */
#define CS_OPT_ARITHABORT       (CS_INT)5017    /* abort on arithmetic
                                                ** exceptions */
#define CS_OPT_PARSEONLY        (CS_INT)5018    /* parse only, return error
                                                ** msgs */
#define CS_OPT_GETDATA          (CS_INT)5020    /* return trigger data */
#define CS_OPT_NOCOUNT          (CS_INT)5021    /* don't print done count */
#define CS_OPT_FORCEPLAN        (CS_INT)5023    /* force variable substitute
                                                ** order */
#define CS_OPT_FORMATONLY       (CS_INT)5024    /* send format w/o row */
#define CS_OPT_CHAINXACTS       (CS_INT)5025    /* chained transaction mode */
#define CS_OPT_CURCLOSEONXACT   (CS_INT)5026    /* close cursor on end trans */
#define CS_OPT_FIPSFLAG         (CS_INT)5027    /* FIPS flag */
#define CS_OPT_RESTREES         (CS_INT)5028    /* return resolution trees */
#define CS_OPT_IDENTITYON       (CS_INT)5029    /* turn on explicit identity */
#define CS_OPT_CURREAD       	(CS_INT)5030    /* Set session label @@curread */
#define CS_OPT_CURWRITE       	(CS_INT)5031    /* Set session label @@curwrite */
#define CS_OPT_IDENTITYOFF      (CS_INT)5032    /* turn off explicit identity */
#define CS_OPT_AUTHOFF      	(CS_INT)5033    /* Set authority level off */
#define CS_OPT_ANSINULL      	(CS_INT)5034    /* ANSI NULLS behavior */
#define CS_OPT_QUOTED_IDENT    	(CS_INT)5035    /* Quoted identifiers */
#define CS_OPT_ANSIPERM    	(CS_INT)5036    /* ANSI permission checking */
#define CS_OPT_STR_RTRUNC    	(CS_INT)5037    /* ANSI right truncation */
#define CS_OPT_SORTMERGE	(CS_INT)5038	/* Sort merge join status */
#define	CS_OPT_JTC		(CS_INT)5039	/* Enable/disable JTC for session */
#define	CS_OPT_CLIENTREALNAME	(CS_INT)5040	/* Set client real name */
#define	CS_OPT_CLIENTHOSTNAME	(CS_INT)5041	/* Set client host name */
#define	CS_OPT_CLIENTAPPLNAME	(CS_INT)5042	/* Set client appl name */
#define CS_OPT_IDENTITYUPD_ON	(CS_INT)5043	/* turn on identity update */
#define CS_OPT_IDENTITYUPD_OFF	(CS_INT)5044	/* turn off identity update */
#define CS_OPT_NODATA		(CS_INT)5045	/* turn on/off nodata option */
#define CS_OPT_CIPHERTEXT	(CS_INT)5046	/* turn on/off ciphertext 
						** encryption */
#define CS_OPT_SHOW_FI		(CS_INT)5047	/* Show Functional Indexes */
#define CS_OPT_HIDE_VCC		(CS_INT)5048	/* Hide Virtual Computed Columns */
#define CS_OPT_LOBLOCATOR	(CS_INT)5049	/* Enable/disable Lob locator */
#define CS_OPT_LOBLOCFETCHSIZE	(CS_INT)5050	/* Lob locator fetch size */

#define CS_MIN_OPTION           CS_OPT_DATEFIRST
#define CS_MAX_OPTION           CS_OPT_LOBLOCFETCHSIZE

/*
** The supported options are summarized below with their defined values
** for `ArgSize' and `OptionArg'. ArgSize specifies the domain of valid
** values that are allowed.
** 
** Option			ArgSize		OptionArg
** ---------------		---------	---------
** CS_OPT_DATEFIRST 		1 byte		Defines below
** CS_OPT_TEXTSIZE 		4 bytes		Size in bytes 
** CS_OPT_ROWCOUNT  		4 bytes		Number of rows 
** CS_OPT_NATLANG 		OptionArg Len	National Lang (string)
** CS_OPT_DATEFORMAT 		1 byte		Defines below
** CS_OPT_ISOLATION 		1 byte		Defines below		
** CS_OPT_AUTHON 		OptionArg Len	Table Name (string)
** CS_OPT_CHARSET 		OptionArg Len	Character set (string)
** CS_OPT_IDENTITYON 		OptionArg Len	Table Name (string)
** CS_OPT_CURREAD 		OptionArg Len	Read Label(string)
** CS_OPT_CURWRITE 		OptionArg Len	Write Label(string)
** CS_OPT_IDENTITYOFF 		OptionArg Len	Table Name (string)
** CS_OPT_AUTHOFF 		OptionArg Len	Table Name (string)
** CS_OPT_IDENTITYUPD_ON 	OptionArg Len	Table Name (string)
** CS_OPT_IDENTITYUPD_OFF 	OptionArg Len	Table Name (string)
** (All remaining options)	1 byte		Boolean value
** 
** All string values must be sent in 7 bit ASCII.
**
*/ 


/* CS_OPT_DATEFIRST */
#define CS_OPT_MONDAY		(CS_INT)1
#define CS_OPT_TUESDAY		(CS_INT)2
#define CS_OPT_WEDNESDAY	(CS_INT)3
#define CS_OPT_THURSDAY		(CS_INT)4
#define CS_OPT_FRIDAY		(CS_INT)5
#define CS_OPT_SATURDAY		(CS_INT)6
#define CS_OPT_SUNDAY		(CS_INT)7

/* CS_OPT_DATEFORMAT */
#define CS_OPT_FMTMDY		(CS_INT)1
#define CS_OPT_FMTDMY		(CS_INT)2
#define CS_OPT_FMTYMD		(CS_INT)3
#define CS_OPT_FMTYDM		(CS_INT)4
#define CS_OPT_FMTMYD		(CS_INT)5
#define CS_OPT_FMTDYM		(CS_INT)6

/* CS_OPT_ISOLATION */
#define CS_OPT_LEVEL0		(CS_INT)0
#define CS_OPT_LEVEL1		(CS_INT)1
#define CS_OPT_LEVEL2		(CS_INT)2
#define CS_OPT_LEVEL3		(CS_INT)3

/*****************************************************************************
**
** Open Client/Server capabilities.
**
*****************************************************************************/

/*
** Capability types.
*/
#define CS_CAP_REQUEST		(CS_INT)1
#define CS_CAP_RESPONSE		(CS_INT)2

/*
** Special capability value to set/get all capability values at once.
*/
#define CS_ALL_CAPS		(CS_INT)2700

/*
** Capability request values.
*/
#define CS_REQ_LANG			(CS_INT)1
#define CS_REQ_RPC			(CS_INT)2
#define CS_REQ_NOTIF			(CS_INT)3
#define CS_REQ_MSTMT			(CS_INT)4
#define CS_REQ_BCP			(CS_INT)5
#define CS_REQ_CURSOR			(CS_INT)6
#define CS_REQ_DYN			(CS_INT)7
#define CS_REQ_MSG			(CS_INT)8
#define CS_REQ_PARAM			(CS_INT)9
#define CS_DATA_INT1			(CS_INT)10
#define CS_DATA_INT2			(CS_INT)11
#define CS_DATA_INT4			(CS_INT)12
#define CS_DATA_BIT			(CS_INT)13
#define CS_DATA_CHAR			(CS_INT)14
#define CS_DATA_VCHAR			(CS_INT)15
#define CS_DATA_BIN			(CS_INT)16
#define CS_DATA_VBIN			(CS_INT)17
#define CS_DATA_MNY8			(CS_INT)18
#define CS_DATA_MNY4			(CS_INT)19
#define CS_DATA_DATE8			(CS_INT)20
#define CS_DATA_DATE4			(CS_INT)21
#define CS_DATA_FLT4			(CS_INT)22
#define CS_DATA_FLT8			(CS_INT)23
#define CS_DATA_NUM			(CS_INT)24
#define CS_DATA_TEXT			(CS_INT)25
#define CS_DATA_IMAGE			(CS_INT)26
#define CS_DATA_DEC			(CS_INT)27
#define CS_DATA_LCHAR			(CS_INT)28
#define CS_DATA_LBIN			(CS_INT)29
#define CS_DATA_INTN			(CS_INT)30
#define CS_DATA_DATETIMEN		(CS_INT)31
#define CS_DATA_MONEYN			(CS_INT)32
#define CS_CSR_PREV			(CS_INT)33
#define CS_CSR_FIRST			(CS_INT)34
#define CS_CSR_LAST			(CS_INT)35
#define CS_CSR_ABS			(CS_INT)36
#define CS_CSR_REL			(CS_INT)37
#define CS_CSR_MULTI			(CS_INT)38
#define CS_CON_OOB			(CS_INT)39
#define CS_CON_INBAND			(CS_INT)40
#define CS_CON_LOGICAL			(CS_INT)41
#define CS_PROTO_TEXT			(CS_INT)42
#define CS_PROTO_BULK			(CS_INT)43
#define CS_REQ_URGNOTIF			(CS_INT)44
#define CS_DATA_SENSITIVITY		(CS_INT)45
#define CS_DATA_BOUNDARY		(CS_INT)46
#define CS_PROTO_DYNAMIC		(CS_INT)47
#define CS_PROTO_DYNPROC		(CS_INT)48
#define CS_DATA_FLTN			(CS_INT)49
#define CS_DATA_BITN			(CS_INT)50
#define CS_OPTION_GET			(CS_INT)51
#define CS_DATA_INT8			(CS_INT)52
#define CS_DATA_VOID			(CS_INT)53	
#define CS_DOL_BULK			(CS_INT)54
#define CS_OBJECT_JAVA1			(CS_INT)55
#define CS_OBJECT_CHAR			(CS_INT)56
#define CS_DATA_COLUMNSTATUS		(CS_INT)57
#define CS_OBJECT_BINARY		(CS_INT)58
#define CS_REQ_RESERVED1		(CS_INT)59
#define CS_WIDETABLES			(CS_INT)60
#define CS_REQ_RESERVED2		(CS_INT)61
#define CS_DATA_UINT2			(CS_INT)62
#define CS_DATA_UINT4			(CS_INT)63
#define CS_DATA_UINT8			(CS_INT)64
#define CS_DATA_UINTN			(CS_INT)65
#define CS_CUR_IMPLICIT			(CS_INT)66
#define CS_DATA_UCHAR			(CS_INT)67
#define CS_IMAGE_NCHAR			(CS_INT)68
#define CS_BLOB_NCHAR_16		(CS_INT)69
#define CS_BLOB_NCHAR_8			(CS_INT)70
#define CS_BLOB_NCHAR_SCSU		(CS_INT)71
#define CS_DATA_DATE			(CS_INT)72
#define CS_DATA_TIME			(CS_INT)73
#define CS_DATA_INTERVAL		(CS_INT)74
#define CS_CSR_SCROLL			(CS_INT)75
#define CS_CSR_SENSITIVE		(CS_INT)76
#define CS_CSR_INSENSITIVE		(CS_INT)77
#define CS_CSR_SEMISENSITIVE		(CS_INT)78
#define CS_CSR_KEYSETDRIVEN		(CS_INT)79
#define CS_REQ_SRVPKTSIZE		(CS_INT)80
#define CS_DATA_UNITEXT			(CS_INT)81
#define CS_CAP_EXTENDEDFAILOVER		(CS_INT)82
#define CS_DATA_SINT1			(CS_INT)83
#define CS_REQ_LARGEIDENT		(CS_INT)84
#define CS_REQ_BLOB_NCHAR_16		(CS_INT)85
#define CS_DATA_XML			(CS_INT)86
#define CS_REQ_CURINFO3			(CS_INT)87
#define CS_REQ_DBRPC2			(CS_INT)88
#define CS_REQ_MIGRATE			(CS_INT)89
#define	CS_REQ_UNDEFINED		(CS_INT)90
#define CS_REQ_RESERVED_091		(CS_INT)91
#define CS_REQ_RESERVED_092		(CS_INT)92
#define CS_REQ_RESERVED_093		(CS_INT)93
#define CS_DATA_BIGDATETIME		(CS_INT)94
#define CS_DATA_BIGTIME			(CS_INT)95
#define CS_RPCPARAM_LOB			(CS_INT)96
#define CS_REQ_INSTID			(CS_INT)97
#define CS_REQ_GRID			(CS_INT)98
#define CS_REQ_DYN_BATCH		(CS_INT)99
#define CS_REQ_LANG_BATCH		(CS_INT)100
#define CS_REQ_RPC_BATCH		(CS_INT)101
#define CS_DATA_LOBLOCATOR		(CS_INT)102
#define CS_REQ_RESERVED_103		(CS_INT)103
#define CS_REQ_RESERVED_104		(CS_INT)104
#define CS_REQ_DYNAMIC_SUPPRESS_PARAMFMT (CS_INT)105

/*
** Minimum and maximum request capability values.
*/
#define CS_MIN_REQ_CAP		CS_REQ_LANG
#define CS_MAX_REQ_CAP		CS_REQ_DYNAMIC_SUPPRESS_PARAMFMT

/*
** Capability response values.
*/
#define CS_RES_NOMSG			(CS_INT)1
#define CS_RES_NOEED			(CS_INT)2
#define CS_RES_NOPARAM			(CS_INT)3
#define CS_DATA_NOINT1			(CS_INT)4
#define CS_DATA_NOINT2			(CS_INT)5
#define CS_DATA_NOINT4			(CS_INT)6
#define CS_DATA_NOBIT			(CS_INT)7
#define CS_DATA_NOCHAR			(CS_INT)8
#define CS_DATA_NOVCHAR			(CS_INT)9
#define CS_DATA_NOBIN			(CS_INT)10
#define CS_DATA_NOVBIN			(CS_INT)11
#define CS_DATA_NOMNY8			(CS_INT)12
#define CS_DATA_NOMNY4			(CS_INT)13
#define CS_DATA_NODATE8			(CS_INT)14
#define CS_DATA_NODATE4			(CS_INT)15
#define CS_DATA_NOFLT4			(CS_INT)16
#define CS_DATA_NOFLT8			(CS_INT)17
#define CS_DATA_NONUM			(CS_INT)18
#define CS_DATA_NOTEXT			(CS_INT)19
#define CS_DATA_NOIMAGE			(CS_INT)20
#define CS_DATA_NODEC			(CS_INT)21
#define CS_DATA_NOLCHAR			(CS_INT)22
#define CS_DATA_NOLBIN			(CS_INT)23
#define CS_DATA_NOINTN			(CS_INT)24
#define CS_DATA_NODATETIMEN		(CS_INT)25
#define CS_DATA_NOMONEYN		(CS_INT)26
#define CS_CON_NOOOB			(CS_INT)27
#define CS_CON_NOINBAND			(CS_INT)28
#define CS_PROTO_NOTEXT			(CS_INT)29
#define CS_PROTO_NOBULK			(CS_INT)30
#define CS_DATA_NOSENSITIVITY		(CS_INT)31
#define CS_DATA_NOBOUNDARY		(CS_INT)32
#define CS_RES_NOTDSDEBUG		(CS_INT)33
#define CS_RES_NOSTRIPBLANKS		(CS_INT)34
#define CS_DATA_NOINT8			(CS_INT)35
#define CS_OBJECT_NOJAVA1		(CS_INT)36
#define CS_OBJECT_NOCHAR		(CS_INT)37
#define CS_DATA_NOCOLUMNSTATUS		(CS_INT)38
#define CS_OBJECT_NOBINARY		(CS_INT)39
#define CS_RES_RESERVED			(CS_INT)40
#define CS_DATA_NOUINT2			(CS_INT)41
#define CS_DATA_NOUINT4			(CS_INT)42
#define CS_DATA_NOUINT8			(CS_INT)43
#define CS_DATA_NOUINTN			(CS_INT)44
#define CS_NOWIDETABLES			(CS_INT)45
#define CS_DATA_NOUCHAR			(CS_INT)46
#define CS_IMAGE_NONCHAR		(CS_INT)47
#define CS_BLOB_NONCHAR_16		(CS_INT)48
#define CS_BLOB_NONCHAR_8		(CS_INT)49
#define CS_BLOB_NONCHAR_SCSU		(CS_INT)50
#define CS_DATA_NODATE			(CS_INT)51
#define CS_DATA_NOTIME			(CS_INT)52
#define CS_DATA_NOINTERVAL		(CS_INT)53
#define CS_DATA_NOUNITEXT		(CS_INT)54
#define CS_DATA_NOSINT1			(CS_INT)55
#define CS_NO_LARGEIDENT		(CS_INT)56
#define CS_NO_BLOB_NCHAR_16		(CS_INT)57
#define CS_NO_SRVPKTSIZE		(CS_INT)58
#define CS_DATA_NOXML			(CS_INT)59
#define CS_NONINT_RETURN_VALUE		(CS_INT)60
#define	CS_RES_NOXNLMETADATA		(CS_INT)61
#define	CS_RES_SUPPRESS_FMT		(CS_INT)62
#define	CS_RES_SUPPRESS_DONEINPROC	(CS_INT)63
#define	CS_UNUSED_RES			(CS_INT)64
#define CS_DATA_NOBIGDATETIME		(CS_INT)65
#define CS_DATA_NOBIGTIME		(CS_INT)66
#define CS_RES_NO_TDSCONTROL		(CS_INT)67
#define CS_RPCPARAM_NOLOB		(CS_INT)68
#define CS_DATA_NOLOBLOCATOR		(CS_INT)69
#define CS_RES_RESERVED_70		(CS_INT)70

/*
** Minimum and maximum response capability values.
*/
#define CS_MIN_RES_CAP		CS_RES_NOMSG
#define CS_MAX_RES_CAP		CS_RES_RESERVED_70

/*
** Minimum and maximum of all capabilities defined above.
*/
#define CS_MIN_CAPVALUE		CS_REQ_LANG
#define CS_MAX_CAPVALUE		CS_REQ_RESERVED_103	

/*
** Size of area to store capabilities. The array len must be greater
** than CS_CAP_ARRAYLEN additional capabilities to be added.
*/
#define CS_CAP_ARRAYLEN		16

/*
** Maximum OID length (bytes)
*/
#define CS_MAX_OIDLEN		255

/*
** Index used by access macros so that the first byte in the array will
** contain the high order bit.
*/
#define CS_CAP_IDX(B)		((CS_CAP_ARRAYLEN - (B)/ CS_BITS_PER_BYTE) - 1)

/*
** Data structure defining storage for capabilities.
*/
typedef	struct _cs_cap_type
{
	CS_BYTE		mask[CS_CAP_ARRAYLEN];
} CS_CAP_TYPE;

/*
** Access macros for CS_CAP_TYPE structure.
*/
#define	CS_SET_CAPMASK(M, B)	((M)->mask[CS_CAP_IDX(B)] |= \
					(1 << ((B) % CS_BITS_PER_BYTE)))
#define	CS_CLR_CAPMASK(M, B)	((M)->mask[CS_CAP_IDX(B)] &= \
					~(1 << ((B) % CS_BITS_PER_BYTE)))
#define	CS_TST_CAPMASK(M, B)	((M)->mask[CS_CAP_IDX(B)] & \
					(1 << ((B) % CS_BITS_PER_BYTE)))

/*****************************************************************************
**
** Defines used in Open Client/Server structures.
**
*****************************************************************************/

/*
** Define I/O types in the CS_IODESC structure.
*/
#define CS_IODATA		(CS_INT)1600
#define CS_IOPARTIAL		(CS_INT)1601

/*
** Define status values for the status field of the CS_SERVERMSG and
** CS_CLIENTMSG structures.
*/
#define CS_HASEED		(CS_INT)0x1
#define CS_FIRST_CHUNK		(CS_INT)0x2
#define CS_LAST_CHUNK		(CS_INT)0x4
#define	CS_EEDINFO		(CS_INT)0x8

/*****************************************************************************
**
** Hidden information structures.
**
*****************************************************************************/

#ifndef CS__INTERNAL_STRUCTS

/*
** If passing code through lint, define the hidden structures as void.
*/
#ifdef lint

#ifndef CS_LOGINFO
#define CS_LOGINFO	CS_VOID
#endif /* CS_LOGINFO */

#ifndef CS_BLKDESC
#define CS_BLKDESC      CS_VOID
#endif /* CS_BLKDESC */

#ifndef CS_BLK_ROW
#define CS_BLK_ROW	CS_VOID
#endif /* CS_BLK_ROW */

#else /* lint */

/*
** Use anonymous structure tags to define the hidden structures.
*/
typedef struct _csloginfo	CS_LOGINFO;
typedef struct _cs_blkdesc	CS_BLKDESC;
typedef	struct _bulkrow		CS_BLK_ROW; 

#endif /* lint */

#endif /* CS__INTERNAL_STRUCTS */

/*****************************************************************************
**
** User-accessible information structures.
**
*****************************************************************************/

/*
** Define the I/O descriptor structure used by Open Client/Server.
**
** iotype		Indicates the type of I/O to perform. For text
**			and image operations, iotype always has the
**			value CS_IODATA.
**
** datatype		The datatype of the data object. The only legal
**			values for datatype are CS_TEXT_TYPE and
**			CS_IMAGE_TYPE and CS_XML_TYPE.
**
** *locale		A pointer to a CS_LOCALE structure containing
**		 	localization information for the text or image
**			value. Set locale to NULL if localization
**			information is not required.
**
** usertype		The SQL Server user-defined datatype of the data
**			object, if any.
**
** total_txtlen		The total length, in bytes, of the text or image
**			value.
**
** offset		Reserved for future use.
**
** log_on_update	Whether the update for this text object should
**			be logged or not.
**
** name			The name of the text or image column. name is a
**			string of the form table.column.
**
** namelen		The actual length of name
**
** timestamp	 	The text timestamp of the column. A text
**			timestamp marks the time of a text or image
**			column's last modification.
**
** timestamplen		The length, in bytes, of timestamp.
**
** textptr		The text pointer for the column. A text pointer
**			is an internal server pointer that points to the
**			data for a text or image column. textptr identifies
**			the target column in a send-data operation.
**
** textptrlen		The length, in bytes, of textptr.
*/

typedef struct _cs_iodesc
{
	CS_INT		iotype;
	CS_INT		datatype;
 	CS_LOCALE	*locale;
 	CS_INT		usertype;
	CS_INT		total_txtlen;
	CS_INT		offset;
	CS_BOOL		log_on_update;
	CS_CHAR		name[CS_OBJ_NAME];
 	CS_INT		namelen;
	CS_BYTE		timestamp[CS_TS_SIZE];
	CS_INT		timestamplen;
	CS_BYTE		textptr[CS_TP_SIZE];
	CS_INT		textptrlen;
	CS_INT		delete_length;
} CS_IODESC;

/*
** Define the browse descriptor structure used by Open Client/Server.
**
** status		A bit mask of either CS_EXPRESSION and/or CS_RENAMED. 
**
** isbrowse		CS_TRUE the column can be browse-mode updated.
**
** origname		The original name of the column in the database.
**
** orignlen		Length of origname in bytes.
**
** tablenum		The number of the table to which the column
**			belongs. The first table in a select statement's
**			from-list is table number 1, the second number 2,
**			and so forth.
**
** tablename		The name of the table to which the column belongs.
**
** tabnlen		Length of tablename in bytes.
**
*/
#if defined(CS_NO_LARGE_IDENTIFIERS)
/*
** In pre-15.0 versions the (old) value for CS_MAX_NAME was used for the
** origname array declaration. Starting from version 15.0, CS_MAX_CHAR
** is used to define this character array size.
*/
typedef struct _cs_browsedesc
{
	CS_INT		status;
	CS_BOOL		isbrowse;
	CS_CHAR		origname[CS_MAX_NAME];
	CS_INT		orignlen;
	CS_INT		tablenum;
	CS_CHAR		tablename[CS_OBJ_NAME];
	CS_INT		tabnlen;
} CS_BROWSEDESC;
#else
typedef struct _cs_browsedesc
{
	CS_INT		status;
	CS_BOOL		isbrowse;
	CS_CHAR		origname[CS_MAX_CHAR];
	CS_INT		orignlen;
	CS_INT		tablenum;
	CS_CHAR		tablename[CS_OBJ_NAME];
	CS_INT		tabnlen;
} CS_BROWSEDESC;
#endif

/*
** Define the server message structure used by Open Client/Server.
**
** msgnumber		The server message number.
**
** state		The server error state.
**
** severity		The severity of the message.
**
** text			The text of the error string. If an application
**			is not sequencing messages, text is guaranteed 
**			to be null-terminated, even if it has been
**			truncated. If an application is sequencing
**			messages, text is null-terminated only if it is
**			the last chunk of a sequenced message.
**
** textlen		The length, in bytes, of text.
**
** svrname		The name of the server that generated the message.
**
** svrnlen		The length, in bytes, of svrname.
**
** proc			The name of the stored procedure which caused
**			the message, if any.
**
** proclen		The length, in bytes, of proc.
**
** line			The line number, if any, of the line that caused
**			the message. line can be a line number in a
**			stored procedure or a line number in a command
**			batch.
**
** status		A bitmask used to indicate various types of
**			information, such as whether or not extended
**			error data is included with the message.
**
** sqlstate		SQL state information.
**
** sqlstatelen		The length, in bytes, of sqlstate.
**
*/
#if defined(CS_NO_LARGE_IDENTIFIERS)
/*
** In pre-15.0 versions the (old) value for CS_MAX_NAME was used for the 
** name array declarations. Starting from version 15.0, CS_MAX_CHAR is
** used to define these character array sizes.
*/
typedef struct _cs_servermsg
{
	CS_MSGNUM	msgnumber;
	CS_INT		state;
	CS_INT		severity;
	CS_CHAR		text[CS_MAX_MSG];
	CS_INT		textlen;
	CS_CHAR		svrname[CS_MAX_NAME];
	CS_INT		svrnlen;
	CS_CHAR		proc[CS_MAX_NAME];
	CS_INT		proclen;
	CS_INT		line;
	CS_INT		status;
	CS_BYTE		sqlstate[CS_SQLSTATE_SIZE];
	CS_INT		sqlstatelen;
} CS_SERVERMSG;
#else
typedef struct _cs_servermsg
{
	CS_MSGNUM	msgnumber;
	CS_INT		state;
	CS_INT		severity;
	CS_CHAR		text[CS_MAX_MSG];
	CS_INT		textlen;
	CS_CHAR		svrname[CS_MAX_CHAR];
	CS_INT		svrnlen;
	CS_CHAR		proc[CS_MAX_CHAR];
	CS_INT		proclen;
	CS_INT		line;
	CS_INT		status;
	CS_BYTE		sqlstate[CS_SQLSTATE_SIZE];
	CS_INT		sqlstatelen;
} CS_SERVERMSG;
#endif

/*
** Define the client message structure used by Open Client/Server.
**
** severity		A symbolic value representing the severity of
**			the message.
**
** msgnumber		The message number. For information on how to
**			interpret this number in Client-Library
**			applications, see the Client-Library Messages
**			topics in the Open Client documentation.
**
** msgstring		The message string. If an application is not
**			sequencing messages, msgstring is guaranteed to
**			be null-terminated, even if it has been truncated.
**			If an application is sequencing messages,
**			msgstring is null-terminated only if it is the
**			last chunk of a sequenced message.
**
** msgstringlen		The length, in bytes, of msgstring.
**
** osnumber		An error may have involved interactions the
**			operating system (OS). If so, the OS error
**			number would be stored here. Otherwise,
**			this will be zero.
**
** osstring		The operating system error text (if any).
**
** osstringlen		The length, in bytes, of osstring.
**
** status		A bitmask used to indicate various types of
**			information, such as whether or not this is the
**			first, a middle, or the last chunk of an error
**			message.
**
** sqlstate		SQL state information.
**
** sqlstatelen		The length, in bytes, of sqlstate.
**
*/
typedef struct _cs_clientmsg
{
	CS_INT		severity;
	CS_MSGNUM	msgnumber;
	CS_CHAR		msgstring[CS_MAX_MSG];
	CS_INT		msgstringlen;
	CS_INT		osnumber;
	CS_CHAR		osstring[CS_MAX_MSG];
	CS_INT		osstringlen;
	CS_INT		status;
	CS_BYTE		sqlstate[CS_SQLSTATE_SIZE];
	CS_INT		sqlstatelen;
} CS_CLIENTMSG;

/*
** Define the constants an application tests for in the DONEPROC callback
*/
#define CS_TDS_DONEPROC_TYPE (CS_INT)1
#define CS_TDS_DONEINPROC_TYPE (CS_INT)2

/*****************************************************************************
**
** Define user-accessable functions for Client/Server Library here.
**
*****************************************************************************/

/*
** Declare all functions.
*/
CS_START_EXTERN_C

/* cscalc.c */
extern CS_RETCODE CS_PUBLIC cs_calc(
	CS_CONTEXT *context,
	CS_INT op,
	CS_INT datatype,
	CS_VOID *var1,
	CS_VOID *var2,
	CS_VOID *dest
	);

/* cscmp.c */
extern CS_RETCODE CS_PUBLIC cs_cmp(
	CS_CONTEXT *context,
	CS_INT datatype,
	CS_VOID *var1,
	CS_VOID *var2,
	CS_INT *result
	);

/* cscnvrt.c */
extern CS_RETCODE CS_PUBLIC cs_convert(
	CS_CONTEXT *context,
	CS_DATAFMT *srcfmt,
	CS_VOID *srcdata,
	CS_DATAFMT *destfmt,
	CS_VOID *destdata,
	CS_INT *outlen
	);
extern CS_RETCODE CS_PUBLIC cs_will_convert(
	CS_CONTEXT *context,
	CS_INT srctype,
	CS_INT desttype,
	CS_BOOL *result
	);
extern CS_RETCODE CS_PUBLIC cs_set_convert(
	CS_CONTEXT *context,
	CS_INT	action,
	CS_INT srctype,
	CS_INT desttype,
	CS_CONV_FUNC *buffer
	);
extern CS_RETCODE CS_PUBLIC cs_setnull(
	CS_CONTEXT *context,
	CS_DATAFMT *datafmt,
	CS_VOID *buf,
	CS_INT buflen
	);

/* csconfig.c */
extern CS_RETCODE CS_PUBLIC cs_config(
	CS_CONTEXT *context,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);

/* csctxall.c */
extern CS_RETCODE CS_PUBLIC cs_ctx_alloc(
	CS_INT version,
	CS_CONTEXT **outptr
	);

/* csctxdrp.c */
extern CS_RETCODE CS_PUBLIC cs_ctx_drop(
	CS_CONTEXT *context
	);

/* csctxglb.c */
extern CS_RETCODE CS_PUBLIC cs_ctx_global(
	CS_INT version,
	CS_CONTEXT **outptr
	);

/* csobjs.c */
extern CS_RETCODE CS_PUBLIC cs_objects(
	CS_CONTEXT 	*context,
	CS_INT		action,
	CS_OBJNAME	*objname,
	CS_OBJDATA	*objdata
	);

/* csdiag.c */
extern CS_RETCODE CS_PUBLIC cs_diag(
	CS_CONTEXT *context,
	CS_INT operation,
	CS_INT type,
	CS_INT idx,
	CS_VOID *buffer
	);

/* csdtcrak.c */
extern CS_RETCODE CS_PUBLIC cs_dt_crack(
	CS_CONTEXT *context,
	CS_INT datetype,
	CS_VOID *dateval,
	CS_DATEREC *daterec
	);

/* csdtinfo.c */
extern CS_RETCODE CS_PUBLIC cs_dt_info(
	CS_CONTEXT *context,
	CS_INT action,
	CS_LOCALE *locale,
	CS_INT type,
	CS_INT item,
	CS_VOID *buffer,
	CS_INT buflen,
	CS_INT *outlen
	);

/* csloc.c */
extern CS_RETCODE CS_PUBLIC cs_locale(
	CS_CONTEXT *context,
	CS_INT action,
	CS_LOCALE *locale,
	CS_INT type,
	CS_CHAR *buffer,
	CS_INT buflen,
	CS_INT *outlen
	);

/* cslocall.c */
extern CS_RETCODE CS_PUBLIC cs_loc_alloc(
	CS_CONTEXT *context,
	CS_LOCALE **loc_pointer
	);

/* cslocator.c */
extern CS_RETCODE CS_PUBLIC cs_locator(
	CS_CONTEXT *context,
	CS_INT action,
	CS_LOCATOR *locator,
	CS_INT type,
	CS_VOID *buffer,
	CS_INT buflen,
	CS_INT *outlen);
extern CS_RETCODE CS_PUBLIC cs_locator_alloc(
	CS_CONTEXT *context,
	CS_LOCATOR **locator);
extern CS_RETCODE CS_PUBLIC cs_locator_drop(
	CS_CONTEXT *context,
	CS_LOCATOR *locator);

/* cslocdrp.c */
extern CS_RETCODE CS_PUBLIC cs_loc_drop(
	CS_CONTEXT *context,
	CS_LOCALE *locale
	);

/* csstr.c */
extern size_t CS_PUBLIC cs_strlcpy(
	char *target_str, 
	const char *source_str, 
	size_t target_size
	);

extern size_t CS_PUBLIC cs_strlcat(
	char *target_str, 
	const char *source_str, 
	size_t target_size
	);

extern void CS_PUBLIC cs_snprintf(
	char *str, 
	size_t size, 
	const char *format, 
	...
	);

#ifdef CS__INTERNAL_STRUCTS
extern CS_RETCODE CS_VARARGS cs_strbuild(
	CS_CONTEXT *context,
	...
	);
#else
extern CS_RETCODE CS_VARARGS cs_strbuild(
	CS_CONTEXT *context,
	CS_CHAR *buf,
	CS_INT buflen,
	CS_INT *outlen,
	CS_CHAR *text,
	CS_INT textlen,
	...
	);
#endif /* CS__INTERNAL_STRUCTS */

/* csstrcmp.c */
extern CS_RETCODE CS_PUBLIC cs_strcmp(
	CS_CONTEXT *context,
	CS_LOCALE *locale,
	CS_INT type,
	CS_CHAR *str1,
	CS_INT len1,
	CS_CHAR *str2,
	CS_INT len2,
	CS_INT *result
	);

/* cstime.c */
extern CS_RETCODE CS_PUBLIC cs_time(
	CS_CONTEXT *context,
	CS_LOCALE *locale,
	CS_VOID	 *buf,
	CS_INT	buflen,
	CS_INT  *outlen,
	CS_DATEREC *drec
	);

/* csmancnt.c */
extern CS_RETCODE CS_PUBLIC cs_manage_convert(
	CS_CONTEXT	*context,
	CS_INT		action,
	CS_INT		srctype, 
	CS_CHAR		*srcname,
	CS_INT		srcnamelen,
	CS_INT		desttype,
	CS_CHAR		*destname,
	CS_INT		destnamelen,
	CS_INT		*maxmultiplier,
	CS_CONV_FUNC	*func
	);

/* csmaxmul.c */
extern CS_RETCODE CS_PUBLIC cs_conv_mult(
	CS_CONTEXT	*context,
	CS_LOCALE       *srcloc,
	CS_LOCALE       *destloc,
	CS_INT          *multiplier
	);
	
CS_END_EXTERN_C

#endif /* __CSPUBLIC_H__ */
