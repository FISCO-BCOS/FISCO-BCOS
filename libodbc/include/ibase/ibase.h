/*
 *	MODULE:		ibase.h
 *	DESCRIPTION:	OSRI entrypoints and defines
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 *
 * 2001.07.28: John Bellardo:  Added blr_skip
 * 2001.09.18: Ann Harrison:   New info codes
 * 17-Oct-2001 Mike Nordell: CPU affinity
 * 2001-04-16 Paul Beach: ISC_TIME_SECONDS_PRECISION_SCALE modified for HP10
 * Compiler Compatibility
 * 2002.02.15 Sean Leyne - Code Cleanup, removed obsolete ports:
 *                          - EPSON, XENIX, MAC (MAC_AUX), Cray and OS/2
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 *
 * 2002.10.29 Sean Leyne - Removed support for obsolete IPX/SPX Protocol
 *
 * 2006.09.06 Steve Boyd - Added various prototypes required by Cobol ESQL
 *                         isc_embed_dsql_length
 *                         isc_event_block_a
 *                         isc_sqlcode_s
 *                         isc_embed_dsql_fetch_a
 *                         isc_event_block_s
 *                         isc_baddress
 *                         isc_baddress_s
 *
 */

#ifndef JRD_IBASE_H
#define JRD_IBASE_H

#define FB_API_VER 20
#define isc_version4

#define  ISC_TRUE	1
#define  ISC_FALSE	0
#if !(defined __cplusplus)
#define  ISC__TRUE	ISC_TRUE
#define  ISC__FALSE	ISC_FALSE
#endif

#define ISC_FAR

#if defined _MSC_VER && _MSC_VER >= 1300
#define FB_API_DEPRECATED __declspec(deprecated)
#elif defined __GNUC__ && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 2))
#define FB_API_DEPRECATED __attribute__((__deprecated__))
#else
#define FB_API_DEPRECATED
#endif


#ifndef INCLUDE_TYPES_PUB_H
#define INCLUDE_TYPES_PUB_H

#include <stddef.h>

#if defined(__GNUC__)
#include <inttypes.h>
#else

#if !defined(_INTPTR_T_DEFINED)
#if defined(_WIN64)
typedef __int64 intptr_t;
typedef unsigned __int64 uintptr_t;
#else
typedef long intptr_t;
typedef unsigned long uintptr_t;
#endif
#endif

#endif

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64) 
typedef unsigned int    FB_API_HANDLE;
#else
typedef void*           FB_API_HANDLE;
#endif

typedef intptr_t ISC_STATUS;

#define ISC_STATUS_LENGTH       20
typedef ISC_STATUS ISC_STATUS_ARRAY[ISC_STATUS_LENGTH];

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
        #define  ISC_EXPORT     __stdcall
        #define  ISC_EXPORT_VARARG      __cdecl
#else
        #define  ISC_EXPORT
        #define  ISC_EXPORT_VARARG
#endif

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__)
typedef int             ISC_LONG;
typedef unsigned int    ISC_ULONG;
#else
typedef signed long     ISC_LONG;
typedef unsigned long   ISC_ULONG;
#endif

typedef signed short    ISC_SHORT;
typedef unsigned short  ISC_USHORT;

typedef unsigned char   ISC_UCHAR;
typedef char            ISC_SCHAR;

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32__)) && !defined(__GNUC__)
typedef __int64                         ISC_INT64;
typedef unsigned __int64        ISC_UINT64;
#else
typedef long long int                   ISC_INT64;
typedef unsigned long long int  ISC_UINT64;
#endif

#ifndef ISC_TIMESTAMP_DEFINED
typedef int                     ISC_DATE;
typedef unsigned int    ISC_TIME;
typedef struct
{
        ISC_DATE timestamp_date;
        ISC_TIME timestamp_time;
} ISC_TIMESTAMP;
#define ISC_TIMESTAMP_DEFINED
#endif  

struct GDS_QUAD_t {
        ISC_LONG gds_quad_high;
        ISC_ULONG gds_quad_low;
};

typedef struct GDS_QUAD_t GDS_QUAD;
typedef struct GDS_QUAD_t ISC_QUAD;

#define isc_quad_high   gds_quad_high
#define isc_quad_low    gds_quad_low

#endif 

/********************************/
/* Firebird Handle Definitions */
/********************************/

typedef FB_API_HANDLE isc_att_handle;
typedef FB_API_HANDLE isc_blob_handle;
typedef FB_API_HANDLE isc_db_handle;
typedef FB_API_HANDLE isc_req_handle;
typedef FB_API_HANDLE isc_stmt_handle;
typedef FB_API_HANDLE isc_svc_handle;
typedef FB_API_HANDLE isc_tr_handle;
typedef void (* isc_callback) ();
typedef ISC_LONG isc_resv_handle;

typedef void (*ISC_PRINT_CALLBACK) (void*, ISC_SHORT, const char*);
typedef void (*ISC_VERSION_CALLBACK)(void*, const char*);
typedef void (*ISC_EVENT_CALLBACK)(void*, ISC_USHORT, const ISC_UCHAR*);

/*******************************************************************/
/* Blob id structure                                               */
/*******************************************************************/

#if !(defined __cplusplus)
typedef GDS_QUAD GDS__QUAD;
#endif /* !(defined __cplusplus) */

typedef struct
{
	short array_bound_lower;
	short array_bound_upper;
} ISC_ARRAY_BOUND;

typedef struct
{
	ISC_UCHAR	array_desc_dtype;
	ISC_SCHAR			array_desc_scale;
	unsigned short	array_desc_length;
	ISC_SCHAR			array_desc_field_name[32];
	ISC_SCHAR			array_desc_relation_name[32];
	short			array_desc_dimensions;
	short			array_desc_flags;
	ISC_ARRAY_BOUND	array_desc_bounds[16];
} ISC_ARRAY_DESC;

typedef struct
{
	short			blob_desc_subtype;
	short			blob_desc_charset;
	short			blob_desc_segment_size;
	ISC_UCHAR	blob_desc_field_name[32];
	ISC_UCHAR	blob_desc_relation_name[32];
} ISC_BLOB_DESC;

/***************************/
/* Blob control structure  */
/***************************/

typedef struct isc_blob_ctl
{
	ISC_STATUS	(* ctl_source)();	/* Source filter */
	struct isc_blob_ctl*	ctl_source_handle;	/* Argument to pass to source filter */
	short					ctl_to_sub_type;		/* Target type */
	short					ctl_from_sub_type;		/* Source type */
	unsigned short			ctl_buffer_length;		/* Length of buffer */
	unsigned short			ctl_segment_length;		/* Length of current segment */
	unsigned short			ctl_bpb_length;			/* Length of blob parameter  block */
	/* Internally, this is const UCHAR*, but this public struct probably can't change. */
	ISC_SCHAR*					ctl_bpb;				/* Address of blob parameter block */
	ISC_UCHAR*			ctl_buffer;				/* Address of segment buffer */
	ISC_LONG				ctl_max_segment;		/* Length of longest segment */
	ISC_LONG				ctl_number_segments;	/* Total number of segments */
	ISC_LONG				ctl_total_length;		/* Total length of blob */
	ISC_STATUS*				ctl_status;				/* Address of status vector */
	long					ctl_data[8];			/* Application specific data */
} * ISC_BLOB_CTL;

/***************************/
/* Blob stream definitions */
/***************************/

typedef struct bstream
{
	isc_blob_handle	bstr_blob;		/* Blob handle */
	ISC_SCHAR *			bstr_buffer;	/* Address of buffer */
	ISC_SCHAR *			bstr_ptr;		/* Next character */
	short			bstr_length;	/* Length of buffer */
	short			bstr_cnt;		/* Characters in buffer */
	char			bstr_mode;		/* (mode) ? OUTPUT : INPUT */
} BSTREAM;

/* Three ugly macros, one even using octal radix... sigh... */
#define getb(p)	(--(p)->bstr_cnt >= 0 ? *(p)->bstr_ptr++ & 0377: BLOB_get (p))
#define putb(x,p) (((x) == '\n' || (!(--(p)->bstr_cnt))) ? BLOB_put ((x),p) : ((int) (*(p)->bstr_ptr++ = (unsigned) (x))))
#define putbx(x,p) ((!(--(p)->bstr_cnt)) ? BLOB_put ((x),p) : ((int) (*(p)->bstr_ptr++ = (unsigned) (x))))

/********************************************************************/
/* CVC: Public blob interface definition held in val.h.             */
/* For some unknown reason, it was only documented in langRef       */
/* and being the structure passed by the engine to UDFs it never    */
/* made its way into this public definitions file.                  */
/* Being its original name "blob", I renamed it blobcallback here.  */
/* I did the full definition with the proper parameters instead of  */
/* the weak C declaration with any number and type of parameters.   */
/* Since the first parameter -BLB- is unknown outside the engine,   */
/* it's more accurate to use void* than int* as the blob pointer    */
/********************************************************************/

#if !defined(JRD_VAL_H) && !defined(REQUESTER)
/* Blob passing structure */

/* This enum applies to parameter "mode" in blob_lseek */
enum blob_lseek_mode {blb_seek_relative = 1, blb_seek_from_tail = 2};
/* This enum applies to the value returned by blob_get_segment */
enum blob_get_result {blb_got_fragment = -1, blb_got_eof = 0, blb_got_full_segment = 1};

typedef struct blobcallback {
    short (*blob_get_segment)
		(void* hnd, ISC_UCHAR* buffer, ISC_USHORT buf_size, ISC_USHORT* result_len);
    void*		blob_handle;
    ISC_LONG	blob_number_segments;
    ISC_LONG	blob_max_segment;
    ISC_LONG	blob_total_length;
    void (*blob_put_segment)
		(void* hnd, const ISC_UCHAR* buffer, ISC_USHORT buf_size);
    ISC_LONG (*blob_lseek)
		(void* hnd, ISC_USHORT mode, ISC_LONG offset);
}  *BLOBCALLBACK;
#endif /* !defined(JRD_VAL_H) && !defined(REQUESTER) */


/********************************************************************/
/* CVC: Public descriptor interface held in dsc2.h.                  */
/* We need it documented to be able to recognize NULL in UDFs.      */
/* Being its original name "dsc", I renamed it paramdsc here.       */
/* Notice that I adjust to the original definition: contrary to     */
/* other cases, the typedef is the same struct not the pointer.     */
/* I included the enumeration of dsc_dtype possible values.         */
/* Ultimately, dsc2.h should be part of the public interface.        */
/********************************************************************/

#if !defined(JRD_DSC_H)
/* This is the famous internal descriptor that UDFs can use, too. */
typedef struct paramdsc {
    ISC_UCHAR	dsc_dtype;
    signed char		dsc_scale;
    ISC_USHORT		dsc_length;
    short		dsc_sub_type;
    ISC_USHORT		dsc_flags;
    ISC_UCHAR	*dsc_address;
} PARAMDSC;

#if !defined(JRD_VAL_H)
/* This is a helper struct to work with varchars. */
typedef struct paramvary {
    ISC_USHORT		vary_length;
    ISC_UCHAR		vary_string[1];
} PARAMVARY;
#endif /* !defined(JRD_VAL_H) */


#ifndef JRD_DSC_PUB_H
#define JRD_DSC_PUB_H

#define DSC_null                1
#define DSC_no_subtype          2       
#define DSC_nullable            4       

#define dtype_unknown   0
#define dtype_text      1
#define dtype_cstring   2
#define dtype_varying   3

#define dtype_packed    6
#define dtype_byte      7
#define dtype_short     8
#define dtype_long      9
#define dtype_quad      10
#define dtype_real      11
#define dtype_double    12
#define dtype_d_float   13
#define dtype_sql_date  14
#define dtype_sql_time  15
#define dtype_timestamp 16
#define dtype_blob      17
#define dtype_array     18
#define dtype_int64     19
#define DTYPE_TYPE_MAX  20

#define ISC_TIME_SECONDS_PRECISION              10000
#define ISC_TIME_SECONDS_PRECISION_SCALE        (-4)

#endif 


#endif /* !defined(JRD_DSC_H) */

/***************************/
/* Dynamic SQL definitions */
/***************************/


#ifndef DSQL_SQLDA_PUB_H
#define DSQL_SQLDA_PUB_H

#define DSQL_close      1
#define DSQL_drop       2

typedef struct
{
        ISC_SHORT       sqltype;                        
        ISC_SHORT       sqlscale;                       
        ISC_SHORT       sqlsubtype;                     
        ISC_SHORT       sqllen;                         
        ISC_SCHAR*      sqldata;                        
        ISC_SHORT*      sqlind;                         
        ISC_SHORT       sqlname_length;         
        ISC_SCHAR       sqlname[32];            
        ISC_SHORT       relname_length;         
        ISC_SCHAR       relname[32];            
        ISC_SHORT       ownname_length;         
        ISC_SCHAR       ownname[32];            
        ISC_SHORT       aliasname_length;       
        ISC_SCHAR       aliasname[32];          
} XSQLVAR;

#define SQLDA_VERSION1          1

typedef struct
{
        ISC_SHORT       version;                        
        ISC_SCHAR       sqldaid[8];                     
        ISC_LONG        sqldabc;                        
        ISC_SHORT       sqln;                           
        ISC_SHORT       sqld;                           
        XSQLVAR sqlvar[1];                      
} XSQLDA;

#define XSQLDA_LENGTH(n)        (sizeof (XSQLDA) + (n - 1) * sizeof (XSQLVAR))

#define SQL_TEXT                           452
#define SQL_VARYING                        448
#define SQL_SHORT                          500
#define SQL_LONG                           496
#define SQL_FLOAT                          482
#define SQL_DOUBLE                         480
#define SQL_D_FLOAT                        530
#define SQL_TIMESTAMP                      510
#define SQL_BLOB                           520
#define SQL_ARRAY                          540
#define SQL_QUAD                           550
#define SQL_TYPE_TIME                      560
#define SQL_TYPE_DATE                      570
#define SQL_INT64                          580

#define SQL_DATE                           SQL_TIMESTAMP

#define SQL_DIALECT_V5                          1       
#define SQL_DIALECT_V6_TRANSITION       2       
#define SQL_DIALECT_V6                          3       
#define SQL_DIALECT_CURRENT             SQL_DIALECT_V6  

#endif 


/***************************/
/* OSRI database functions */
/***************************/

#ifdef __cplusplus
extern "C" {
#endif

ISC_STATUS ISC_EXPORT isc_attach_database(ISC_STATUS*,
										  short,
										  const ISC_SCHAR*,
										  isc_db_handle*,
										  short,
										  const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_array_gen_sdl(ISC_STATUS*,
										const ISC_ARRAY_DESC*,
										ISC_SHORT*,
										ISC_UCHAR*,
										ISC_SHORT*);

ISC_STATUS ISC_EXPORT isc_array_get_slice(ISC_STATUS*,
										  isc_db_handle*,
										  isc_tr_handle*,
										  ISC_QUAD*,
										  const ISC_ARRAY_DESC*,
										  void*,
										  ISC_LONG*);

ISC_STATUS ISC_EXPORT isc_array_lookup_bounds(ISC_STATUS*,
											  isc_db_handle*,
											  isc_tr_handle*,
											  const ISC_SCHAR*,
											  const ISC_SCHAR*,
											  ISC_ARRAY_DESC*);

ISC_STATUS ISC_EXPORT isc_array_lookup_desc(ISC_STATUS*,
											isc_db_handle*,
											isc_tr_handle*,
											const ISC_SCHAR*,
											const ISC_SCHAR*,
											ISC_ARRAY_DESC*);

ISC_STATUS ISC_EXPORT isc_array_set_desc(ISC_STATUS*,
										 const ISC_SCHAR*,
										 const ISC_SCHAR*,
										 const short*,
										 const short*,
										 const short*,
										 ISC_ARRAY_DESC*);

ISC_STATUS ISC_EXPORT isc_array_put_slice(ISC_STATUS*,
										  isc_db_handle*,
										  isc_tr_handle*,
										  ISC_QUAD*,
										  const ISC_ARRAY_DESC*,
										  void*,
										  ISC_LONG *);

void ISC_EXPORT isc_blob_default_desc(ISC_BLOB_DESC*,
									  const ISC_UCHAR*,
									  const ISC_UCHAR*);

ISC_STATUS ISC_EXPORT isc_blob_gen_bpb(ISC_STATUS*,
									   const ISC_BLOB_DESC*,
									   const ISC_BLOB_DESC*,
									   unsigned short,
									   ISC_UCHAR*,
									   unsigned short*);

ISC_STATUS ISC_EXPORT isc_blob_info(ISC_STATUS*,
									isc_blob_handle*,
									short,
									const ISC_SCHAR*,
									short,
									ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_blob_lookup_desc(ISC_STATUS*,
										   isc_db_handle*,
										   isc_tr_handle*,
										   const ISC_UCHAR*,
										   const ISC_UCHAR*,
										   ISC_BLOB_DESC*,
										   ISC_UCHAR*);

ISC_STATUS ISC_EXPORT isc_blob_set_desc(ISC_STATUS*,
										const ISC_UCHAR*,
										const ISC_UCHAR*,
										short,
										short,
										short,
										ISC_BLOB_DESC*);

ISC_STATUS ISC_EXPORT isc_cancel_blob(ISC_STATUS *,
									  isc_blob_handle *);

ISC_STATUS ISC_EXPORT isc_cancel_events(ISC_STATUS *,
										isc_db_handle *,
										ISC_LONG *);

ISC_STATUS ISC_EXPORT isc_close_blob(ISC_STATUS *,
									 isc_blob_handle *);

ISC_STATUS ISC_EXPORT isc_commit_retaining(ISC_STATUS *,
										   isc_tr_handle *);

ISC_STATUS ISC_EXPORT isc_commit_transaction(ISC_STATUS *,
											 isc_tr_handle *);

ISC_STATUS ISC_EXPORT isc_create_blob(ISC_STATUS*,
									  isc_db_handle*,
									  isc_tr_handle*,
									  isc_blob_handle*,
									  ISC_QUAD*);

ISC_STATUS ISC_EXPORT isc_create_blob2(ISC_STATUS*,
									   isc_db_handle*,
									   isc_tr_handle*,
									   isc_blob_handle*,
									   ISC_QUAD*,
									   short,
									   const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_create_database(ISC_STATUS*,
										  short,
										  const ISC_SCHAR*,
										  isc_db_handle*,
										  short,
										  const ISC_SCHAR*,
										  short);

ISC_STATUS ISC_EXPORT isc_database_info(ISC_STATUS*,
										isc_db_handle*,
										short,
										const ISC_SCHAR*,
										short,
										ISC_SCHAR*);

void ISC_EXPORT isc_decode_date(const ISC_QUAD*,
								void*);

void ISC_EXPORT isc_decode_sql_date(const ISC_DATE*,
									void*);

void ISC_EXPORT isc_decode_sql_time(const ISC_TIME*,
									void*);

void ISC_EXPORT isc_decode_timestamp(const ISC_TIMESTAMP*,
									 void*);

ISC_STATUS ISC_EXPORT isc_detach_database(ISC_STATUS *,
										  isc_db_handle *);

ISC_STATUS ISC_EXPORT isc_drop_database(ISC_STATUS *,
										isc_db_handle *);

ISC_STATUS ISC_EXPORT isc_dsql_allocate_statement(ISC_STATUS *,
												  isc_db_handle *,
												  isc_stmt_handle *);

ISC_STATUS ISC_EXPORT isc_dsql_alloc_statement2(ISC_STATUS *,
												isc_db_handle *,
												isc_stmt_handle *);

ISC_STATUS ISC_EXPORT isc_dsql_describe(ISC_STATUS *,
										isc_stmt_handle *,
										unsigned short,
										XSQLDA *);

ISC_STATUS ISC_EXPORT isc_dsql_describe_bind(ISC_STATUS *,
											 isc_stmt_handle *,
											 unsigned short,
											 XSQLDA *);

ISC_STATUS ISC_EXPORT isc_dsql_exec_immed2(ISC_STATUS*,
										   isc_db_handle*,
										   isc_tr_handle*,
										   unsigned short,
										   const ISC_SCHAR*,
										   unsigned short,
										   XSQLDA*,
										   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_dsql_execute(ISC_STATUS*,
									   isc_tr_handle*,
									   isc_stmt_handle*,
									   unsigned short,
									   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_dsql_execute2(ISC_STATUS*,
										isc_tr_handle*,
										isc_stmt_handle*,
										unsigned short,
										XSQLDA*,
										XSQLDA*);

ISC_STATUS ISC_EXPORT isc_dsql_execute_immediate(ISC_STATUS*,
												 isc_db_handle*,
												 isc_tr_handle*,
												 unsigned short,
												 const ISC_SCHAR*,
												 unsigned short,
												 XSQLDA*);

ISC_STATUS ISC_EXPORT isc_dsql_fetch(ISC_STATUS *,
									 isc_stmt_handle *,
									 unsigned short,
									 XSQLDA *);

ISC_STATUS ISC_EXPORT isc_dsql_finish(isc_db_handle *);

ISC_STATUS ISC_EXPORT isc_dsql_free_statement(ISC_STATUS *,
											  isc_stmt_handle *,
											  unsigned short);

ISC_STATUS ISC_EXPORT isc_dsql_insert(ISC_STATUS*,
									  isc_stmt_handle*,
									  unsigned short,
									  XSQLDA*);

ISC_STATUS ISC_EXPORT isc_dsql_prepare(ISC_STATUS*,
									   isc_tr_handle*,
									   isc_stmt_handle*,
									   unsigned short,
									   const ISC_SCHAR*,
									   unsigned short,
									   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_dsql_set_cursor_name(ISC_STATUS*,
											   isc_stmt_handle*,
											   const ISC_SCHAR*,
											   unsigned short);

ISC_STATUS ISC_EXPORT isc_dsql_sql_info(ISC_STATUS*,
										isc_stmt_handle*,
										short,
										const ISC_SCHAR*,
										short,
										ISC_SCHAR*);

void ISC_EXPORT isc_encode_date(const void*,
								ISC_QUAD*);

void ISC_EXPORT isc_encode_sql_date(const void*,
									ISC_DATE*);

void ISC_EXPORT isc_encode_sql_time(const void*,
									ISC_TIME*);

void ISC_EXPORT isc_encode_timestamp(const void*,
									 ISC_TIMESTAMP*);

ISC_LONG ISC_EXPORT_VARARG isc_event_block(ISC_UCHAR**,
										   ISC_UCHAR**,
										   ISC_USHORT, ...);

ISC_USHORT ISC_EXPORT isc_event_block_a(ISC_SCHAR**,
										ISC_SCHAR**,
										ISC_USHORT, 
										ISC_SCHAR**);

void ISC_EXPORT isc_event_block_s(ISC_SCHAR**,
								  ISC_SCHAR**,
								  ISC_USHORT,
								  ISC_SCHAR**,
								  ISC_USHORT*);

void ISC_EXPORT isc_event_counts(ISC_ULONG*,
								 short,
								 ISC_UCHAR*,
								 const ISC_UCHAR *);

/* 17 May 2001 - isc_expand_dpb is DEPRECATED */
void FB_API_DEPRECATED ISC_EXPORT_VARARG isc_expand_dpb(ISC_SCHAR**,
											  			short*, ...);

int ISC_EXPORT isc_modify_dpb(ISC_SCHAR**,
							  short*,
							  unsigned short,
							  const ISC_SCHAR*,
							  short);

ISC_LONG ISC_EXPORT isc_free(ISC_SCHAR *);

ISC_STATUS ISC_EXPORT isc_get_segment(ISC_STATUS *,
									  isc_blob_handle *,
									  unsigned short *,
									  unsigned short,
									  ISC_SCHAR *);

ISC_STATUS ISC_EXPORT isc_get_slice(ISC_STATUS*,
									isc_db_handle*,
									isc_tr_handle*,
									ISC_QUAD*,
									short,
									const ISC_SCHAR*,
									short,
									const ISC_LONG*,
									ISC_LONG,
									void*,
									ISC_LONG*);

/* CVC: This non-const signature is needed for compatibility, see gds.cpp. */
ISC_LONG FB_API_DEPRECATED ISC_EXPORT isc_interprete(ISC_SCHAR*,
									 ISC_STATUS**);

/* This const params version used in the engine and other places. */
ISC_LONG ISC_EXPORT fb_interpret(ISC_SCHAR*,
								 unsigned int,
								 const ISC_STATUS**);

ISC_STATUS ISC_EXPORT isc_open_blob(ISC_STATUS*,
									isc_db_handle*,
									isc_tr_handle*,
									isc_blob_handle*,
									ISC_QUAD*);

ISC_STATUS ISC_EXPORT isc_open_blob2(ISC_STATUS*,
									 isc_db_handle*,
									 isc_tr_handle*,
									 isc_blob_handle*,
									 ISC_QUAD*,
									 ISC_USHORT,
									 const ISC_UCHAR*);

ISC_STATUS ISC_EXPORT isc_prepare_transaction2(ISC_STATUS*,
											   isc_tr_handle*,
											   ISC_USHORT,
											   const ISC_UCHAR*);

void ISC_EXPORT isc_print_sqlerror(ISC_SHORT,
								   const ISC_STATUS*);

ISC_STATUS ISC_EXPORT isc_print_status(const ISC_STATUS*);

ISC_STATUS ISC_EXPORT isc_put_segment(ISC_STATUS*,
									  isc_blob_handle*,
									  unsigned short,
									  const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_put_slice(ISC_STATUS*,
									isc_db_handle*,
									isc_tr_handle*,
									ISC_QUAD*,
									short,
									const ISC_SCHAR*,
									short,
									const ISC_LONG*,
									ISC_LONG,
									void*);

ISC_STATUS ISC_EXPORT isc_que_events(ISC_STATUS*,
									 isc_db_handle*,
									 ISC_LONG*,
									 short,
									 const ISC_UCHAR*,
									 ISC_EVENT_CALLBACK,
									 void*);

ISC_STATUS ISC_EXPORT isc_rollback_retaining(ISC_STATUS *,
											 isc_tr_handle *);

ISC_STATUS ISC_EXPORT isc_rollback_transaction(ISC_STATUS *,
											   isc_tr_handle *);

ISC_STATUS ISC_EXPORT isc_start_multiple(ISC_STATUS *,
										 isc_tr_handle *,
										 short,
										 void *);

ISC_STATUS ISC_EXPORT_VARARG isc_start_transaction(ISC_STATUS *,
												   isc_tr_handle *,
												   short, ...);

ISC_LONG ISC_EXPORT isc_sqlcode(const ISC_STATUS*);

void ISC_EXPORT isc_sqlcode_s(const ISC_STATUS*,
							  ISC_ULONG*);

void ISC_EXPORT isc_sql_interprete(short,
								   ISC_SCHAR*,
								   short);

ISC_STATUS ISC_EXPORT isc_transaction_info(ISC_STATUS*,
										   isc_tr_handle*,
										   short,
										   const ISC_SCHAR*,
										   short,
										   ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_transact_request(ISC_STATUS*,
										   isc_db_handle*,
										   isc_tr_handle*,
										   unsigned short,
										   const ISC_SCHAR*,
										   unsigned short,
										   ISC_SCHAR*,
										   unsigned short,
										   ISC_SCHAR*);

ISC_LONG ISC_EXPORT isc_vax_integer(const ISC_SCHAR*,
									short);

ISC_INT64 ISC_EXPORT isc_portable_integer(const ISC_UCHAR*,
										  short);

/*************************************/
/* Security Functions and structures */
/*************************************/

#define sec_uid_spec		    0x01
#define sec_gid_spec		    0x02
#define sec_server_spec		    0x04
#define sec_password_spec	    0x08
#define sec_group_name_spec	    0x10
#define sec_first_name_spec	    0x20
#define sec_middle_name_spec        0x40
#define sec_last_name_spec	    0x80
#define sec_dba_user_name_spec      0x100
#define sec_dba_password_spec       0x200

#define sec_protocol_tcpip            1
#define sec_protocol_netbeui          2
#define sec_protocol_spx              3 /* -- Deprecated Protocol. Declaration retained for compatibility   */
#define sec_protocol_local            4

typedef struct {
	short sec_flags;			/* which fields are specified */
	int uid;					/* the user's id */
	int gid;					/* the user's group id */
	int protocol;				/* protocol to use for connection */
	ISC_SCHAR *server;				/* server to administer */
	ISC_SCHAR *user_name;			/* the user's name */
	ISC_SCHAR *password;				/* the user's password */
	ISC_SCHAR *group_name;			/* the group name */
	ISC_SCHAR *first_name;			/* the user's first name */
	ISC_SCHAR *middle_name;			/* the user's middle name */
	ISC_SCHAR *last_name;			/* the user's last name */
	ISC_SCHAR *dba_user_name;		/* the dba user name */
	ISC_SCHAR *dba_password;			/* the dba password */
} USER_SEC_DATA;

ISC_STATUS ISC_EXPORT isc_add_user(ISC_STATUS*, const USER_SEC_DATA*);

ISC_STATUS ISC_EXPORT isc_delete_user(ISC_STATUS*, const USER_SEC_DATA*);

ISC_STATUS ISC_EXPORT isc_modify_user(ISC_STATUS*, const USER_SEC_DATA*);

/**********************************/
/*  Other OSRI functions          */
/**********************************/

ISC_STATUS ISC_EXPORT isc_compile_request(ISC_STATUS*,
										  isc_db_handle*,
										  isc_req_handle*,
										  short,
										  const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_compile_request2(ISC_STATUS*,
										   isc_db_handle*,
										   isc_req_handle*,
										   short,
										   const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_ddl(ISC_STATUS*,
							  isc_db_handle*,
							  isc_tr_handle*,
							  short,
							  const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_prepare_transaction(ISC_STATUS*,
											  isc_tr_handle*);


ISC_STATUS ISC_EXPORT isc_receive(ISC_STATUS*,
								  isc_req_handle*,
								  short,
								  short,
								  void*,
								  short);

ISC_STATUS ISC_EXPORT isc_reconnect_transaction(ISC_STATUS*,
												isc_db_handle*,
												isc_tr_handle*,
												short,
												const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_release_request(ISC_STATUS*,
										  isc_req_handle*);

ISC_STATUS ISC_EXPORT isc_request_info(ISC_STATUS*,
									   isc_req_handle*,
									   short,
									   short,
									   const ISC_SCHAR*,
									   short,
									   ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_seek_blob(ISC_STATUS*,
									isc_blob_handle*,
									short,
									ISC_LONG,
									ISC_LONG*);

ISC_STATUS ISC_EXPORT isc_send(ISC_STATUS*,
							   isc_req_handle*,
							   short,
							   short,
							   const void*,
							   short);

ISC_STATUS ISC_EXPORT isc_start_and_send(ISC_STATUS*,
										 isc_req_handle*,
										 isc_tr_handle*,
										 short,
										 short,
										 const void*,
										 short);

ISC_STATUS ISC_EXPORT isc_start_request(ISC_STATUS *,
										isc_req_handle *,
										isc_tr_handle *,
										short);

ISC_STATUS ISC_EXPORT isc_unwind_request(ISC_STATUS *,
										 isc_tr_handle *,
										 short);

ISC_STATUS ISC_EXPORT isc_wait_for_event(ISC_STATUS*,
										 isc_db_handle*,
										 short,
										 const ISC_UCHAR*,
										 ISC_UCHAR*);


/*****************************/
/* Other Sql functions       */
/*****************************/

ISC_STATUS ISC_EXPORT isc_close(ISC_STATUS*,
								const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_declare(ISC_STATUS*,
								  const ISC_SCHAR*,
								  const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_describe(ISC_STATUS*,
								   const ISC_SCHAR*,
								   XSQLDA *);

ISC_STATUS ISC_EXPORT isc_describe_bind(ISC_STATUS*,
										const ISC_SCHAR*,
										XSQLDA*);

ISC_STATUS ISC_EXPORT isc_execute(ISC_STATUS*,
								  isc_tr_handle*,
								  const ISC_SCHAR*,
								  XSQLDA*);

ISC_STATUS ISC_EXPORT isc_execute_immediate(ISC_STATUS*,
											isc_db_handle*,
											isc_tr_handle*,
											short*,
											const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_fetch(ISC_STATUS*,
								const ISC_SCHAR*,
								XSQLDA*);

ISC_STATUS ISC_EXPORT isc_open(ISC_STATUS*,
							   isc_tr_handle*,
							   const ISC_SCHAR*,
							   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_prepare(ISC_STATUS*,
								  isc_db_handle*,
								  isc_tr_handle*,
								  const ISC_SCHAR*,
								  short*,
								  const ISC_SCHAR*,
								  XSQLDA*);


/*************************************/
/* Other Dynamic sql functions       */
/*************************************/

ISC_STATUS ISC_EXPORT isc_dsql_execute_m(ISC_STATUS*,
										 isc_tr_handle*,
										 isc_stmt_handle*,
										 unsigned short,
										 const ISC_SCHAR*,
										 unsigned short,
										 unsigned short,
										 ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_dsql_execute2_m(ISC_STATUS*,
										  isc_tr_handle*,
										  isc_stmt_handle*,
										  unsigned short,
										  const ISC_SCHAR*,
										  unsigned short,
										  unsigned short,
										  const ISC_SCHAR*,
										  unsigned short,
										  ISC_SCHAR*,
										  unsigned short,
										  unsigned short,
										  ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_dsql_execute_immediate_m(ISC_STATUS*,
												   isc_db_handle*,
												   isc_tr_handle*,
												   unsigned short,
												   const ISC_SCHAR*,
												   unsigned short,
												   unsigned short,
												   const ISC_SCHAR*,
												   unsigned short,
												   unsigned short,
												   ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_dsql_exec_immed3_m(ISC_STATUS*,
											 isc_db_handle*,
											 isc_tr_handle*,
											 unsigned short,
											 const ISC_SCHAR*,
											 unsigned short,
											 unsigned short,
											 const ISC_SCHAR*,
											 unsigned short,
											 unsigned short,
											 ISC_SCHAR*,
											 unsigned short,
											 ISC_SCHAR*,
											 unsigned short,
											 unsigned short,
											 ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_dsql_fetch_m(ISC_STATUS*,
									   isc_stmt_handle*,
									   unsigned short,
									   const ISC_SCHAR*,
									   unsigned short,
									   unsigned short,
									   ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_dsql_insert_m(ISC_STATUS*,
										isc_stmt_handle*,
										unsigned short,
										const ISC_SCHAR*,
										unsigned short,
										unsigned short,
										const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_dsql_prepare_m(ISC_STATUS*,
										 isc_tr_handle*,
										 isc_stmt_handle*,
										 unsigned short,
										 const ISC_SCHAR*,
										 unsigned short,
										 unsigned short,
										 const ISC_SCHAR*,
										 unsigned short,
										 ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_dsql_release(ISC_STATUS*,
									   const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_close(ISC_STATUS*,
										   const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_declare(ISC_STATUS*,
											 const ISC_SCHAR*,
											 const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_describe(ISC_STATUS*,
											  const ISC_SCHAR*,
											  unsigned short,
											  XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_describe_bind(ISC_STATUS*,
												   const ISC_SCHAR*,
												   unsigned short,
												   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_execute(ISC_STATUS*,
											 isc_tr_handle*,
											 const ISC_SCHAR*,
											 unsigned short,
											 XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_execute2(ISC_STATUS*,
											  isc_tr_handle*,
											  const ISC_SCHAR*,
											  unsigned short,
											  XSQLDA*,
											  XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_execute_immed(ISC_STATUS*,
												   isc_db_handle*,
												   isc_tr_handle*,
												   unsigned short,
												   const ISC_SCHAR*,
												   unsigned short,
												   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_fetch(ISC_STATUS*,
										   const ISC_SCHAR*,
										   unsigned short,
										   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_fetch_a(ISC_STATUS*,
											 int*,
											 const ISC_SCHAR*,
											 ISC_USHORT,
											 XSQLDA*);

void ISC_EXPORT isc_embed_dsql_length(const ISC_UCHAR*,
									  ISC_USHORT*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_open(ISC_STATUS*,
										  isc_tr_handle*,
										  const ISC_SCHAR*,
										  unsigned short,
										  XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_open2(ISC_STATUS*,
										   isc_tr_handle*,
										   const ISC_SCHAR*,
										   unsigned short,
										   XSQLDA*,
										   XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_insert(ISC_STATUS*,
											const ISC_SCHAR*,
											unsigned short,
											XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_prepare(ISC_STATUS*,
											 isc_db_handle*,
											 isc_tr_handle*,
											 const ISC_SCHAR*,
											 unsigned short,
											 const ISC_SCHAR*,
											 unsigned short,
											 XSQLDA*);

ISC_STATUS ISC_EXPORT isc_embed_dsql_release(ISC_STATUS*,
											 const ISC_SCHAR*);


/******************************/
/* Other Blob functions       */
/******************************/

BSTREAM* ISC_EXPORT BLOB_open(isc_blob_handle,
									  ISC_SCHAR*,
									  int);

int ISC_EXPORT BLOB_put(ISC_SCHAR,
						BSTREAM*);

int ISC_EXPORT BLOB_close(BSTREAM*);

int ISC_EXPORT BLOB_get(BSTREAM*);

int ISC_EXPORT BLOB_display(ISC_QUAD*,
							isc_db_handle,
							isc_tr_handle,
							const ISC_SCHAR*);

int ISC_EXPORT BLOB_dump(ISC_QUAD*,
						 isc_db_handle,
						 isc_tr_handle,
						 const ISC_SCHAR*);

int ISC_EXPORT BLOB_edit(ISC_QUAD*,
						 isc_db_handle,
						 isc_tr_handle,
						 const ISC_SCHAR*);

int ISC_EXPORT BLOB_load(ISC_QUAD*,
						 isc_db_handle,
						 isc_tr_handle,
						 const ISC_SCHAR*);

int ISC_EXPORT BLOB_text_dump(ISC_QUAD*,
							  isc_db_handle,
							  isc_tr_handle,
							  const ISC_SCHAR*);

int ISC_EXPORT BLOB_text_load(ISC_QUAD*,
							  isc_db_handle,
							  isc_tr_handle,
							  const ISC_SCHAR*);

BSTREAM* ISC_EXPORT Bopen(ISC_QUAD*,
								  isc_db_handle,
								  isc_tr_handle,
								  const ISC_SCHAR*);

/* Disabled, not found anywhere.
BSTREAM* ISC_EXPORT Bopen2(ISC_QUAD*,
								   isc_db_handle,
								   isc_tr_handle,
								   const ISC_SCHAR*,
								   unsigned short);
*/


/******************************/
/* Other Misc functions       */
/******************************/

ISC_LONG ISC_EXPORT isc_ftof(const ISC_SCHAR*,
							 const unsigned short,
							 ISC_SCHAR*,
							 const unsigned short);

ISC_STATUS ISC_EXPORT isc_print_blr(const ISC_SCHAR*,
									ISC_PRINT_CALLBACK,
									void*,
									short);

void ISC_EXPORT isc_set_debug(int);

void ISC_EXPORT isc_qtoq(const ISC_QUAD*,
						 ISC_QUAD*);

void ISC_EXPORT isc_vtof(const ISC_SCHAR*,
						 ISC_SCHAR*,
						 unsigned short);

void ISC_EXPORT isc_vtov(const ISC_SCHAR*,
						 ISC_SCHAR*,
						 short);

int ISC_EXPORT isc_version(isc_db_handle*,
						   ISC_VERSION_CALLBACK,
						   void*);

ISC_LONG ISC_EXPORT isc_reset_fpe(ISC_USHORT);

uintptr_t	ISC_EXPORT isc_baddress(ISC_SCHAR*);
void		ISC_EXPORT isc_baddress_s(const ISC_SCHAR*,
								  uintptr_t*);

/*****************************************/
/* Service manager functions             */
/*****************************************/

#define ADD_SPB_LENGTH(p, length)	{*(p)++ = (length); \
    					 *(p)++ = (length) >> 8;}

#define ADD_SPB_NUMERIC(p, data)	{*(p)++ = (ISC_SCHAR) (data); \
    					 *(p)++ = (ISC_SCHAR) ((data) >> 8); \
					 *(p)++ = (ISC_SCHAR) ((data) >> 16); \
					 *(p)++ = (ISC_SCHAR) ((data) >> 24);}

ISC_STATUS ISC_EXPORT isc_service_attach(ISC_STATUS*,
										 unsigned short,
										 const ISC_SCHAR*,
										 isc_svc_handle*,
										 unsigned short,
										 const ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_service_detach(ISC_STATUS *,
										 isc_svc_handle *);

ISC_STATUS ISC_EXPORT isc_service_query(ISC_STATUS*,
										isc_svc_handle*,
										isc_resv_handle*,
										unsigned short,
										const ISC_SCHAR*,
										unsigned short,
										const ISC_SCHAR*,
										unsigned short,
										ISC_SCHAR*);

ISC_STATUS ISC_EXPORT isc_service_start(ISC_STATUS*,
										isc_svc_handle*,
										isc_resv_handle*,
										unsigned short,
										const ISC_SCHAR*);


/********************************/
/* Client information functions */
/********************************/

void ISC_EXPORT isc_get_client_version ( ISC_SCHAR  *);
int  ISC_EXPORT isc_get_client_major_version ();
int  ISC_EXPORT isc_get_client_minor_version ();

#ifdef __cplusplus
}	/* extern "C" */
#endif


/***************************************************/
/* Actions to pass to the blob filter (ctl_source) */
/***************************************************/

#define isc_blob_filter_open             0
#define isc_blob_filter_get_segment      1
#define isc_blob_filter_close            2
#define isc_blob_filter_create           3
#define isc_blob_filter_put_segment      4
#define isc_blob_filter_alloc            5
#define isc_blob_filter_free             6
#define isc_blob_filter_seek             7

/*******************/
/* Blr definitions */
/*******************/


#ifndef JRD_BLR_H
#define JRD_BLR_H

#define blr_text                (unsigned char)14
#define blr_text2               (unsigned char)15       
#define blr_short               (unsigned char)7
#define blr_long                (unsigned char)8
#define blr_quad                (unsigned char)9
#define blr_float               (unsigned char)10
#define blr_double              (unsigned char)27
#define blr_d_float             (unsigned char)11
#define blr_timestamp           (unsigned char)35
#define blr_varying             (unsigned char)37
#define blr_varying2            (unsigned char)38       
#define blr_blob                (unsigned short)261
#define blr_cstring             (unsigned char)40       
#define blr_cstring2            (unsigned char)41       
#define blr_blob_id             (unsigned char)45       
#define blr_sql_date            (unsigned char)12
#define blr_sql_time            (unsigned char)13
#define blr_int64               (unsigned char)16
#define blr_blob2                       (unsigned char)17
#define blr_domain_name         (unsigned char)18
#define blr_domain_name2        (unsigned char)19
#define blr_not_nullable        (unsigned char)20

#define blr_domain_type_of      (unsigned char)0
#define blr_domain_full         (unsigned char)1

#define blr_date                blr_timestamp

#define blr_inner               (unsigned char)0
#define blr_left                (unsigned char)1
#define blr_right               (unsigned char)2
#define blr_full                (unsigned char)3

#define blr_gds_code            (unsigned char)0
#define blr_sql_code            (unsigned char)1
#define blr_exception           (unsigned char)2
#define blr_trigger_code        (unsigned char)3
#define blr_default_code        (unsigned char)4
#define blr_raise                       (unsigned char)5
#define blr_exception_msg       (unsigned char)6

#define blr_version4            (unsigned char)4
#define blr_version5            (unsigned char)5
#define blr_eoc                 (unsigned char)76
#define blr_end                 (unsigned char)255      

#define blr_assignment          (unsigned char)1
#define blr_begin               (unsigned char)2
#define blr_dcl_variable        (unsigned char)3        
#define blr_message             (unsigned char)4
#define blr_erase               (unsigned char)5
#define blr_fetch               (unsigned char)6
#define blr_for                 (unsigned char)7
#define blr_if                  (unsigned char)8
#define blr_loop                (unsigned char)9
#define blr_modify              (unsigned char)10
#define blr_handler             (unsigned char)11
#define blr_receive             (unsigned char)12
#define blr_select              (unsigned char)13
#define blr_send                (unsigned char)14
#define blr_store               (unsigned char)15
#define blr_label               (unsigned char)17
#define blr_leave               (unsigned char)18
#define blr_store2              (unsigned char)19
#define blr_post                (unsigned char)20
#define blr_literal             (unsigned char)21
#define blr_dbkey               (unsigned char)22
#define blr_field               (unsigned char)23
#define blr_fid                 (unsigned char)24
#define blr_parameter           (unsigned char)25
#define blr_variable            (unsigned char)26
#define blr_average             (unsigned char)27
#define blr_count               (unsigned char)28
#define blr_maximum             (unsigned char)29
#define blr_minimum             (unsigned char)30
#define blr_total               (unsigned char)31

#define blr_add                 (unsigned char)34
#define blr_subtract            (unsigned char)35
#define blr_multiply            (unsigned char)36
#define blr_divide              (unsigned char)37
#define blr_negate              (unsigned char)38
#define blr_concatenate         (unsigned char)39
#define blr_substring           (unsigned char)40
#define blr_parameter2          (unsigned char)41
#define blr_from                (unsigned char)42
#define blr_via                 (unsigned char)43
#define blr_parameter2_old      (unsigned char)44       
#define blr_user_name           (unsigned char)44       
#define blr_null                (unsigned char)45

#define blr_equiv                       (unsigned char)46
#define blr_eql                 (unsigned char)47
#define blr_neq                 (unsigned char)48
#define blr_gtr                 (unsigned char)49
#define blr_geq                 (unsigned char)50
#define blr_lss                 (unsigned char)51
#define blr_leq                 (unsigned char)52
#define blr_containing          (unsigned char)53
#define blr_matching            (unsigned char)54
#define blr_starting            (unsigned char)55
#define blr_between             (unsigned char)56
#define blr_or                  (unsigned char)57
#define blr_and                 (unsigned char)58
#define blr_not                 (unsigned char)59
#define blr_any                 (unsigned char)60
#define blr_missing             (unsigned char)61
#define blr_unique              (unsigned char)62
#define blr_like                (unsigned char)63

#define blr_rse                 (unsigned char)67
#define blr_first               (unsigned char)68
#define blr_project             (unsigned char)69
#define blr_sort                (unsigned char)70
#define blr_boolean             (unsigned char)71
#define blr_ascending           (unsigned char)72
#define blr_descending          (unsigned char)73
#define blr_relation            (unsigned char)74
#define blr_rid                 (unsigned char)75
#define blr_union               (unsigned char)76
#define blr_map                 (unsigned char)77
#define blr_group_by            (unsigned char)78
#define blr_aggregate           (unsigned char)79
#define blr_join_type           (unsigned char)80

#define blr_agg_count           (unsigned char)83
#define blr_agg_max             (unsigned char)84
#define blr_agg_min             (unsigned char)85
#define blr_agg_total           (unsigned char)86
#define blr_agg_average         (unsigned char)87
#define blr_parameter3          (unsigned char)88       
#define blr_run_max             (unsigned char)89
#define blr_run_min             (unsigned char)90
#define blr_run_total           (unsigned char)91
#define blr_run_average         (unsigned char)92
#define blr_agg_count2          (unsigned char)93
#define blr_agg_count_distinct  (unsigned char)94
#define blr_agg_total_distinct  (unsigned char)95
#define blr_agg_average_distinct (unsigned char)96

#define blr_function            (unsigned char)100
#define blr_gen_id              (unsigned char)101
#define blr_prot_mask           (unsigned char)102
#define blr_upcase              (unsigned char)103
#define blr_lock_state          (unsigned char)104
#define blr_value_if            (unsigned char)105
#define blr_matching2           (unsigned char)106
#define blr_index               (unsigned char)107
#define blr_ansi_like           (unsigned char)108

#define blr_seek                (unsigned char)112

#define blr_continue            (unsigned char)0
#define blr_forward             (unsigned char)1
#define blr_backward            (unsigned char)2
#define blr_bof_forward         (unsigned char)3
#define blr_eof_backward        (unsigned char)4

#define blr_run_count           (unsigned char)118      
#define blr_rs_stream           (unsigned char)119
#define blr_exec_proc           (unsigned char)120

#define blr_procedure           (unsigned char)124
#define blr_pid                 (unsigned char)125
#define blr_exec_pid            (unsigned char)126
#define blr_singular            (unsigned char)127
#define blr_abort               (unsigned char)128
#define blr_block               (unsigned char)129
#define blr_error_handler       (unsigned char)130

#define blr_cast                (unsigned char)131

#define blr_start_savepoint     (unsigned char)134
#define blr_end_savepoint       (unsigned char)135

#define blr_plan                (unsigned char)139      
#define blr_merge               (unsigned char)140
#define blr_join                (unsigned char)141
#define blr_sequential          (unsigned char)142
#define blr_navigational        (unsigned char)143
#define blr_indices             (unsigned char)144
#define blr_retrieve            (unsigned char)145

#define blr_relation2           (unsigned char)146
#define blr_rid2                (unsigned char)147

#define blr_set_generator       (unsigned char)150

#define blr_ansi_any            (unsigned char)151   
#define blr_exists              (unsigned char)152   

#define blr_record_version      (unsigned char)154      
#define blr_stall               (unsigned char)155      

#define blr_ansi_all            (unsigned char)158   

#define blr_extract             (unsigned char)159

#define blr_extract_year                (unsigned char)0
#define blr_extract_month               (unsigned char)1
#define blr_extract_day                 (unsigned char)2
#define blr_extract_hour                (unsigned char)3
#define blr_extract_minute              (unsigned char)4
#define blr_extract_second              (unsigned char)5
#define blr_extract_weekday             (unsigned char)6
#define blr_extract_yearday             (unsigned char)7
#define blr_extract_millisecond (unsigned char)8
#define blr_extract_week                (unsigned char)9

#define blr_current_date        (unsigned char)160
#define blr_current_timestamp   (unsigned char)161
#define blr_current_time        (unsigned char)162

#define blr_post_arg            (unsigned char)163
#define blr_exec_into           (unsigned char)164
#define blr_user_savepoint      (unsigned char)165
#define blr_dcl_cursor          (unsigned char)166
#define blr_cursor_stmt         (unsigned char)167
#define blr_current_timestamp2  (unsigned char)168
#define blr_current_time2       (unsigned char)169
#define blr_agg_list            (unsigned char)170
#define blr_agg_list_distinct   (unsigned char)171
#define blr_modify2                     (unsigned char)172

#define blr_current_role        (unsigned char)174
#define blr_skip                (unsigned char)175

#define blr_exec_sql            (unsigned char)176
#define blr_internal_info       (unsigned char)177
#define blr_nullsfirst          (unsigned char)178
#define blr_writelock           (unsigned char)179
#define blr_nullslast       (unsigned char)180

#define blr_lowcase                     (unsigned char)181
#define blr_strlen                      (unsigned char)182

#define blr_strlen_bit          (unsigned char)0
#define blr_strlen_char         (unsigned char)1
#define blr_strlen_octet        (unsigned char)2

#define blr_trim                        (unsigned char)183

#define blr_trim_both           (unsigned char)0
#define blr_trim_leading        (unsigned char)1
#define blr_trim_trailing       (unsigned char)2

#define blr_trim_spaces         (unsigned char)0
#define blr_trim_characters     (unsigned char)1

#define blr_savepoint_set       (unsigned char)0
#define blr_savepoint_release   (unsigned char)1
#define blr_savepoint_undo      (unsigned char)2
#define blr_savepoint_release_single    (unsigned char)3

#define blr_cursor_open                 (unsigned char)0
#define blr_cursor_close                (unsigned char)1
#define blr_cursor_fetch                (unsigned char)2

#define blr_init_variable       (unsigned char)184
#define blr_recurse                     (unsigned char)185
#define blr_sys_function        (unsigned char)186

#endif 


#ifndef INCLUDE_CONSTS_PUB_H
#define INCLUDE_CONSTS_PUB_H

#define isc_dpb_version1                  1
#define isc_dpb_cdd_pathname              1
#define isc_dpb_allocation                2
#define isc_dpb_journal                   3
#define isc_dpb_page_size                 4
#define isc_dpb_num_buffers               5
#define isc_dpb_buffer_length             6
#define isc_dpb_debug                     7
#define isc_dpb_garbage_collect           8
#define isc_dpb_verify                    9
#define isc_dpb_sweep                     10
#define isc_dpb_enable_journal            11
#define isc_dpb_disable_journal           12
#define isc_dpb_dbkey_scope               13
#define isc_dpb_number_of_users           14
#define isc_dpb_trace                     15
#define isc_dpb_no_garbage_collect        16
#define isc_dpb_damaged                   17
#define isc_dpb_license                   18
#define isc_dpb_sys_user_name             19
#define isc_dpb_encrypt_key               20
#define isc_dpb_activate_shadow           21
#define isc_dpb_sweep_interval            22
#define isc_dpb_delete_shadow             23
#define isc_dpb_force_write               24
#define isc_dpb_begin_log                 25
#define isc_dpb_quit_log                  26
#define isc_dpb_no_reserve                27
#define isc_dpb_user_name                 28
#define isc_dpb_password                  29
#define isc_dpb_password_enc              30
#define isc_dpb_sys_user_name_enc         31
#define isc_dpb_interp                    32
#define isc_dpb_online_dump               33
#define isc_dpb_old_file_size             34
#define isc_dpb_old_num_files             35
#define isc_dpb_old_file                  36
#define isc_dpb_old_start_page            37
#define isc_dpb_old_start_seqno           38
#define isc_dpb_old_start_file            39
#define isc_dpb_drop_walfile              40
#define isc_dpb_old_dump_id               41
#define isc_dpb_wal_backup_dir            42
#define isc_dpb_wal_chkptlen              43
#define isc_dpb_wal_numbufs               44
#define isc_dpb_wal_bufsize               45
#define isc_dpb_wal_grp_cmt_wait          46
#define isc_dpb_lc_messages               47
#define isc_dpb_lc_ctype                  48
#define isc_dpb_cache_manager             49
#define isc_dpb_shutdown                  50
#define isc_dpb_online                    51
#define isc_dpb_shutdown_delay            52
#define isc_dpb_reserved                  53
#define isc_dpb_overwrite                 54
#define isc_dpb_sec_attach                55
#define isc_dpb_disable_wal               56
#define isc_dpb_connect_timeout           57
#define isc_dpb_dummy_packet_interval     58
#define isc_dpb_gbak_attach               59
#define isc_dpb_sql_role_name             60
#define isc_dpb_set_page_buffers          61
#define isc_dpb_working_directory         62
#define isc_dpb_sql_dialect               63
#define isc_dpb_set_db_readonly           64
#define isc_dpb_set_db_sql_dialect        65
#define isc_dpb_gfix_attach               66
#define isc_dpb_gstat_attach              67
#define isc_dpb_set_db_charset            68
#define isc_dpb_gsec_attach               69
#define isc_dpb_address_path              70
#define isc_dpb_process_id                71
#define isc_dpb_no_db_triggers            72
#define isc_dpb_trusted_auth                      73
#define isc_dpb_process_name              74

#define isc_dpb_address 1

#define isc_dpb_addr_protocol 1
#define isc_dpb_addr_endpoint 2

#define isc_dpb_pages                     1
#define isc_dpb_records                   2
#define isc_dpb_indices                   4
#define isc_dpb_transactions              8
#define isc_dpb_no_update                 16
#define isc_dpb_repair                    32
#define isc_dpb_ignore                    64

#define isc_dpb_shut_cache               0x1
#define isc_dpb_shut_attachment          0x2
#define isc_dpb_shut_transaction         0x4
#define isc_dpb_shut_force               0x8
#define isc_dpb_shut_mode_mask          0x70

#define isc_dpb_shut_default             0x0
#define isc_dpb_shut_normal             0x10
#define isc_dpb_shut_multi              0x20
#define isc_dpb_shut_single             0x30
#define isc_dpb_shut_full               0x40

#define RDB_system                         1
#define RDB_id_assigned                    2

#define isc_tpb_version1                  1
#define isc_tpb_version3                  3
#define isc_tpb_consistency               1
#define isc_tpb_concurrency               2
#define isc_tpb_shared                    3
#define isc_tpb_protected                 4
#define isc_tpb_exclusive                 5
#define isc_tpb_wait                      6
#define isc_tpb_nowait                    7
#define isc_tpb_read                      8
#define isc_tpb_write                     9
#define isc_tpb_lock_read                 10
#define isc_tpb_lock_write                11
#define isc_tpb_verb_time                 12
#define isc_tpb_commit_time               13
#define isc_tpb_ignore_limbo              14
#define isc_tpb_read_committed            15
#define isc_tpb_autocommit                16
#define isc_tpb_rec_version               17
#define isc_tpb_no_rec_version            18
#define isc_tpb_restart_requests          19
#define isc_tpb_no_auto_undo              20
#define isc_tpb_lock_timeout              21

#define isc_bpb_version1                  1
#define isc_bpb_source_type               1
#define isc_bpb_target_type               2
#define isc_bpb_type                      3
#define isc_bpb_source_interp             4
#define isc_bpb_target_interp             5
#define isc_bpb_filter_parameter          6
#define isc_bpb_storage                   7

#define isc_bpb_type_segmented            0x0
#define isc_bpb_type_stream               0x1
#define isc_bpb_storage_main              0x0
#define isc_bpb_storage_temp              0x2

#define isc_spb_version1                  1
#define isc_spb_current_version           2
#define isc_spb_version                   isc_spb_current_version
#define isc_spb_user_name                 isc_dpb_user_name
#define isc_spb_sys_user_name             isc_dpb_sys_user_name
#define isc_spb_sys_user_name_enc         isc_dpb_sys_user_name_enc
#define isc_spb_password                  isc_dpb_password
#define isc_spb_password_enc              isc_dpb_password_enc
#define isc_spb_command_line              105
#define isc_spb_dbname                    106
#define isc_spb_verbose                   107
#define isc_spb_options                   108
#define isc_spb_address_path              109
#define isc_spb_process_id                110
#define isc_spb_trusted_auth                      111
#define isc_spb_process_name              112

#define isc_spb_connect_timeout           isc_dpb_connect_timeout
#define isc_spb_dummy_packet_interval     isc_dpb_dummy_packet_interval
#define isc_spb_sql_role_name             isc_dpb_sql_role_name

#define isc_action_svc_backup          1        
#define isc_action_svc_restore         2        
#define isc_action_svc_repair          3        
#define isc_action_svc_add_user        4        
#define isc_action_svc_delete_user     5        
#define isc_action_svc_modify_user     6        
#define isc_action_svc_display_user    7        
#define isc_action_svc_properties      8        
#define isc_action_svc_add_license     9        
#define isc_action_svc_remove_license 10        
#define isc_action_svc_db_stats       11        
#define isc_action_svc_get_ib_log     12        
#define isc_action_svc_get_fb_log     12        

#define isc_info_svc_svr_db_info      50        
#define isc_info_svc_get_license      51        
#define isc_info_svc_get_license_mask 52        
#define isc_info_svc_get_config       53        
#define isc_info_svc_version          54        
#define isc_info_svc_server_version   55        
#define isc_info_svc_implementation   56        
#define isc_info_svc_capabilities     57        
#define isc_info_svc_user_dbpath      58        
#define isc_info_svc_get_env          59        
#define isc_info_svc_get_env_lock     60        
#define isc_info_svc_get_env_msg      61        
#define isc_info_svc_line             62        
#define isc_info_svc_to_eof           63        
#define isc_info_svc_timeout          64        
#define isc_info_svc_get_licensed_users 65      
#define isc_info_svc_limbo_trans        66      
#define isc_info_svc_running            67      
#define isc_info_svc_get_users          68      

#define isc_spb_sec_userid            5
#define isc_spb_sec_groupid           6
#define isc_spb_sec_username          7
#define isc_spb_sec_password          8
#define isc_spb_sec_groupname         9
#define isc_spb_sec_firstname         10
#define isc_spb_sec_middlename        11
#define isc_spb_sec_lastname          12

#define isc_spb_lic_key               5
#define isc_spb_lic_id                6
#define isc_spb_lic_desc              7

#define isc_spb_bkp_file                 5
#define isc_spb_bkp_factor               6
#define isc_spb_bkp_length               7
#define isc_spb_bkp_ignore_checksums     0x01
#define isc_spb_bkp_ignore_limbo         0x02
#define isc_spb_bkp_metadata_only        0x04
#define isc_spb_bkp_no_garbage_collect   0x08
#define isc_spb_bkp_old_descriptions     0x10
#define isc_spb_bkp_non_transportable    0x20
#define isc_spb_bkp_convert              0x40
#define isc_spb_bkp_expand               0x80

#define isc_spb_prp_page_buffers                5
#define isc_spb_prp_sweep_interval              6
#define isc_spb_prp_shutdown_db                 7
#define isc_spb_prp_deny_new_attachments        9
#define isc_spb_prp_deny_new_transactions       10
#define isc_spb_prp_reserve_space               11
#define isc_spb_prp_write_mode                  12
#define isc_spb_prp_access_mode                 13
#define isc_spb_prp_set_sql_dialect             14
#define isc_spb_prp_activate                    0x0100
#define isc_spb_prp_db_online                   0x0200

#define isc_spb_prp_res_use_full        35
#define isc_spb_prp_res                 36

#define isc_spb_prp_wm_async            37
#define isc_spb_prp_wm_sync                     38

#define isc_spb_prp_am_readonly         39
#define isc_spb_prp_am_readwrite        40

#define isc_spb_rpr_commit_trans                15
#define isc_spb_rpr_rollback_trans              34
#define isc_spb_rpr_recover_two_phase   17
#define isc_spb_tra_id                                  18
#define isc_spb_single_tra_id                   19
#define isc_spb_multi_tra_id                    20
#define isc_spb_tra_state                               21
#define isc_spb_tra_state_limbo                 22
#define isc_spb_tra_state_commit                23
#define isc_spb_tra_state_rollback              24
#define isc_spb_tra_state_unknown               25
#define isc_spb_tra_host_site                   26
#define isc_spb_tra_remote_site                 27
#define isc_spb_tra_db_path                             28
#define isc_spb_tra_advise                              29
#define isc_spb_tra_advise_commit               30
#define isc_spb_tra_advise_rollback             31
#define isc_spb_tra_advise_unknown              33

#define isc_spb_rpr_validate_db                 0x01
#define isc_spb_rpr_sweep_db                    0x02
#define isc_spb_rpr_mend_db                             0x04
#define isc_spb_rpr_list_limbo_trans    0x08
#define isc_spb_rpr_check_db                    0x10
#define isc_spb_rpr_ignore_checksum             0x20
#define isc_spb_rpr_kill_shadows                0x40
#define isc_spb_rpr_full                                0x80

#define isc_spb_res_buffers                             9
#define isc_spb_res_page_size                   10
#define isc_spb_res_length                              11
#define isc_spb_res_access_mode                 12
#define isc_spb_res_deactivate_idx              0x0100
#define isc_spb_res_no_shadow                   0x0200
#define isc_spb_res_no_validity                 0x0400
#define isc_spb_res_one_at_a_time               0x0800
#define isc_spb_res_replace                             0x1000
#define isc_spb_res_create                              0x2000
#define isc_spb_res_use_all_space               0x4000

#define isc_spb_res_am_readonly                 isc_spb_prp_am_readonly
#define isc_spb_res_am_readwrite                isc_spb_prp_am_readwrite

#define isc_spb_num_att                 5
#define isc_spb_num_db                  6

#define isc_spb_sts_data_pages          0x01
#define isc_spb_sts_db_log                      0x02
#define isc_spb_sts_hdr_pages           0x04
#define isc_spb_sts_idx_pages           0x08
#define isc_spb_sts_sys_relations       0x10
#define isc_spb_sts_record_versions     0x20
#define isc_spb_sts_table                       0x40
#define isc_spb_sts_nocreation          0x80

#define isc_dyn_version_1                 1
#define isc_dyn_eoc                       255

#define isc_dyn_begin                     2
#define isc_dyn_end                       3
#define isc_dyn_if                        4
#define isc_dyn_def_database              5
#define isc_dyn_def_global_fld            6
#define isc_dyn_def_local_fld             7
#define isc_dyn_def_idx                   8
#define isc_dyn_def_rel                   9
#define isc_dyn_def_sql_fld               10
#define isc_dyn_def_view                  12
#define isc_dyn_def_trigger               15
#define isc_dyn_def_security_class        120
#define isc_dyn_def_dimension             140
#define isc_dyn_def_generator             24
#define isc_dyn_def_function              25
#define isc_dyn_def_filter                26
#define isc_dyn_def_function_arg          27
#define isc_dyn_def_shadow                34
#define isc_dyn_def_trigger_msg           17
#define isc_dyn_def_file                  36
#define isc_dyn_mod_database              39
#define isc_dyn_mod_rel                   11
#define isc_dyn_mod_global_fld            13
#define isc_dyn_mod_idx                   102
#define isc_dyn_mod_local_fld             14
#define isc_dyn_mod_sql_fld               216
#define isc_dyn_mod_view                  16
#define isc_dyn_mod_security_class        122
#define isc_dyn_mod_trigger               113
#define isc_dyn_mod_trigger_msg           28
#define isc_dyn_delete_database           18
#define isc_dyn_delete_rel                19
#define isc_dyn_delete_global_fld         20
#define isc_dyn_delete_local_fld          21
#define isc_dyn_delete_idx                22
#define isc_dyn_delete_security_class     123
#define isc_dyn_delete_dimensions         143
#define isc_dyn_delete_trigger            23
#define isc_dyn_delete_trigger_msg        29
#define isc_dyn_delete_filter             32
#define isc_dyn_delete_function           33
#define isc_dyn_delete_shadow             35
#define isc_dyn_grant                     30
#define isc_dyn_revoke                    31
#define isc_dyn_def_primary_key           37
#define isc_dyn_def_foreign_key           38
#define isc_dyn_def_unique                40
#define isc_dyn_def_procedure             164
#define isc_dyn_delete_procedure          165
#define isc_dyn_def_parameter             135
#define isc_dyn_delete_parameter          136
#define isc_dyn_mod_procedure             175

#define isc_dyn_def_exception             181
#define isc_dyn_mod_exception             182
#define isc_dyn_del_exception             183

#define isc_dyn_def_difference            220
#define isc_dyn_drop_difference           221
#define isc_dyn_begin_backup              222
#define isc_dyn_end_backup                223
#define isc_dyn_debug_info                240

#define isc_dyn_view_blr                  43
#define isc_dyn_view_source               44
#define isc_dyn_view_relation             45
#define isc_dyn_view_context              46
#define isc_dyn_view_context_name         47

#define isc_dyn_rel_name                  50
#define isc_dyn_fld_name                  51
#define isc_dyn_new_fld_name              215
#define isc_dyn_idx_name                  52
#define isc_dyn_description               53
#define isc_dyn_security_class            54
#define isc_dyn_system_flag               55
#define isc_dyn_update_flag               56
#define isc_dyn_prc_name                  166
#define isc_dyn_prm_name                  137
#define isc_dyn_sql_object                196
#define isc_dyn_fld_character_set_name    174

#define isc_dyn_rel_dbkey_length          61
#define isc_dyn_rel_store_trig            62
#define isc_dyn_rel_modify_trig           63
#define isc_dyn_rel_erase_trig            64
#define isc_dyn_rel_store_trig_source     65
#define isc_dyn_rel_modify_trig_source    66
#define isc_dyn_rel_erase_trig_source     67
#define isc_dyn_rel_ext_file              68
#define isc_dyn_rel_sql_protection        69
#define isc_dyn_rel_constraint            162
#define isc_dyn_delete_rel_constraint     163

#define isc_dyn_rel_temporary                           238
#define isc_dyn_rel_temp_global_preserve        1
#define isc_dyn_rel_temp_global_delete          2

#define isc_dyn_fld_type                  70
#define isc_dyn_fld_length                71
#define isc_dyn_fld_scale                 72
#define isc_dyn_fld_sub_type              73
#define isc_dyn_fld_segment_length        74
#define isc_dyn_fld_query_header          75
#define isc_dyn_fld_edit_string           76
#define isc_dyn_fld_validation_blr        77
#define isc_dyn_fld_validation_source     78
#define isc_dyn_fld_computed_blr          79
#define isc_dyn_fld_computed_source       80
#define isc_dyn_fld_missing_value         81
#define isc_dyn_fld_default_value         82
#define isc_dyn_fld_query_name            83
#define isc_dyn_fld_dimensions            84
#define isc_dyn_fld_not_null              85
#define isc_dyn_fld_precision             86
#define isc_dyn_fld_char_length           172
#define isc_dyn_fld_collation             173
#define isc_dyn_fld_default_source        193
#define isc_dyn_del_default               197
#define isc_dyn_del_validation            198
#define isc_dyn_single_validation         199
#define isc_dyn_fld_character_set         203

#define isc_dyn_fld_source                90
#define isc_dyn_fld_base_fld              91
#define isc_dyn_fld_position              92
#define isc_dyn_fld_update_flag           93

#define isc_dyn_idx_unique                100
#define isc_dyn_idx_inactive              101
#define isc_dyn_idx_type                  103
#define isc_dyn_idx_foreign_key           104
#define isc_dyn_idx_ref_column            105
#define isc_dyn_idx_statistic             204

#define isc_dyn_trg_type                  110
#define isc_dyn_trg_blr                   111
#define isc_dyn_trg_source                112
#define isc_dyn_trg_name                  114
#define isc_dyn_trg_sequence              115
#define isc_dyn_trg_inactive              116
#define isc_dyn_trg_msg_number            117
#define isc_dyn_trg_msg                   118

#define isc_dyn_scl_acl                   121
#define isc_dyn_grant_user                130
#define isc_dyn_grant_user_explicit       219
#define isc_dyn_grant_proc                186
#define isc_dyn_grant_trig                187
#define isc_dyn_grant_view                188
#define isc_dyn_grant_options             132
#define isc_dyn_grant_user_group          205
#define isc_dyn_grant_role                218

#define isc_dyn_dim_lower                 141
#define isc_dyn_dim_upper                 142

#define isc_dyn_file_name                 125
#define isc_dyn_file_start                126
#define isc_dyn_file_length               127
#define isc_dyn_shadow_number             128
#define isc_dyn_shadow_man_auto           129
#define isc_dyn_shadow_conditional        130

#define isc_dyn_function_name             145
#define isc_dyn_function_type             146
#define isc_dyn_func_module_name          147
#define isc_dyn_func_entry_point          148
#define isc_dyn_func_return_argument      149
#define isc_dyn_func_arg_position         150
#define isc_dyn_func_mechanism            151
#define isc_dyn_filter_in_subtype         152
#define isc_dyn_filter_out_subtype        153

#define isc_dyn_description2              154
#define isc_dyn_fld_computed_source2      155
#define isc_dyn_fld_edit_string2          156
#define isc_dyn_fld_query_header2         157
#define isc_dyn_fld_validation_source2    158
#define isc_dyn_trg_msg2                  159
#define isc_dyn_trg_source2               160
#define isc_dyn_view_source2              161
#define isc_dyn_xcp_msg2                  184

#define isc_dyn_generator_name            95
#define isc_dyn_generator_id              96

#define isc_dyn_prc_inputs                167
#define isc_dyn_prc_outputs               168
#define isc_dyn_prc_source                169
#define isc_dyn_prc_blr                   170
#define isc_dyn_prc_source2               171
#define isc_dyn_prc_type                  239

#define isc_dyn_prc_t_selectable          1
#define isc_dyn_prc_t_executable          2

#define isc_dyn_prm_number                138
#define isc_dyn_prm_type                  139
#define isc_dyn_prm_mechanism             241

#define isc_dyn_xcp_msg                   185

#define isc_dyn_foreign_key_update        205
#define isc_dyn_foreign_key_delete        206
#define isc_dyn_foreign_key_cascade       207
#define isc_dyn_foreign_key_default       208
#define isc_dyn_foreign_key_null          209
#define isc_dyn_foreign_key_none          210

#define isc_dyn_def_sql_role              211
#define isc_dyn_sql_role_name             212
#define isc_dyn_grant_admin_options       213
#define isc_dyn_del_sql_role              214

#define isc_dyn_delete_generator          217

#define isc_dyn_mod_function              224
#define isc_dyn_mod_filter                225
#define isc_dyn_mod_generator             226
#define isc_dyn_mod_sql_role              227
#define isc_dyn_mod_charset               228
#define isc_dyn_mod_collation             229
#define isc_dyn_mod_prc_parameter         230

#define isc_dyn_def_collation                                           231
#define isc_dyn_coll_for_charset                                        232
#define isc_dyn_coll_from                                                       233
#define isc_dyn_coll_from_external                                      239
#define isc_dyn_coll_attribute                                          234
#define isc_dyn_coll_specific_attributes_charset        235
#define isc_dyn_coll_specific_attributes                        236
#define isc_dyn_del_collation                                           237

#define isc_dyn_last_dyn_value            242

#define isc_sdl_version1                  1
#define isc_sdl_eoc                       255
#define isc_sdl_relation                  2
#define isc_sdl_rid                       3
#define isc_sdl_field                     4
#define isc_sdl_fid                       5
#define isc_sdl_struct                    6
#define isc_sdl_variable                  7
#define isc_sdl_scalar                    8
#define isc_sdl_tiny_integer              9
#define isc_sdl_short_integer             10
#define isc_sdl_long_integer              11
#define isc_sdl_literal                   12
#define isc_sdl_add                       13
#define isc_sdl_subtract                  14
#define isc_sdl_multiply                  15
#define isc_sdl_divide                    16
#define isc_sdl_negate                    17
#define isc_sdl_eql                       18
#define isc_sdl_neq                       19
#define isc_sdl_gtr                       20
#define isc_sdl_geq                       21
#define isc_sdl_lss                       22
#define isc_sdl_leq                       23
#define isc_sdl_and                       24
#define isc_sdl_or                        25
#define isc_sdl_not                       26
#define isc_sdl_while                     27
#define isc_sdl_assignment                28
#define isc_sdl_label                     29
#define isc_sdl_leave                     30
#define isc_sdl_begin                     31
#define isc_sdl_end                       32
#define isc_sdl_do3                       33
#define isc_sdl_do2                       34
#define isc_sdl_do1                       35
#define isc_sdl_element                   36

#define isc_interp_eng_ascii              0
#define isc_interp_jpn_sjis               5
#define isc_interp_jpn_euc                6

#define isc_blob_untyped                  0

#define isc_blob_text                     1
#define isc_blob_blr                      2
#define isc_blob_acl                      3
#define isc_blob_ranges                   4
#define isc_blob_summary                  5
#define isc_blob_format                   6
#define isc_blob_tra                      7
#define isc_blob_extfile                  8
#define isc_blob_debug_info               9
#define isc_blob_max_predefined_subtype   10

#define isc_blob_formatted_memo           20
#define isc_blob_paradox_ole              21
#define isc_blob_graphic                  22
#define isc_blob_dbase_ole                23
#define isc_blob_typed_binary             24

#define isc_info_db_SQL_dialect           62
#define isc_dpb_SQL_dialect               63
#define isc_dpb_set_db_SQL_dialect        65

#define fb_dbg_version                          1
#define fb_dbg_end                                      255
#define fb_dbg_map_src2blr                      2
#define fb_dbg_map_varname                      3
#define fb_dbg_map_argument                     4

#define fb_dbg_arg_input                        0
#define fb_dbg_arg_output                       1

#endif 

/*********************************/
/* Information call declarations */
/*********************************/


#ifndef JRD_INF_PUB_H
#define JRD_INF_PUB_H

#define isc_info_end                    1
#define isc_info_truncated              2
#define isc_info_error                  3
#define isc_info_data_not_ready           4
#define isc_info_length                 126
#define isc_info_flag_end               127

enum db_info_types
{
        isc_info_db_id                  = 4,
        isc_info_reads                  = 5,
        isc_info_writes             = 6,
        isc_info_fetches                = 7,
        isc_info_marks                  = 8,

        isc_info_implementation = 11,
        isc_info_isc_version            = 12,
        isc_info_base_level             = 13,
        isc_info_page_size              = 14,
        isc_info_num_buffers    = 15,
        isc_info_limbo                  = 16,
        isc_info_current_memory = 17,
        isc_info_max_memory             = 18,
        isc_info_window_turns   = 19,
        isc_info_license                = 20,

        isc_info_allocation             = 21,
        isc_info_attachment_id   = 22,
        isc_info_read_seq_count = 23,
        isc_info_read_idx_count = 24,
        isc_info_insert_count           = 25,
        isc_info_update_count           = 26,
        isc_info_delete_count           = 27,
        isc_info_backout_count          = 28,
        isc_info_purge_count            = 29,
        isc_info_expunge_count          = 30,

        isc_info_sweep_interval = 31,
        isc_info_ods_version            = 32,
        isc_info_ods_minor_version      = 33,
        isc_info_no_reserve             = 34,

        isc_info_logfile                = 35,
        isc_info_cur_logfile_name       = 36,
        isc_info_cur_log_part_offset    = 37,
        isc_info_num_wal_buffers        = 38,
        isc_info_wal_buffer_size        = 39,
        isc_info_wal_ckpt_length        = 40,

        isc_info_wal_cur_ckpt_interval = 41,
        isc_info_wal_prv_ckpt_fname     = 42,
        isc_info_wal_prv_ckpt_poffset   = 43,
        isc_info_wal_recv_ckpt_fname    = 44,
        isc_info_wal_recv_ckpt_poffset = 45,
        isc_info_wal_grpc_wait_usecs    = 47,
        isc_info_wal_num_io             = 48,
        isc_info_wal_avg_io_size        = 49,
        isc_info_wal_num_commits        = 50,
        isc_info_wal_avg_grpc_size      = 51,

        isc_info_forced_writes          = 52,
        isc_info_user_names = 53,
        isc_info_page_errors = 54,
        isc_info_record_errors = 55,
        isc_info_bpage_errors = 56,
        isc_info_dpage_errors = 57,
        isc_info_ipage_errors = 58,
        isc_info_ppage_errors = 59,
        isc_info_tpage_errors = 60,

        isc_info_set_page_buffers = 61,
        isc_info_db_sql_dialect = 62,   
        isc_info_db_read_only = 63,
        isc_info_db_size_in_pages = 64,

        frb_info_att_charset = 101,
        isc_info_db_class = 102,
        isc_info_firebird_version = 103,
        isc_info_oldest_transaction = 104,
        isc_info_oldest_active = 105,
        isc_info_oldest_snapshot = 106,
        isc_info_next_transaction = 107,
        isc_info_db_provider = 108,
        isc_info_active_transactions = 109,
        isc_info_active_tran_count = 110,
        isc_info_creation_date = 111,
        isc_info_db_file_size = 112,

        isc_info_db_last_value   
};

#define isc_info_version isc_info_isc_version

enum  info_db_implementations
{
        isc_info_db_impl_rdb_vms = 1,
        isc_info_db_impl_rdb_eln = 2,
        isc_info_db_impl_rdb_eln_dev = 3,
        isc_info_db_impl_rdb_vms_y = 4,
        isc_info_db_impl_rdb_eln_y = 5,
        isc_info_db_impl_jri = 6,
        isc_info_db_impl_jsv = 7,

        isc_info_db_impl_isc_apl_68K = 25,
        isc_info_db_impl_isc_vax_ultr = 26,
        isc_info_db_impl_isc_vms = 27,
        isc_info_db_impl_isc_sun_68k = 28,
        isc_info_db_impl_isc_os2 = 29,
        isc_info_db_impl_isc_sun4 = 30,    

        isc_info_db_impl_isc_hp_ux = 31,
        isc_info_db_impl_isc_sun_386i = 32,
        isc_info_db_impl_isc_vms_orcl = 33,
        isc_info_db_impl_isc_mac_aux = 34,
        isc_info_db_impl_isc_rt_aix = 35,
        isc_info_db_impl_isc_mips_ult = 36,
        isc_info_db_impl_isc_xenix = 37,
        isc_info_db_impl_isc_dg = 38,
        isc_info_db_impl_isc_hp_mpexl = 39,
        isc_info_db_impl_isc_hp_ux68K = 40,       

        isc_info_db_impl_isc_sgi = 41,
        isc_info_db_impl_isc_sco_unix = 42,
        isc_info_db_impl_isc_cray = 43,
        isc_info_db_impl_isc_imp = 44,
        isc_info_db_impl_isc_delta = 45,
        isc_info_db_impl_isc_next = 46,
        isc_info_db_impl_isc_dos = 47,
        isc_info_db_impl_m88K = 48,
        isc_info_db_impl_unixware = 49,
        isc_info_db_impl_isc_winnt_x86 = 50,

        isc_info_db_impl_isc_epson = 51,
        isc_info_db_impl_alpha_osf = 52,
        isc_info_db_impl_alpha_vms = 53,
        isc_info_db_impl_netware_386 = 54, 
        isc_info_db_impl_win_only = 55,
        isc_info_db_impl_ncr_3000 = 56,
        isc_info_db_impl_winnt_ppc = 57,
        isc_info_db_impl_dg_x86 = 58,
        isc_info_db_impl_sco_ev = 59,
        isc_info_db_impl_i386 = 60,

        isc_info_db_impl_freebsd = 61,
        isc_info_db_impl_netbsd = 62,
        isc_info_db_impl_darwin_ppc = 63,
        isc_info_db_impl_sinixz = 64,

        isc_info_db_impl_linux_sparc = 65,
        isc_info_db_impl_linux_amd64 = 66,

        isc_info_db_impl_freebsd_amd64 = 67,

        isc_info_db_impl_winnt_amd64 = 68,

        isc_info_db_impl_linux_ppc = 69,
        isc_info_db_impl_darwin_x86 = 70,
        isc_info_db_impl_linux_mipsel = 71,
        isc_info_db_impl_linux_mips = 72,
        isc_info_db_impl_darwin_x64 = 73,

        isc_info_db_impl_last_value   
};

#define isc_info_db_impl_isc_a            isc_info_db_impl_isc_apl_68K
#define isc_info_db_impl_isc_u            isc_info_db_impl_isc_vax_ultr
#define isc_info_db_impl_isc_v            isc_info_db_impl_isc_vms
#define isc_info_db_impl_isc_s            isc_info_db_impl_isc_sun_68k

enum info_db_class
{
        isc_info_db_class_access = 1,
        isc_info_db_class_y_valve = 2,
        isc_info_db_class_rem_int = 3,
        isc_info_db_class_rem_srvr = 4,
        isc_info_db_class_pipe_int = 7,
        isc_info_db_class_pipe_srvr = 8,
        isc_info_db_class_sam_int = 9,
        isc_info_db_class_sam_srvr = 10,
        isc_info_db_class_gateway = 11,
        isc_info_db_class_cache = 12,
        isc_info_db_class_classic_access = 13,
        isc_info_db_class_server_access = 14,

        isc_info_db_class_last_value   
};

enum info_db_provider
{
        isc_info_db_code_rdb_eln = 1,
        isc_info_db_code_rdb_vms = 2,
        isc_info_db_code_interbase = 3,
        isc_info_db_code_firebird = 4,

        isc_info_db_code_last_value   
};

#define isc_info_number_messages        4
#define isc_info_max_message            5
#define isc_info_max_send               6
#define isc_info_max_receive            7
#define isc_info_state                  8
#define isc_info_message_number 9
#define isc_info_message_size           10
#define isc_info_request_cost           11
#define isc_info_access_path            12
#define isc_info_req_select_count       13
#define isc_info_req_insert_count       14
#define isc_info_req_update_count       15
#define isc_info_req_delete_count       16

#define isc_info_rsb_end                0
#define isc_info_rsb_begin              1
#define isc_info_rsb_type               2
#define isc_info_rsb_relation           3
#define isc_info_rsb_plan                       4

#define isc_info_rsb_unknown            1
#define isc_info_rsb_indexed            2
#define isc_info_rsb_navigate           3
#define isc_info_rsb_sequential 4
#define isc_info_rsb_cross              5
#define isc_info_rsb_sort               6
#define isc_info_rsb_first              7
#define isc_info_rsb_boolean            8
#define isc_info_rsb_union              9
#define isc_info_rsb_aggregate          10
#define isc_info_rsb_merge              11
#define isc_info_rsb_ext_sequential     12
#define isc_info_rsb_ext_indexed        13
#define isc_info_rsb_ext_dbkey          14
#define isc_info_rsb_left_cross 15
#define isc_info_rsb_select             16
#define isc_info_rsb_sql_join           17
#define isc_info_rsb_simulate           18
#define isc_info_rsb_sim_cross          19
#define isc_info_rsb_once               20
#define isc_info_rsb_procedure          21
#define isc_info_rsb_skip               22
#define isc_info_rsb_virt_sequential    23
#define isc_info_rsb_recursive  24

#define isc_info_rsb_and                1
#define isc_info_rsb_or         2
#define isc_info_rsb_dbkey              3
#define isc_info_rsb_index              4

#define isc_info_req_active             2
#define isc_info_req_inactive           3
#define isc_info_req_send               4
#define isc_info_req_receive            5
#define isc_info_req_select             6
#define isc_info_req_sql_stall          7

#define isc_info_blob_num_segments      4
#define isc_info_blob_max_segment       5
#define isc_info_blob_total_length      6
#define isc_info_blob_type              7

#define isc_info_tra_id                                         4
#define isc_info_tra_oldest_interesting         5
#define isc_info_tra_oldest_snapshot            6
#define isc_info_tra_oldest_active                      7
#define isc_info_tra_isolation                          8
#define isc_info_tra_access                                     9
#define isc_info_tra_lock_timeout                       10

#define isc_info_tra_consistency                1
#define isc_info_tra_concurrency                2
#define isc_info_tra_read_committed             3

#define isc_info_tra_no_rec_version             0
#define isc_info_tra_rec_version                1

#define isc_info_tra_readonly   0
#define isc_info_tra_readwrite  1

#define isc_info_sql_select             4
#define isc_info_sql_bind               5
#define isc_info_sql_num_variables      6
#define isc_info_sql_describe_vars      7
#define isc_info_sql_describe_end       8
#define isc_info_sql_sqlda_seq          9
#define isc_info_sql_message_seq        10
#define isc_info_sql_type               11
#define isc_info_sql_sub_type           12
#define isc_info_sql_scale              13
#define isc_info_sql_length             14
#define isc_info_sql_null_ind           15
#define isc_info_sql_field              16
#define isc_info_sql_relation           17
#define isc_info_sql_owner              18
#define isc_info_sql_alias              19
#define isc_info_sql_sqlda_start        20
#define isc_info_sql_stmt_type          21
#define isc_info_sql_get_plan             22
#define isc_info_sql_records              23
#define isc_info_sql_batch_fetch          24
#define isc_info_sql_relation_alias             25

#define isc_info_sql_stmt_select          1
#define isc_info_sql_stmt_insert          2
#define isc_info_sql_stmt_update          3
#define isc_info_sql_stmt_delete          4
#define isc_info_sql_stmt_ddl             5
#define isc_info_sql_stmt_get_segment     6
#define isc_info_sql_stmt_put_segment     7
#define isc_info_sql_stmt_exec_procedure  8
#define isc_info_sql_stmt_start_trans     9
#define isc_info_sql_stmt_commit          10
#define isc_info_sql_stmt_rollback        11
#define isc_info_sql_stmt_select_for_upd  12
#define isc_info_sql_stmt_set_generator   13
#define isc_info_sql_stmt_savepoint       14

#endif 


#include "iberror.h"

#endif /* JRD_IBASE_H */

