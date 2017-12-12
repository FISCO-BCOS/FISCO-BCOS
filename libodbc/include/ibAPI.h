/*!
 * interface for the IibClient class.
 * 
 * Copyright (c) 2005 by <your name/ organization here>
 */

#if !defined(__IBAPI_H__)
#define __IBAPI_H__

#include "SQLAPI.h"

#ifdef __SUNPRO_CC
#include <inttypes.h>
#define _INTPTR_T_DEFINED
#endif

#include <ibase.h>

#ifdef SA_64BIT
#define ISC_NULL_HANDLE	0
#else
#define ISC_NULL_HANDLE	NULL
#endif

extern long g_nIB_DLLVersionLoaded;

extern void AddIBSupport(const SAConnection *pCon);
extern void ReleaseIBSupport();

typedef ISC_STATUS  (ISC_EXPORT *isc_attach_database_t) (ISC_STATUS ISC_FAR *, 
					    short, 
					    char ISC_FAR *, 
					    isc_db_handle ISC_FAR *, 
					    short, 
					    char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_array_gen_sdl_t) (ISC_STATUS ISC_FAR *, 
					  ISC_ARRAY_DESC ISC_FAR *,
					  short ISC_FAR *, 
					  char ISC_FAR *, 
					  short ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_array_get_slice_t) (ISC_STATUS ISC_FAR *, 
					    isc_db_handle ISC_FAR *, 
					    isc_tr_handle ISC_FAR *, 
					    ISC_QUAD ISC_FAR *, 
					    ISC_ARRAY_DESC ISC_FAR *, 
					    void ISC_FAR *, 
					    ISC_LONG ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_array_lookup_bounds_t) (ISC_STATUS ISC_FAR *, 
						isc_db_handle ISC_FAR *, 
						isc_tr_handle ISC_FAR *, 
						char ISC_FAR *,
						char ISC_FAR *, 
						ISC_ARRAY_DESC ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_array_lookup_desc_t) (ISC_STATUS ISC_FAR *, 
					      isc_db_handle ISC_FAR *,
					      isc_tr_handle ISC_FAR *, 
					      char ISC_FAR *, 
					      char ISC_FAR *, 
					      ISC_ARRAY_DESC ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_array_set_desc_t) (ISC_STATUS ISC_FAR *, 
					   char ISC_FAR *, 
					   char ISC_FAR *,
					   short ISC_FAR *, 
					   short ISC_FAR *, 
					   short ISC_FAR *, 
					   ISC_ARRAY_DESC ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_array_put_slice_t) (ISC_STATUS ISC_FAR *, 
					    isc_db_handle ISC_FAR *, 
					    isc_tr_handle ISC_FAR *, 
					    ISC_QUAD ISC_FAR *, 
					    ISC_ARRAY_DESC ISC_FAR *, 
					    void ISC_FAR *, 
					    ISC_LONG ISC_FAR *);

typedef void       (ISC_EXPORT *isc_blob_default_desc_t) (ISC_BLOB_DESC ISC_FAR *,
                                        unsigned char ISC_FAR *,
                                        unsigned char ISC_FAR *);

typedef ISC_STATUS (ISC_EXPORT *isc_blob_gen_bpb_t) (ISC_STATUS ISC_FAR *,
					ISC_BLOB_DESC ISC_FAR *,
					ISC_BLOB_DESC ISC_FAR *,
					unsigned short,
					unsigned char ISC_FAR *,
					unsigned short ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_blob_info_t) (ISC_STATUS ISC_FAR *, 
				      isc_blob_handle ISC_FAR *, 
				      short,
 				      char ISC_FAR *, 
				      short, 
				      char ISC_FAR *);

typedef ISC_STATUS (ISC_EXPORT *isc_blob_lookup_desc_t) (ISC_STATUS ISC_FAR *,
					    isc_db_handle ISC_FAR *,
					    isc_tr_handle ISC_FAR *,
					    unsigned char ISC_FAR *,
					    unsigned char ISC_FAR *,
					    ISC_BLOB_DESC ISC_FAR *,
					    unsigned char ISC_FAR *);

typedef ISC_STATUS (ISC_EXPORT *isc_blob_set_desc_t) (ISC_STATUS ISC_FAR *,
					 unsigned char ISC_FAR *,
					 unsigned char ISC_FAR *,
					 short,
					 short,
					 short,
					 ISC_BLOB_DESC ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_cancel_blob_t) (ISC_STATUS ISC_FAR *, 
				        isc_blob_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_cancel_events_t) (ISC_STATUS ISC_FAR *, 
					  isc_db_handle ISC_FAR *, 
					  ISC_LONG ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_close_blob_t) (ISC_STATUS ISC_FAR *, 
				       isc_blob_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_commit_retaining_t) (ISC_STATUS ISC_FAR *, 
					     isc_tr_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_commit_transaction_t) (ISC_STATUS ISC_FAR *, 
					       isc_tr_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_create_blob_t) (ISC_STATUS ISC_FAR *, 
					isc_db_handle ISC_FAR *, 
					isc_tr_handle ISC_FAR *, 
					isc_blob_handle ISC_FAR *, 
					ISC_QUAD ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_create_blob2_t) (ISC_STATUS ISC_FAR *, 
					 isc_db_handle ISC_FAR *, 
					 isc_tr_handle ISC_FAR *, 
					 isc_blob_handle ISC_FAR *, 
					 ISC_QUAD ISC_FAR *, 
					 short,  
					 char ISC_FAR *); 

typedef ISC_STATUS  (ISC_EXPORT *isc_create_database_t) (ISC_STATUS ISC_FAR *, 
					    short, 
					    char ISC_FAR *, 
					    isc_db_handle ISC_FAR *, 
					    short, 
					    char ISC_FAR *, 
					    short);

typedef ISC_STATUS  (ISC_EXPORT *isc_database_info_t) (ISC_STATUS ISC_FAR *, 
					  isc_db_handle ISC_FAR *, 
					  short, 
					  char ISC_FAR *, 
					  short, 
					  char ISC_FAR *);

typedef void        (ISC_EXPORT *isc_decode_date_t) (ISC_QUAD ISC_FAR *, 
					void ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_detach_database_t) (ISC_STATUS ISC_FAR *,  
					    isc_db_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_drop_database_t) (ISC_STATUS ISC_FAR *,  
					  isc_db_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_allocate_statement_t) (ISC_STATUS ISC_FAR *, 
						    isc_db_handle ISC_FAR *, 
						    isc_stmt_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_alloc_statement2_t) (ISC_STATUS ISC_FAR *, 
						  isc_db_handle ISC_FAR *, 
						  isc_stmt_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_describe_t) (ISC_STATUS ISC_FAR *, 
					  isc_stmt_handle ISC_FAR *, 
					  unsigned short, 
					  XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_describe_bind_t) (ISC_STATUS ISC_FAR *, 
					       isc_stmt_handle ISC_FAR *, 
					       unsigned short, 
					       XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_exec_immed2_t) (ISC_STATUS ISC_FAR *, 
					     isc_db_handle ISC_FAR *, 
					     isc_tr_handle ISC_FAR *, 
					     unsigned short, 
					     char ISC_FAR *, 
					     unsigned short, 
					     XSQLDA ISC_FAR *, 
					     XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_execute_t) (ISC_STATUS ISC_FAR *, 
					 isc_tr_handle ISC_FAR *,
					 isc_stmt_handle ISC_FAR *, 
					 unsigned short, 
					 XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_execute2_t) (ISC_STATUS ISC_FAR *, 
					  isc_tr_handle ISC_FAR *,
					  isc_stmt_handle ISC_FAR *, 
					  unsigned short, 
					  XSQLDA ISC_FAR *,
					  XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_execute_immediate_t) (ISC_STATUS ISC_FAR *, 
						   isc_db_handle ISC_FAR *, 
						   isc_tr_handle ISC_FAR *, 
						   unsigned short, 
						   char ISC_FAR *, 
						   unsigned short, 
						   XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_fetch_t) (ISC_STATUS ISC_FAR *, 
				       isc_stmt_handle ISC_FAR *, 
				       unsigned short, 
				       XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_finish_t) (isc_db_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_free_statement_t) (ISC_STATUS ISC_FAR *, 
						isc_stmt_handle ISC_FAR *, 
						unsigned short);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_insert_t) (ISC_STATUS ISC_FAR *, 
				       isc_stmt_handle ISC_FAR *, 
				       unsigned short, 
				       XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_prepare_t) (ISC_STATUS ISC_FAR *, 
					 isc_tr_handle ISC_FAR *, 
					 isc_stmt_handle ISC_FAR *, 
					 unsigned short, 
					 char ISC_FAR *, 
					 unsigned short, 
				 	 XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_set_cursor_name_t) (ISC_STATUS ISC_FAR *, 
						 isc_stmt_handle ISC_FAR *, 
						 char ISC_FAR *, 
						 unsigned short);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_sql_info_t) (ISC_STATUS ISC_FAR *, 
					  isc_stmt_handle ISC_FAR *, 
					  short, 
					  char ISC_FAR *, 
					  short, 
					  char ISC_FAR *);

typedef void        (ISC_EXPORT *isc_encode_date_t) (void ISC_FAR *, 
					ISC_QUAD ISC_FAR *);

typedef ISC_LONG    (ISC_EXPORT_VARARG *isc_event_block_t) (char ISC_FAR * ISC_FAR *, 
					       char ISC_FAR * ISC_FAR *, 
					       unsigned short, ...);

typedef void       (ISC_EXPORT *isc_event_counts_t) (ISC_ULONG ISC_FAR *, 
					 short, 
					 char ISC_FAR *,
					 char ISC_FAR *);

typedef void        (ISC_EXPORT_VARARG *isc_expand_dpb_t) (char ISC_FAR * ISC_FAR *, 
					      short ISC_FAR *, ...);

typedef int        (ISC_EXPORT *isc_modify_dpb_t) (char ISC_FAR * ISC_FAR *, 
					 short ISC_FAR *, unsigned short,
					 char ISC_FAR *, short );

typedef ISC_LONG    (ISC_EXPORT *isc_free_t) (char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_get_segment_t) (ISC_STATUS ISC_FAR *, 
				        isc_blob_handle ISC_FAR *, 
				        unsigned short ISC_FAR *, 
				        unsigned short, 
				        char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_get_slice_t) (ISC_STATUS ISC_FAR *, 
				      isc_db_handle ISC_FAR *, 
				      isc_tr_handle ISC_FAR *, 
 				      ISC_QUAD ISC_FAR *, 
 				      short, 
				      char ISC_FAR *, 
				      short, 
				      ISC_LONG ISC_FAR *, 
				      ISC_LONG, 
				      void ISC_FAR *, 
				      ISC_LONG ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_interprete_t) (char ISC_FAR *, 
				       ISC_STATUS ISC_FAR * ISC_FAR *);

/* Firebird safe string verison of isc_interprete */
typedef ISC_STATUS  (ISC_EXPORT *fb_interpret_t)(char ISC_FAR *,
					  unsigned int,
					  ISC_STATUS ISC_FAR * ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_open_blob_t) (ISC_STATUS ISC_FAR *, 
				      isc_db_handle ISC_FAR *, 
				      isc_tr_handle ISC_FAR *, 
				      isc_blob_handle ISC_FAR *, 
				      ISC_QUAD ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_open_blob2_t) (ISC_STATUS ISC_FAR *, 
				       isc_db_handle ISC_FAR *, 
				       isc_tr_handle ISC_FAR *,
				       isc_blob_handle ISC_FAR *, 
				       ISC_QUAD ISC_FAR *, 
				       short,  
				       char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_prepare_transaction2_t) (ISC_STATUS ISC_FAR *, 
						 isc_tr_handle ISC_FAR *, 
						 short, 
						 char ISC_FAR *);

typedef void        (ISC_EXPORT *isc_print_sqlerror_t) (short, 
					   ISC_STATUS ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_print_status_t) (ISC_STATUS ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_put_segment_t) (ISC_STATUS ISC_FAR *, 
					isc_blob_handle ISC_FAR *, 
					unsigned short, 
					char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_put_slice_t) (ISC_STATUS ISC_FAR *, 
				      isc_db_handle ISC_FAR *, 
				      isc_tr_handle ISC_FAR *, 
				      ISC_QUAD ISC_FAR *, 
				      short, 
				      char ISC_FAR *, 
				      short, 
				      ISC_LONG ISC_FAR *, 
				      ISC_LONG, 
				      void ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_que_events_t) (ISC_STATUS ISC_FAR *, 
				       isc_db_handle ISC_FAR *, 
				       ISC_LONG ISC_FAR *, 
				       short, 
				       char ISC_FAR *, 
				       isc_callback, 
				       void ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_rollback_transaction_t) (ISC_STATUS ISC_FAR *, 
						 isc_tr_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_start_multiple_t) (ISC_STATUS ISC_FAR *, 
					   isc_tr_handle ISC_FAR *, 
					   short, 
					   void ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT_VARARG *isc_start_transaction_t) (ISC_STATUS ISC_FAR *, 
						     isc_tr_handle ISC_FAR *,
						     short, ...);

typedef ISC_LONG    (ISC_EXPORT *isc_sqlcode_t) (ISC_STATUS ISC_FAR *);

typedef void        (ISC_EXPORT *isc_sql_interprete_t) (short, 
					   char ISC_FAR *, 
					   short);

typedef ISC_STATUS  (ISC_EXPORT *isc_transaction_info_t) (ISC_STATUS ISC_FAR *,  
					     isc_tr_handle ISC_FAR *, 
					     short, 
					     char ISC_FAR *, 
					     short,  
					     char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_transact_request_t) (ISC_STATUS ISC_FAR *,  
					     isc_db_handle ISC_FAR *, 
					     isc_tr_handle ISC_FAR *,
					     unsigned short, 
					     char ISC_FAR *, 
					     unsigned short,  
					     char ISC_FAR *,
					     unsigned short,
					     char ISC_FAR *);

typedef ISC_LONG    (ISC_EXPORT *isc_vax_integer_t) (char ISC_FAR *, 
					short);


typedef int (ISC_EXPORT *isc_add_user_t) (ISC_STATUS ISC_FAR *, USER_SEC_DATA *);

typedef int (ISC_EXPORT *isc_delete_user_t) (ISC_STATUS ISC_FAR *, USER_SEC_DATA *);

typedef int (ISC_EXPORT *isc_modify_user_t) (ISC_STATUS ISC_FAR *, USER_SEC_DATA *);

                                          
typedef ISC_STATUS  (ISC_EXPORT *isc_compile_request_t) (ISC_STATUS ISC_FAR *, 
					    isc_db_handle ISC_FAR *,
		  			    isc_req_handle ISC_FAR *, 
					    short, 
					    char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_compile_request2_t) (ISC_STATUS ISC_FAR *, 
					     isc_db_handle ISC_FAR *,
					     isc_req_handle ISC_FAR *, 
					     short, 
					     char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_ddl_t) (ISC_STATUS ISC_FAR *,
			        isc_db_handle ISC_FAR *, 
			        isc_tr_handle ISC_FAR *,
			        short, 
			        char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_prepare_transaction_t) (ISC_STATUS ISC_FAR *, 
						isc_tr_handle ISC_FAR *);


typedef ISC_STATUS  (ISC_EXPORT *isc_receive_t) (ISC_STATUS ISC_FAR *, 
				    isc_req_handle ISC_FAR *, 
				    short, 
			 	    short, 
				    void ISC_FAR *, 
				    short);

typedef ISC_STATUS  (ISC_EXPORT *isc_reconnect_transaction_t) (ISC_STATUS ISC_FAR *,
						  isc_db_handle ISC_FAR *, 
						  isc_tr_handle ISC_FAR *, 
						  short, 
						  char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_release_request_t) (ISC_STATUS ISC_FAR *, 
					    isc_req_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_request_info_t) (ISC_STATUS ISC_FAR *,  
					 isc_req_handle ISC_FAR *, 
					 short, 
	  				 short, 
					 char ISC_FAR *, 
					 short, 
					 char ISC_FAR *);	 

typedef ISC_STATUS  (ISC_EXPORT *isc_seek_blob_t) (ISC_STATUS ISC_FAR *, 
				      isc_blob_handle ISC_FAR *, 
				      short, 
				      ISC_LONG, 
				      ISC_LONG ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_send_t) (ISC_STATUS ISC_FAR *, 
				 isc_req_handle ISC_FAR *, 
				 short, 
				 short,
				 void ISC_FAR *, 
				 short);

typedef ISC_STATUS  (ISC_EXPORT *isc_start_and_send_t) (ISC_STATUS ISC_FAR *, 
					   isc_req_handle ISC_FAR *, 
					   isc_tr_handle ISC_FAR *, 
					   short, 
					   short, 
					   void ISC_FAR *, 
					   short);

typedef ISC_STATUS  (ISC_EXPORT *isc_start_request_t) (ISC_STATUS ISC_FAR *, 
					  isc_req_handle ISC_FAR *,
					  isc_tr_handle ISC_FAR *,
					  short);

typedef ISC_STATUS  (ISC_EXPORT *isc_unwind_request_t) (ISC_STATUS ISC_FAR *, 
					   isc_tr_handle ISC_FAR *,
					   short);

typedef ISC_STATUS  (ISC_EXPORT *isc_wait_for_event_t) (ISC_STATUS ISC_FAR *, 
					   isc_db_handle ISC_FAR *, 
					   short, 
					   char ISC_FAR *, 
					   char ISC_FAR *);


typedef ISC_STATUS  (ISC_EXPORT *isc_close_t) (ISC_STATUS ISC_FAR *, 
				  char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_declare_t) (ISC_STATUS ISC_FAR *, 
				    char ISC_FAR *, 
				    char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_execute_immediate_t) (ISC_STATUS ISC_FAR *, 
					      isc_db_handle ISC_FAR *,
					      isc_tr_handle ISC_FAR *, 
					      short ISC_FAR *, 
					      char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_execute_m_t) (ISC_STATUS ISC_FAR *, 
					   isc_tr_handle ISC_FAR *,
					   isc_stmt_handle ISC_FAR *, 
					   unsigned short, 
					   char ISC_FAR *, 
					   unsigned short, 
					   unsigned short, 
					   char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_execute2_m_t) (ISC_STATUS ISC_FAR *, 
					   isc_tr_handle ISC_FAR *,
					   isc_stmt_handle ISC_FAR *, 
					   unsigned short, 
					   char ISC_FAR *, 
					   unsigned short, 
					   unsigned short, 
					   char ISC_FAR *,
					   unsigned short, 
					   char ISC_FAR *, 
					   unsigned short, 
					   unsigned short, 
					   char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_execute_immediate_m_t) (ISC_STATUS ISC_FAR *, 
						     isc_db_handle ISC_FAR *, 
						     isc_tr_handle ISC_FAR *, 
						     unsigned short, 
						     char ISC_FAR *, 
						     unsigned short, 
						     unsigned short, 
						     char ISC_FAR *,
						     unsigned short,
						     unsigned short,
						     char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_exec_immed3_m_t) (ISC_STATUS ISC_FAR *, 
					       isc_db_handle ISC_FAR *, 
					       isc_tr_handle ISC_FAR *, 
					       unsigned short, 
					       char ISC_FAR *, 
					       unsigned short, 
					       unsigned short, 
					       char ISC_FAR *,
					       unsigned short,
					       unsigned short,
					       char ISC_FAR *,
					       unsigned short, 
					       char ISC_FAR *,
					       unsigned short,
					       unsigned short,
					       char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_fetch_m_t) (ISC_STATUS ISC_FAR *, 
					 isc_stmt_handle ISC_FAR *, 
					 unsigned short, 
					 char ISC_FAR *, 
					 unsigned short, 
					 unsigned short, 
					 char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_insert_m_t) (ISC_STATUS ISC_FAR *, 
					  isc_stmt_handle ISC_FAR *, 
					  unsigned short, 
					  char ISC_FAR *, 
					  unsigned short, 
					  unsigned short, 
					  char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_prepare_m_t) (ISC_STATUS ISC_FAR *, 
					   isc_tr_handle ISC_FAR *,
				 	   isc_stmt_handle ISC_FAR *, 
					   unsigned short,  
					   char ISC_FAR *, 
					   unsigned short,
					   unsigned short, 
				  	   char ISC_FAR *, 
				 	   unsigned short,
					   char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_dsql_release_t) (ISC_STATUS ISC_FAR *, 
					 char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_close_t) (ISC_STATUS ISC_FAR *, 
					     char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_declare_t) (ISC_STATUS ISC_FAR *, 
					      char ISC_FAR *, 
					      char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_describe_t) (ISC_STATUS ISC_FAR *, 
						char ISC_FAR *, 
						unsigned short, 
						XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_describe_bind_t) (ISC_STATUS ISC_FAR *, 
						     char ISC_FAR *, 
						     unsigned short, 
						     XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_execute_t) (ISC_STATUS ISC_FAR *, 
					       isc_tr_handle ISC_FAR *,
					       char ISC_FAR *, 
					       unsigned short, 
					       XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_execute2_t) (ISC_STATUS ISC_FAR *,
						isc_tr_handle ISC_FAR *,
						char ISC_FAR *,
						unsigned short,
						XSQLDA ISC_FAR *,
						XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_execute_immed_t) (ISC_STATUS ISC_FAR *, 
						     isc_db_handle ISC_FAR *, 
						     isc_tr_handle ISC_FAR *, 
						     unsigned short, 
						     char ISC_FAR *, 	
						     unsigned short, 
						     XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_fetch_t) (ISC_STATUS ISC_FAR *, 
					     char ISC_FAR *, 
					     unsigned short, 
					     XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_open_t) (ISC_STATUS ISC_FAR *, 
					    isc_tr_handle ISC_FAR *, 
					    char ISC_FAR *, 
					    unsigned short, 
					    XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_open2_t) (ISC_STATUS ISC_FAR *, 
					     isc_tr_handle ISC_FAR *, 
					     char ISC_FAR *, 
					     unsigned short, 
					     XSQLDA ISC_FAR *,
					     XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_insert_t) (ISC_STATUS ISC_FAR *, 
					      char ISC_FAR *, 
					      unsigned short, 
					      XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_prepare_t) (ISC_STATUS ISC_FAR *, 
					       isc_db_handle ISC_FAR *,
					       isc_tr_handle ISC_FAR *, 
					       char ISC_FAR *, 
					       unsigned short, 
					       char ISC_FAR *, 
					       unsigned short, 
					       XSQLDA ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_embed_dsql_release_t) (ISC_STATUS ISC_FAR *, 
					       char ISC_FAR *);

typedef BSTREAM     ISC_FAR * (ISC_EXPORT *BLOB_open_t) (isc_blob_handle,  
				        char ISC_FAR *,  
				        int);

typedef int  	    (ISC_EXPORT *BLOB_put_t) (char, 
				 BSTREAM ISC_FAR *);

typedef int  	    (ISC_EXPORT *BLOB_close_t) (BSTREAM ISC_FAR *);

typedef int  	    (ISC_EXPORT *BLOB_get_t) (BSTREAM ISC_FAR *);

typedef int         (ISC_EXPORT *BLOB_display_t) (ISC_QUAD ISC_FAR *, 
				     isc_db_handle, 
				     isc_tr_handle,
				     char ISC_FAR *);

typedef int         (ISC_EXPORT *BLOB_dump_t) (ISC_QUAD ISC_FAR *, 
				  isc_db_handle, 
				  isc_tr_handle,
				  char ISC_FAR *);

typedef int         (ISC_EXPORT *BLOB_edit_t) (ISC_QUAD ISC_FAR *, 
				  isc_db_handle, 
				  isc_tr_handle,
				  char ISC_FAR *);

typedef int         (ISC_EXPORT *BLOB_load_t) (ISC_QUAD ISC_FAR *, 
				  isc_db_handle, 
				  isc_tr_handle,
				  char ISC_FAR *);

typedef int         (ISC_EXPORT *BLOB_text_dump_t) (ISC_QUAD ISC_FAR *, 
				  isc_db_handle, 
				  isc_tr_handle,
				  char ISC_FAR *);

typedef int         (ISC_EXPORT *BLOB_text_load_t) (ISC_QUAD ISC_FAR *, 
				  isc_db_handle, 
				  isc_tr_handle,
				  char ISC_FAR *);

typedef BSTREAM     ISC_FAR * (ISC_EXPORT *Bopen_t) (ISC_QUAD ISC_FAR *, 
			       	    isc_db_handle, 
			       	    isc_tr_handle,  
			       	    char ISC_FAR *);

typedef BSTREAM     ISC_FAR * (ISC_EXPORT *Bopen2_t) (ISC_QUAD ISC_FAR *, 
				     isc_db_handle,  
				     isc_tr_handle,  
				     char ISC_FAR *,
				     unsigned short);


typedef ISC_LONG    (ISC_EXPORT *isc_ftof_t) (char ISC_FAR *, 
				 unsigned short, 
				 char ISC_FAR *, 
				 unsigned short);

typedef ISC_STATUS  (ISC_EXPORT *isc_print_blr_t) (char ISC_FAR *, 
				      isc_callback, 
				      void ISC_FAR *, 
				      short);

typedef void        (ISC_EXPORT *isc_set_debug_t) (int);

typedef void        (ISC_EXPORT *isc_qtoq_t) (ISC_QUAD ISC_FAR *, 
				 ISC_QUAD ISC_FAR *);

typedef void        (ISC_EXPORT *isc_vtof_t) (char ISC_FAR *, 
				 char ISC_FAR *,
				 unsigned short);

typedef void        (ISC_EXPORT *isc_vtov_t) (char ISC_FAR *, 
				 char ISC_FAR *, 
				 short);

typedef int         (ISC_EXPORT *isc_version_t) (isc_db_handle ISC_FAR *, 
				    isc_callback, 
				    void ISC_FAR *);

typedef ISC_LONG    (ISC_EXPORT *isc_reset_fpe_t) (unsigned short);


typedef ISC_STATUS  (ISC_EXPORT *isc_attach_service_t) (ISC_STATUS ISC_FAR *, 
					   unsigned short, 
					   char ISC_FAR *,
					   isc_svc_handle ISC_FAR *, 
					   unsigned short, 
					   char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_detach_service_t) (ISC_STATUS ISC_FAR *, 
					   isc_svc_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_query_service_t) (ISC_STATUS ISC_FAR *, 
					  isc_svc_handle ISC_FAR *,
					  unsigned short, 
					  char ISC_FAR *, 
					  unsigned short, 
					  char ISC_FAR *, 
					  unsigned short, 
					  char ISC_FAR *);

/* InterBase API
typedef ISC_STATUS  (ISC_EXPORT *isc_compile_map_t) (ISC_STATUS ISC_FAR *, 
					isc_form_handle ISC_FAR *,
					isc_req_handle ISC_FAR *, 
					short ISC_FAR *, 
					char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_compile_menu_t) (ISC_STATUS ISC_FAR *, 
					 isc_form_handle ISC_FAR *,
					 isc_req_handle ISC_FAR *, 
					 short ISC_FAR *, 
				 	 char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_compile_sub_map_t) (ISC_STATUS ISC_FAR *, 
					    isc_win_handle ISC_FAR *,
					    isc_req_handle ISC_FAR *, 
					    short ISC_FAR *, 
					    char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_create_window_t) (ISC_STATUS ISC_FAR *, 
					  isc_win_handle ISC_FAR *, 
					  short ISC_FAR *, 
					  char ISC_FAR *, 
					  short ISC_FAR *, 
					  short ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_delete_window_t) (ISC_STATUS ISC_FAR *, 
					  isc_win_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_drive_form_t) (ISC_STATUS ISC_FAR *, 
				       isc_db_handle ISC_FAR *, 
				       isc_tr_handle ISC_FAR *, 
				       isc_win_handle ISC_FAR *, 
				       isc_req_handle ISC_FAR *, 
				       unsigned char ISC_FAR *, 
				       unsigned char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_drive_menu_t) (ISC_STATUS ISC_FAR *, 
				       isc_win_handle ISC_FAR *, 
				       isc_req_handle ISC_FAR *, 
				       short ISC_FAR *, 
				       char ISC_FAR *, 
				       short ISC_FAR *, 
				       char ISC_FAR *,
				       short ISC_FAR *, 
				       short ISC_FAR *, 
				       char ISC_FAR *, 
				       ISC_LONG ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_form_delete_t) (ISC_STATUS ISC_FAR *, 
					isc_form_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_form_fetch_t) (ISC_STATUS ISC_FAR *, 
				       isc_db_handle ISC_FAR *, 
				       isc_tr_handle ISC_FAR *, 
				       isc_req_handle ISC_FAR *, 
				       unsigned char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_form_insert_t) (ISC_STATUS ISC_FAR *, 
					isc_db_handle ISC_FAR *, 
					isc_tr_handle ISC_FAR *, 
					isc_req_handle ISC_FAR *, 
					unsigned char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_get_entree_t) (ISC_STATUS ISC_FAR *, 
				       isc_req_handle ISC_FAR *, 
				       short ISC_FAR *, 
				       char ISC_FAR *, 
				       ISC_LONG ISC_FAR *, 
				       short ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_initialize_menu_t) (ISC_STATUS ISC_FAR *, 
					    isc_req_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_menu_t) (ISC_STATUS ISC_FAR *, 
				 isc_win_handle ISC_FAR *, 
				 isc_req_handle ISC_FAR *, 
			 	 short ISC_FAR *, 
				 char ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_load_form_t) (ISC_STATUS ISC_FAR *, 
				      isc_db_handle ISC_FAR *, 
				      isc_tr_handle ISC_FAR *, 
				      isc_form_handle ISC_FAR *, 
				      short ISC_FAR *, 
				      char ISC_FAR *);
																
typedef ISC_STATUS  (ISC_EXPORT *isc_pop_window_t) (ISC_STATUS ISC_FAR *, 
				       isc_win_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_put_entree_t) (ISC_STATUS ISC_FAR *, 
				       isc_req_handle ISC_FAR *, 
				       short ISC_FAR *, 
				       char ISC_FAR *, 
				       ISC_LONG ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_reset_form_t) (ISC_STATUS ISC_FAR *, 
				       isc_req_handle ISC_FAR *);

typedef ISC_STATUS  (ISC_EXPORT *isc_suspend_window_t) (ISC_STATUS ISC_FAR *, 
					   isc_win_handle ISC_FAR *);
*/

// API declarations
class SQLAPI_API ibAPI : public saAPI
{
public:
	ibAPI();

	isc_add_user_t				isc_add_user;
	isc_array_gen_sdl_t			isc_array_gen_sdl;
	isc_array_get_slice_t		isc_array_get_slice;
	isc_array_lookup_bounds_t	isc_array_lookup_bounds;
	isc_array_lookup_desc_t		isc_array_lookup_desc;
	isc_array_put_slice_t		isc_array_put_slice;
	isc_array_set_desc_t		isc_array_set_desc;
	isc_attach_database_t		isc_attach_database;
	isc_blob_default_desc_t		isc_blob_default_desc;
	isc_blob_gen_bpb_t			isc_blob_gen_bpb;
	isc_blob_info_t				isc_blob_info;
	isc_blob_lookup_desc_t		isc_blob_lookup_desc;
	isc_blob_set_desc_t			isc_blob_set_desc;
	isc_cancel_blob_t			isc_cancel_blob;
	isc_cancel_events_t			isc_cancel_events;
	isc_close_blob_t			isc_close_blob;
	isc_commit_retaining_t		isc_commit_retaining;
	isc_commit_transaction_t	isc_commit_transaction;
	isc_create_blob_t			isc_create_blob;
	isc_create_blob2_t			isc_create_blob2;
	isc_create_database_t		isc_create_database;
	isc_database_info_t			isc_database_info;
	isc_decode_date_t			isc_decode_date;
	isc_detach_database_t		isc_detach_database;
	isc_drop_database_t			isc_drop_database;
	isc_dsql_allocate_statement_t	isc_dsql_allocate_statement;
	isc_dsql_alloc_statement2_t	isc_dsql_alloc_statement2;
	isc_dsql_describe_t	isc_dsql_describe;
	isc_dsql_describe_bind_t	isc_dsql_describe_bind;
	isc_dsql_exec_immed2_t	isc_dsql_exec_immed2;
	isc_dsql_execute_t	isc_dsql_execute;
	isc_dsql_execute2_t	isc_dsql_execute2;
	isc_dsql_execute_immediate_t	isc_dsql_execute_immediate;
	isc_dsql_fetch_t	isc_dsql_fetch;
	isc_dsql_finish_t	isc_dsql_finish;
	isc_dsql_free_statement_t	isc_dsql_free_statement;
	isc_dsql_insert_t	isc_dsql_insert;
	isc_dsql_prepare_t	isc_dsql_prepare;
	isc_dsql_set_cursor_name_t	isc_dsql_set_cursor_name;
	isc_dsql_sql_info_t	isc_dsql_sql_info;
	isc_encode_date_t	isc_encode_date;
	isc_event_block_t	isc_event_block;
	isc_event_counts_t	isc_event_counts;
	isc_expand_dpb_t	isc_expand_dpb;
	isc_modify_dpb_t	isc_modify_dpb;
	isc_free_t	isc_free;
	isc_get_segment_t	isc_get_segment;
	isc_get_slice_t	isc_get_slice;
	isc_interprete_t	isc_interprete;
	isc_open_blob_t	isc_open_blob;
	isc_open_blob2_t	isc_open_blob2;
	isc_prepare_transaction2_t	isc_prepare_transaction2;
	isc_print_sqlerror_t	isc_print_sqlerror;
	isc_print_status_t	isc_print_status;
	isc_put_segment_t	isc_put_segment;
	isc_put_slice_t	isc_put_slice;
	isc_que_events_t	isc_que_events;
	isc_rollback_transaction_t	isc_rollback_transaction;
	isc_start_multiple_t	isc_start_multiple;
	isc_start_transaction_t	isc_start_transaction;
	isc_sqlcode_t	isc_sqlcode;
	isc_sql_interprete_t	isc_sql_interprete;
	isc_transaction_info_t	isc_transaction_info;
	isc_transact_request_t	isc_transact_request;
	isc_vax_integer_t	isc_vax_integer;
	isc_delete_user_t	isc_delete_user;
	isc_modify_user_t	isc_modify_user;
	isc_compile_request_t	isc_compile_request;
	isc_compile_request2_t	isc_compile_request2;
	isc_ddl_t	isc_ddl;
	isc_prepare_transaction_t	isc_prepare_transaction;
	isc_receive_t	isc_receive;
	isc_reconnect_transaction_t	isc_reconnect_transaction;
	isc_release_request_t	isc_release_request;
	isc_request_info_t	isc_request_info;
	isc_seek_blob_t	isc_seek_blob;
	isc_send_t	isc_send;
	isc_start_and_send_t	isc_start_and_send;
	isc_start_request_t	isc_start_request;
	isc_unwind_request_t	isc_unwind_request;
	isc_wait_for_event_t	isc_wait_for_event;
	isc_close_t	isc_close;
	isc_declare_t	isc_declare;
	isc_execute_immediate_t	isc_execute_immediate;
	isc_dsql_execute_m_t	isc_dsql_execute_m;
	isc_dsql_execute2_m_t	isc_dsql_execute2_m;
	isc_dsql_execute_immediate_m_t	isc_dsql_execute_immediate_m;
	isc_dsql_exec_immed3_m_t	isc_dsql_exec_immed3_m;
	isc_dsql_fetch_m_t	isc_dsql_fetch_m;
	isc_dsql_insert_m_t	isc_dsql_insert_m;
	isc_dsql_prepare_m_t	isc_dsql_prepare_m;
	isc_dsql_release_t	isc_dsql_release;
	isc_embed_dsql_close_t	isc_embed_dsql_close;
	isc_embed_dsql_declare_t	isc_embed_dsql_declare;
	isc_embed_dsql_describe_t	isc_embed_dsql_describe;
	isc_embed_dsql_describe_bind_t	isc_embed_dsql_describe_bind;
	isc_embed_dsql_execute_t	isc_embed_dsql_execute;
	isc_embed_dsql_execute2_t	isc_embed_dsql_execute2;
	isc_embed_dsql_execute_immed_t	isc_embed_dsql_execute_immed;
	isc_embed_dsql_fetch_t	isc_embed_dsql_fetch;
	isc_embed_dsql_open_t	isc_embed_dsql_open;
	isc_embed_dsql_open2_t	isc_embed_dsql_open2;
	isc_embed_dsql_insert_t	isc_embed_dsql_insert;
	isc_embed_dsql_prepare_t	isc_embed_dsql_prepare;
	isc_embed_dsql_release_t	isc_embed_dsql_release;
	isc_ftof_t	isc_ftof;
	isc_print_blr_t	isc_print_blr;
	isc_set_debug_t	isc_set_debug;
	isc_qtoq_t	isc_qtoq;
	isc_vtof_t	isc_vtof;
	isc_vtov_t	isc_vtov;
	isc_version_t	isc_version;
	fb_interpret_t	fb_interpret;

};

class SQLAPI_API ibConnectionHandles : public saConnectionHandles
{
public:
	ibConnectionHandles();

	isc_db_handle	m_db_handle;			// Database handle
	isc_tr_handle	m_tr_handle;			// Transaction handle
};

class SQLAPI_API ibCommandHandles : public saCommandHandles
{
public:
	ibCommandHandles();

	isc_stmt_handle	m_stmt_handle;
};

extern ibAPI g_ibAPI;

#endif // !defined(__IBAPI_H__)
