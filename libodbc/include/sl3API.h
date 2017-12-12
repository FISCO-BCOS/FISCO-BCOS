//////////////////////////////////////////////////////////////////////
// slAPI.h
//////////////////////////////////////////////////////////////////////

#if !defined(__SLAPI_H__)
#define __SLAPI_H__

#include <SQLAPI.h>
#include <sqlite3.h>

extern void AddSQLite3Support(const SAConnection * pCon);
extern void ReleaseSQLite3Support();

typedef int (*sqlite3_open_t)(const void *filename, sqlite3 **ppDb);
typedef const char * (*sqlite3_libversion_t)(void);
typedef int (*sqlite3_libversion_number_t)(void);
typedef int (*sqlite3_threadsafe_t)(void);
typedef int (*sqlite3_close_t)(sqlite3 *);
typedef int (*sqlite3_extended_result_codes_t)(sqlite3*, int onoff);
typedef sqlite3_int64 (*sqlite3_last_insert_rowid_t)(sqlite3*);
typedef int (*sqlite3_changes_t)(sqlite3*);
typedef int (*sqlite3_total_changes_t)(sqlite3*);
typedef void (*sqlite3_interrupt_t)(sqlite3*);
typedef int (*sqlite3_complete_t)(const char *sql);
typedef int (*sqlite3_complete16_t)(const void *sql);
typedef int (*sqlite3_busy_handler_t)(sqlite3*, int(*)(void*,int), void*);
typedef int (*sqlite3_busy_timeout_t)(sqlite3*, int ms);
typedef void (*sqlite3_free_table_t)(char **result);
typedef char *(*sqlite3_mprintf_t)(const char*,...);
typedef char *(*sqlite3_vmprintf_t)(const char*, va_list);
typedef char *(*sqlite3_snprintf_t)(int,char*,const char*, ...);
typedef void *(*sqlite3_malloc_t)(int);
typedef void *(*sqlite3_realloc_t)(void*, int);
typedef void (*sqlite3_free_t)(void*);
typedef sqlite3_int64 (*sqlite3_memory_used_t)(void);
typedef sqlite3_int64 (*sqlite3_memory_highwater_t)(int resetFlag);
typedef void *(*sqlite3_trace_t)(sqlite3*, void(*xTrace)(void*,const char*), void*);
typedef void (*sqlite3_progress_handler_t)(sqlite3*, int, int(*)(void*), void*);
typedef int (*sqlite3_errcode_t)(sqlite3 *db);
typedef const void *(*sqlite3_errmsg_t)(sqlite3*);
typedef int (*sqlite3_bind_blob_t)(sqlite3_stmt*, int, const void*, int n, void(*)(void*));
typedef int (*sqlite3_bind_double_t)(sqlite3_stmt*, int, double);
typedef int (*sqlite3_bind_int_t)(sqlite3_stmt*, int, int);
typedef int (*sqlite3_bind_int64_t)(sqlite3_stmt*, int, sqlite3_int64);
typedef int (*sqlite3_bind_null_t)(sqlite3_stmt*, int);
typedef int (*sqlite3_bind_text_t)(sqlite3_stmt*, int, const void*, int n, void(*)(void*));
typedef int (*sqlite3_bind_value_t)(sqlite3_stmt*, int, const sqlite3_value*);
typedef int (*sqlite3_bind_zeroblob_t)(sqlite3_stmt*, int, int n);
typedef int (*sqlite3_bind_parameter_count_t)(sqlite3_stmt*);
typedef const char *(*sqlite3_bind_parameter_name_t)(sqlite3_stmt*, int);
typedef int (*sqlite3_bind_parameter_index_t)(sqlite3_stmt*, const char *zName);
typedef int (*sqlite3_clear_bindings_t)(sqlite3_stmt*);
typedef int (*sqlite3_column_count_t)(sqlite3_stmt *pStmt);
typedef const void *(*sqlite3_column_name_t)(sqlite3_stmt*, int N);
typedef const char *(*sqlite3_column_database_name_t)(sqlite3_stmt*,int);
typedef const void *(*sqlite3_column_database_name16_t)(sqlite3_stmt*,int);
typedef const char *(*sqlite3_column_table_name_t)(sqlite3_stmt*,int);
typedef const void *(*sqlite3_column_table_name16_t)(sqlite3_stmt*,int);
typedef const char *(*sqlite3_column_origin_name_t)(sqlite3_stmt*,int);
typedef const void *(*sqlite3_column_origin_name16_t)(sqlite3_stmt*,int);
typedef const void *(*sqlite3_column_decltype_t)(sqlite3_stmt *, int);
typedef int (*sqlite3_step_t)(sqlite3_stmt*);
typedef int (*sqlite3_data_count_t)(sqlite3_stmt *pStmt);
typedef const void *(*sqlite3_column_blob_t)(sqlite3_stmt*, int iCol);
typedef int (*sqlite3_column_bytes_t)(sqlite3_stmt*, int iCol);
typedef double (*sqlite3_column_double_t)(sqlite3_stmt*, int iCol);
typedef int (*sqlite3_column_int_t)(sqlite3_stmt*, int iCol);
typedef sqlite3_int64 (*sqlite3_column_int64_t)(sqlite3_stmt*, int iCol);
typedef const void *(*sqlite3_column_text_t)(sqlite3_stmt*, int iCol);
typedef int (*sqlite3_column_type_t)(sqlite3_stmt*, int iCol);
typedef sqlite3_value *(*sqlite3_column_value_t)(sqlite3_stmt*, int iCol);
typedef int (*sqlite3_finalize_t)(sqlite3_stmt *pStmt);
typedef int (*sqlite3_reset_t)(sqlite3_stmt *pStmt);
typedef int (*sqlite3_aggregate_count_t)(sqlite3_context*);
typedef int (*sqlite3_expired_t)(sqlite3_stmt*);
typedef int (*sqlite3_transfer_bindings_t)(sqlite3_stmt*, sqlite3_stmt*);
typedef int (*sqlite3_global_recover_t)(void);
typedef void (*sqlite3_thread_cleanup_t)(void);
typedef const void *(*sqlite3_value_blob_t)(sqlite3_value*);
typedef int (*sqlite3_value_bytes_t)(sqlite3_value*);
typedef int (*sqlite3_value_bytes16_t)(sqlite3_value*);
typedef double (*sqlite3_value_double_t)(sqlite3_value*);
typedef int (*sqlite3_value_int_t)(sqlite3_value*);
typedef sqlite3_int64 (*sqlite3_value_int64_t)(sqlite3_value*);
typedef const unsigned char *(*sqlite3_value_text_t)(sqlite3_value*);
typedef const void *(*sqlite3_value_text16_t)(sqlite3_value*);
typedef const void *(*sqlite3_value_text16le_t)(sqlite3_value*);
typedef const void *(*sqlite3_value_text16be_t)(sqlite3_value*);
typedef int (*sqlite3_value_type_t)(sqlite3_value*);
typedef int (*sqlite3_value_numeric_type_t)(sqlite3_value*);
typedef void *(*sqlite3_aggregate_context_t)(sqlite3_context*, int nBytes);
typedef void *(*sqlite3_user_data_t)(sqlite3_context*);
typedef void *(*sqlite3_get_auxdata_t)(sqlite3_context*, int);
typedef void (*sqlite3_set_auxdata_t)(sqlite3_context*, int, void*, void (*)(void*));
typedef void (*sqlite3_result_blob_t)(sqlite3_context*, const void*, int, void(*)(void*));
typedef void (*sqlite3_result_double_t)(sqlite3_context*, double);
typedef void (*sqlite3_result_error_t)(sqlite3_context*, const char*, int);
typedef void (*sqlite3_result_error16_t)(sqlite3_context*, const void*, int);
typedef void (*sqlite3_result_error_toobig_t)(sqlite3_context*);
typedef void (*sqlite3_result_error_nomem_t)(sqlite3_context*);
typedef void (*sqlite3_result_int_t)(sqlite3_context*, int);
typedef void (*sqlite3_result_int64_t)(sqlite3_context*, sqlite3_int64);
typedef void (*sqlite3_result_null_t)(sqlite3_context*);
typedef void (*sqlite3_result_text_t)(sqlite3_context*, const char*, int, void(*)(void*));
typedef void (*sqlite3_result_text16_t)(sqlite3_context*, const void*, int, void(*)(void*));
typedef void (*sqlite3_result_text16le_t)(sqlite3_context*, const void*, int,void(*)(void*));
typedef void (*sqlite3_result_text16be_t)(sqlite3_context*, const void*, int,void(*)(void*));
typedef void (*sqlite3_result_value_t)(sqlite3_context*, sqlite3_value*);
typedef void (*sqlite3_result_zeroblob_t)(sqlite3_context*, int n);
typedef int (*sqlite3_sleep_t)(int);
typedef int (*sqlite3_get_autocommit_t)(sqlite3*);
typedef sqlite3 *(*sqlite3_db_handle_t)(sqlite3_stmt*);
typedef void *(*sqlite3_commit_hook_t)(sqlite3*, int(*)(void*), void*);
typedef void *(*sqlite3_rollback_hook_t)(sqlite3*, void(*)(void *), void*);
typedef int (*sqlite3_enable_shared_cache_t)(int);
typedef int (*sqlite3_release_memory_t)(int);
typedef void (*sqlite3_soft_heap_limit_t)(int);
typedef int (*sqlite3_enable_load_extension_t)(sqlite3 *db, int onoff);
typedef int (*sqlite3_auto_extension_t)(void *xEntryPoint);
typedef void (*sqlite3_reset_auto_extension_t)(void);
typedef int (*sqlite3_declare_vtab_t)(sqlite3*, const char *zCreateTable);
typedef int (*sqlite3_overload_function_t)(sqlite3*, const char *zFuncName, int nArg);
typedef int (*sqlite3_blob_close_t)(sqlite3_blob *);
typedef int (*sqlite3_blob_bytes_t)(sqlite3_blob *);
typedef int (*sqlite3_blob_read_t)(sqlite3_blob *, void *z, int n, int iOffset);
typedef int (*sqlite3_blob_write_t)(sqlite3_blob *, const void *z, int n, int iOffset);
typedef sqlite3_vfs *(*sqlite3_vfs_find_t)(const char *zVfsName);
typedef int (*sqlite3_vfs_register_t)(sqlite3_vfs*, int makeDflt);
typedef int (*sqlite3_vfs_unregister_t)(sqlite3_vfs*);
typedef sqlite3_mutex *(*sqlite3_mutex_alloc_t)(int);
typedef void (*sqlite3_mutex_free_t)(sqlite3_mutex*);
typedef void (*sqlite3_mutex_enter_t)(sqlite3_mutex*);
typedef int (*sqlite3_mutex_try_t)(sqlite3_mutex*);
typedef void (*sqlite3_mutex_leave_t)(sqlite3_mutex*);
typedef int (*sqlite3_mutex_held_t)(sqlite3_mutex*);
typedef int (*sqlite3_mutex_notheld_t)(sqlite3_mutex*);
typedef int (*sqlite3_file_control_t)(sqlite3*, const char *zDbName, int op, void*);
typedef int (*sqlite3_exec_t)(
  sqlite3*,                                  /* An open database */
  const char *sql,                           /* SQL to be evaluted */
  int (*callback)(void*,int,char**,char**),  /* Callback function */
  void *,                                    /* 1st argument to callback */
  char **errmsg                              /* Error msg written here */
);
typedef int (*sqlite3_prepare_t)(
  sqlite3 *db,            /* Database handle */
  const void *zSql,       /* SQL statement, UTF-16 encoded */
  int nByte,              /* Maximum length of zSql in bytes. */
  sqlite3_stmt **ppStmt,  /* OUT: Statement handle */
  const void **pzTail     /* OUT: Pointer to unused portion of zSql */
);
typedef int (*sqlite3_open_v2_t)(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);

typedef sqlite3_backup *(*sqlite3_backup_init_t)(
  sqlite3 *pDest,                        /* Destination database handle */
  const char *zDestName,                 /* Destination database name */
  sqlite3 *pSource,                      /* Source database handle */
  const char *zSourceName                /* Source database name */
);
typedef int (*sqlite3_backup_step_t)(sqlite3_backup *p, int nPage);
typedef int (*sqlite3_backup_finish_t)(sqlite3_backup *p);
typedef int (*sqlite3_backup_remaining_t)(sqlite3_backup *p);
typedef int (*sqlite3_backup_pagecount_t)(sqlite3_backup *p);


typedef int (*sqlite3_table_column_metadata_t)(
  sqlite3 *db,                /* Connection handle */
  const char *zDbName,        /* Database name or NULL */
  const char *zTableName,     /* Table name */
  const char *zColumnName,    /* Column name */
  char const **pzDataType,    /* OUTPUT: Declared data type */
  char const **pzCollSeq,     /* OUTPUT: Collation sequence name */
  int *pNotNull,              /* OUTPUT: True if NOT NULL constraint exists */
  int *pPrimaryKey,           /* OUTPUT: True if column part of PK */
  int *pAutoinc               /* OUTPUT: True if column is auto-increment */
);

// API declarations
class SQLAPI_API sl3API : public saAPI
{
public:
	sl3API();

	sqlite3_open_t sqlite3_open;
	sqlite3_libversion_t sqlite3_libversion;
	sqlite3_libversion_number_t sqlite3_libversion_number;
	sqlite3_errcode_t sqlite3_errcode;
	sqlite3_errmsg_t sqlite3_errmsg;
	sqlite3_close_t sqlite3_close;
	sqlite3_exec_t sqlite3_exec;
	sqlite3_prepare_t sqlite3_prepare;
	sqlite3_bind_parameter_index_t sqlite3_bind_parameter_index;
	sqlite3_column_count_t sqlite3_column_count;
	sqlite3_column_name_t sqlite3_column_name;
	sqlite3_column_type_t sqlite3_column_type;
	sqlite3_column_bytes_t sqlite3_column_bytes;
	sqlite3_step_t sqlite3_step;
	sqlite3_db_handle_t sqlite3_db_handle;
	sqlite3_reset_t sqlite3_reset;
	sqlite3_clear_bindings_t sqlite3_clear_bindings;
	sqlite3_finalize_t sqlite3_finalize;
	sqlite3_interrupt_t sqlite3_interrupt;
	sqlite3_changes_t sqlite3_changes;
	sqlite3_column_int64_t sqlite3_column_int64;
	sqlite3_column_double_t sqlite3_column_double;
	sqlite3_column_blob_t sqlite3_column_blob;
	sqlite3_column_text_t sqlite3_column_text;
	sqlite3_bind_blob_t sqlite3_bind_blob;
	sqlite3_bind_double_t sqlite3_bind_double;
	sqlite3_bind_int_t sqlite3_bind_int;
	sqlite3_bind_int64_t sqlite3_bind_int64;
	sqlite3_bind_null_t sqlite3_bind_null;
	sqlite3_bind_text_t sqlite3_bind_text;

	sqlite3_busy_handler_t sqlite3_busy_handler;
	sqlite3_busy_timeout_t sqlite3_busy_timeout;
	sqlite3_threadsafe_t sqlite3_threadsafe;
	sqlite3_last_insert_rowid_t sqlite3_last_insert_rowid;

	sqlite3_column_decltype_t sqlite3_column_decltype;

	sqlite3_open_v2_t sqlite3_open_v2;

	sqlite3_backup_init_t sqlite3_backup_init;
	sqlite3_backup_step_t sqlite3_backup_step;
	sqlite3_backup_finish_t sqlite3_backup_finish;
	sqlite3_backup_remaining_t sqlite3_backup_remaining;
	sqlite3_backup_pagecount_t sqlite3_backup_pagecount;

	sqlite3_table_column_metadata_t sqlite3_table_column_metadata;

	sqlite3_column_value_t sqlite3_column_value;
	sqlite3_value_type_t sqlite3_value_type;
};

class SQLAPI_API sl3ConnectionHandles : public saConnectionHandles
{
public:
	sl3ConnectionHandles();
	sqlite3 *pDb;
};

class SQLAPI_API sl3CommandHandles : public saCommandHandles
{
public:
	sl3CommandHandles();
	sqlite3_stmt *pStmt;
};

extern sl3API g_sl3API;

#endif //__SLAPI_H__
