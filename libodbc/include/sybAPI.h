// sybAPI.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__SYBAPI_H__)
#define __SYBAPI_H__

#include "SQLAPI.h"

// API header(s)
#include <ctpublic.h>

//! Sybase client and server messages handling callback
typedef void (SQLAPI_CALLBACK *saSybMsgHandler_t)(void *pMessageStruct, bool bIsServerMessage, void *pAddInfo);

class SQLAPI_API SASybErrInfo : public SAMutex
{
public:
	SASybErrInfo();

public:
	CS_MSGNUM	msgnumber;
	CS_CHAR		msgstring[CS_MAX_MSG];
	CS_INT		line;

	saSybMsgHandler_t fMsgHandler;
	void * pMsgAddInfo;
} ;

extern void AddSybSupport(const SAConnection *pCon);
extern void ReleaseSybSupport();

typedef CS_RETCODE (CS_PUBLIC *ct_debug_t)(
	CS_CONTEXT *context,
	CS_CONNECTION *connection,
	CS_INT operation,
	CS_INT flag,
	CS_CHAR *filename,
	CS_INT fnamelen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_bind_t)(
	CS_COMMAND *cmd,
	CS_INT item,
	CS_DATAFMT *datafmt,
	CS_VOID *buf,
	CS_INT *outputlen,
	CS_SMALLINT *indicator
	);
typedef CS_RETCODE (CS_PUBLIC *ct_br_column_t)(
	CS_COMMAND *cmd,
	CS_INT colnum,
	CS_BROWSEDESC *browsedesc
	);
typedef CS_RETCODE (CS_PUBLIC *ct_br_table_t)(
	CS_COMMAND *cmd,
	CS_INT tabnum,
	CS_INT type,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_callback_t)(
	CS_CONTEXT *context,
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT type,
	CS_VOID *func
	);
typedef CS_RETCODE (CS_PUBLIC *ct_cancel_t)(
	CS_CONNECTION *connection,
	CS_COMMAND *cmd,
	CS_INT type
	);
typedef CS_RETCODE (CS_PUBLIC *ct_capability_t)(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT type,
	CS_INT capability,
	CS_VOID *val
	);
typedef CS_RETCODE (CS_PUBLIC *ct_compute_info_t)(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_INT colnum,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_close_t)(
	CS_CONNECTION *connection,
	CS_INT option
	);
typedef CS_RETCODE (CS_PUBLIC *ct_cmd_alloc_t)(
	CS_CONNECTION *connection,
	CS_COMMAND **cmdptr
	);
typedef CS_RETCODE (CS_PUBLIC *ct_cmd_drop_t)(
	CS_COMMAND *cmd
	);
typedef CS_RETCODE (CS_PUBLIC *ct_cmd_props_t)(
	CS_COMMAND *cmd,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_command_t)(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_CHAR *buf,
	CS_INT buflen,
	CS_INT option
	);
typedef CS_RETCODE (CS_PUBLIC *ct_con_alloc_t)(
	CS_CONTEXT *context,
	CS_CONNECTION **connection
	);
typedef CS_RETCODE (CS_PUBLIC *ct_con_drop_t)(
	CS_CONNECTION *connection
	);
typedef CS_RETCODE (CS_PUBLIC *ct_con_props_t)(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_connect_t)(
	CS_CONNECTION *connection,
	CS_CHAR *server_name,
	CS_INT snamelen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_config_t)(
	CS_CONTEXT *context,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_cursor_t)(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_CHAR *name,
	CS_INT namelen,
	CS_CHAR *text,
	CS_INT tlen,
	CS_INT option
	);
typedef CS_RETCODE (CS_PUBLIC *ct_dyndesc_t)(
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
typedef CS_RETCODE (CS_PUBLIC *ct_describe_t)(
	CS_COMMAND *cmd,
	CS_INT item,
	CS_DATAFMT *datafmt
	);
typedef CS_RETCODE (CS_PUBLIC *ct_diag_t)(
	CS_CONNECTION *connection,
	CS_INT operation,
	CS_INT type,
	CS_INT idx,
	CS_VOID *buffer
	);
typedef CS_RETCODE (CS_PUBLIC *ct_dynamic_t)(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_CHAR *id,
	CS_INT idlen,
	CS_CHAR *buf,
	CS_INT buflen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_dynsqlda_t)(
	CS_COMMAND	*cmd,
	CS_INT		type,
	SQLDA		*dap,
	CS_INT		operation
	);
typedef CS_RETCODE (CS_PUBLIC *ct_exit_t)(
	CS_CONTEXT *context,
	CS_INT option
	);
typedef CS_RETCODE (CS_PUBLIC *ct_fetch_t)(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_INT offset,
	CS_INT option,
	CS_INT *count
	);
typedef CS_RETCODE (CS_PUBLIC *ct_getformat_t)(
	CS_COMMAND *cmd,
	CS_INT colnum,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_keydata_t)(
	CS_COMMAND *cmd,
	CS_INT action,
	CS_INT colnum,
	CS_VOID *buffer,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_init_t)(
	CS_CONTEXT *context,
	CS_INT version
	);
typedef CS_RETCODE (CS_PUBLIC *ct_options_t)(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_INT option,
	CS_VOID *param,
	CS_INT paramlen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *syb_ct_param_t)(
	CS_COMMAND *cmd,
	CS_DATAFMT *datafmt,
	CS_VOID *data,
	CS_INT datalen,
	CS_SMALLINT indicator
	);
typedef CS_RETCODE (CS_PUBLIC *ct_getloginfo_t)(
	CS_CONNECTION *connection,
	CS_LOGINFO **logptr
	);
typedef CS_RETCODE (CS_PUBLIC *ct_setloginfo_t)(
	CS_CONNECTION *connection,
	CS_LOGINFO *loginfo
	);
typedef CS_RETCODE (CS_PUBLIC *ct_recvpassthru_t)(
	CS_COMMAND *cmd,
	CS_VOID **recvptr
	);
typedef CS_RETCODE (CS_PUBLIC *ct_sendpassthru_t)(
	CS_COMMAND *cmd,
	CS_VOID *send_bufp
	);
typedef CS_RETCODE (CS_PUBLIC *ct_poll_t)(
	CS_CONTEXT *context,
	CS_CONNECTION *connection,
	CS_INT milliseconds,
	CS_CONNECTION **compconn,
	CS_COMMAND **compcmd,
	CS_INT *compid,
	CS_INT *compstatus
	);
typedef CS_RETCODE (CS_PUBLIC *ct_remote_pwd_t)(
	CS_CONNECTION *connection,
	CS_INT action,
	CS_CHAR *server_name,
	CS_INT snamelen,
	CS_CHAR *password,
	CS_INT pwdlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_results_t)(
	CS_COMMAND *cmd,
	CS_INT *result_type
	);
typedef CS_RETCODE (CS_PUBLIC *ct_res_info_t)(
	CS_COMMAND *cmd,
	CS_INT operation,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_send_t)(
	CS_COMMAND *cmd
	);
typedef CS_RETCODE (CS_PUBLIC *ct_get_data_t)(
	CS_COMMAND *cmd,
	CS_INT colnum,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_send_data_t)(
	CS_COMMAND *cmd,
	CS_VOID *buf,
	CS_INT buflen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_data_info_t)(
	CS_COMMAND *cmd,
	CS_INT action,
	CS_INT colnum,
	CS_IODESC *iodesc
	);
typedef CS_RETCODE (CS_PUBLIC *ct_wakeup_t)(
	CS_CONNECTION *connection,
	CS_COMMAND *cmd,
	CS_INT func_id,
	CS_RETCODE status
	);
typedef CS_RETCODE (CS_PUBLIC *ct_labels_t)(
	CS_CONNECTION   *connection,
	CS_INT          action,
	CS_CHAR         *labelname,
	CS_INT          namelen,
	CS_CHAR         *labelvalue,
	CS_INT          valuelen,
	CS_INT 		*outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_ds_lookup_t)(
	CS_CONNECTION		*connection,
	CS_INT			action,
	CS_INT			*reqidp,
	CS_DS_LOOKUP_INFO	*lookupinfo,
	CS_VOID			*userdatap
	);
typedef CS_RETCODE (CS_PUBLIC *ct_ds_dropobj_t)(
	CS_CONNECTION	*connection,
	CS_DS_OBJECT	*object 
	);
typedef CS_RETCODE (CS_PUBLIC *ct_ds_objinfo_t)(
	CS_DS_OBJECT	*objclass,
	CS_INT          action,
	CS_INT          objinfo,
	CS_INT          number,
	CS_VOID         *buffer,
	CS_INT          buflen,
	CS_INT          *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *ct_setparam_t)(
	CS_COMMAND *cmd,
	CS_DATAFMT *datafmt,
	CS_VOID *data,
	CS_INT *datalenp,
	CS_SMALLINT *indp
	);

typedef CS_RETCODE (CS_PUBLIC *cs_calc_t)(
	CS_CONTEXT *context,
	CS_INT op,
	CS_INT datatype,
	CS_VOID *var1,
	CS_VOID *var2,
	CS_VOID *dest
	);
typedef CS_RETCODE (CS_PUBLIC *cs_cmp_t)(
	CS_CONTEXT *context,
	CS_INT datatype,
	CS_VOID *var1,
	CS_VOID *var2,
	CS_INT *result
	);
typedef CS_RETCODE (CS_PUBLIC *cs_convert_t)(
	CS_CONTEXT *context,
	CS_DATAFMT *srcfmt,
	CS_VOID *srcdata,
	CS_DATAFMT *destfmt,
	CS_VOID *destdata,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *cs_will_convert_t)(
	CS_CONTEXT *context,
	CS_INT srctype,
	CS_INT desttype,
	CS_BOOL *result
	);
typedef CS_RETCODE (CS_PUBLIC *cs_set_convert_t)(
	CS_CONTEXT *context,
	CS_INT	action,
	CS_INT srctype,
	CS_INT desttype,
	CS_CONV_FUNC *buffer
	);
typedef CS_RETCODE (CS_PUBLIC *cs_setnull_t)(
	CS_CONTEXT *context,
	CS_DATAFMT *datafmt,
	CS_VOID *buf,
	CS_INT buflen
	);
typedef CS_RETCODE (CS_PUBLIC *cs_config_t)(
	CS_CONTEXT *context,
	CS_INT action,
	CS_INT property,
	CS_VOID *buf,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *cs_ctx_alloc_t)(
	CS_INT version,
	CS_CONTEXT **outptr
	);
typedef CS_RETCODE (CS_PUBLIC *cs_ctx_drop_t)(
	CS_CONTEXT *context
	);
typedef CS_RETCODE (CS_PUBLIC *cs_ctx_global_t)(
	CS_INT version,
	CS_CONTEXT **outptr
	);
typedef CS_RETCODE (CS_PUBLIC *cs_objects_t)(
	CS_CONTEXT 	*context,
	CS_INT		action,
	CS_OBJNAME	*objname,
	CS_OBJDATA	*objdata
	);
typedef CS_RETCODE (CS_PUBLIC *cs_diag_t)(
	CS_CONTEXT *context,
	CS_INT operation,
	CS_INT type,
	CS_INT idx,
	CS_VOID *buffer
	);
typedef CS_RETCODE (CS_PUBLIC *cs_dt_crack_t)(
	CS_CONTEXT *context,
	CS_INT datetype,
	CS_VOID *dateval,
	CS_DATEREC *daterec
	);
typedef CS_RETCODE (CS_PUBLIC *cs_dt_info_t)(
	CS_CONTEXT *context,
	CS_INT action,
	CS_LOCALE *locale,
	CS_INT type,
	CS_INT item,
	CS_VOID *buffer,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *cs_locale_t)(
	CS_CONTEXT *context,
	CS_INT action,
	CS_LOCALE *locale,
	CS_INT type,
	CS_CHAR *buffer,
	CS_INT buflen,
	CS_INT *outlen
	);
typedef CS_RETCODE (CS_PUBLIC *cs_loc_alloc_t)(
	CS_CONTEXT *context,
	CS_LOCALE **loc_pointer
	);
typedef CS_RETCODE (CS_PUBLIC *cs_loc_drop_t)(
	CS_CONTEXT *context,
	CS_LOCALE *locale
	);
#ifdef CS__INTERNAL_STRUCTS
typedef CS_RETCODE (CS_VARARGS *cs_strbuild_t)(
	CS_CONTEXT *context,
	...
	);
#else
typedef CS_RETCODE (CS_VARARGS *cs_strbuild_t)(
	CS_CONTEXT *context,
	CS_CHAR *buf,
	CS_INT buflen,
	CS_INT *outlen,
	CS_CHAR *text,
	CS_INT textlen,
	...
	);
#endif /* CS__INTERNAL_STRUCTS */
typedef CS_RETCODE (CS_PUBLIC *cs_strcmp_t)(
	CS_CONTEXT *context,
	CS_LOCALE *locale,
	CS_INT type,
	CS_CHAR *str1,
	CS_INT len1,
	CS_CHAR *str2,
	CS_INT len2,
	CS_INT *result
	);
typedef CS_RETCODE (CS_PUBLIC *cs_time_t)(
	CS_CONTEXT *context,
	CS_LOCALE *locale,
	CS_VOID	 *buf,
	CS_INT	buflen,
	CS_INT  *outlen,
	CS_DATEREC *drec
	);
typedef CS_RETCODE (CS_PUBLIC *cs_manage_convert_t)(
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
typedef CS_RETCODE (CS_PUBLIC *cs_conv_mult_t)(
	CS_CONTEXT	*context,
	CS_LOCALE       *srcloc,
	CS_LOCALE       *destloc,
	CS_INT          *multiplier
	);

typedef CS_RETCODE (CS_PUBLIC *ct_scroll_fetch_t)(
	CS_COMMAND *cmd,
	CS_INT type,
	CS_INT offset,
	CS_INT option,
	CS_INT *count
	);

// API declarations
class SQLAPI_API sybAPI : public saAPI
{
public:
	sybAPI();

	ct_debug_t	ct_debug;
	ct_bind_t	ct_bind;
	ct_br_column_t	ct_br_column;
	ct_br_table_t	ct_br_table;
	ct_callback_t	ct_callback;
	ct_cancel_t	ct_cancel;
	ct_capability_t	ct_capability;
	ct_compute_info_t	ct_compute_info;
	ct_close_t	ct_close;
	ct_cmd_alloc_t	ct_cmd_alloc;
	ct_cmd_drop_t	ct_cmd_drop;
	ct_cmd_props_t	ct_cmd_props;
	ct_command_t	ct_command;
	ct_con_alloc_t	ct_con_alloc;
	ct_con_drop_t	ct_con_drop;
	ct_con_props_t	ct_con_props;
	ct_connect_t	ct_connect;
	ct_config_t	ct_config;
	ct_cursor_t	ct_cursor;
	ct_dyndesc_t	ct_dyndesc;
	ct_describe_t	ct_describe;
	ct_diag_t	ct_diag;
	ct_dynamic_t	ct_dynamic;
	ct_dynsqlda_t	ct_dynsqlda;
	ct_exit_t	ct_exit;
	ct_fetch_t	ct_fetch;
	ct_getformat_t	ct_getformat;
	ct_keydata_t	ct_keydata;
	ct_init_t	ct_init;
	ct_options_t	ct_options;
	syb_ct_param_t	ct_param;
	ct_getloginfo_t	ct_getloginfo;
	ct_setloginfo_t	ct_setloginfo;
	ct_recvpassthru_t	ct_recvpassthru;
	ct_sendpassthru_t	ct_sendpassthru;
	ct_poll_t	ct_poll;
	ct_remote_pwd_t	ct_remote_pwd;
	ct_results_t	ct_results;
	ct_res_info_t	ct_res_info;
	ct_send_t	ct_send;
	ct_get_data_t	ct_get_data;
	ct_send_data_t	ct_send_data;
	ct_data_info_t	ct_data_info;
	ct_wakeup_t	ct_wakeup;
	ct_labels_t	ct_labels;
	ct_ds_lookup_t	ct_ds_lookup;
	ct_ds_dropobj_t	ct_ds_dropobj;
	ct_ds_objinfo_t	ct_ds_objinfo;
	ct_setparam_t	ct_setparam;

	cs_calc_t	cs_calc;
	cs_cmp_t	cs_cmp;
	cs_convert_t	cs_convert;
	cs_will_convert_t	cs_will_convert;
	cs_set_convert_t	cs_set_convert;
	cs_setnull_t	cs_setnull;
	cs_config_t	cs_config;
	cs_ctx_alloc_t	cs_ctx_alloc;
	cs_ctx_drop_t	cs_ctx_drop;
	cs_ctx_global_t	cs_ctx_global;
	cs_objects_t	cs_objects;
	cs_diag_t	cs_diag;
	cs_dt_crack_t	cs_dt_crack;
	cs_dt_info_t	cs_dt_info;
	cs_locale_t	cs_locale;
	cs_loc_alloc_t	cs_loc_alloc;
	cs_loc_drop_t	cs_loc_drop;
	cs_strbuild_t	cs_strbuild;
	cs_strcmp_t	cs_strcmp;
	cs_time_t	cs_time;
	cs_manage_convert_t	cs_manage_convert;
	cs_conv_mult_t	cs_conv_mult;

	ct_scroll_fetch_t ct_scroll_fetch;

	static void SetMessageCallback(saSybMsgHandler_t fHandler, void *pAddInfo, SAConnection *pCon = NULL);
	static int& DefaultLongMaxLength();

	SASybErrInfo errorInfo;
};

class SQLAPI_API sybConnectionHandles : public saConnectionHandles
{
public:
	sybConnectionHandles();

	CS_CONTEXT *m_context;
	CS_CONNECTION *m_connection;
	CS_LOCALE* m_locale;
};

class SQLAPI_API sybCommandHandles : public saCommandHandles
{
public:
	sybCommandHandles();

	CS_COMMAND *m_command;
};

class SQLAPI_API sybExternalConnection
{
	bool m_bAttached;

	SAConnection *m_pCon;
	CS_CONTEXT *m_contextSaved;
	CS_CONNECTION *m_connectionSaved;

	CS_CONTEXT *m_context;
	CS_CONNECTION *m_connection;
	CS_VOID	*m_ExternalContextClientMsg_cb;
	CS_VOID	*m_ExternalContextServerMsg_cb;
	CS_VOID	*m_ExternalConnectionClientMsg_cb;
	CS_VOID	*m_ExternalConnectionServerMsg_cb;
	CS_INT m_nExternalUserDataLen;
	CS_VOID *m_pExternalUserData;
	CS_INT m_nExternalUserDataAllocated;
	SASybErrInfo m_SybErrInfo;

public:
	sybExternalConnection(
		SAConnection *pCon,
		CS_CONTEXT *context,
		CS_CONNECTION *connection);
	void Attach();
	void Detach();
	~sybExternalConnection();
};



extern sybAPI g_sybAPI;

#endif // !defined(__SYBAPI_H__)
