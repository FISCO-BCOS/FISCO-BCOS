// ora7API.h
//
//////////////////////////////////////////////////////////////////////

#if !defined(__ORA7API_H__)
#define __ORA7API_H__

#include "SQLAPI.h"

// API header(s)
#include <ociapr.h>
#include <ocidfn.h>

extern long g_nORA7DLLVersionLoaded;

extern void AddORA7Support();
extern void ReleaseORA7Support();

/*
 * Oci BIND (Piecewise or with Skips) 
 */
typedef sword  (*obindps_t)(struct cda_def *cursor, ub1 opcode, text *sqlvar, 
	       sb4 sqlvl, ub1 *pvctx, sb4 progvl, 
	       sword ftype, sword scale,
	       sb2 *indp, ub2 *alen, ub2 *arcode, 
	       sb4 pv_skip, sb4 ind_skip, sb4 alen_skip, sb4 rc_skip,
	       ub4 maxsiz, ub4 *cursiz,
	       text *fmt, sb4 fmtl, sword fmtt);
typedef sword  (*obreak_t)(struct cda_def *lda);
typedef sword  (*ocan_t)(struct cda_def *cursor);
typedef sword  (*oclose_t)(struct cda_def *cursor);
typedef sword  (*ocof_t)(struct cda_def *lda);
typedef sword  (*ocom_t)(struct cda_def *lda);
typedef sword  (*ocon_t)(struct cda_def *lda);


/*
 * Oci DEFINe (Piecewise or with Skips) 
 */
typedef sword  (*odefinps_t)(struct cda_def *cursor, ub1 opcode, sword pos,ub1 *bufctx,
		sb4 bufl, sword ftype, sword scale, 
		sb2 *indp, text *fmt, sb4 fmtl, sword fmtt, 
		ub2 *rlen, ub2 *rcode,
		sb4 pv_skip, sb4 ind_skip, sb4 alen_skip, sb4 rc_skip);
typedef sword  (*odessp_t)(struct cda_def *cursor, text *objnam, size_t onlen,
              ub1 *rsv1, size_t rsv1ln, ub1 *rsv2, size_t rsv2ln,
              ub2 *ovrld, ub2 *pos, ub2 *level, text **argnam,
              ub2 *arnlen, ub2 *dtype, ub1 *defsup, ub1* mode,
              ub4 *dtsiz, sb2 *prec, sb2 *scale, ub1 *radix,
              ub4 *spare, ub4 *arrsiz);
typedef sword  (*odescr_t)(struct cda_def *cursor, sword pos, sb4 *dbsize,
                 sb2 *dbtype, sb1 *cbuf, sb4 *cbufl, sb4 *dsize,
                 sb2 *prec, sb2 *scale, sb2 *nullok);
typedef sword  (*oerhms_t)(struct cda_def *lda, sb2 rcode, text *buf,
                 sword bufsiz);
typedef sword  (*oermsg_t)(sb2 rcode, text *buf);
typedef sword  (*oexec_t)(struct cda_def *cursor);
typedef sword  (*oexfet_t)(struct cda_def *cursor, ub4 nrows,
                 sword cancel, sword exact);
typedef sword  (*oexn_t)(struct cda_def *cursor, sword iters, sword rowoff);
typedef sword  (*ofen_t)(struct cda_def *cursor, sword nrows);
typedef sword  (*ofetch_t)(struct cda_def *cursor);
typedef sword  (*oflng_t)(struct cda_def *cursor, sword pos, ub1 *buf,
                 sb4 bufl, sword dtype, ub4 *retl, sb4 offset);
typedef sword  (*ogetpi_t)   (struct cda_def *cursor, ub1 *piecep, dvoid **ctxpp, 
                 ub4 *iterp, ub4 *indexp);
typedef sword  (*oopt_t)     (struct cda_def *cursor, sword rbopt, sword waitopt);
typedef sword  (*opinit_t)   (ub4 mode);
typedef sword  (*olog_t)     (struct cda_def *lda, ub1* hda,
                 text *uid, sword uidl,
                 text *pswd, sword pswdl, 
                 text *conn, sword connl, 
                 ub4 mode);
typedef sword  (*ologof_t)   (struct cda_def *lda);
typedef sword  (*oopen_t)    (struct cda_def *cursor, struct cda_def *lda,
                 text *dbn, sword dbnl, sword arsize,
                 text *uid, sword uidl);
typedef sword  (*oparse_t)   (struct cda_def *cursor, text *sqlstm, sb4 sqllen,
                 sword defflg, ub4 lngflg);
typedef sword  (*orol_t)     (struct cda_def *lda);
typedef sword  (*osetpi_t)   (struct cda_def *cursor, ub1 piece, dvoid *bufp, ub4 *lenp);

typedef void (*sqlld2_t)     (struct cda_def *lda, text *cname, sb4 *cnlen);
typedef void (*sqllda_t)     (struct cda_def *lda);

/* non-blocking functions */
typedef sword (*onbset_t)    (struct cda_def *lda ); 
typedef sword (*onbtst_t)    (struct cda_def *lda ); 
typedef sword (*onbclr_t)    (struct cda_def *lda ); 
typedef sword (*ognfd_t)     (struct cda_def *lda, dvoid *fdp);

/* 
 * OBSOLETE CALLS 
 */

/* 
 * OBSOLETE BIND CALLS
 */
typedef sword  (*obndra_t)(struct cda_def *cursor, text *sqlvar, sword sqlvl,
                 ub1 *progv, sword progvl, sword ftype, sword scale,
                 sb2 *indp, ub2 *alen, ub2 *arcode, ub4 maxsiz,
                 ub4 *cursiz, text *fmt, sword fmtl, sword fmtt);
typedef sword  (*obndrn_t)(struct cda_def *cursor, sword sqlvn, ub1 *progv,
                 sword progvl, sword ftype, sword scale, sb2 *indp,
                 text *fmt, sword fmtl, sword fmtt);
typedef sword  (*obndrv_t)(struct cda_def *cursor, text *sqlvar, sword sqlvl,
                 ub1 *progv, sword progvl, sword ftype, sword scale,
                 sb2 *indp, text *fmt, sword fmtl, sword fmtt);

/*
 * OBSOLETE DEFINE CALLS
 */
typedef sword  (*odefin_t)(struct cda_def *cursor, sword pos, ub1 *buf,
	      sword bufl, sword ftype, sword scale, sb2 *indp,
	      text *fmt, sword fmtl, sword fmtt, ub2 *rlen, ub2 *rcode);

/* older calls ; preferred equivalent calls above */

typedef sword  (*oname_t)    (struct cda_def *cursor, sword pos, sb1 *tbuf,
                 sb2 *tbufl, sb1 *buf, sb2 *bufl);
typedef sword  (*orlon_t)    (struct cda_def *lda, ub1 *hda, 
                 text *uid, sword uidl, 
                 text *pswd, sword pswdl, 
                 sword audit);
typedef sword  (*olon_t)     (struct cda_def *lda, text *uid, sword uidl,
                 text *pswd, sword pswdl, sword audit);
typedef sword  (*osql3_t)    (struct cda_def *cda, text *sqlstm, sword sqllen);
typedef sword  (*odsc_t)     (struct cda_def *cursor, sword pos, sb2 *dbsize,
                 sb2 *fsize, sb2 *rcode, sb2 *dtype, sb1 *buf,
                 sb2 *bufl, sb2 *dsize);

// API declarations
class SQLAPI_API ora7API : public saAPI
{
public:
	ora7API();

	obindps_t	obindps;
	obreak_t	obreak;
	ocan_t		ocan;
	oclose_t	oclose;
	ocof_t		ocof;
	ocom_t		ocom;
	ocon_t		ocon;
	odefinps_t	odefinps;
	odessp_t	odessp;
	odescr_t	odescr;
	oerhms_t	oerhms;
	oermsg_t	oermsg;
	oexec_t		oexec;
	oexfet_t	oexfet;
	oexn_t		oexn;
	ofen_t		ofen;
	ofetch_t	ofetch;
	oflng_t		oflng;
	ogetpi_t	ogetpi;
	oopt_t		oopt;
	opinit_t	opinit;
	olog_t		olog;
	ologof_t	ologof;
	oopen_t		oopen;
	oparse_t	oparse;
	orol_t		orol;
	osetpi_t	osetpi;
	sqlld2_t	sqlld2;
	sqllda_t	sqllda;
	onbset_t	onbset;
	onbtst_t	onbtst;
	onbclr_t	onbclr;
	ognfd_t		ognfd;
	obndra_t	obndra;
	obndrn_t	obndrn;
	obndrv_t	obndrv;
	odefin_t	odefin;
	oname_t		oname;
	orlon_t		orlon;
	olon_t		olon;
	osql3_t		osql3;
	odsc_t		odsc;
};

class SQLAPI_API ora7ConnectionHandles : public saConnectionHandles
{
public:
	ora7ConnectionHandles();

	Lda_Def	m_lda;
	ub1     m_hda[512];
};

class SQLAPI_API ora7CommandHandles : public saCommandHandles
{
public:
	ora7CommandHandles();

	Cda_Def	m_cda;
};

extern ora7API g_ora7API;

#endif // !defined(__ORA7API_H__)
