/*
**	Sybase OC-LIBRARY
**	Confidential Property of Sybase, Inc.
**	(c) Copyright Sybase, Inc. 1991 - 2005
**	All rights reserved
*/

/*
** sqlca.h - This is the header file for the sqlca structure for precompilers
*/
#ifndef __SQLCA_H__

#define __SQLCA_H__

/*****************************************************************************
**
** sqlca structure used
**
*****************************************************************************/

typedef struct _sqlca
{
	char	sqlcaid[8];
	long	sqlcabc;
	long	sqlcode;
	
	struct
	{
		long		sqlerrml;
		char		sqlerrmc[256];
	} sqlerrm;

	char	sqlerrp[8];
	long	sqlerrd[6];
	char	sqlwarn[8];
	char	sqlext[8];

} SQLCA;

#endif /* __SQLCA_H__ */
