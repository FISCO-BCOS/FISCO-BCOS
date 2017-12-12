// ss6API.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__SS6API_H__)
#define __SS6API_H__

#include "SQLAPI.h"

// API header(s)
#define DBNTWIN32
#include "./ss_win/sqlfront.h"
#include "./ss_win/sqldb.h"

extern long g_nSSDBLibDLLVersionLoaded;

extern void AddSSDbLibSupport();
extern void ReleaseSSDbLibSupport();

typedef DBERRHANDLE_PROC (SQLAPI *dberrhandle_t)(DBERRHANDLE_PROC);
typedef DBMSGHANDLE_PROC (SQLAPI *dbmsghandle_t)(DBMSGHANDLE_PROC);
typedef DBERRHANDLE_PROC (SQLAPI *dbprocerrhandle_t)(PDBHANDLE, DBERRHANDLE_PROC);
typedef DBMSGHANDLE_PROC (SQLAPI *dbprocmsghandle_t)(PDBHANDLE, DBMSGHANDLE_PROC);

// Two-phase commit functions
typedef RETCODE      (SQLAPI *abort_xact_t) (PDBPROCESS, DBINT);
typedef void         (SQLAPI *build_xact_string_t) (LPCSTR, LPCSTR, DBINT, LPSTR);
typedef void         (SQLAPI *close_commit_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *commit_xact_t) (PDBPROCESS, DBINT);
typedef PDBPROCESS   (SQLAPI *open_commit_t) (PLOGINREC, LPCSTR);
typedef RETCODE      (SQLAPI *remove_xact_t) (PDBPROCESS, DBINT, INT);
typedef RETCODE      (SQLAPI *scan_xact_t) (PDBPROCESS, DBINT);
typedef DBINT        (SQLAPI *start_xact_t) (PDBPROCESS, LPCSTR, LPCSTR, INT);
typedef INT          (SQLAPI *stat_xact_t) (PDBPROCESS, DBINT);

// BCP functions
typedef DBINT        (SQLAPI *bcp_batch_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *bcp_bind_t) (PDBPROCESS, LPCBYTE, INT, DBINT, LPCBYTE, INT, INT, INT);
typedef RETCODE      (SQLAPI *bcp_colfmt_t) (PDBPROCESS, INT, BYTE, INT, DBINT, LPCBYTE, INT, INT);
typedef RETCODE      (SQLAPI *bcp_collen_t) (PDBPROCESS, DBINT, INT);
typedef RETCODE      (SQLAPI *bcp_colptr_t) (PDBPROCESS, LPCBYTE, INT);
typedef RETCODE      (SQLAPI *bcp_columns_t) (PDBPROCESS, INT);
typedef RETCODE      (SQLAPI *bcp_control_t) (PDBPROCESS, INT, DBINT);
typedef DBINT        (SQLAPI *bcp_done_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *bcp_exec_t) (PDBPROCESS, LPDBINT);
typedef RETCODE      (SQLAPI *bcp_init_t) (PDBPROCESS, LPCSTR, LPCSTR, LPCSTR, INT);
typedef RETCODE      (SQLAPI *bcp_moretext_t) (PDBPROCESS, DBINT, LPCBYTE);
typedef RETCODE      (SQLAPI *bcp_readfmt_t) (PDBPROCESS, LPCSTR);
typedef RETCODE      (SQLAPI *bcp_sendrow_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *bcp_setl_t) (PLOGINREC, BOOL);
typedef RETCODE      (SQLAPI *bcp_writefmt_t) (PDBPROCESS, LPCSTR);

// Standard DB-Library functions
typedef LPCBYTE      (SQLAPI *dbadata_t) (PDBPROCESS, INT, INT);
typedef DBINT        (SQLAPI *dbadlen_t) (PDBPROCESS, INT, INT);
typedef RETCODE      (SQLAPI *dbaltbind_t) (PDBPROCESS, INT, INT, INT, DBINT, LPCBYTE);
typedef INT          (SQLAPI *dbaltcolid_t) (PDBPROCESS, INT, INT);
typedef DBINT        (SQLAPI *dbaltlen_t) (PDBPROCESS, INT, INT);
typedef INT          (SQLAPI *dbaltop_t) (PDBPROCESS, INT, INT);
typedef INT          (SQLAPI *dbalttype_t) (PDBPROCESS, INT, INT);
typedef DBINT        (SQLAPI *dbaltutype_t) (PDBPROCESS, INT, INT);
typedef RETCODE      (SQLAPI *dbanullbind_t) (PDBPROCESS, INT, INT, LPCDBINT);
typedef RETCODE      (SQLAPI *dbbind_t) (PDBPROCESS, INT, INT, DBINT, LPBYTE);
typedef LPCBYTE      (SQLAPI *dbbylist_t) (PDBPROCESS, INT, LPINT);
typedef RETCODE      (SQLAPI *dbcancel_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbcanquery_t) (PDBPROCESS);
typedef LPCSTR       (SQLAPI *dbchange_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbclose_t) (PDBPROCESS);
typedef void         (SQLAPI *dbclrbuf_t) (PDBPROCESS, DBINT);
typedef RETCODE      (SQLAPI *dbclropt_t) (PDBPROCESS, INT, LPCSTR);
typedef RETCODE      (SQLAPI *dbcmd_t) (PDBPROCESS, LPCSTR);
typedef RETCODE      (SQLAPI *dbcmdrow_t) (PDBPROCESS);
typedef BOOL         (SQLAPI *dbcolbrowse_t) (PDBPROCESS, INT);
typedef RETCODE      (SQLAPI *dbcolinfo_t) (PDBHANDLE, INT, INT, INT, LPDBCOL);
typedef DBINT        (SQLAPI *dbcollen_t) (PDBPROCESS, INT);
typedef LPCSTR       (SQLAPI *dbcolname_t) (PDBPROCESS, INT);
typedef LPCSTR       (SQLAPI *dbcolsource_t) (PDBPROCESS, INT);
typedef INT          (SQLAPI *dbcoltype_t) (PDBPROCESS, INT);
typedef DBINT        (SQLAPI *dbcolutype_t) (PDBPROCESS, INT);
typedef INT          (SQLAPI *dbconvert_t) (PDBPROCESS, INT, LPCBYTE, DBINT, INT, LPBYTE, DBINT);
typedef DBINT        (SQLAPI *dbcount_t) (PDBPROCESS);
typedef INT          (SQLAPI *dbcurcmd_t) (PDBPROCESS);
typedef DBINT        (SQLAPI *dbcurrow_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbcursor_t) (PDBCURSOR, INT, INT, LPCSTR, LPCSTR);
typedef RETCODE      (SQLAPI *dbcursorbind_t) (PDBCURSOR, INT, INT, DBINT, LPDBINT, LPBYTE);
typedef RETCODE      (SQLAPI *dbcursorclose_t) (PDBHANDLE);
typedef RETCODE      (SQLAPI *dbcursorcolinfo_t) (PDBCURSOR, INT, LPSTR, LPINT, LPDBINT, LPINT);
typedef RETCODE      (SQLAPI *dbcursorfetch_t) (PDBCURSOR,  INT, INT);
typedef RETCODE      (SQLAPI *dbcursorfetchex_t) (PDBCURSOR, INT, DBINT, DBINT, DBINT);
typedef RETCODE      (SQLAPI *dbcursorinfo_t) (PDBCURSOR, LPINT, LPDBINT);
typedef RETCODE      (SQLAPI *dbcursorinfoex_t) (PDBCURSOR, LPDBCURSORINFO);
typedef PDBCURSOR    (SQLAPI *dbcursoropen_t) (PDBPROCESS, LPCSTR, INT, INT,UINT, LPDBINT);
typedef LPCBYTE      (SQLAPI *dbdata_t) (PDBPROCESS, INT);
typedef BOOL         (SQLAPI *dbdataready_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbdatecrack_t) (PDBPROCESS, LPDBDATEREC, LPCDBDATETIME);
typedef DBINT        (SQLAPI *dbdatlen_t) (PDBPROCESS, INT);
typedef BOOL         (SQLAPI *dbdead_t) (PDBPROCESS);
typedef void         (SQLAPI *dbexit_t) (void);
typedef RETCODE 	    (SQLAPI *dbenlisttrans_t) (PDBPROCESS, LPVOID);
typedef RETCODE	    (SQLAPI *dbenlistxatrans_t) (PDBPROCESS, BOOL);
typedef RETCODE	    (SQLAPI *dbfcmd_t) (PDBPROCESS, LPCSTR, ...);
typedef DBINT        (SQLAPI *dbfirstrow_t) (PDBPROCESS);
typedef void         (SQLAPI *dbfreebuf_t) (PDBPROCESS);
typedef void         (SQLAPI *dbfreelogin_t) (PLOGINREC);
typedef void         (SQLAPI *dbfreequal_t) (LPCSTR);
typedef LPSTR        (SQLAPI *dbgetchar_t) (PDBPROCESS, INT);
typedef SHORT        (SQLAPI *dbgetmaxprocs_t) (void);
typedef INT          (SQLAPI *dbgetoff_t) (PDBPROCESS, DBUSMALLINT, INT);
typedef UINT         (SQLAPI *dbgetpacket_t) (PDBPROCESS);
typedef STATUS       (SQLAPI *dbgetrow_t) (PDBPROCESS, DBINT);
typedef INT          (SQLAPI *dbgettime_t) (void);
typedef LPVOID       (SQLAPI *dbgetuserdata_t) (PDBPROCESS);
typedef BOOL         (SQLAPI *dbhasretstat_t) (PDBPROCESS);
typedef LPCSTR       (SQLAPI *dbinit_t) (void);
typedef BOOL         (SQLAPI *dbisavail_t) (PDBPROCESS);
typedef BOOL         (SQLAPI *dbiscount_t) (PDBPROCESS);
typedef BOOL         (SQLAPI *dbisopt_t) (PDBPROCESS, INT, LPCSTR);
typedef DBINT        (SQLAPI *dblastrow_t) (PDBPROCESS);
typedef PLOGINREC    (SQLAPI *dblogin_t) (void);
typedef RETCODE      (SQLAPI *dbmorecmds_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbmoretext_t) (PDBPROCESS, DBINT, LPCBYTE);
typedef LPCSTR       (SQLAPI *dbname_t) (PDBPROCESS);
typedef STATUS       (SQLAPI *dbnextrow_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbnullbind_t) (PDBPROCESS, INT, LPCDBINT);
typedef INT          (SQLAPI *dbnumalts_t) (PDBPROCESS, INT);
typedef INT          (SQLAPI *dbnumcols_t) (PDBPROCESS);
typedef INT          (SQLAPI *dbnumcompute_t) (PDBPROCESS);
typedef INT          (SQLAPI *dbnumorders_t) (PDBPROCESS);
typedef INT          (SQLAPI *dbnumrets_t) (PDBPROCESS);
typedef PDBPROCESS   (SQLAPI *dbopen_t) (PLOGINREC, LPCSTR);
typedef INT          (SQLAPI *dbordercol_t) (PDBPROCESS, INT);
typedef RETCODE      (SQLAPI *dbprocinfo_t) (PDBPROCESS, LPDBPROCINFO);
typedef void         (SQLAPI *dbprhead_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbprrow_t) (PDBPROCESS);
typedef LPCSTR       (SQLAPI *dbprtype_t) (INT);
typedef LPCSTR       (SQLAPI *dbqual_t) (PDBPROCESS, INT, LPCSTR);
typedef DBINT        (SQLAPI *dbreadpage_t) (PDBPROCESS, LPCSTR, DBINT, LPBYTE);
typedef DBINT        (SQLAPI *dbreadtext_t) (PDBPROCESS, LPVOID, DBINT);
typedef RETCODE      (SQLAPI *dbresults_t) (PDBPROCESS);
typedef LPCBYTE      (SQLAPI *dbretdata_t) (PDBPROCESS, INT);
typedef DBINT        (SQLAPI *dbretlen_t) (PDBPROCESS, INT);
typedef LPCSTR       (SQLAPI *dbretname_t) (PDBPROCESS, INT);
typedef DBINT        (SQLAPI *dbretstatus_t) (PDBPROCESS);
typedef INT          (SQLAPI *dbrettype_t) (PDBPROCESS, INT);
typedef RETCODE      (SQLAPI *dbrows_t) (PDBPROCESS);
typedef STATUS       (SQLAPI *dbrowtype_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbrpcinit_t) (PDBPROCESS, LPCSTR, DBSMALLINT);
typedef RETCODE      (SQLAPI *dbrpcparam_t) (PDBPROCESS, LPCSTR, BYTE, INT, DBINT, DBINT, LPCBYTE);
typedef RETCODE      (SQLAPI *dbrpcsend_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbrpcexec_t) (PDBPROCESS);
typedef void         (SQLAPI *dbrpwclr_t) (PLOGINREC);
typedef RETCODE      (SQLAPI *dbrpwset_t) (PLOGINREC, LPCSTR, LPCSTR, INT);
typedef INT          (SQLAPI *dbserverenum_t) (USHORT, LPSTR, USHORT, LPUSHORT);
typedef void         (SQLAPI *dbsetavail_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbsetmaxprocs_t) (SHORT);
typedef RETCODE      (SQLAPI *dbsetlname_t) (PLOGINREC, LPCSTR, INT);
typedef RETCODE      (SQLAPI *dbsetlogintime_t) (INT);
typedef RETCODE      (SQLAPI *dbsetlpacket_t) (PLOGINREC, USHORT);
typedef RETCODE      (SQLAPI *dbsetnull_t) (PDBPROCESS, INT, INT, LPCBYTE);
typedef RETCODE      (SQLAPI *dbsetopt_t) (PDBPROCESS, INT, LPCSTR);
typedef RETCODE      (SQLAPI *dbsettime_t) (INT);
typedef void         (SQLAPI *dbsetuserdata_t) (PDBPROCESS, LPVOID);
typedef RETCODE      (SQLAPI *dbsqlexec_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbsqlok_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbsqlsend_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbstrcpy_t) (PDBPROCESS, INT, INT, LPSTR);
typedef INT          (SQLAPI *dbstrlen_t) (PDBPROCESS);
typedef BOOL         (SQLAPI *dbtabbrowse_t) (PDBPROCESS, INT);
typedef INT          (SQLAPI *dbtabcount_t) (PDBPROCESS);
typedef LPCSTR       (SQLAPI *dbtabname_t) (PDBPROCESS, INT);
typedef LPCSTR       (SQLAPI *dbtabsource_t) (PDBPROCESS, INT, LPINT);
typedef INT          (SQLAPI *dbtsnewlen_t) (PDBPROCESS);
typedef LPCDBBINARY  (SQLAPI *dbtsnewval_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbtsput_t) (PDBPROCESS, LPCDBBINARY, INT, INT, LPCSTR);
typedef LPCDBBINARY  (SQLAPI *dbtxptr_t) (PDBPROCESS, INT);
typedef LPCDBBINARY  (SQLAPI *dbtxtimestamp_t) (PDBPROCESS, INT);
typedef LPCDBBINARY  (SQLAPI *dbtxtsnewval_t) (PDBPROCESS);
typedef RETCODE      (SQLAPI *dbtxtsput_t) (PDBPROCESS, LPCDBBINARY, INT);
typedef RETCODE      (SQLAPI *dbuse_t) (PDBPROCESS, LPCSTR);
typedef BOOL         (SQLAPI *dbvarylen_t) (PDBPROCESS, INT);
typedef BOOL         (SQLAPI *dbwillconvert_t) (INT, INT);
typedef RETCODE      (SQLAPI *dbwritepage_t) (PDBPROCESS, LPCSTR, DBINT, DBINT, LPBYTE);
typedef RETCODE      (SQLAPI *dbwritetext_t) (PDBPROCESS, LPCSTR, LPCDBBINARY, DBTINYINT, LPCDBBINARY, BOOL, DBINT, LPCBYTE);
typedef RETCODE      (SQLAPI *dbupdatetext_t) (PDBPROCESS, LPCSTR, LPCDBBINARY, LPCDBBINARY, INT, DBINT, DBINT, LPCSTR, DBINT, LPCDBBINARY);

// API declarations
class ssAPI : public saAPI
{
public:
	ssAPI();

	dberrhandle_t		dberrhandle;
	dbmsghandle_t		dbmsghandle;
	dbprocerrhandle_t	dbprocerrhandle;
	dbprocmsghandle_t	dbprocmsghandle;

	// Two-phase commit functions
	abort_xact_t		abort_xact;
	build_xact_string_t	build_xact_string;
	close_commit_t		close_commit;
	commit_xact_t		commit_xact;
	open_commit_t		open_commit;
	remove_xact_t		remove_xact;
	scan_xact_t			scan_xact;
	start_xact_t		start_xact;
	stat_xact_t			stat_xact;

	// BCP functions
	bcp_batch_t		bcp_batch;
	bcp_bind_t		bcp_bind;
	bcp_colfmt_t	bcp_colfmt;
	bcp_collen_t	bcp_collen;
	bcp_colptr_t	bcp_colptr;
	bcp_columns_t	bcp_columns;
	bcp_control_t	bcp_control;
	bcp_done_t		bcp_done;
	bcp_exec_t		bcp_exec;
	bcp_init_t		bcp_init;
	bcp_moretext_t	bcp_moretext;
	bcp_readfmt_t	bcp_readfmt;
	bcp_sendrow_t	bcp_sendrow;
	bcp_setl_t		bcp_setl;
	bcp_writefmt_t	bcp_writefmt;

	// Standard DB-Library functions
	dbadata_t	dbadata;
	dbadlen_t	dbadlen;
	dbaltbind_t	dbaltbind;
	dbaltcolid_t	dbaltcolid;
	dbaltlen_t	dbaltlen;
	dbaltop_t	dbaltop;
	dbalttype_t	dbalttype;
	dbaltutype_t	dbaltutype;
	dbanullbind_t	dbanullbind;
	dbbind_t	dbbind;
	dbbylist_t	dbbylist;
	dbcancel_t	dbcancel;
	dbcanquery_t	dbcanquery;
	dbchange_t	dbchange;
	dbclose_t	dbclose;
	dbclrbuf_t	dbclrbuf;
	dbclropt_t	dbclropt;
	dbcmd_t	dbcmd;
	dbcmdrow_t	dbcmdrow;
	dbcolbrowse_t	dbcolbrowse;
	dbcolinfo_t	dbcolinfo;
	dbcollen_t	dbcollen;
	dbcolname_t	dbcolname;
	dbcolsource_t	dbcolsource;
	dbcoltype_t	dbcoltype;
	dbcolutype_t	dbcolutype;
	dbconvert_t	dbconvert;
	dbcount_t	dbcount;
	dbcurcmd_t	dbcurcmd;
	dbcurrow_t	dbcurrow;
	dbcursor_t	dbcursor;
	dbcursorbind_t	dbcursorbind;
	dbcursorclose_t	dbcursorclose;
	dbcursorcolinfo_t	dbcursorcolinfo;
	dbcursorfetch_t	dbcursorfetch;
	dbcursorfetchex_t	dbcursorfetchex;
	dbcursorinfo_t	dbcursorinfo;
	dbcursorinfoex_t	dbcursorinfoex;
	dbcursoropen_t	dbcursoropen;
	dbdata_t	dbdata;
	dbdataready_t	dbdataready;
	dbdatecrack_t	dbdatecrack;
	dbdatlen_t	dbdatlen;
	dbdead_t	dbdead;
	dbexit_t	dbexit;
	dbenlisttrans_t	dbenlisttrans;
	dbenlistxatrans_t	dbenlistxatrans;
	dbfcmd_t	dbfcmd;
	dbfirstrow_t	dbfirstrow;
	dbfreebuf_t	dbfreebuf;
	dbfreelogin_t	dbfreelogin;
	dbfreequal_t	dbfreequal;
	dbgetchar_t	dbgetchar;
	dbgetmaxprocs_t	dbgetmaxprocs;
	dbgetoff_t	dbgetoff;
	dbgetpacket_t	dbgetpacket;
	dbgetrow_t	dbgetrow;
	dbgettime_t	dbgettime;
	dbgetuserdata_t	dbgetuserdata;
	dbhasretstat_t	dbhasretstat;
	dbinit_t	dbinit;
	dbisavail_t	dbisavail;
	dbiscount_t	dbiscount;
	dbisopt_t	dbisopt;
	dblastrow_t	dblastrow;
	dblogin_t	dblogin;
	dbmorecmds_t	dbmorecmds;
	dbmoretext_t	dbmoretext;
	dbname_t	dbname;
	dbnextrow_t	dbnextrow;
	dbnullbind_t	dbnullbind;
	dbnumalts_t	dbnumalts;
	dbnumcols_t	dbnumcols;
	dbnumcompute_t	dbnumcompute;
	dbnumorders_t	dbnumorders;
	dbnumrets_t	dbnumrets;
	dbopen_t	dbopen;
	dbordercol_t	dbordercol;
	dbprocinfo_t	dbprocinfo;
	dbprhead_t	dbprhead;
	dbprrow_t	dbprrow;
	dbprtype_t	dbprtype;
	dbqual_t	dbqual;
	dbreadpage_t	dbreadpage;
	dbreadtext_t	dbreadtext;
	dbresults_t	dbresults;
	dbretdata_t	dbretdata;
	dbretlen_t	dbretlen;
	dbretname_t	dbretname;
	dbretstatus_t	dbretstatus;
	dbrettype_t	dbrettype;
	dbrows_t	dbrows;
	dbrowtype_t	dbrowtype;
	dbrpcinit_t	dbrpcinit;
	dbrpcparam_t	dbrpcparam;
	dbrpcsend_t	dbrpcsend;
	dbrpcexec_t	dbrpcexec;
	dbrpwclr_t	dbrpwclr;
	dbrpwset_t	dbrpwset;
	dbserverenum_t	dbserverenum;
	dbsetavail_t	dbsetavail;
	dbsetmaxprocs_t	dbsetmaxprocs;
	dbsetlname_t	dbsetlname;
	dbsetlogintime_t	dbsetlogintime;
	dbsetlpacket_t	dbsetlpacket;
	dbsetnull_t	dbsetnull;
	dbsetopt_t	dbsetopt;
	dbsettime_t	dbsettime;
	dbsetuserdata_t	dbsetuserdata;
	dbsqlexec_t	dbsqlexec;
	dbsqlok_t	dbsqlok;
	dbsqlsend_t	dbsqlsend;
	dbstrcpy_t	dbstrcpy;
	dbstrlen_t	dbstrlen;
	dbtabbrowse_t	dbtabbrowse;
	dbtabcount_t	dbtabcount;
	dbtabname_t	dbtabname;
	dbtabsource_t	dbtabsource;
	dbtsnewlen_t	dbtsnewlen;
	dbtsnewval_t	dbtsnewval;
	dbtsput_t	dbtsput;
	dbtxptr_t	dbtxptr;
	dbtxtimestamp_t	dbtxtimestamp;
	dbtxtsnewval_t	dbtxtsnewval;
	dbtxtsput_t	dbtxtsput;
	dbuse_t	dbuse;
	dbvarylen_t	dbvarylen;
	dbwillconvert_t	dbwillconvert;
	dbwritepage_t	dbwritepage;
	dbwritetext_t	dbwritetext;
	dbupdatetext_t	dbupdatetext;
};

class SQLAPI_API ssConnectionHandles : public saConnectionHandles
{
public:
	ssConnectionHandles();

	PDBPROCESS	m_dbproc;
};

class SQLAPI_API ssCommandHandles : public saCommandHandles
{
public:
	ssCommandHandles();
};

extern ssAPI g_ssAPI;

#endif // !defined(__SS6API_H__)
