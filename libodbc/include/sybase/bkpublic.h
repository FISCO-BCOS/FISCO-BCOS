/*
**      Sybase OpenClient
**      Confidential Property of Sybase, Inc.
**      (c) Copyright Sybase, Inc. 1991 - 2005
**      All rights reserved
*/

/*
** blkpublic.h - for the bulk copy library API routines
*/
#ifndef __BKPUBLIC_H__

#define __BKPUBLIC_H__


#ifndef __NO_INCLUDE__
#include        <ctpublic.h>
#endif /* __NO_INCLUDE__ */

/*****************************************************************************
**
** defines used in BLK-Lib applications
**
*****************************************************************************/

/*
** Define for a constant to differentiate the
** blklib api's/
*/
#define BLK_API_CONT            (CS_INT) 500
/*
** define for each BLK-Lib API
*/

#define BLK_NOAPI               ((CS_INT) 0 + BLK_API_CONT)
#define BLK_BIND                ((CS_INT) 1 + BLK_API_CONT)
#define BLK_DESCRIBE            ((CS_INT) 2 + BLK_API_CONT)
#define BLK_DEFAULT             ((CS_INT) 3 + BLK_API_CONT)
#define BLK_INIT                ((CS_INT) 4 + BLK_API_CONT)
#define BLK_DONE                ((CS_INT) 5 + BLK_API_CONT)
#define BLK_ROWXFER             ((CS_INT) 6 + BLK_API_CONT)
#define BLK_TEXTXFER            ((CS_INT) 7 + BLK_API_CONT)
#define BLK_SENDROW             ((CS_INT) 8 + BLK_API_CONT)
#define BLK_SENDTEXT            ((CS_INT) 9 + BLK_API_CONT)
#define BLK_DROP                ((CS_INT) 10 + BLK_API_CONT)
#define BLK_GETROW		((CS_INT) 11 + BLK_API_CONT)
#define BLK_COLVAL		((CS_INT) 12 + BLK_API_CONT)
#define BLK_GETTEXT		((CS_INT) 13 + BLK_API_CONT)
#define BLK_PROPS		((CS_INT) 14 + BLK_API_CONT)


/*
** The direction of the bulk copy - set in blk_init()
*/
#define CS_BLK_IN	(CS_INT) 1
#define	CS_BLK_OUT	(CS_INT) 2

/*
** For the blk_done - 
**	just send a batch of records over
** 	end the bulk copy operation
**	Cancel the bulk copy operation
*/
#define CS_BLK_BATCH	(CS_INT) 1
#define CS_BLK_ALL	(CS_INT) 2
#define CS_BLK_CANCEL	(CS_INT) 3

/*
** For blk_default when there is no default value for the column
*/
#define	CS_NO_DEFAULT	(CS_INT) (-999)


#ifndef		BLK_VERSION_100
#define		BLK_VERSION_100	CS_VERSION_100
#define		BLK_VERSION_100_NO_LRGID CS_VERSION_100_NO_LRGID
#endif		/* BLK_VERSION_100 */

/* 
** define BLK_VERSION_110 as BLK_VERSION_100
*/
#ifndef		BLK_VERSION_110
#define		BLK_VERSION_110	BLK_VERSION_100
#define		BLK_VERSION_110_NO_LRGID BLK_VERSION_100_NO_LRGID
#endif		/* BLK_VERSION_110 */

/* 
** define BLK_VERSION_120 as BLK_VERSION_100
*/
#ifndef		BLK_VERSION_120
#define		BLK_VERSION_120	BLK_VERSION_100
#define		BLK_VERSION_12_NO_LRGID0 BLK_VERSION_100_NO_LRGID
#endif		/* BLK_VERSION_120 */

/* 
** define BLK_VERSION_125 as CS_VERSION_125
*/
#ifndef		BLK_VERSION_125
#define		BLK_VERSION_125	CS_VERSION_125
#define		BLK_VERSION_125_NO_LRGID CS_VERSION_125_NO_LRGID
#endif		/* BLK_VERSION_125 */

/* 
** define BLK_VERSION_150 as CS_VERSION_150
*/
#ifndef		BLK_VERSION_150
#define		BLK_VERSION_150 CS_VERSION_150
#define		BLK_VERSION_150_NO_LRGID CS_VERSION_150_NO_LRGID
#endif		/* BLK_VERSION_150 */

/*
** define BLK_VERSION_155 as CS_VERSION_155
*/
#ifndef		BLK_VERSION_155
#define		BLK_VERSION_155 CS_VERSION_155
#endif		/* BLK_VERSION_155 */

/*
** define BLK_VERSION_157 as CS_VERSION_157
*/
#ifndef		BLK_VERSION_157
#define		BLK_VERSION_157 CS_VERSION_157
#endif		/* BLK_VERSION_157 */

/*
** For the bulk copy options - used with blk_props()
*/
#define BLK_IDENTITY    	(CS_INT) (1)
#define BLK_SENSITIVITY_LBL     (CS_INT) (2)
#define BLK_NOAPI_CHK    	(CS_INT) (3)
#define BLK_SLICENUM    	(CS_INT) (4)
#define BLK_IDSTARTNUM    	(CS_INT) (5)
#define BLK_ARRAY_INSERT        (CS_INT) (6)
#define ARRAY_INSERT            BLK_ARRAY_INSERT
#define BLK_CONV		(CS_INT) (7)
#define BLK_PARTITION		(CS_INT) (8)
#define BLK_CUSTOM_CLAUSE	(CS_INT) (9)

/*
** Special value for datafmt.status in blklib with array binding
*/
#define CS_BLK_ARRAY_MAXLEN		(CS_INT)0x1000

/*****************************************************************************
**
** define all user accessable functions here
**
*****************************************************************************/

/*
** declare all functions
*/

CS_START_EXTERN_C

extern CS_RETCODE CS_PUBLIC blk_alloc PROTOTYPE((
        CS_CONNECTION *connection,
        CS_INT version,
        CS_BLKDESC **blkdesc
        ));

extern CS_RETCODE CS_PUBLIC blk_bind PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_INT colnum,
	CS_DATAFMT *datafmt,
	CS_VOID *buf,
	CS_INT *datalen,
	CS_SMALLINT *indicator
	));

extern CS_RETCODE CS_PUBLIC blk_colval PROTOTYPE((
        CS_VOID *srvproc,
        CS_BLKDESC *blkdesc,
        CS_BLK_ROW *row,
        CS_INT colnum,
        CS_VOID *value,
        CS_INT valuelen,
        CS_INT *outlen
        ));

extern CS_RETCODE CS_PUBLIC blk_default PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_INT colnum,
	CS_VOID *value,
	CS_INT valuelen,
	CS_INT *outlen
	));

extern CS_RETCODE CS_PUBLIC blk_describe PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_INT colnum,
	CS_DATAFMT *datafmt
	));

extern CS_RETCODE CS_PUBLIC blk_done PROTOTYPE((
        CS_BLKDESC *blkdesc,
        CS_INT option,
        CS_INT *outrows
        ));

extern CS_RETCODE CS_PUBLIC blk_drop PROTOTYPE((
	CS_BLKDESC *blkdesc
	));

extern CS_RETCODE CS_PUBLIC blk_getrow PROTOTYPE((
        CS_VOID *srvproc,
        CS_BLKDESC *blkdesc,
        CS_BLK_ROW *row
        ));

extern CS_RETCODE CS_PUBLIC blk_gettext PROTOTYPE((
        CS_VOID *srvproc,
        CS_BLKDESC *blkdesc,
        CS_BLK_ROW *row,
        CS_BYTE *buf,
        CS_INT buflen,
        CS_INT *bytes_read
        ));

extern CS_RETCODE CS_PUBLIC blk_init PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_INT direction,
	CS_CHAR *tblname,
	CS_INT tblnamelen
	));

extern CS_RETCODE CS_PUBLIC blk_rowalloc PROTOTYPE((
        CS_VOID *srvproc,
        CS_BLK_ROW **row
        ));

extern CS_RETCODE CS_PUBLIC blk_rowdrop PROTOTYPE((
        CS_VOID *srvproc,
        CS_BLK_ROW *row
        ));

extern CS_RETCODE CS_PUBLIC blk_rowxfer PROTOTYPE((
	CS_BLKDESC *blkdesc
	));

extern CS_RETCODE CS_PUBLIC blk_rowxfer_mult PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_INT *rowcount
	));

extern CS_RETCODE CS_PUBLIC blk_sendrow PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_BLK_ROW *row
	));

extern CS_RETCODE CS_PUBLIC blk_sendtext PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_BLK_ROW *row,
	CS_BYTE *buf,
	CS_INT buflen
	));

extern CS_RETCODE CS_PUBLIC blk_srvinit PROTOTYPE((
        CS_VOID *srvproc,
        CS_BLKDESC *blkdesc
        ));

extern CS_RETCODE CS_PUBLIC blk_textxfer PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_BYTE *buf,
	CS_INT buflen,
	CS_INT *outlen
	));

extern CS_RETCODE CS_PUBLIC blk_props PROTOTYPE((
	CS_BLKDESC *blkdesc,
	CS_INT action,
	CS_INT property,
	CS_VOID *buffer,
	CS_INT buflen,
	CS_INT *outlen
	));

extern CS_RETCODE CS_PUBLIC blk_bcp_indicator PROTOTYPE((
        CS_BLKDESC *blkdesc,
        CS_BOOL action
        ));

CS_END_EXTERN_C

#endif /* end of __BKPUBLIC_H__ */
