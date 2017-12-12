/*
**	Sybase CT-LIBRARY
**	Confidential Property of Sybase, Inc.
**	(c) Copyright Sybase, Inc. 1991 - 2012
**	All rights reserved
*/

/*
** ctpublic.h - This is the public header file for CT-Lib.
*/
#ifndef __CTPUBLIC_H__
#define __CTPUBLIC_H__

/*
** include common defines and typedefs
*/
#ifndef __NO_INCLUDE__
#include <cspublic.h>
#include <sqlda.h>
#endif /* __NO_INCLUDE__ */

/*****************************************************************************
**
** defines used in CT-Lib applications
**
*****************************************************************************/

/*
** define for each CT-Lib API
*/
#define CT_BIND			(CS_INT) 0
#define CT_BR_COLUMN		(CS_INT) 1
#define CT_BR_TABLE		(CS_INT) 2
#define CT_CALLBACK		(CS_INT) 3
#define CT_CANCEL		(CS_INT) 4
#define CT_CAPABILITY		(CS_INT) 5
#define CT_CLOSE		(CS_INT) 6
#define CT_CMD_ALLOC		(CS_INT) 7
#define CT_CMD_DROP		(CS_INT) 8
#define CT_CMD_PROPS		(CS_INT) 9
#define CT_COMMAND		(CS_INT) 10
#define CT_COMPUTE_INFO		(CS_INT) 11
#define CT_CON_ALLOC		(CS_INT) 12
#define CT_CON_DROP		(CS_INT) 13
#define CT_CON_PROPS		(CS_INT) 14
#define CT_CON_XFER		(CS_INT) 15
#define CT_CONFIG		(CS_INT) 16
#define CT_CONNECT		(CS_INT) 17
#define CT_CURSOR		(CS_INT) 18
#define CT_DATA_INFO		(CS_INT) 19
#define CT_DEBUG		(CS_INT) 20
#define CT_DESCRIBE		(CS_INT) 21
#define CT_DIAG			(CS_INT) 22
#define CT_DYNAMIC		(CS_INT) 23
#define CT_DYNDESC		(CS_INT) 24
#define CT_EXIT			(CS_INT) 25
#define CT_FETCH		(CS_INT) 26
#define CT_GET_DATA		(CS_INT) 27
#define CT_GETFORMAT		(CS_INT) 28
#define CT_GETLOGINFO		(CS_INT) 29
#define CT_INIT			(CS_INT) 30
#define CT_KEYDATA		(CS_INT) 31
#define CT_OPTIONS		(CS_INT) 32
#define CT_PARAM		(CS_INT) 33
#define CT_POLL			(CS_INT) 34
#define CT_RECVPASSTHRU		(CS_INT) 35
#define CT_REMOTE_PWD		(CS_INT) 36
#define CT_RES_INFO		(CS_INT) 37
#define CT_RESULTS		(CS_INT) 38
#define CT_SEND			(CS_INT) 39
#define CT_SEND_DATA		(CS_INT) 40
#define CT_SENDPASSTHRU		(CS_INT) 41
#define CT_SETLOGINFO		(CS_INT) 42
#define CT_WAKEUP		(CS_INT) 43
#define CT_LABELS		(CS_INT) 44
#define CT_DS_LOOKUP		(CS_INT) 45
#define CT_DS_DROP		(CS_INT) 46
#define CT_DS_OBJINFO		(CS_INT) 47
#define CT_SETPARAM		(CS_INT) 48
#define CT_DYNSQLDA		(CS_INT) 49
#define CT_SCROLL_FETCH		(CS_INT) 50
#define CT_SEND_PARAMS		(CS_INT) 51

#define CT_NOTIFICATION		(CS_INT) 1000	/* id for event notfication
						** completion
						*/
#define CT_USER_FUNC		(CS_INT) 10000	/* minimum user-defined
						** function id
						*/


/*****************************************************************************
**
** define all user accessable functions here
**
*****************************************************************************/

/*
** declare all functions
*/
CS_START_EXTERN_C

/* ctdebug.c */
extern CS_RETCODE CS_PUBLIC ct_debug(
	CS_CONTEXT *context,
	CS_CONNECTION *connection,
	CS_INT operation,
	CS_INT flag,
	CS_CHAR *filename,
	CS_INT fnamelen
	);
/* ctbind.c */
extern CS_RETCODE CS_PUBLIC ct_bind(
	CS_COMMAND *cmd,
	CS_INT item,
	CS_DATAFMT *datafmt,
	CS_VOID *buf,
	CS_INT *outputlen,
	CS_SMALLINT *indicator
	);
/* ctbr.c */
extern CS_RETCODE CS_PUBLIC ct_br_column(
	CS_COMMAND *cmd,
	CS_INT colnum,
	CS_BROWSEDESC *browsedesc
	);
extern CS_RETCODE CS_PUBLIC ct_br_table(
	CS_COMMAND *cmd,
	CS_INT tabnum,
	CS_INT type,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
/* ctcallbk.c */
extern CS_RETCODE CS_PUBLIC ct_callback(
	CS_CONTEXT *context,
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT type,
	CS_VOID *func
	);
/* ctcancel.c */
extern CS_RETCODE CS_PUBLIC ct_cancel(
	CS_CONNECTION *connection,
	CS_COMMAND *cmd,
	CS_INT type
	);
/* ctcap.c */
extern CS_RETCODE CS_PUBLIC ct_capability(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT type,
	CS_INT capability,
	CS_VOID *val
	);
/* ctcinfo.c */
extern CS_RETCODE CS_PUBLIC ct_compute_info(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_INT colnum,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
/* ctclose.c */
extern CS_RETCODE CS_PUBLIC ct_close(
	CS_CONNECTION *connection,
	CS_INT option
	);
/* ctcmd.c */
extern CS_RETCODE CS_PUBLIC ct_cmd_alloc(
	CS_CONNECTION *connection,
	CS_COMMAND **cmdptr
	);
extern CS_RETCODE CS_PUBLIC ct_cmd_drop(
	CS_COMMAND *cmd
	);
extern CS_RETCODE CS_PUBLIC ct_cmd_props(
	CS_COMMAND *cmd,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
extern CS_RETCODE CS_PUBLIC ct_command(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_CHAR *buf,
	CS_INT buflen,
	CS_INT option
	);
/* ctcon.c */
extern CS_RETCODE CS_PUBLIC ct_con_alloc(
	CS_CONTEXT *context,
	CS_CONNECTION **connection
	);
extern CS_RETCODE CS_PUBLIC ct_con_drop(
	CS_CONNECTION *connection
	);
extern CS_RETCODE CS_PUBLIC ct_con_props(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
extern CS_RETCODE CS_PUBLIC ct_connect(
	CS_CONNECTION *connection,
	CS_CHAR *server_name,
	CS_INT snamelen
	);
/* ctconfig.c */
extern CS_RETCODE CS_PUBLIC ct_config(
	CS_CONTEXT *context,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
/* ctcursor.c */
extern CS_RETCODE CS_PUBLIC ct_cursor(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_CHAR *name,
	CS_INT namelen,
	CS_CHAR *text,
	CS_INT tlen,
	CS_INT option
	);
/* ctddesc.c */
extern CS_RETCODE CS_PUBLIC ct_dyndesc(
	CS_COMMAND *cmd,
	CS_CHAR *descriptor,
	CS_INT desclen,
	CS_INT operation,
	CS_INT idx,
	CS_DATAFMT *datafmt,
	CS_VOID *buffer,
	CS_INT buflen,
	CS_INT *copied,
	CS_SMALLINT *indicator
	);
/* ctdesc.c */
extern CS_RETCODE CS_PUBLIC ct_describe(
	CS_COMMAND *cmd,
	CS_INT item,
	CS_DATAFMT *datafmt
	);
/* ctdiag.c */
extern CS_RETCODE CS_PUBLIC ct_diag(
	CS_CONNECTION *connection,
	CS_INT operation,
	CS_INT type,
	CS_INT idx,
	CS_VOID *buffer
	);
/* ctdyn.c */
extern CS_RETCODE CS_PUBLIC ct_dynamic(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_CHAR *id,
	CS_INT idlen,
	CS_CHAR *buf,
	CS_INT buflen
	);
/* ctdynsqd.c */
extern CS_RETCODE CS_PUBLIC ct_dynsqlda(
	CS_COMMAND	*cmd,
	CS_INT		type,
	SQLDA		*dap,
	CS_INT		operation
	);
/* ctexit.c */
extern CS_RETCODE CS_PUBLIC ct_exit(
	CS_CONTEXT *context,
	CS_INT option
	);
/* ctfetch.c */
extern CS_RETCODE CS_PUBLIC ct_fetch(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_INT offset,
	CS_INT option,
	CS_INT *count
	);
/* ctfetch.c */
extern CS_RETCODE CS_PUBLIC ct_scroll_fetch(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_INT offset,
	CS_INT option,
	CS_INT *count
	);
/* ctgfmt.c */
extern CS_RETCODE CS_PUBLIC ct_getformat(
	CS_COMMAND *cmd,
	CS_INT colnum,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
/* ctkeydat.c */
extern CS_RETCODE CS_PUBLIC ct_keydata(
	CS_COMMAND *cmd,
	CS_INT action,
	CS_INT colnum,
	CS_VOID *buffer,
	CS_INT buflen,
	CS_INT *outlen
	);
/* ctinit.c */
extern CS_RETCODE CS_PUBLIC ct_init(
	CS_CONTEXT *context,
	CS_INT version
	);
/* ctopt.c */
extern CS_RETCODE CS_PUBLIC ct_options(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT option,
	CS_VOID *param,
	CS_INT paramlen,
	CS_INT *outlen
	);
/* ctparam.c */
extern CS_RETCODE CS_PUBLIC ct_param(
	CS_COMMAND *cmd,
	CS_DATAFMT *datafmt,
	CS_VOID *data,
	CS_INT datalen,
	CS_SMALLINT indicator
	);
/* ctpass.c */
extern CS_RETCODE CS_PUBLIC ct_getloginfo(
	CS_CONNECTION *connection,
	CS_LOGINFO **logptr
	);
extern CS_RETCODE CS_PUBLIC ct_setloginfo(
	CS_CONNECTION *connection,
	CS_LOGINFO *loginfo
	);
extern CS_RETCODE CS_PUBLIC ct_recvpassthru(
	CS_COMMAND *cmd,
	CS_VOID **recvptr
	);
extern CS_RETCODE CS_PUBLIC ct_sendpassthru(
	CS_COMMAND *cmd,
	CS_VOID *send_bufp
	);
/* ctpoll.c */
extern CS_RETCODE CS_PUBLIC ct_poll(
	CS_CONTEXT *context,
	CS_CONNECTION *connection,
	CS_INT milliseconds,
	CS_CONNECTION **compconn,
	CS_COMMAND **compcmd,
	CS_INT *compid,
	CS_INT *compstatus
	);
/* ctrempwd.c */
extern CS_RETCODE CS_PUBLIC ct_remote_pwd(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_CHAR *server_name,
	CS_INT snamelen,
	CS_CHAR *password,
	CS_INT pwdlen
	);
/* ctresult.c */
extern CS_RETCODE CS_PUBLIC ct_results(
	CS_COMMAND *cmd,
	CS_INT *result_type
	);
/* ctrinfo.c */
extern CS_RETCODE CS_PUBLIC ct_res_info(
	CS_COMMAND *cmd,
	CS_INT operation,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
/* ctsend.c */
extern CS_RETCODE CS_PUBLIC ct_send(
	CS_COMMAND *cmd
	);
extern CS_RETCODE CS_PUBLIC ct_send_params(
	CS_COMMAND *cmd,
	CS_INT unused
	);

/* ctgtdata.c */
extern CS_RETCODE CS_PUBLIC ct_get_data(
	CS_COMMAND *cmd,
	CS_INT colnum,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
/* ctsndata.c */
extern CS_RETCODE CS_PUBLIC ct_send_data(
	CS_COMMAND *cmd,
	CS_VOID *buf,
	CS_INT buflen
	);
/* ctdinfo.c */
extern CS_RETCODE CS_PUBLIC ct_data_info(
	CS_COMMAND *cmd,
	CS_INT action,
	CS_INT colnum,
	CS_IODESC *iodesc
	);
/* ctwakeup.c */
extern CS_RETCODE CS_PUBLIC ct_wakeup(
	CS_CONNECTION *connection,
	CS_COMMAND *cmd,
	CS_INT func_id,
	CS_RETCODE status
	);
/* ctsetlab.c */
extern CS_RETCODE CS_PUBLIC ct_labels(
	CS_CONNECTION   *connection,
	CS_INT          action,
	CS_CHAR         *labelname,
	CS_INT          namelen,
	CS_CHAR         *labelvalue,
	CS_INT          valuelen,
	CS_INT 		*outlen
	);
/* ctdsbrse.c */
extern CS_RETCODE CS_PUBLIC ct_ds_lookup(
	CS_CONNECTION		*connection,
	CS_INT			action,
	CS_INT			*reqidp,
	CS_DS_LOOKUP_INFO	*lookupinfo,
	CS_VOID			*userdatap
	);
/* ctdsdrop.c */
extern CS_RETCODE CS_PUBLIC ct_ds_dropobj(
	CS_CONNECTION	*connection,
	CS_DS_OBJECT	*object 
	);
/* ctdsobji.c */
extern CS_RETCODE CS_PUBLIC ct_ds_objinfo(
	CS_DS_OBJECT	*objclass,
	CS_INT          action,
	CS_INT          objinfo,
	CS_INT          number,
	CS_VOID         *buffer,
	CS_INT          buflen,
	CS_INT          *outlen
	);
/* ctsetpar.c */
extern CS_RETCODE CS_PUBLIC ct_setparam(
	CS_COMMAND *cmd,
	CS_DATAFMT *datafmt,
	CS_VOID *data,
	CS_INT *datalenp,
	CS_SMALLINT *indp
	);
CS_END_EXTERN_C

#endif /* __CTPUBLIC_H__ */
