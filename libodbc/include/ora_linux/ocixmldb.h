/* Copyright (c) 2003, Oracle Corporation.  All rights reserved.  */
 
/* 
   NAME 
     ocixmldb.h - XDB public functions

   DESCRIPTION 
     This file contains XDB specific public functions required for DOM C-API. 

   RELATED DOCUMENTS 

 
   EXPORT FUNCTION(S) 
   struct xmlctx *OCIXmlDbInitXmlCtx(OCIEnv *, OCISvcCtx *, OCIError *,
                               ocixmldbparam *params, int num_params);

   void    OCIXmlDbFreeXmlCtx(struct xmlctx *xctx);


  ------------------------------------------------------------------------
   EXAMPLES
   
   NOTES

   MODIFIED   (MM/DD/YY)
   ataracha    12/11/03 - remove redundant definitions
   ataracha    05/28/03 - change names
   ataracha    02/18/03 - add oratypes, remove XMLERR_*
   imacky      02/01/03 - remove xml.h; xdbs fix
   ataracha    01/24/03 - use "struct xmlctx" instead of xmlctx
   imacky      01/28/03 - fix XMLERR defs
   ataracha    01/21/03 - ataracha_uni_capi_cleanup
   ataracha    01/09/03 - Creation

*/

#ifndef ORATYPES 
#include <oratypes.h> 
#endif
 
#ifndef OCI_ORACLE
# include <oci.h>
#endif

#ifndef OCIXMLDB_ORACLE
# define OCIXMLDB_ORACLE


/*---------------------------------------------------------------------------
                     PUBLIC TYPES AND CONSTANTS
  ---------------------------------------------------------------------------*/

#ifndef XMLCTX_DEFINED
# define XMLCTX_DEFINED
/* DATATYPE xmlctx - XML top-level context
*/
struct xmlctx; typedef struct xmlctx xmlctx;
#endif

typedef enum 
{
  XCTXINIT_OCIDUR  = 1,
  XCTXINIT_ERRHDL  = 2
} ocixmldbpname;

typedef struct ocixmldbparam
{
  ocixmldbpname   name_ocixmldbparam;
  void           *value_ocixmldbparam;
} ocixmldbparam;

#define NUM_OCIXMLDBPARAMS 2

/*---------------------------------------------------------------------------
                     PRIVATE TYPES AND CONSTANTS
  ---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------
                           EXPORT FUNCTIONS
  ---------------------------------------------------------------------------*/
/*****************************************************************************
                              DESCRIPTION

-----------------------------OCIXmlDbInitXmlCtx---------------------------------
Name
OCIXmlDbInitXmlCtx
Purpose
To get a xmlctx structure initialized with error-handler and XDB callbacks. 
Syntax
struct xmlctx *OCIXmlDbInitXmlCtx (OCIEnv           *envhp,
                                 OCISvcCtx        *svchp, 
                                 OCIError         *err, 
                                 params_ocixmldb *params,
                                 int               num_params);
Parameters
envhp (IN) - The OCI environment handle
svchp (IN) - The OCI service handle
errhp (IN) - The OCI error handle
params (IN)- This contains the following optional parameters :
   (a) OCIDuration dur (IN - The OCI Duration (Default: OCI_DURATION_SESSION)
   (b) void (*err_handler) (sword, (const oratext *) (IN) - 
       Pointer to the error handling function (Default: null)
num_params (IN) - Number of parameters to be read from parameter params.
                  If the value of num_params exceeds the size of array 
                  "params", unexpected behavior will result.

Returns
A pointer to xmlctx structure, with xdb context, error handler and callbacks 
populated with appropriate values. This is later used for all API calls. NULL
if no database connection available.

-----------------------------OCIXmlDbFreeXmlCtx----------------------------
Name
OCIXmlDbFreeXmlCtx
Pupose
To free any allocations done during OCIXmlDbInitXmlCtx.
Syntax
void OCIXmlDbFreeXmlCtx (struct xmlctx *xctx)
Parameters
xctx (IN) - The xmlctx to terminate
Returns
-
******************************************************************************/

struct xmlctx *OCIXmlDbInitXmlCtx(OCIEnv *, OCISvcCtx *, OCIError *, 
                            ocixmldbparam *, int);

void    OCIXmlDbFreeXmlCtx(struct xmlctx *xctx);
/*---------------------------------------------------------------------------
                          INTERNAL FUNCTIONS
  ---------------------------------------------------------------------------*/


#endif                                              /* OCIXMLDB_ORACLE */
