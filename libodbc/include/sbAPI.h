// sbAPI.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__SB7API_H__)
#define __SB7API_H__

#include "SQLAPI.h"

// API header(s)
#define SQL_32BITTARG 1
#include <sql.h>

extern long g_nSBDLLVersionLoaded;

extern void AddSB6Support(const SAConnection * pCon);
extern void ReleaseSB6Support();
extern bool CanBeLoadedSB7(const SAConnection * pCon);
extern void AddSB7Support(const SAConnection * pCon);
extern void ReleaseSB7Support();

typedef byte2 (SBSTDCALL *sqlarf_t)(SQLTCUR	   cur	   , SQLTFNP	 fnp	 ,
			   SQLTFNL	   fnl	   , SQLTCHO	 cho	 );
typedef byte2 (SBSTDCALL *sqlbbr_t)(SQLTCUR	   cur	   , SQLTXER PTR errnum	 ,
			   SQLTDAP	   errbuf  , SQLTDAL PTR buflen  ,
			   SQLTBIR PTR errrow  , SQLTRBF PTR rbf	 ,
			   SQLTBIR	   errseq  );
typedef byte2 (SBSTDCALL *sqlbdb_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
			   SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
			   SQLTBOO	   over    );
typedef byte2 (SBSTDCALL *sqlbef_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlber_t)(SQLTCUR	   cur	   , SQLTRCD PTR rcd	 ,
			   SQLTBIR PTR errrow  , SQLTRBF PTR rbf	 ,
			   SQLTBIR	   errseq  );
typedef byte2 (SBSTDCALL *sqlbkp_t)(SQLTCUR	   cur	   , SQLTBOO	 defalt  ,
			   SQLTBOO	   overwrt , SQLTFNP	 bkfname ,
			   SQLTFNL	   bkfnlen );
typedef byte2 (SBSTDCALL *sqlbld_t)(SQLTCUR	   cur	   , SQLTBNP	 bnp	 ,
			   SQLTBNL	   bnl	   );
typedef byte2 (SBSTDCALL *sqlblf_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
			   SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
			   SQLTBOO	   over    );
typedef byte2 (SBSTDCALL *sqlblk_t)(SQLTCUR	   cur	   , SQLTFLG	 blkflg  );
typedef byte2 (SBSTDCALL *sqlbln_t)(SQLTCUR	   cur	   , SQLTBNN	 bnn	 );
typedef byte2 (SBSTDCALL *sqlbna_t)(SQLTCUR	   cur	   , SQLTBNP	 bnp	 ,
			   SQLTBNL	   bnl	   , SQLTDAP	 dap	 ,
			   SQLTDAL	   dal	   , SQLTSCA	 sca	 ,
			   SQLTPDT	   pdt	   , SQLTNUL	 nli	 );
typedef byte2 (SBSTDCALL *sqlbnd_t)(SQLTCUR	   cur	   , SQLTBNP	 bnp	 ,
			   SQLTBNL	   bnl	   , SQLTDAP	 dap	 ,
			   SQLTDAL	   dal	   , SQLTSCA	 sca	 ,
			   SQLTPDT	   pdt	   );
typedef byte2 (SBSTDCALL *sqlbnn_t)(SQLTCUR	   cur	   , SQLTBNN	 bnn	 ,
			   SQLTDAP	   dap	   , SQLTDAL	 dal	 ,
			   SQLTSCA	   sca	   , SQLTPDT	 pdt	 );
typedef byte2 (SBSTDCALL *sqlbnu_t)(SQLTCUR	   cur	   , SQLTBNN	 bnn	 ,
			   SQLTDAP	   dap	   , SQLTDAL	 dal	 ,
			   SQLTSCA	   sca	   , SQLTPDT	 pdt	 ,
			   SQLTNUL	   nli	   );
typedef byte2 (SBSTDCALL *sqlbss_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
			   SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
			   SQLTBOO	   over    );
typedef byte2 (SBSTDCALL *sqlcan_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlcbv_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlcdr_t)(SQLTSVH	   shandle,  SQLTCUR	 cur	 );
typedef byte2 (SBSTDCALL *sqlcex_t)(SQLTCUR	   cur	   , SQLTDAP	 dap	 ,
			   SQLTDAL	   dal	   );
typedef byte2 (SBSTDCALL *sqlclf_t)(SQLTSVH	   cur	   , SQLTDAP	 logfile ,
			   SQLTFMD	   startflag);
typedef byte2 (SBSTDCALL *sqlcmt_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlcnc_t)(SQLTCUR PTR curp    , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  );
typedef byte2 (SBSTDCALL *sqlcnr_t)(SQLTCUR PTR curp    , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  );
typedef byte2 (SBSTDCALL *sqlcom_t)(SQLTCUR	   cur	   , SQLTDAP	 cmdp	 ,
			   SQLTDAL	   cmdl    );
typedef byte2 (SBSTDCALL *sqlcon_t)(SQLTCUR PTR curp    , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  , SQLTWSI	 cursiz  ,
			   SQLTNPG	   pages   , SQLTRCF	 recovr  ,
			   SQLTDAL	   outsize , SQLTDAL	 insize  );
typedef byte2 (SBSTDCALL *sqlcpy_t)(SQLTCUR	   fcur    , SQLTDAP	 selp	 ,
			   SQLTDAL	   sell    , SQLTCUR	 tcur	 ,
			   SQLTDAP	   isrtp   , SQLTDAL	 isrtl	 );
typedef byte2 (SBSTDCALL *sqlcre_t)(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  );
typedef byte2 (SBSTDCALL *sqlcrf_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel );
typedef byte2 (SBSTDCALL *sqlcrs_t)(SQLTCUR	   cur	   , SQLTDAP	 rsp	 ,
			   SQLTDAL	   rsl	   );
typedef byte2 (SBSTDCALL *sqlcsv_t)(SQLTSVH PTR shandlep, SQLTDAP	 serverid,
			   SQLTDAP	   password);
typedef byte2 (SBSTDCALL *sqlcty_t)(SQLTCUR	   cur	   , SQLTCTY PTR cty	 );
typedef byte2 (SBSTDCALL *sqldbn_t)(SQLTDAP	   serverid, SQLTDAP	 buffer  ,
			   SQLTDAL	   length  );
typedef byte2 (SBSTDCALL *sqlded_t)(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  );
typedef byte2 (SBSTDCALL *sqldel_t)(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  );
typedef byte2 (SBSTDCALL *sqldes_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTDDT PTR ddt	   , SQLTDDL PTR ddl	 ,
			   SQLTCHP	   chp	   , SQLTCHL PTR chlp	 ,
			   SQLTPRE PTR prep    , SQLTSCA PTR scap	 );
typedef byte2 (SBSTDCALL *sqldid_t)(SQLTDAP	   dbname  , SQLTDAL	 dbnamel );
typedef byte2 (SBSTDCALL *sqldii_t)(SQLTCUR	   cur	   , SQLTSLC	 ivn	 ,
			   SQLTDAP	   inp	   , SQLTCHL*	 inlp	 );
typedef byte2 (SBSTDCALL *sqldin_t)(SQLTDAP	   dbnamp  , SQLTDAL	 dbnaml  );
typedef byte2 (SBSTDCALL *sqldir_t)(SQLTSVN	   srvno   , SQLTDAP	 buffer  ,
			   SQLTDAL	   length  );
typedef byte2 (SBSTDCALL *sqldis_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqldon_t)(void);
typedef byte2 (SBSTDCALL *sqldox_t)(SQLTSVH	   shandle , SQLTDAP	 dirnamep,
			   SQLTFAT	   fattr   );
typedef byte2 (SBSTDCALL *sqldrc_t)(SQLTSVH	   cur	   );
typedef byte2 (SBSTDCALL *sqldro_t)(SQLTSVH	   shandle , SQLTDAP	 dirname );
typedef byte2 (SBSTDCALL *sqldrr_t)(SQLTSVH	   shandle , SQLTDAP	 filename);
typedef byte2 (SBSTDCALL *sqldrs_t)(SQLTCUR	   cur	   , SQLTDAP	 rsp	 ,
			   SQLTDAL	   rsl	   );
typedef byte2 (SBSTDCALL *sqldsc_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTDDT PTR edt	   , SQLTDDL PTR edl	 ,
			   SQLTCHP	   chp	   , SQLTCHL PTR chlp	 ,
			   SQLTPRE PTR prep    , SQLTSCA PTR scap	 );
typedef byte2 (SBSTDCALL *sqldst_t)(SQLTCUR	   cur	   , SQLTDAP	 cnp	 ,
			   SQLTDAL	   cnl	   );
typedef byte2 (SBSTDCALL *sqldsv_t)(SQLTSVH	   shandle );
typedef byte2 (SBSTDCALL *sqlebk_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlefb_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlelo_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlenr_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel );
typedef byte2 (SBSTDCALL *sqlepo_t)(SQLTCUR	   cur	   , SQLTEPO PTR epo	 );
typedef byte2 (SBSTDCALL *sqlerf_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlerr_t)(SQLTRCD	   error   , SQLTDAP	 msg	 );
typedef byte2 (SBSTDCALL *sqlers_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqletx_t)(SQLTRCD	   error   , SQLTPTY	 msgtyp  ,
			   SQLTDAP	   bfp	   , SQLTDAL	 bfl	 ,
			   SQLTDAL PTR txtlen  );
typedef byte2 (SBSTDCALL *sqlexe_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlexp_t)(SQLTCUR	   cur	   , SQLTDAP	 buffer  ,
			   SQLTDAL	   length  );
typedef byte2 (SBSTDCALL *sqlfbk_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlfer_t)(SQLTRCD	   error   , SQLTDAP	 msg	 );
typedef byte2 (SBSTDCALL *sqlfet_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlfgt_t)(SQLTSVH	   cur	   , SQLTDAP	 srvfile ,
			   SQLTDAP	   lclfile );
typedef byte2 (SBSTDCALL *sqlfpt_t)(SQLTSVH	   cur	   , SQLTDAP	 srvfile ,
			   SQLTDAP	   lclfile );
typedef byte2 (SBSTDCALL *sqlfqn_t)(SQLTCUR	   cur	   , SQLTFLD	 field	 ,
			   SQLTDAP	   nameptr , SQLTDAL PTR namelen );
typedef byte2 (SBSTDCALL *sqlgbi_t)(SQLTCUR	   cur	   , SQLTCUR PTR pcur	 ,
			   SQLTPNM PTR ppnm  );
typedef byte2 (SBSTDCALL *sqlgdi_t)(SQLTCUR	   cur	   , SQLTPGD	 gdi	 );
typedef byte2 (SBSTDCALL *sqlget_t)(SQLTCUR	   cur	   , SQLTPTY	 parm	 ,
			   SQLTDAP	   p	   , SQLTDAL PTR l	 );
typedef byte2 (SBSTDCALL *sqlgfi_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTCDL PTR cvl	   , SQLTFSC PTR fsc	 );
typedef byte2 (SBSTDCALL *sqlgls_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTLSI PTR size    );
typedef byte2 (SBSTDCALL *sqlgnl_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTLNG PTR lognum  );
typedef byte2 (SBSTDCALL *sqlgnr_t)(SQLTCUR	   cur	   , SQLTDAP	 tbnam	 ,
			   SQLTDAL	   tbnaml  , SQLTROW PTR rows	 );
typedef byte2 (SBSTDCALL *sqlgsi_t)(SQLTSVH	   shandle , SQLTFLG	 infoflags,
			   SQLTDAP	   buffer  , SQLTDAL	 buflen  ,
			   SQLTDAL PTR rbuflen );
typedef byte2 (SBSTDCALL *sqlidb_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlims_t)(SQLTCUR	   cur	   , SQLTDAL	 insize  );
typedef byte2 (SBSTDCALL *sqlind_t)(SQLTSVH	   shandle , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  );
typedef byte2 (SBSTDCALL *sqlini_t)(SQLTPFP	   callback);
typedef byte2 (SBSTDCALL *sqlins_t)(SQLTSVN	   srvno   , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  , SQLTFLG	 createflag,
			   SQLTFLG	   overwrite);
typedef byte2 (SBSTDCALL *sqllab_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTCHP	   lbp	   , SQLTCHL PTR lblp	 );
typedef byte2 (SBSTDCALL *sqlldp_t)(SQLTCUR	   cur	   , SQLTDAP	 cmdp	 ,
			   SQLTDAL	   cmdl    );
typedef byte2 (SBSTDCALL *sqllsk_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTLSI	   pos	   );
typedef byte2 (SBSTDCALL *sqlmcl_t)(SQLTSVH	   shandle , SQLTFLH	 fd	 );
typedef byte2 (SBSTDCALL *sqlmdl_t)(SQLTSVH	   shandle , SQLTDAP	 filename);
typedef byte2 (SBSTDCALL *sqlmop_t)(SQLTSVH	   shandle , SQLTFLH PTR fdp	 ,
			   SQLTDAP	   filename, SQLTFMD	 openmode);
typedef byte2 (SBSTDCALL *sqlmrd_t)(SQLTSVH	   shandle , SQLTFLH	 fd	 ,
			   SQLTDAP	   buffer  , SQLTDAL	 len	 ,
			   SQLTDAL PTR rlen    );
typedef byte2 (SBSTDCALL *sqlmsk_t)(SQLTSVH	   shandle , SQLTFLH	 fd	 ,
			   SQLTLNG	   offset  , SQLTWNC	 whence  ,
			   SQLTLNG PTR roffset );
typedef byte2 (SBSTDCALL *sqlmwr_t)(SQLTSVH	   shandle , SQLTFLH	 fd	 ,
			   SQLTDAP	   buffer  , SQLTDAL	 len	 ,
			   SQLTDAL PTR rlen    );
typedef byte2 (SBSTDCALL *sqlnbv_t)(SQLTCUR	   cur	   , SQLTNBV PTR nbv	 );
typedef byte2 (SBSTDCALL *sqlnii_t)(SQLTCUR	   cur	   , SQLTNSI PTR nii	 );
typedef byte2 (SBSTDCALL *sqlnrr_t)(SQLTCUR	   cur	   , SQLTROW PTR rcountp );
typedef byte2 (SBSTDCALL *sqlnsi_t)(SQLTCUR	   cur	   , SQLTNSI PTR nsi	 );
typedef byte2 (SBSTDCALL *sqloms_t)(SQLTCUR	   cur	   , SQLTDAL	 outsize );
typedef byte2 (SBSTDCALL *sqlprs_t)(SQLTCUR	   cur	   , SQLTROW	 row	 );
typedef byte2 (SBSTDCALL *sqlrbf_t)(SQLTCUR	   cur	   , SQLTRBF PTR rbf	 );
typedef byte2 (SBSTDCALL *sqlrbk_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlrcd_t)(SQLTCUR	   cur	   , SQLTRCD PTR rcd	 );
typedef byte2 (SBSTDCALL *sqlrdb_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
			   SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
			   SQLTBOO	   over    );
typedef byte2 (SBSTDCALL *sqlrdc_t)(SQLTCUR	   cur	   , SQLTDAP	 bufp	 ,
			   SQLTDAL	   bufl    , SQLTDAL PTR readl	 );
typedef byte2 (SBSTDCALL *sqlrel_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlres_t)(SQLTCUR PTR curptr  , SQLTFNP	 bkfname ,
			   SQLTFNL	   bkfnlen , SQLTSVN	 bkfserv ,
			   SQLTBOO	   overwrt , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnlen  , SQLTSVN	 dbserv  );
typedef byte2 (SBSTDCALL *sqlret_t)(SQLTCUR	   cur	   , SQLTDAP	 cnp	 ,
			   SQLTDAL	   cnl	   );
typedef byte2 (SBSTDCALL *sqlrlf_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
			   SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
			   SQLTBOO	   over    );
typedef byte2 (SBSTDCALL *sqlrlo_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTDAP	   bufp    , SQLTDAL	 bufl	 ,
			   SQLTDAL PTR readl   );
typedef byte2 (SBSTDCALL *sqlrof_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTRFM	 mode	 ,
			   SQLTDAP	   datetime, SQLTDAL	 datetimel);
typedef byte2 (SBSTDCALL *sqlrow_t)(SQLTCUR	   cur	   , SQLTROW PTR row	 );
typedef byte2 (SBSTDCALL *sqlrrd_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlrrs_t)(SQLTCUR	   cur	   , SQLTDAP	 rsp	 ,
			   SQLTDAL	   rsl	   );
typedef byte2 (SBSTDCALL *sqlrsi_t)(SQLTSVH	   shandle );
typedef byte2 (SBSTDCALL *sqlrss_t)(SQLTSVH	   shandle , SQLTDAP	 dbname  ,
			   SQLTDAL	   dbnamel , SQLTFNP	 bkpdir  ,
			   SQLTFNL	   bkpdirl , SQLTBOO	 local	 ,
			   SQLTBOO	   over    );
typedef byte2 (SBSTDCALL *sqlsab_t)(SQLTSVH	   shandle , SQLTPNM	 pnum	 );
typedef byte2 (SBSTDCALL *sqlsap_t)(SQLTSVN	   srvno   , SQLTDAP	 password,
			   SQLTPNM	   pnum    );
typedef byte2 (SBSTDCALL *sqlscl_t)(SQLTCUR	   cur	   , SQLTDAP	 namp	 ,
			   SQLTDAL	   naml    );
typedef byte2 (SBSTDCALL *sqlscn_t)(SQLTCUR	   cur	   , SQLTDAP	 namp	 ,
			   SQLTDAL	   naml    );
typedef byte2 (SBSTDCALL *sqlscp_t)(SQLTNPG	   pages   );
typedef byte2 (SBSTDCALL *sqlsdn_t)(SQLTDAP	   dbnamp  , SQLTDAL	 dbnaml  );
typedef byte2 (SBSTDCALL *sqlsds_t)(SQLTSVH	   shandle,  SQLTFLG shutdownflg);
typedef byte2 (SBSTDCALL *sqlsdx_t)(SQLTSVH	   shandle,  SQLTDAP	 dbnamp,
			   SQLTDAL	   dbnaml  , SQLTFLG	 shutdownflg);
typedef byte2 (SBSTDCALL *sqlset_t)(SQLTCUR	   cur	   , SQLTPTY	 parm	 ,
			   SQLTDAP	   p	   , SQLTDAL	 l	 );
typedef byte2 (SBSTDCALL *sqlsil_t)(SQLTCUR	   cur	   , SQLTILV	 isolation);
typedef byte2 (SBSTDCALL *sqlslp_t)(SQLTCUR	   cur	   , SQLTNPG	 lpt	 ,
			   SQLTNPG	   lpm	   );
typedef byte2 (SBSTDCALL *sqlspr_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlsrf_t)(SQLTCUR	   cur	   , SQLTDAP	 fnp	 ,
			   SQLTDAL	   fnl	   );
typedef byte2 (SBSTDCALL *sqlsrs_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlssb_t)(SQLTCUR	   cur	   , SQLTSLC	 slc	 ,
			   SQLTPDT	   pdt	   , SQLTDAP	 pbp	 ,
			   SQLTPDL	   pdl	   , SQLTSCA	 sca	 ,
			   SQLTCDL PTR pcv	   , SQLTFSC PTR pfc	 );
typedef byte2 (SBSTDCALL *sqlsss_t)(SQLTCUR	   cur	   , SQLTDAL	 size	 );
typedef byte2 (SBSTDCALL *sqlsta_t)(SQLTCUR	   cur	   , SQLTSTC PTR svr	 ,
			   SQLTSTC PTR svw	   , SQLTSTC PTR spr	 ,
			   SQLTSTC PTR spw	   );
typedef byte2 (SBSTDCALL *sqlstm_t)(SQLTSVH	   shandle );
typedef byte2 (SBSTDCALL *sqlsto_t)(SQLTCUR	   cur	   , SQLTDAP	 cnp	 ,
			   SQLTDAL	   cnl	   , SQLTDAP	 ctp	 ,
			   SQLTDAL	   ctl	   );
typedef byte2 (SBSTDCALL *sqlstr_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlsxt_t)(SQLTSVN	   srvno   , SQLTDAP	 password);
typedef byte2 (SBSTDCALL *sqlsys_t)(SQLTCUR	   cur	   , SQLTSYS PTR sys	 );
typedef byte2 (SBSTDCALL *sqltec_t)(SQLTRCD	   rcd	   , SQLTRCD PTR np	 );
typedef byte2 (SBSTDCALL *sqltem_t)(SQLTCUR	   cur	   , SQLTXER PTR xer	 ,
			   SQLTPTY	   msgtyp  , SQLTDAP	 bfp	 ,
			   SQLTDAL	   bfl	   , SQLTDAL PTR txtlen  );
typedef byte2 (SBSTDCALL *sqltio_t)(SQLTCUR	   cur	   , SQLTTIV	 _timeout);
typedef byte2 (SBSTDCALL *sqlunl_t)(SQLTCUR	   cur	   , SQLTDAP	 cmdp	 ,
			   SQLTDAL	   cmdl    );
typedef byte2 (SBSTDCALL *sqlurs_t)(SQLTCUR	   cur	   );
typedef byte2 (SBSTDCALL *sqlwdc_t)(SQLTCUR	   cur	   , SQLTDAP	 bufp	 ,
			   SQLTDAL	   bufl    );
typedef byte2 (SBSTDCALL *sqlwlo_t)(SQLTCUR	   cur	   , SQLTDAP	 bufp	 ,
			   SQLTDAL	   bufl    );
typedef byte2 (SBSTDCALL *sqlxad_t)(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
			   SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
			   SQLTNML	   nl2	   );
typedef byte2 (SBSTDCALL *sqlxcn_t)(SQLTNMP	   op	   , SQLTDAP	 ip	 ,
			   SQLTDAL	   il	   );
typedef byte2 (SBSTDCALL *sqlxda_t)(SQLTNMP	   op	   , SQLTNMP	 dp	 ,
			   SQLTNML	   dl	   , SQLTDAY	 days	 );
typedef byte2 (SBSTDCALL *sqlxdp_t)(SQLTDAP	   op	   , SQLTDAL	 ol	 ,
			   SQLTNMP	   ip	   , SQLTNML	 il	 ,
			   SQLTDAP	   pp	   , SQLTDAL	 pl	 );
typedef byte2 (SBSTDCALL *sqlxdv_t)(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
			   SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
			   SQLTNML	   nl2	   );
typedef byte2 (SBSTDCALL *sqlxer_t)(SQLTCUR	   cur	   , SQLTXER PTR errnum,
			   SQLTDAP	   errbuf  , SQLTDAL PTR buflen  );
typedef byte2 (SBSTDCALL *sqlxml_t)(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
			   SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
			   SQLTNML	   nl2	   );
typedef byte2 (SBSTDCALL *sqlxnp_t)(SQLTDAP	   outp    , SQLTDAL	 outl	 ,
			   SQLTNMP	   isnp    , SQLTNML	 isnl	 ,
			   SQLTDAP	   picp    , SQLTDAL	 picl	 );
typedef byte2 (SBSTDCALL *sqlxpd_t)(SQLTNMP	   op	   , SQLTNML PTR olp	 ,
			   SQLTDAP	   ip	   , SQLTDAP	 pp	 ,
			   SQLTDAL	   pl	   );
typedef byte2 (SBSTDCALL *sqlxsb_t)(SQLTNMP	   op	   , SQLTNMP	 np1	 ,
			   SQLTNML	   nl1	   , SQLTNMP	 np2	 ,
			   SQLTNML	   nl2	   );

// version 7 specific
typedef byte2 (SBSTDCALL *sqlcch_t)(SQLTCON PTR hConp   , SQLTDAP	 dbnamp  ,
			   SQLTDAL	   dbnaml  , SQLTMOD	 fType	 );
typedef byte2 (SBSTDCALL *sqldch_t)(SQLTCON	   hCon    );
typedef byte2 (SBSTDCALL *sqlopc_t)(SQLTCUR PTR curp    , SQLTCON	 hCon	 ,
			   SQLTMOD	   fType   );

// API declarations
class SQLAPI_API sb6API : public saAPI
{
public:
	sb6API();

	sqlarf_t	sqlarf;
	sqlbbr_t	sqlbbr;
	sqlbdb_t	sqlbdb;
	sqlbef_t	sqlbef;
	sqlber_t	sqlber;
	sqlbkp_t	sqlbkp;
	sqlbld_t	sqlbld;
	sqlblf_t	sqlblf;
	sqlblk_t	sqlblk;
	sqlbln_t	sqlbln;
	sqlbna_t	sqlbna;
	sqlbnd_t	sqlbnd;
	sqlbnn_t	sqlbnn;
	sqlbnu_t	sqlbnu;
	sqlbss_t	sqlbss;
	sqlcan_t	sqlcan;
	sqlcbv_t	sqlcbv;
	sqlcdr_t	sqlcdr;
	sqlcex_t	sqlcex;
	sqlclf_t	sqlclf;
	sqlcmt_t	sqlcmt;
	sqlcnc_t	sqlcnc;
	sqlcnr_t	sqlcnr;
	sqlcom_t	sqlcom;
	sqlcon_t	sqlcon;
	sqlcpy_t	sqlcpy;
	sqlcre_t	sqlcre;
	sqlcrf_t	sqlcrf;
	sqlcrs_t	sqlcrs;
	sqlcsv_t	sqlcsv;
	sqlcty_t	sqlcty;
	sqldbn_t	sqldbn;
	sqlded_t	sqlded;
	sqldel_t	sqldel;
	sqldes_t	sqldes;
	sqldid_t	sqldid;
	sqldii_t	sqldii;
	sqldin_t	sqldin;
	sqldir_t	sqldir;
	sqldis_t	sqldis;
	sqldon_t	sqldon;
	sqldox_t	sqldox;
	sqldrc_t	sqldrc;
	sqldro_t	sqldro;
	sqldrr_t	sqldrr;
	sqldrs_t	sqldrs;
	sqldsc_t	sqldsc;
	sqldst_t	sqldst;
	sqldsv_t	sqldsv;
	sqlebk_t	sqlebk;
	sqlefb_t	sqlefb;
	sqlelo_t	sqlelo;
	sqlenr_t	sqlenr;
	sqlepo_t	sqlepo;
	sqlerf_t	sqlerf;
	sqlerr_t	sqlerr;
	sqlers_t	sqlers;
	sqletx_t	sqletx;
	sqlexe_t	sqlexe;
	sqlexp_t	sqlexp;
	sqlfbk_t	sqlfbk;
	sqlfer_t	sqlfer;
	sqlfet_t	sqlfet;
	sqlfgt_t	sqlfgt;
	sqlfpt_t	sqlfpt;
	sqlfqn_t	sqlfqn;
	sqlgbi_t	sqlgbi;
	sqlgdi_t	sqlgdi;
	sqlget_t	sqlget;
	sqlgfi_t	sqlgfi;
	sqlgls_t	sqlgls;
	sqlgnl_t	sqlgnl;
	sqlgnr_t	sqlgnr;
	sqlgsi_t	sqlgsi;
	sqlidb_t	sqlidb;
	sqlims_t	sqlims;
	sqlind_t	sqlind;
	sqlini_t	sqlini;
	sqlins_t	sqlins;
	sqllab_t	sqllab;
	sqlldp_t	sqlldp;
	sqllsk_t	sqllsk;
	sqlmcl_t	sqlmcl;
	sqlmdl_t	sqlmdl;
	sqlmop_t	sqlmop;
	sqlmrd_t	sqlmrd;
	sqlmsk_t	sqlmsk;
	sqlmwr_t	sqlmwr;
	sqlnbv_t	sqlnbv;
	sqlnii_t	sqlnii;
	sqlnrr_t	sqlnrr;
	sqlnsi_t	sqlnsi;
	sqloms_t	sqloms;
	sqlprs_t	sqlprs;
	sqlrbf_t	sqlrbf;
	sqlrbk_t	sqlrbk;
	sqlrcd_t	sqlrcd;
	sqlrdb_t	sqlrdb;
	sqlrdc_t	sqlrdc;
	sqlrel_t	sqlrel;
	sqlres_t	sqlres;
	sqlret_t	sqlret;
	sqlrlf_t	sqlrlf;
	sqlrlo_t	sqlrlo;
	sqlrof_t	sqlrof;
	sqlrow_t	sqlrow;
	sqlrrd_t	sqlrrd;
	sqlrrs_t	sqlrrs;
	sqlrsi_t	sqlrsi;
	sqlrss_t	sqlrss;
	sqlsab_t	sqlsab;
	sqlsap_t	sqlsap;
	sqlscl_t	sqlscl;
	sqlscn_t	sqlscn;
	sqlscp_t	sqlscp;
	sqlsdn_t	sqlsdn;
	sqlsds_t	sqlsds;
	sqlsdx_t	sqlsdx;
	sqlset_t	sqlset;
	sqlsil_t	sqlsil;
	sqlslp_t	sqlslp;
	sqlspr_t	sqlspr;
	sqlsrf_t	sqlsrf;
	sqlsrs_t	sqlsrs;
	sqlssb_t	sqlssb;
	sqlsss_t	sqlsss;
	sqlsta_t	sqlsta;
	sqlstm_t	sqlstm;
	sqlsto_t	sqlsto;
	sqlstr_t	sqlstr;
	sqlsxt_t	sqlsxt;
	sqlsys_t	sqlsys;
	sqltec_t	sqltec;
	sqltem_t	sqltem;
	sqltio_t	sqltio;
	sqlunl_t	sqlunl;
	sqlurs_t	sqlurs;
	sqlwdc_t	sqlwdc;
	sqlwlo_t	sqlwlo;
	sqlxad_t	sqlxad;
	sqlxcn_t	sqlxcn;
	sqlxda_t	sqlxda;
	sqlxdp_t	sqlxdp;
	sqlxdv_t	sqlxdv;
	sqlxer_t	sqlxer;
	sqlxml_t	sqlxml;
	sqlxnp_t	sqlxnp;
	sqlxpd_t	sqlxpd;
	sqlxsb_t	sqlxsb;
};

// version 7 specific
class SQLAPI_API sb7API : public sb6API
{
public:
	sb7API();

	sqlcch_t	sqlcch;
	sqldch_t	sqldch;
	sqlopc_t	sqlopc;
};

class SQLAPI_API sb6ConnectionHandles : public saConnectionHandles
{
public:
	sb6ConnectionHandles();

	SQLTCUR	m_cur;	// SQLBase cursor number
};

class SQLAPI_API sb7ConnectionHandles : public sb6ConnectionHandles
{
public:
	sb7ConnectionHandles();

	SQLTCON	m_hCon;
};

class SQLAPI_API sbCommandHandles : public saCommandHandles
{
public:
	sbCommandHandles();

	SQLTCUR m_cur;
};

extern sb7API g_sb7API;
extern sb6API &g_sb6API;

#endif // !defined(__SB7API_H__)
