// ora8API.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__ORA8API_H__)
#define __ORA8API_H__

#include "SQLAPI.h"

// API header(s)
#include <oci.h>

extern long g_nORA8DLLVersionLoaded;

extern void AddORA8Support(const SAConnection * pCon);
extern void ReleaseORA8Support();

// 8.0.x calls
typedef sword   (*OCIInitialize_t)(ub4 mode, dvoid *ctxp, 
                 dvoid *(*malocfp)(dvoid *ctxp, size_t size),
                 dvoid *(*ralocfp)(dvoid *ctxp, dvoid *memptr, size_t newsize),
                 void   (*mfreefp)(dvoid *ctxp, dvoid *memptr) );

typedef sword   (*OCIHandleAlloc_t)(CONST dvoid *parenth, dvoid **hndlpp, CONST ub4 type, 
                       CONST size_t xtramem_sz, dvoid **usrmempp);

typedef sword   (*OCIHandleFree_t)(dvoid *hndlp, CONST ub4 type);


typedef sword   (*OCIDescriptorAlloc_t)(CONST dvoid *parenth, dvoid **descpp, CONST ub4 type,
                     CONST size_t xtramem_sz, dvoid **usrmempp);

typedef sword   (*OCIDescriptorFree_t)(dvoid *descp, CONST ub4 type);

typedef sword   (*OCIEnvInit_t)(OCIEnv **envp, ub4 mode, 
                    size_t xtramem_sz, dvoid **usrmempp);

typedef sword   (*OCIServerAttach_t)(OCIServer *srvhp, OCIError *errhp,
                          CONST text *dblink, sb4 dblink_len, ub4 mode);

typedef sword   (*OCIServerDetach_t)(OCIServer *srvhp, OCIError *errhp, ub4 mode);

typedef sword   (*OCISessionBegin_t)(OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp,
                          ub4 credt, ub4 mode);

typedef sword   (*OCISessionEnd_t)(OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp, 
                         ub4 mode);

typedef sword   (*OCILogon_t)(OCIEnv *envhp, OCIError *errhp, OCISvcCtx **svchp, 
		  CONST text *username, ub4 uname_len, 
		  CONST text *password, ub4 passwd_len, 
		  CONST text *dbname, ub4 dbname_len);

typedef sword   (*OCILogoff_t)(OCISvcCtx *svchp, OCIError *errhp);


typedef sword   (*OCIPasswordChange_t)(OCISvcCtx *svchp, OCIError *errhp, 
                             CONST text *user_name, ub4 usernm_len, 
                             CONST text *opasswd, ub4 opasswd_len, 
                             CONST text *npasswd, ub4 npasswd_len, ub4 mode);

typedef sword   (*OCIStmtPrepare_t)(OCIStmt *stmtp, OCIError *errhp, CONST text *stmt,
                          ub4 stmt_len, ub4 language, ub4 mode);

typedef sword   (*OCIBindByPos_t)(OCIStmt *stmtp, OCIBind **bindp, OCIError *errhp,
		       ub4 position, dvoid *valuep, sb4 value_sz,
		       ub2 dty, dvoid *indp, ub2 *alenp, ub2 *rcodep,
		       ub4 maxarr_len, ub4 *curelep, ub4 mode);

typedef sword   (*OCIBindByName_t)(OCIStmt *stmtp, OCIBind **bindp, OCIError *errhp,
			 CONST text *placeholder, sb4 placeh_len, 
                         dvoid *valuep, sb4 value_sz, ub2 dty, 
                         dvoid *indp, ub2 *alenp, ub2 *rcodep, 
                         ub4 maxarr_len, ub4 *curelep, ub4 mode);

typedef sword   (*OCIBindObject_t)(OCIBind *bindp, OCIError *errhp, CONST OCIType *type, 
			dvoid **pgvpp, ub4 *pvszsp, dvoid **indpp, 
			ub4 *indszp);

typedef sword   (*OCIBindDynamic_t)(OCIBind *bindp, OCIError *errhp, dvoid *ictxp,
			  OCICallbackInBind icbfp, dvoid *octxp,
			  OCICallbackOutBind ocbfp);

typedef sword   (*OCIBindArrayOfStruct_t)(OCIBind *bindp, OCIError *errhp, 
                                ub4 pvskip, ub4 indskip,
                                ub4 alskip, ub4 rcskip);

typedef sword   (*OCIStmtGetPieceInfo_t)(OCIStmt *stmtp, OCIError *errhp, 
                               dvoid **hndlpp, ub4 *typep,
                               ub1 *in_outp, ub4 *iterp, ub4 *idxp, 
                               ub1 *piecep);

typedef sword   (*OCIStmtSetPieceInfo_t)(dvoid *hndlp, ub4 type, OCIError *errhp, 
                               CONST dvoid *bufp, ub4 *alenp, ub1 piece, 
                               CONST dvoid *indp, ub2 *rcodep);

typedef sword   (*OCIStmtExecute_t)(OCISvcCtx *svchp, OCIStmt *stmtp, OCIError *errhp, 
                         ub4 iters, ub4 rowoff, CONST OCISnapshot *snap_in, 
                         OCISnapshot *snap_out, ub4 mode);

typedef sword   (*OCIDefineByPos_t)(OCIStmt *stmtp, OCIDefine **defnp, OCIError *errhp,
			 ub4 position, dvoid *valuep, sb4 value_sz, ub2 dty,
			 dvoid *indp, ub2 *rlenp, ub2 *rcodep, ub4 mode);

typedef sword   (*OCIDefineObject_t)(OCIDefine *defnp, OCIError *errhp, 
                          CONST OCIType *type, dvoid **pgvpp, 
                          ub4 *pvszsp, dvoid **indpp, ub4 *indszp);

typedef sword   (*OCIDefineDynamic_t)(OCIDefine *defnp, OCIError *errhp, dvoid *octxp,
                            OCICallbackDefine ocbfp);

typedef sword   (*OCIDefineArrayOfStruct_t)(OCIDefine *defnp, OCIError *errhp, ub4 pvskip,
                                 ub4 indskip, ub4 rlskip, ub4 rcskip);

typedef sword   (*OCIStmtFetch_t)(OCIStmt *stmtp, OCIError *errhp, ub4 nrows, 
                        ub2 orientation, ub4 mode);

typedef sword   (*OCIStmtGetBindInfo_t)(OCIStmt *stmtp, OCIError *errhp, ub4 size, 
                              ub4 startloc,
                              sb4 *found, text *bvnp[], ub1 bvnl[],
                              text *invp[], ub1 inpl[], ub1 dupl[],
                              OCIBind *hndl[]);

typedef sword   (*OCIDescribeAny_t)(OCISvcCtx *svchp, OCIError *errhp, 
                         dvoid *objptr, 
                         ub4 objnm_len, ub1 objptr_typ, ub1 info_level,
			 ub1 objtyp, OCIDescribe *dschp);

typedef sword   (*OCIParamGet_t)(CONST dvoid *hndlp, ub4 htype, OCIError *errhp, 
                     dvoid **parmdpp, ub4 pos);

typedef sword   (*OCIParamSet_t)(dvoid *hdlp, ub4 htyp, OCIError *errhp, CONST dvoid *dscp,
                    ub4 dtyp, ub4 pos);

typedef sword   (*OCITransStart_t)(OCISvcCtx *svchp, OCIError *errhp, 
                        uword timeout, ub4 flags );

typedef sword   (*OCITransDetach_t)(OCISvcCtx *svchp, OCIError *errhp, ub4 flags );

typedef sword   (*OCITransCommit_t)(OCISvcCtx *svchp, OCIError *errhp, ub4 flags);

typedef sword   (*OCITransRollback_t)(OCISvcCtx *svchp, OCIError *errhp, ub4 flags);

typedef sword   (*OCITransPrepare_t)(OCISvcCtx *svchp, OCIError *errhp, ub4 flags);

typedef sword   (*OCITransForget_t)(OCISvcCtx *svchp, OCIError *errhp, ub4 flags);

typedef sword   (*OCIErrorGet_t)(dvoid *hndlp, ub4 recordno, text *sqlstate,
                       sb4 *errcodep, text *bufp, ub4 bufsiz, ub4 type);

typedef sword   (*OCILobAppend_t)(OCISvcCtx *svchp, OCIError *errhp, 
                       OCILobLocator *dst_locp,
                       OCILobLocator *src_locp);

typedef sword   (*OCILobAssign_t)(OCIEnv *envhp, OCIError *errhp, 
                      CONST OCILobLocator *src_locp, 
                      OCILobLocator **dst_locpp);

typedef sword   (*OCILobCharSetForm_t)(OCIEnv *envhp, OCIError *errhp, 
                           CONST OCILobLocator *locp, ub1 *csfrm);

typedef sword   (*OCILobCharSetId_t)(OCIEnv *envhp, OCIError *errhp, 
                         CONST OCILobLocator *locp, ub2 *csid);

typedef sword   (*OCILobCopy_t)(OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *dst_locp,
                    OCILobLocator *src_locp, ub4 amount, ub4 dst_offset, 
                    ub4 src_offset);

typedef sword   (*OCILobDisableBuffering_t)(OCISvcCtx      *svchp,
                                OCIError       *errhp,
                                OCILobLocator  *locp);

typedef sword   (*OCILobEnableBuffering_t)(OCISvcCtx      *svchp,
                               OCIError       *errhp,
                               OCILobLocator  *locp);

typedef sword   (*OCILobErase_t)(OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                      ub4 *amount, ub4 offset);

typedef sword   (*OCILobFileClose_t)(OCISvcCtx *svchp, OCIError *errhp, 
                         OCILobLocator *filep);

typedef sword   (*OCILobFileCloseAll_t)(OCISvcCtx *svchp, OCIError *errhp);

typedef sword   (*OCILobFileExists_t)(OCISvcCtx *svchp, OCIError *errhp, 
			  OCILobLocator *filep,
			  boolean *flag);

typedef sword   (*OCILobFileGetName_t)(OCIEnv *envhp, OCIError *errhp, 
                           CONST OCILobLocator *filep, 
                           text *dir_alias, ub2 *d_length, 
                           text *filename, ub2 *f_length);

typedef sword   (*OCILobFileIsOpen_t)(OCISvcCtx *svchp, OCIError *errhp, 
                          OCILobLocator *filep,
                          boolean *flag);

typedef sword   (*OCILobFileOpen_t)(OCISvcCtx *svchp, OCIError *errhp, 
                        OCILobLocator *filep,
                        ub1 mode);

typedef sword   (*OCILobFileSetName_t)(OCIEnv *envhp, OCIError *errhp, 
                           OCILobLocator **filepp, 
                           CONST text *dir_alias, ub2 d_length, 
                           CONST text *filename, ub2 f_length);

typedef sword   (*OCILobFlushBuffer_t)(OCISvcCtx       *svchp,
                           OCIError        *errhp,
                           OCILobLocator   *locp,
                           ub4              flag);

typedef sword   (*OCILobGetLength_t)(OCISvcCtx *svchp, OCIError *errhp, 
                          OCILobLocator *locp,
                          ub4 *lenp);

typedef sword   (*OCILobIsEqual_t)(OCIEnv *envhp, CONST OCILobLocator *x, 
                        CONST OCILobLocator *y, 
                        boolean *is_equal);

typedef sword   (*OCILobLoadFromFile_t)(OCISvcCtx *svchp, OCIError *errhp, 
                            OCILobLocator *dst_locp,
       	                    OCILobLocator *src_filep, 
                            ub4 amount, ub4 dst_offset, 
                            ub4 src_offset);

typedef sword   (*OCILobLocatorIsInit_t)(OCIEnv *envhp, OCIError *errhp, 
                             CONST OCILobLocator *locp, 
                             boolean *is_initialized);

typedef sword   (*OCILobRead_t)(OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                     ub4 *amtp, ub4 offset, dvoid *bufp, ub4 bufl, 
                     dvoid *ctxp, sb4 (*cbfp)(dvoid *ctxp, 
                                              CONST dvoid *bufp, 
                                              ub4 len, 
                                              ub1 piece),
                     ub2 csid, ub1 csfrm);

typedef sword   (*OCILobRead2_t)(OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                     oraub8 *byte_amtp, oraub8 *char_amtp, oraub8 offset, dvoid *bufp, oraub8 bufl, 
                     ub1 piece, dvoid *ctxp, sb4 (*cbfp)(dvoid *ctxp, 
                                              CONST dvoid *bufp, 
                                              oraub8 len, ub1 piecep,
                                              dvoid **changed_bufpp,oraub8 *changed_lenp),
                     ub2 csid, ub1 csfrm);

typedef sword   (*OCILobTrim_t)(OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                     ub4 newlen);

typedef sword   (*OCILobWrite_t)(OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                      ub4 *amtp, ub4 offset, dvoid *bufp, ub4 buflen, 
                      ub1 piece, dvoid *ctxp, 
                      sb4 (*cbfp)(dvoid *ctxp, 
                                  dvoid *bufp, 
                                  ub4 *len, 
                                  ub1 *piece),
                      ub2 csid, ub1 csfrm);

typedef sword   (*OCILobWrite2_t)(OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                      oraub8 *byte_amtp, oraub8 *char_amtp, oraub8 offset,
                      void  *bufp, oraub8 buflen, ub1 piece, void  *ctxp, 
                      OCICallbackLobWrite2 cbfp, ub2 csid, ub1 csfrm);

typedef sword   (*OCIBreak_t)(dvoid *hndlp, OCIError *errhp);

typedef sword   (*OCIReset_t)(dvoid *hndlp, OCIError *errhp);

typedef sword   (*OCIServerVersion_t)(dvoid *hndlp, OCIError *errhp, text *bufp, 
                           ub4 bufsz,
                           ub1 hndltype);


typedef sword   (*OCIAttrGet_t)(CONST dvoid *trgthndlp, ub4 trghndltyp, 
                    dvoid *attributep, ub4 *sizep, ub4 attrtype, 
                    OCIError *errhp);

typedef sword   (*OCIAttrSet_t)(dvoid *trgthndlp, ub4 trghndltyp, dvoid *attributep,
                    ub4 size, ub4 attrtype, OCIError *errhp);

typedef sword   (*OCISvcCtxToLda_t)(OCISvcCtx *svchp, OCIError *errhp, Lda_Def *ldap);

typedef sword   (*OCILdaToSvcCtx_t)(OCISvcCtx **svchpp, OCIError *errhp, Lda_Def *ldap);

typedef sword   (*OCIResultSetToStmt_t)(OCIResult *rsetdp, OCIError *errhp);


// 8.1.x (8i) calls
typedef sword   (*OCIEnvCreate_t)(OCIEnv **envp, ub4 mode, dvoid *ctxp,
                 dvoid *(*malocfp)(dvoid *ctxp, size_t size),
                 dvoid *(*ralocfp)(dvoid *ctxp, dvoid *memptr, size_t newsize),
                 void   (*mfreefp)(dvoid *ctxp, dvoid *memptr),
                 size_t xtramem_sz, dvoid **usrmempp);

typedef sword (*OCIEnvNlsCreate_t)(OCIEnv **envhpp, ub4 mode, dvoid *ctxp,
                 dvoid *(*malocfp)(dvoid *ctxp, size_t size),
                 dvoid *(*ralocfp)(dvoid *ctxp, dvoid *memptr, size_t newsize),
                 void   (*mfreefp)(dvoid *ctxp, dvoid *memptr),
                 size_t xtramemsz, dvoid **usrmempp,
                 ub2 charset, ub2 ncharset);

typedef sword (*OCIDurationBegin_t)(    OCIEnv *env, OCIError *err, CONST OCISvcCtx *svc, 
                           OCIDuration parent, OCIDuration *dur    );

typedef sword (*OCIDurationEnd_t)(    OCIEnv *env, OCIError *err, CONST OCISvcCtx *svc, 
                         OCIDuration duration    );

typedef sword (*OCILobCreateTemporary_t)(OCISvcCtx          *svchp,
                            OCIError           *errhp,
                            OCILobLocator      *locp,
                            ub2                 csid,
                            ub1                 csfrm,
                            ub1                 lobtype,
                            boolean             cache,
                            OCIDuration         duration);

typedef sword (*OCILobFreeTemporary_t)(OCISvcCtx          *svchp,
                          OCIError           *errhp,
                          OCILobLocator      *locp);

typedef sword (*OCILobIsTemporary_t)(OCIEnv            *envp,
                        OCIError          *errhp,
                        OCILobLocator     *locp,
                        boolean           *is_temporary);

typedef sword (*OCIAQEnq_t)(OCISvcCtx *svchp, OCIError *errhp, OraText *queue_name,
		 OCIAQEnqOptions *enqopt, OCIAQMsgProperties *msgprop,
		 OCIType *payload_tdo, dvoid **payload, dvoid **payload_ind, 
		 OCIRaw **msgid, ub4 flags); 

typedef sword (*OCIAQDeq_t)(OCISvcCtx *svchp, OCIError *errhp, OraText *queue_name,
		 OCIAQDeqOptions *deqopt, OCIAQMsgProperties *msgprop,
		 OCIType *payload_tdo, dvoid **payload, dvoid **payload_ind, 
		 OCIRaw **msgid, ub4 flags); 

typedef sword (*OCIAQListen_t)(OCISvcCtx *svchp, OCIError *errhp, 
		      OCIAQAgent **agent_list, ub4 num_agents,
		      sb4 wait, OCIAQAgent **agent,
		      ub4 flags);

typedef sword (*OCIStmtFetch2_t)(OCIStmt *stmtp, OCIError *errhp, ub4 nrows, 
		 ub2 orientation, sb4 scrollOffset, ub4 mode );

//--------------- Begin OCI Client Notification Interfaces ------------------

typedef sword (*OCISubscriptionRegister_t)(OCISvcCtx *svchp, OCISubscription **subscrhpp, 
			      ub2 count, OCIError *errhp, ub4 mode);

typedef sword (*OCISubscriptionPost_t)(OCISvcCtx *svchp, OCISubscription **subscrhpp, 
			      ub2 count, OCIError *errhp, ub4 mode);

typedef sword (*OCISubscriptionUnRegister_t)(OCISvcCtx *svchp, OCISubscription *subscrhp, 
			      OCIError *errhp, ub4 mode);

typedef sword (*OCISubscriptionDisable_t)(OCISubscription *subscrhp, 
			   OCIError *errhp, ub4 mode);

typedef sword (*OCISubscriptionEnable_t)(OCISubscription *subscrhp, 
			  OCIError *errhp, ub4 mode);

//------------------- End OCI Publish/Subscribe Interfaces ------------------


typedef sword (*OCIDateTimeConstruct_t)( dvoid *hndl,
				OCIError *err, OCIDateTime *datetime,
				sb2 year, ub1 month, ub1 day,
				ub1 hour, ub1 min, ub1 sec, ub4 fsec,
				OraText *timezone, size_t timezone_length);

typedef sword (*OCIDateTimeGetDate_t)(dvoid *hndl,
				OCIError *err, CONST OCIDateTime *datetime,
				sb2 *year, ub1 *month, ub1 *day);

typedef sword (*OCIDateTimeGetTime_t)(dvoid *hndl,
				OCIError *err, OCIDateTime *datetime,
				ub1 *hour, ub1 *min, ub1 *sec, ub4 *fsec);


typedef sword (*OCINlsNumericInfoGet_t)(dvoid *envhp, OCIError *errhp, sb4 *val, ub2 item);

// API declarations
class SQLAPI_API ora8API : public saAPI
{
public:
	ora8API();

	// 8.0.x calls
	OCIInitialize_t	OCIInitialize;
	OCIHandleAlloc_t	OCIHandleAlloc;
	OCIHandleFree_t	OCIHandleFree;
	OCIDescriptorAlloc_t	OCIDescriptorAlloc;
	OCIDescriptorFree_t	OCIDescriptorFree;
	OCIEnvInit_t	OCIEnvInit;
	OCIServerAttach_t	OCIServerAttach;
	OCIServerDetach_t	OCIServerDetach;
	OCISessionBegin_t	OCISessionBegin;
	OCISessionEnd_t	OCISessionEnd;
	OCILogon_t	OCILogon;
	OCILogoff_t	OCILogoff;
	OCIPasswordChange_t	OCIPasswordChange;
	OCIStmtPrepare_t	OCIStmtPrepare;
	OCIBindByPos_t	OCIBindByPos;
	OCIBindByName_t	OCIBindByName;
	OCIBindObject_t	OCIBindObject;
	OCIBindDynamic_t	OCIBindDynamic;
	OCIBindArrayOfStruct_t	OCIBindArrayOfStruct;
	OCIStmtGetPieceInfo_t	OCIStmtGetPieceInfo;
	OCIStmtSetPieceInfo_t	OCIStmtSetPieceInfo;
	OCIStmtExecute_t	OCIStmtExecute;
	OCIDefineByPos_t	OCIDefineByPos;
	OCIDefineObject_t	OCIDefineObject;
	OCIDefineDynamic_t	OCIDefineDynamic;
	OCIDefineArrayOfStruct_t	OCIDefineArrayOfStruct;
	OCIStmtFetch_t	OCIStmtFetch;
	OCIStmtGetBindInfo_t	OCIStmtGetBindInfo;
	OCIDescribeAny_t	OCIDescribeAny;
	OCIParamGet_t	OCIParamGet;
	OCIParamSet_t	OCIParamSet;
	OCITransStart_t	OCITransStart;
	OCITransDetach_t	OCITransDetach;
	OCITransCommit_t	OCITransCommit;
	OCITransRollback_t	OCITransRollback;
	OCITransPrepare_t	OCITransPrepare;
	OCITransForget_t	OCITransForget;
	OCIErrorGet_t	OCIErrorGet;
	OCILobAppend_t	OCILobAppend;
	OCILobAssign_t	OCILobAssign;
	OCILobCharSetForm_t	OCILobCharSetForm;
	OCILobCharSetId_t	OCILobCharSetId;
	OCILobCopy_t	OCILobCopy;
	OCILobDisableBuffering_t	OCILobDisableBuffering;
	OCILobEnableBuffering_t	OCILobEnableBuffering;
	OCILobErase_t	OCILobErase;
	OCILobFileClose_t	OCILobFileClose;
	OCILobFileCloseAll_t	OCILobFileCloseAll;
	OCILobFileExists_t	OCILobFileExists;
	OCILobFileGetName_t	OCILobFileGetName;
	OCILobFileIsOpen_t	OCILobFileIsOpen;
	OCILobFileOpen_t	OCILobFileOpen;
	OCILobFileSetName_t	OCILobFileSetName;
	OCILobFlushBuffer_t	OCILobFlushBuffer;
	OCILobGetLength_t	OCILobGetLength;
	OCILobIsEqual_t	OCILobIsEqual;
	OCILobLoadFromFile_t	OCILobLoadFromFile;
	OCILobLocatorIsInit_t	OCILobLocatorIsInit;
	OCILobRead_t	OCILobRead;
	OCILobRead2_t	OCILobRead2;
	OCILobTrim_t	OCILobTrim;
	OCILobWrite_t	OCILobWrite;
	OCILobWrite2_t	OCILobWrite2;
	OCIBreak_t	OCIBreak;
	OCIReset_t	OCIReset;
	OCIServerVersion_t	OCIServerVersion;
	OCIAttrGet_t	OCIAttrGet;
	OCIAttrSet_t	OCIAttrSet;
	OCISvcCtxToLda_t	OCISvcCtxToLda;
	OCILdaToSvcCtx_t	OCILdaToSvcCtx;
	OCIResultSetToStmt_t	OCIResultSetToStmt;

	// 8.1.x (8i) calls
	OCIEnvCreate_t	OCIEnvCreate;
	OCIEnvNlsCreate_t OCIEnvNlsCreate;
	OCIDurationBegin_t	OCIDurationBegin;
	OCIDurationEnd_t	OCIDurationEnd;
	OCILobCreateTemporary_t	OCILobCreateTemporary;
	OCILobFreeTemporary_t	OCILobFreeTemporary;
	OCILobIsTemporary_t	OCILobIsTemporary;

	OCIAQEnq_t	OCIAQEnq;
	OCIAQDeq_t	OCIAQDeq;
	OCIAQListen_t	OCIAQListen;
	OCISubscriptionRegister_t	OCISubscriptionRegister;
	OCISubscriptionPost_t	OCISubscriptionPost;
	OCISubscriptionUnRegister_t	OCISubscriptionUnRegister;
	OCISubscriptionDisable_t	OCISubscriptionDisable;
	OCISubscriptionEnable_t	OCISubscriptionEnable;

	OCIDateTimeConstruct_t OCIDateTimeConstruct;
	OCIDateTimeGetDate_t OCIDateTimeGetDate;
	OCIDateTimeGetTime_t OCIDateTimeGetTime;

	OCINlsNumericInfoGet_t OCINlsNumericInfoGet;

	OCIStmtFetch2_t	OCIStmtFetch2;
};

class SQLAPI_API ora8ConnectionHandles : public saConnectionHandles
{
public:
	ora8ConnectionHandles();

	OCIEnv		*m_pOCIEnv;
	OCIError	*m_pOCIError;
	OCISvcCtx	*m_pOCISvcCtx;
	OCIServer	*m_pOCIServer;
	OCISession	*m_pOCISession;
};

class SQLAPI_API ora8CommandHandles : public saCommandHandles
{
public:
	ora8CommandHandles();

	OCIStmt			*m_pOCIStmt;
	OCIError		*m_pOCIError;	// cursor owned error handle
};

extern ora8API g_ora8API;

#endif // !defined(__ORA8API_H__)
