/* Copyright (c) 1995, 2008, Oracle. All rights reserved.  */
 
/* 
   NAME 
     oci.h - V8 Oracle Call Interface public definitions

   DESCRIPTION 
     This file defines all the constants and structures required by a V8
     OCI programmer.

   RELATED DOCUMENTS 
     V8 OCI Functional Specification 
     Oracle Call Interface Programmer's Guide Vol 1 and 2
 
   INSPECTION STATUS 
     Inspection date: 
     Inspection status: 
     Estimated increasing cost defects per page: 
     Rule sets: 
 
   ACCEPTANCE REVIEW STATUS 
     Review date: 
     Review status: 
     Reviewers: 
 
   PUBLIC FUNCTION(S) 
     None

   PRIVATE FUNCTION(S) 
     None
 
   EXAMPLES 
 
   NOTES 


   MODIFIED   (MM/DD/YY)
   nikeda      05/13/08 - Backport nikeda_bug-6964328 from main
   umabhat     09/20/07 - bug6119750 added OCI_FNCODE_APPCTXSET &
                          OCI_FNCODE_APPCTXCLEARALL
   msakayed    05/24/07 - Bug #5095734: add OCI_ATTR_DIRPATH_RESERVED_19
   schoi       03/02/07 - Get/SetOptions API change
   ebatbout    03/30/07 - 5598333: Add OCI_ATTR_DIRPATH_RESERVED_18
   nikeda      03/21/07 - Add OCI_ATTR_RESERVED_37 
   abande      03/06/07 - Remove attributes for global stmt cache and 
                          metadata cache
   rphillip    02/20/07 - Add OCI_ATTR_DIRPATH_RESERVED_17
   shan        11/16/06 - bug 5595911.
   msakayed    12/04/06 - Bug #5660845: add OCI_DIRPATH_INPUT_OCI
   gviswana    10/26/06 - Remove OCI_ATTR_CURRENT_EDITION
   maramali    09/29/06 - bug 5568492, added OCI_NLS_LOCALE_A2_ISO_2_ORA
   gviswana    09/29/06 - CURRENT_EDITION -> EDITION
   aramappa    09/20/06 - Update major and minor version information
   slynn       07/28/06 - Migrate to new 11g LOB terminiology
   debanerj    07/20/06 - Add OCI_ATTR_LOBPREFETCH_LENGTH
   mbastawa    06/25/06 - add OCI_ATTR_RESERVED_36
   hqian       05/22/06 - 11gR1 proj-18303: add OCI_SYSASM 
   dkogan      04/06/06 - disable charset validation by default 
   jhealy      05/15/06 - Add TimesTen OCI adapter. 
   slynn       06/20/06 - GetSharedRegions
   rthammai    06/13/06 - add reserved attribute 
   msakayed    06/15/06 - Project 20586: interval partitioning support 
   debanerj    10/25/05 - LOB prefetch
   slynn       05/25/06 - New NG Lob Functionality. 
   yujwang     05/16/06 - Add OCI_ATTR_RESERVED_33, OCI_ATTR_RESERVED_34
   abande      04/25/06 - 18297: Add attributes for global stmt cache and 
                          metadata cache 
   ssvemuri    04/26/06 - Constants for Query Notification support
   jgiloni     05/05/06 - Add OCI_ATCH_RESERVED_7 
   mxyang      02/01/06 - Added OCI_ATTR_CURRENT_EDITION attribute
   hqian       05/04/06 - new runtime capability attribute for asm volume
   nikeda      06/06/06 - OCI_TT: Add new OCIP attributes 
   aramappa    04/17/06 - Added OCI_FNCODE_ARRAYDESCRIPTORALLOC and 
                          OCI_FNCODE_ARRAYDESCRIPTORFREE 
   debanerj    05/04/06 - 18313: OCI Net Fusion
   rupsingh    05/26/06 - 
   jacao       05/11/06 - 
   absaxena    04/17/06 - add notification grouping attributes
   rpingte     02/02/06 - add OCI_ATCH_RESERVED_6
   rpingte     04/27/06 - Add OCI_ATTR_DRIVER_NAME
   jawilson    02/14/06 - add OCI_FNCODE_AQENQSTREAM
   kneel       04/03/06 - Adding support in kjhn for critical severity 
   rphillip    03/31/06 - Add OCI_ATTR_DIRPATH_RESERVED_14 
   mxyang      02/01/06 - Added OCI_ATTR_APPLICATION_EDITION attribute
   rphillip    01/30/06 - Add new DPAPI attrs 
   ebatbout    11/03/05 - Add direct path support for multiple subtypes
   porangas    02/22/06 - 5055398: Define OCI_STMT_CALL
   mbastawa    01/31/06 - add OCI_ATTR_RESERVED_26
   yohu        01/27/06 - align Execution Modes macros
   sjanardh    01/25/06 - add OCI_EXEC_RESERVED_6 
   sichandr    01/18/06 - add OCI_ATTR_XMLTYPE_BINARY_XML 
   yohu        12/22/05 - add OCI_TRANS_PROMOTE
   srseshad    09/12/05 - stmtcache: callback 
   krajan      10/25/05 - Added ENABLE_BEQUEATH attach flag
   mbastawa    09/16/05 - dbhygiene
   porangas    07/20/04 - 1175350: adding attribute for ognfd
   chliang     06/30/05 - add OCI_SUPPRESS_NLS_VALIDATION mode
   aahluwal    03/15/05 - [Bug 4235014]:add ASM, Preconnect events 
   ssappara    08/12/04 - Bug3669429 add OCI_ATTR_DESC_SYNBAS
   absaxena    03/24/05 - remove OCI_AQ_RESERVED_5 
   mbastawa    03/01/05 - add OCI_EXEC_RESERVED_5 
   msakayed    02/15/05 - Bug #3147299: Add OCI_ATTR_CURRENT_ERRCOL
   aahluwal    01/11/05 - [Bug 3944589]: add OCI_AUTH_RESERVED_5 
   nikeda      11/15/04 - Add OCIP_IIO 
   rvissapr    11/10/04 - bug 3843644 - isencrypted 
   hohung      11/22/04 - add OCI_BIND_RESERVED_3
   cchui       10/25/04 - add OCI_ATTR_PROXY_CLIENT 
   aahluwal    09/27/04 - add incarnation, reason, cardinality to event handle 
   msakayed    09/14/04 - column encryption support (project id 5578) 
   jacao       08/17/04 - Add OCI_ATTR_DB_CHARSET_ID 
   mhho        08/29/04 - resolve conflicting mode declaration
   sgollapu    05/28/04 - Add OCI_AUTH_RESERVED_3 
   mbastawa    08/05/04 - add OCI_ATTR_RESERVED_21
   ebatbout    07/27/04 - add OCI_ATTR_DIRPATH_RESERVED_9 and move all direct
                          path attributes into a separate area in this file.
   clei        06/29/04 - add OCI_ATTR_ENCC_SIZE
   weiwang     05/06/04 - add OCIAQListenOpts and OCIAQLisMsgProps 
   weiwang     04/30/04 - add OCI_AQ_RESERVED_5
   nbhatt      04/27/04 - add new attribute 
   ssvemuri    06/19/04 -  change notification descriptors and attributes
   ksurlake    06/01/04 - grabtrans 'ksurlake_txn_skmishra_clone' 
   ksurlake    05/13/04 - add subscriber handle attributes
   mbastawa    06/01/04 - add 3 more OCI_FETCH_RESERVED modes
   chliang     05/28/04 - add nchar literal replacement modes
   nikeda      05/14/04 - [OLS on RAC] new authentication mode 
   debanerj    05/17/04 - 13064: add fncodes for LOB array Read and Write
   nikeda      05/20/04 - [OCI Events] Add incarnation, cardinality,reason 
   nikeda      05/18/04 - [OCI Events] Add OCI_ATTR_SERVICENAME 
   nikeda      05/17/04 - Add event handle 
   nikeda      05/13/04 - [OCI Events] Rename HACBK->EVTCBK, HACTX->EVTCTX 
   nikeda      05/10/04 - [OCI Events] code review changes 
   nikeda      04/15/04 - [OCI Events] OCI_SESSRLS_DROPSESS_FORCE 
   nikeda      04/12/04 - [OCI Events] Add OCI_ATTR_USER_MEMORY 
   aahluwal    04/12/04 - add OCI_HNDLFR_RESERVED5 
   vraja       04/28/04 - add options for redo sync on commit 
   aahluwal    05/29/04 - [OCI Events]: add support for svc, svc member events 
   nikeda      05/28/04 - grabtrans 'nikeda_oci_events_copy' 
   nikeda      05/18/04 - [OCI Events] Add OCI_ATTR_SERVICENAME 
   nikeda      05/17/04 - Add event handle 
   nikeda      05/13/04 - [OCI Events] Rename HACBK->EVTCBK, HACTX->EVTCTX 
   nikeda      05/10/04 - [OCI Events] code review changes 
   nikeda      04/15/04 - [OCI Events] OCI_SESSRLS_DROPSESS_FORCE 
   nikeda      04/12/04 - [OCI Events] Add OCI_ATTR_USER_MEMORY 
   aahluwal    04/12/04 - add OCI_HNDLFR_RESERVED5 
   jciminsk    04/28/04 - merge from RDBMS_MAIN_SOLARIS_040426 
   jacao       03/06/04 - add OCI_ATTR_CURRENT_SCHEMA 
   aahluwal    01/20/04 - remove OCI_KEEP_FETCH_STATE
   aahluwal    03/25/04 - [OCI Events] add OCI_HTYPE_HAEVENT and related attrs 
   nikeda      03/19/04 - [OCI Events] Add OCI_ATTR_HACBK and OCI_ATTR_HACTX 
   dfrumkin    12/04/03 - Add database startup/shutdown
   chliang     12/22/03 - grid/main merge: add OCI_ATTR_RESERVED_20 
   jciminsk    12/12/03 - merge from RDBMS_MAIN_SOLARIS_031209 
   sgollapu    09/19/03 - Add fetch modes 
   sgollapu    07/30/03 - Add TSM attributes
   sgollapu    06/26/03 - Add OCI_MUTEX_TRY
   aime        06/23/03 - sync grid with main
   sgollapu    06/07/03 - Add reserved attribute
   sgollapu    06/05/03 - Add reserved auth flag
   rpingte     05/22/03 - Add OCI_ATCH_RESERVED_5
   sgollapu    05/06/03 - Add TSM attributes
   sgollapu    04/10/03 - Session migration Flags/interfaces
   dfrumkin    04/23/04 - add OCI_PREP2_RESERVED_1
   rpingte     05/06/04 - add major and minor version information
   bsinha      04/06/04 - add new OCI_TRANS flag
   chliang     11/26/03 - add OCI_ATTR_RESERVED_19 
   preilly     10/23/03 - Make OCI_ATTR_DIRPATH_METADATA_BUF private 
   chliang     08/07/03 - add OCI_ATTR_SKIP_BUFFER
   srseshad    03/12/03 - convert public oci api to ansi
   weiwang     05/14/03 - remove iot creation for rule sets
   rkoti       04/15/03 - [2746515] add fntcodes for Unlimited size LOB 6003
   tcruanes    05/13/03 - add slave SQL OCI execution mode
   rkoti       02/21/03 - [2761455] add OCI_FNCODE_AQENQARRAY,
                          OCI_FNCODE_AQDEQARRAY and update OCI_FNCODE_MAXFCN
   tkeefe      01/29/03 - bug-2773794: Add new interface for setting Kerb attrs
   aahluwal    02/06/03 - add OCI_ATTR_TRANSFORMATION_NO
   weiwang     12/05/02 - add OCI_ATTR_USER_PROPERTY
   ataracha    01/03/03 - include ocixmldb.h
   preilly     12/05/02 - Add wait attribute for locking when using dir path
   tkeefe      01/03/03 - bug-2623771: Added OCI_ATTR_KERBEROS_KEY
   lchidamb    12/13/02 - end-to-end tracing attributes
   msakayed    10/28/02 - Bug #2643907: add OCI_ATTR_DIRPATH_SKIPINDEX_METHOD
   rphillip    11/13/02 - Add OCIP_ATTR_DIRPATH_INDEX
   sagrawal    10/13/02 - liniting
   sagrawal    10/03/02 - PL/SQL Compiler warnings
   jstenois    11/07/02 - remove ocixad.h
   chliang     10/21/02 - add OCI_ATTR_RESERVED_16,17
   hsbedi      10/30/02 - grabtrans 'jstenois_fix_xt_convert'
   aahluwal    10/12/02 - add OCI_ATTR_AQ_NUM_E_ERRORS/OCI_ATTR_AQ_ERROR_INDEX
   bdagevil    10/21/02 - add SQL analyze internal exec mode
   csteinba    10/11/02 - add OCI_ATTR_RESERVED_16
   chliang     10/12/02 - add bind row callback attributes
   preilly     10/25/02 - Add new reserved parameters
   tkeefe      10/31/02 - bug-2623771: Added OCI_ATTR_AUDIT_SESSION_ID
   csteinba    10/04/02 - Add OCI_ATTR_RESERVED_15
   mhho        10/11/02 - add new credential constant
   thoang      09/25/02 - Add OCI_XMLTYPE_CREATE_CLOB
   skaluska    10/07/02 - describe rules objects
   csteinba    09/16/02 - Remove OCI_CACHE
   gtarora     10/03/02 - OCI_ATTR_COL_SUBS => OCI_ATTR_OBJ_SUBS
   msakayed    09/09/02 - Bug #2482469: add OCI_ATTR_DIRPATH_RESERVED_[3-6]
   aahluwal    08/30/02 - adding dequeue across txn group
   srseshad    04/24/02 - Add attribute OCI_ATTR_SPOOL_STMTCACHESIZE.
   ebatbout    07/22/02 - Remove OCI_ATTR_RESERVED_11.
   abande      01/17/02 - Bug 1788921; Add external attribute.
   aahluwal    06/04/02 - bug 2360115
   pbagal      05/24/02 - Incorporate review comments
   pbagal      05/22/02 - Introduce instance type attribute.
   whe         07/01/02 - add OCI_BIND_DEFINE_SOFT flags
   gtarora     07/01/02 - Add OCI_ATTR_COL_SUBS
   tkeefe      05/30/02 - Add support for new proxy authentication credentials
   dgprice     12/18/01 - bug 2102779 add reserved force describe
   schandir    11/19/01 - add/modify modes.
   schandir    11/15/01 - add OCI_SPC_STMTCACHE.
   schandir    12/06/01 - change mode value of OCI_SPOOL.
   msakayed    11/02/01 - Bug #2094292: add OCI_ATTR_DIRPATH_INPUT
   dsaha       11/09/01 - add OCI_DTYPE_RESERVED1
   skabraha    11/05/01 - new method flag
   skabraha    10/25/01 - another flag for XML
   skabraha    10/11/01 - describe flags for subtypes
   nbhatt      09/18/01 - new reserved AQ flags
   celsbern    10/19/01 - merge LOG to MAIN
   ksurlake    10/12/01 - add OCI_ATTR_RESERVED_13
   ksurlake    08/13/01 - add OCI_ATTR_RESERVED_12
   schandir    09/24/01 - Adding stmt caching
   abande      09/04/01 - Adding session pooling
   sagrawal    10/23/01 - add new bit for OCIPHandleFree
   preilly     10/25/01 - Add support for specifying metadata on DirPathCtx
   skabraha    09/24/01 - describe flags for XML type
   schandir    09/24/01 - Adding stmt caching
   abande      09/04/01 - Adding session pooling
   stakeda     09/17/01 - add OCI_NLS_CHARSET_ID
   whe         09/19/01 - add OCIXMLType create options
   rpingte     09/11/01 - add OCI_MUTEX_ENV_ONLY and OCI_NO_MUTEX_STMT
   cmlim       08/28/01 - mod datecache attrs to use same naming as dpapi attrs
   wzhang      08/24/01 - Add new keywords for OCINlsNameMap.
   rphillip    05/02/01 - Add date cache attributes
   rphillip    08/22/01 - Add new stream version
   ebatbout    04/13/01 - add definition, OCI_ATTR_RESERVED_11
   chliang     04/12/01 - add shortnames for newer oci funcation
   wzhang      04/11/01 - Add new OCI NLS constants.
   cmlim       04/13/01 - remove attrs not used by dpapi (151 & 152 avail)
   rkambo      03/23/01 - bugfix 1421793
   cmlim       04/02/01 - remove OCI_ATTR_DIRPATH_{NESTED_TBL, SUBST_OBJ_TBL}
                        - note: attribute #s 186 & 205 available
   whe         03/28/01 - add OCI_AFC_PAD_ON/OFF mode
   preilly     03/05/01 - Add stream versioning support to DirPath context
   schandir    12/18/00 - remove attr CONN_INCR_DELAY.
   schandir    12/12/00 - change mode from OCI_POOL to OCI_CPOOL.
   cbarclay    01/12/01 - add atribute for OCIP_ATTR_TMZ
   whe         01/07/01 - add attributes related to UTF16 env mode
   slari       12/29/00 - add blank line
   slari       12/28/00 - OCI_ATTR_RESERVED_10
   whe         12/19/00 - add OCI_ENVCR_RESERVED3
   rpang       11/29/00 - Added OCI_ATTR_ORA_DEBUG_JDWP attribute
   cmlim       11/28/00 - support substitutable object tables in dpapi
   akatti      10/09/00 - [198379]:add OCIRowidToChar
   sgollapu    10/11/00 - Add OCI_PREP_RESERVED_1
   sgollapu    08/27/00 - add attribute to get erroneous column
   sgollapu    07/29/00 - Add snapshot attributes
   kmohan      09/18/00 - add OCI_FNCODE_LOGON2
   abrumm      10/08/00 - include ocixad.h
   mbastawa    10/04/00 - add OCI_ATTR_ROWS_FETCHED
   nbhatt      08/24/00 - add transformation attribute
   dmwong      08/22/00 - OCI_ATTR_CID_VALUE -> OCI_ATTR_CLIENT_IDENTIFIER.
   cmlim       08/30/00 - add OCI_ATTR_DIRPATH_SID
   dsaha       08/18/00 - add OCI_ATTR_RESERVED_5
   amangal     08/17/00 - Merge into 8.2 : 1194361
   slari       08/03/00 - add OCI_ATTR_HANDLE_POSITION
   dsaha       07/20/00 - 2rt exec
   sgollapu    07/04/00 - Add virtual session flag
   cmlim       07/07/00 - add OCI_ATTR_DIRPATH_OID, OCI_ATTR_DIRPATH_NESTED_TBL
   etucker     07/28/00 - add OCIIntervalFromTZ
   rwessman    06/26/00 - N-tier: added new credential attributes
   whe         07/27/00 - add OCI_UTF16 mode
   vjayaram    07/18/00 - add connection pooling changes
   etucker     07/12/00 - add dls apis
   cmlim       07/07/00 - add OCI_ATTR_DIRPATH_OID, OCI_ATTR_DIRPATH_NESTED_TBL
   sgollapu    07/04/00 - Add virtual session flag
   najain      05/01/00 - AQ Signature support
   sgollapu    06/14/00 - Add reserved OCI mode
   rkambo      06/08/00 - notification presentation support
   sagrawal    06/04/00 - ref cursor to c
   ksurlake    06/07/00 - define OCI_POOL
   mbastawa    06/05/00 - added scrollable cursor attributes
   weiwang     03/31/00 - add LDAP support
   whe         05/30/00 - add OCI_ATTR_MAXCHAR_SIZE
   whe         05/23/00 - validate OCI_NO_CACHE mode
   dsaha       02/02/00 - Add no-cache attr in statement handle
   whe         05/23/00 - add OCIP_ICACHE
   allee       05/17/00 - describe support for JAVA implmented TYPE
   preilly     05/30/00 - Continue adding support for objects in direct path lo
   cmlim       05/16/00 - 8.2 dpapi support of ADTs
   rxgovind    05/04/00 - OCIAnyDataSet changes
   rkasamse    05/25/00 - add OCIAnyDataCtx
   rmurthy     04/26/00 - describe support for inheritance
   ksurlake    04/18/00 - Add credential type
   whe         05/24/00 - add OCI_ATTR_CHAR_ attrs
   rkambo      04/19/00 - subscription enhancement
   rmurthy     04/26/00 - describe support for inheritance
   delson      03/28/00 - add OCI_ATTR_RESERVED_2
   abrumm      03/31/00 - external table support
   rkasamse    03/13/00 - add declarations for OCIAnyData
   najain      02/24/00 - support for dequeue as select
   dsaha       03/10/00 - Add OCI_ALWAYS_BLOCKING
   esoyleme    04/25/00 - separated transactions
   sgollapu    12/23/99 - OCIServerAttach extensions
   slari       08/23/99 - add OCI_DTYPE_UCB
   slari       08/20/99 - add OCI_UCBTYPE_REPLACE
   hsbedi      08/31/99 - Memory Stats .
   sgollapu    08/02/99 - oci sql routing
   slari       08/06/99 - rename values for OCI_SERVER_STATUS
   slari       08/02/99 - add OCI_ATTR_SERVER_STATUS
   tnbui       07/28/99 - Remove OCI_DTYPE_TIMESTAMP_ITZ                       
   amangal     07/19/99 - Merge into 8.1.6 : bug 785797
   tnbui       07/07/99 - Change ADJUSTMENT modes                              
   dsaha       07/07/99 - OCI_SAHRED_EXT
   dmwong      06/08/99 - add OCI_ATTR_APPCTX_*
   vyanaman    06/23/99 -
   vyanaman    06/21/99 - Add new OCI Datetime and Interval descriptors
   esoyleme    06/29/99 - expose MTS performance enhancements                  
   rshaikh     04/23/99 - add OCI_SQL_VERSION_*
   tnbui       05/24/99 - Remove OCIAdjStr                                     
   dsaha       05/21/99 - Add OCI_ADJUST_UNK
   mluong      05/17/99 - fix merge
   tnbui       04/05/99 - ADJUSTMENT values
   abrumm      04/16/99 - dpapi: more attributes
   dsaha       02/24/99 - Add OCI_SHOW_DML_WARNINGS
   jiyang      12/07/98 - Add OCI_NLS_DUAL_CURRENCY
   slari       12/07/98 - change OCI_NOMUTEX to OCI_NO_MUTEX
   aroy        11/30/98 - change OCI_NOCALLBACK to OCI_NO_UCB
   aroy        11/13/98 - add env modes to process modes
   slari       09/08/98 - add OCI_FNCODE_SVC2HST and _SVCRH
   aroy        09/04/98 - Add OCI_ATTR_MIGSESSION
   skray       08/14/98 - server groups for session switching
   mluong      08/11/98 - add back OCI_HTYPE_LAST.
   aroy        05/25/98 - add process handle type                              
   aroy        04/06/98 - add shared mode                                      
   slari       07/13/98 -  merge forward to 8.1.4
   slari       07/09/98 -  add OCI_BIND_RESERVED_2
   slari       07/08/98 -  add OCI_EXACT_FETCH_RESERVED_1
   dsaha       07/07/98 -  Add OCI_PARSE_ONLY
   dsaha       06/29/98 -  Add OCI_PARSE_ONLY
   slari       07/01/98 -  add OCI_BIND_RESERVED_2
   sgollapu    06/25/98 -  Fix bug 683565
   slari       06/17/98 -  remove OC_FETCH_RESERVED_2
   slari       06/11/98 -  add OCI_FETCH_RESERVED_1 and 2
   jhasenbe    05/27/98 -  Remove definitions for U-Calls (Unicode)
   jiyang      05/18/98 - remove OCI_ATTR_CARTLANG
   nbhatt      05/20/98 -  OCI_DEQ_REMOVE_NODATA
   nbhatt      05/19/98 - correct AQ opcode
   skmishra    05/06/98 - Add precision attribute to Attributes list
   aroy        04/20/98 - merge forward 8.0.5 -> 8.1.3
   schandra    05/01/98 - OCI sender id
   sgollapu    02/19/98 - enhanced array DML
   nbhatt      05/15/98 -  AQ listen call
   sgollapu    04/27/98 - more attributes
   skaluska    04/06/98 - Add OCI_PTYPE_SCHEMA, OCI_PTYPE_DATABASE
   slari       04/28/98 - add OCI_ATTR_PDPRC
   lchidamb    05/05/98 - change OCI_NAMESPACE_AQ to 1
   nbhatt      04/27/98 - AQ Notification Descriptor
   abrumm      06/24/98 - more direct path attributes
   abrumm      05/27/98 - OCI direct path interface support
   abrumm      05/08/98 - OCI direct path interface support
   lchidamb    03/02/98 - client notification additions
   kkarun      04/17/98 - Add more Interval functions
   vyanaman    04/16/98 - Add get/set TZ
   kkarun      04/14/98 - Add OCI Datetime shortnames
   vyanaman    04/13/98 - Add OCI DateTime and Interval check error codes
   kkarun      04/07/98 - Add OCI_DTYPE_DATETIME and OCI_DTYPE_INTERVAL
   esoyleme    12/15/97 - support failover callback retry
   esoyleme    04/22/98 - merge support for failover callback retry
   mluong      04/16/98 - add OCI_FNCODE_LOBLOCATORASSIGN
   rkasamse    04/17/98 - add short names for OCIPickler(Memory/Ctx) cart servi
   slari       04/10/98 - add OCI_FNCODE_SVCCTXTOLDA
   slari       04/09/98 - add OCI_FNCODE_RESET
   slari       04/07/98 - add OCI_FNCODE_LOBFILEISOPEN
   slari       04/06/98 - add OCI_FNCODE_LOBOPEN
   slari       03/20/98 - change OCI_CBTYPE_xxx to OCI_UCBTYPE_xxx
   slari       03/18/98 - add OCI_FNCODE_MAXFCN
   slari       02/12/98 - add OCI_ENV_NO_USRCB
   skabraha    04/09/98 - adding shortnames for OCIFile
   rhwu        04/03/98 - Add short names for the OCIThread package
   tanguyen    04/03/98 - add OCI_ATTR_xxxx for type inheritance
   rkasamse    04/02/98 - add OCI_ATTR_UCI_REFRESH
   nramakri    04/01/98 - Add short names for the OCIExtract package
   ewaugh      03/31/98 - Add short names for the OCIFormat package.
   jhasenbe    04/06/98 - Add definitions for U-Calls (Unicode)
                          (OCI_TEXT, OCI_UTEXT, OCI_UTEXT4)
   skmishra    03/03/98 - Add OCI_ATTR_PARSE_ERROR_OFFSET
   rwessman    03/11/98 - Added OCI_CRED_PROXY for proxy authentication
   abrumm      03/31/98 - OCI direct path interface support
   nmallava    03/03/98 - add constants for temp lob apis
   skotsovo    03/05/98 - resolve merge conflicts
   skotsovo    02/24/98 - add OCI_DTYPE_LOC
   skaluska    01/21/98 - Add OCI_ATTR_LTYPE
   rkasamse    01/06/98 - add OCI_ATTR* for obj cache enhancements
   dchatter    01/08/98 - more comments
   skabraha    12/02/97 - moved oci1.h to the front of include files.
   jiyang      12/18/97 - Add OCI_NLS_MAX_BUFSZ
   rhwu        12/02/97 - move oci1.h up
   ewaugh      12/15/97 - Add short names for the OCIFormat package.
   rkasamse    12/02/97 - Add a constant for memory cartridge services -- OCI_M
   nmallava    12/31/97 - open/close for internal lobs
   khnguyen    11/27/97 - add OCI_ATTR_LFPRECISION, OCI_ATTR_FSPRECISION
   rkasamse    11/03/97 - add types for pickler cartridge services
   mluong      11/20/97 - changed ubig_ora to ub4 per skotsovo
   ssamu       11/14/97 - add oci1.h
   jiyang      11/13/97 - Add NLS service for cartridge
   esoyleme    12/15/97 - support failover callback retry
   jwijaya     10/21/97 - change OCILobOffset/Length from ubig_ora to ub4
   cxcheng     07/28/97 - fix compile with SLSHORTNAME
   schandra    06/25/97 - AQ OCI interface
   sgollapu    07/25/97 - Add OCI_ATTR_DESC_PUBLIC
   cxcheng     06/16/97 - add OCI_ATTR_TDO
   skotsovo    06/05/97 - add fntcodes for lob buffering subsystem
   esoyleme    05/13/97 - move failover callback prototype
   skmishra    05/06/97 - stdc compiler fixes
   skmishra    04/22/97 - Provide C++ compatibility
   lchidamb    04/19/97 - add OCI_ATTR_SESSLANG
   ramkrish    04/15/97 - Add OCI_LOB_BUFFER_(NO)FREE
   sgollapu    04/18/97 - Add OCI_ATTR_TABLESPACE
   skaluska    04/17/97 - Add OCI_ATTR_SUB_NAME
   schandra    04/10/97 - Use long OCI names
   aroy        03/27/97 - add OCI_DTYPE_FILE
   sgollapu    03/26/97 - Add OCI_OTYPEs
   skmishra    04/09/97 - Added constant OCI_ROWID_LEN
   dchatter    03/21/97 - add attr OCI_ATTR_IN_V8_MODE
   lchidamb    03/21/97 - add OCI_COMMIT_ON_SUCCESS execution mode
   skmishra    03/20/97 - Added OCI_ATTR_LOBEMPTY
   sgollapu    03/19/97 - Add OCI_ATTR_OVRLD_ID
   aroy        03/17/97 - add postprocessing callback
   sgollapu    03/15/97 - Add OCI_ATTR_PARAM
   cxcheng     02/07/97 - change OCI_PTYPE codes for type method for consistenc
   cxcheng     02/05/97 - add OCI_PTYPE_TYPE_RESULT
   cxcheng     02/04/97 - rename OCI_PTYPE constants to be more consistent
   cxcheng     02/03/97 - add OCI_ATTR, OCI_PTYPE contants for describe type
   esoyleme    01/23/97 - merge neerja callback
   sgollapu    12/30/96 - Remove OCI_DTYPE_SECURITY
   asurpur     12/26/96 - CHanging OCI_NO_AUTH to OCI_AUTH
   sgollapu    12/23/96 - Add more attrs to COL, ARG, and SEQ
   sgollapu    12/12/96 - Add OCI_DESCRIBE_ONLY
   slari       12/11/96 - change prototype of OCICallbackInBind
   nbhatt      12/05/96 - "callback"
   lchidamb    11/19/96 - handle subclassing
   sgollapu    11/09/96 - OCI_PATTR_*
   dchatter    11/04/96 - add attr OCI_ATTR_CHRCNT
   mluong      11/01/96 - test
   cxcheng     10/31/96 - add #defines for OCILobLength etc
   dchatter    10/31/96 - add lob read write call back fp defs
   dchatter    10/30/96 - more changes
   rhari       10/30/96 - Include ociextp.h at the very end
   lchidamb    10/22/96 - add fdo attribute for bind/server handle
   dchatter    10/22/96 - change attr defn for prefetch parameters & lobs/file
                          calls
   slari       10/21/96 - add OCI_ENV_NO_MUTEX
   rhari       10/25/96 - Include ociextp.h
   rxgovind    10/25/96 - add OCI_LOBMAXSIZE, remove OCI_FILE_READWRITE
   sgollapu    10/24/96 - Correct OCILogon and OCILogoff
   sgollapu    10/24/96 - Correct to OCILogon and OCILogoff
   sgollapu    10/21/96 - Add ocilon and ociloff
   skaluska    10/31/96 - Add OCI_PTYPE values
   sgollapu    10/17/96 - correct OCI_ATTR_SVCCTX to OCI_ATTR_SERVER
   rwessman    10/16/96 - Added security functions and fixed olint errors.
   sthakur     10/14/96 - add more COR attributes
   cxcheng     10/14/96 - re-enable LOB functions
   sgollapu    10/10/96 - Add ocibdp and ocibdn
   slari       10/07/96 - add back OCIRowid
   aroy        10/08/96 -  add typedef ocibfill for PRO*C
   mluong      10/11/96 - replace OCI_ATTR_CHARSET* with OCI_ATTR_CHARSET_*
   cxcheng     10/10/96 - temporarily take out #define for lob functions
   sgollapu    10/02/96 - Rename OCI functions and datatypes
   skotsovo    10/01/96 - move orl lob fnts to oci
   aroy        09/10/96 - fix merge errors
   aroy        08/19/96 - NCHAR support
   jboonleu    09/05/96 - add OCI attributes for object cache
   dchatter    08/20/96 - HTYPE ranges from 1-50; DTYPE from 50-255
   slari       08/06/96 - define OCI_DTYPE_ROWID
   sthakur     08/14/96 - complex object support
   schandra    06/17/96 - Convert XA to use new OCI
   abrik       08/15/96 - OCI_ATTR_HEAPALLOC added
   aroy        07/17/96 - terminology change: ocilobd => ocilobl
   aroy        07/03/96 - add lob typedefs for Pro*C
   slari       06/28/96 - add OCI_ATTR_STMT_TYPE
   lchidamb    06/26/96 - reorg #ifndef
   schandra    05/31/96 - attribute types for internal and external client name
   asurpur     05/30/96 - Changing the value of mode
   schandra    05/18/96 - OCI_TRANS_TWOPHASE -> 0x00000001 to 0x00100000
   slari       05/30/96 - add callback function prototypes
   jbellemo    05/23/96 - remove ociisc
   schandra    04/23/96 - loosely-coupled branches
   asurpur     05/15/96 - New mode for ocicpw
   aroy        04/24/96 - making ocihandles opaque
   slari       04/18/96 - add missing defines
   schandra    03/27/96 - V8OCI - add transaction related calls
   dchatter    04/01/96 - add OCI_FILE options
   dchatter    03/21/96 - add oci2lda conversion routines
   dchatter    03/07/96 - add OCI piece definition
   slari       03/12/96 - add describe attributes
   slari       03/12/96 - add OCI_OTYPE_QUERY
   aroy        02/28/96 - Add column attributes
   slari       02/09/96 - add OCI_OBJECT
   slari       02/07/96 - add OCI_HYTPE_DSC
   aroy        01/10/96 - adding function code defines...
   dchatter    01/03/96 - define OCI_NON_BLOCKING
   dchatter    01/02/96 - Add Any descriptor
   dchatter    01/02/96 - Add Select List descriptor
   dchatter    12/29/95 - V8 OCI definitions
   dchatter    12/29/95 - Creation

*/

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORATYPES 
#include <oratypes.h> 
#endif
 
#ifndef OCIDFN
#include <ocidfn.h>
#endif

#ifndef OCI_ORACLE
# define OCI_ORACLE

 
/*--------------------------------------------------------------------------- 
 Short names provided for platforms which do not allow extended symbolic names 
  ---------------------------------------------------------------------------*/

#ifdef SLSHORTNAME
/* Translation of the long function/type names to short names for IBM only */
/* maybe lint will use this too */
#define OCISessionEnd              ocitac
#define OCIResultSetToStmt         ocirs2sh
#define OCISessionBegin            ociauth
#define OCIServerAttach            ociatch
#define OCIDescriptorAlloc         ocigdesc
#define OCIServerDetach            ocidtch
#define OCIDescriptorFree          ocifdesc
#define OCIServerVersion           ocivers
#define OCIDescribeAny             ocidsca
#define OCIBindDynamic             ocibda
#define OCIBindByName              ocibdn
#define OCIBindByPos               ocibdp
#define OCIErrorGet                ocigdr
#define OCIBindArrayOfStruct       ocibsa
#define OCIEnvInit                 ociinit
#define OCIBindObject              ocibndt
#define OCIHandleAlloc             ocighndl
#define OCIHandleFree              ocifhndl
#define OCIRowidToChar             ociri2c
#ifdef NEVER
#define OCIStmtBindByPos           ocibndp
#define OCIStmtBindByName          ocibndn
#endif
#define OCIAttrGet                 ocigattr
#define OCIDefineByPos             ocidfne
#define OCIAttrSet                 ocisattr
#define OCIDefineDynamic           ociddf
#define OCILdaToSvcCtx             ocild2sv
#define OCIDefineArrayOfStruct     ocidarr
#define OCIInitialize              ocipi
#define OCIDefineObject            ocidndt
#define OCIStmtExecute             ociexec
#define OCILobAppend               ocilfap
#define OCILobOpenFile             ocifopn
#define OCILobCloseFile            ocifcls
#define OCILobLocator              ocilobd
#define OCILobGetDeduplicateRegions ocilgshr
#define OCILobRegion               ocilregd
#define OCILobCopy                 ocilfcp
#define OCILobFileCreate           ocifcrt
#define OCILobFileDelete           ocifdel
#define OCILobGetLength            ocilfln
#define OCILobWrite                ocilfwr
#define OCILobRead                 ocilfrd
#define OCILobErase                ocilfer
#define OCILobTrim                 ocilftr
#define OCILobSetOptions           ocinglso
#define OCILobGetOptions           ocinglgo
#define OCILobFragmentInsert       ocinglfi
#define OCILobFragmentDelete       ocinglfd
#define OCILobFragmentMove         ocinglfm
#define OCILobFragmentReplace      ocinglfr

#define OCIStmtFetch               ocifch
#define OCIStmtGetBindInfo         ocigbp
#define OCIStmtGetPieceInfo        ocigpi
#define OCIStmtPrepare             ocireq
#define OCIStmtSetPieceInfo        ocispi
#define OCISvcCtxToLda             ocisv2ld
#define OCITransCommit             ocitxcm
#define OCITransDetach             ocitxdt
#define OCITransForget             ocitxfgt
#define OCITransPrepare            ocitxpre
#define OCITransRollback           ocitxrl
#define OCIPasswordChange          ocicpw
#define OCITransStart              ocitxst
#define OCITransMultiPrepare       ocitxmp

#define OCIBreak                   ocibreak
#define OCIParamGet                ocigparm
#define OCIParamSet                ocisparm

#define OCISecurityOpenWallet           ocizwOpenWallet
#define OCISecurityCloseWallet          ocizwCloseWallet
#define OCISecurityCreateWallet         ocizwCreateWallet
#define OCISecurityDestroyWallet        ocizwDestroyWallet
#define OCISecurityStorePersona         ocizeStorePersona
#define OCISecurityOpenPersona          ocizeOpenPersona
#define OCISecurityClosePersona         ocizeClosePersona
#define OCISecurityRemovePersona        ocizeRemovePersona
#define OCISecurityCreatePersona        ocizeCreatePersona
#define OCISecuritySetProtection        ocizeSetProtection
#define OCISecurityGetProtection        ocizeGetProtection
#define OCISecurityRemoveIdentity       ociziRemoveIdentity
#define OCISecurityCreateIdentity       ociziCreateIdentity
#define OCISecurityAbortIdentity        ociziAbortIdentity
#define OCISecurityFreeIdentity         ociziFreeIdentity
#define OCISecurityStoreTrustedIdentity ociziStoreTrustedIdentity
#define OCISecuritySign                 ocizSign
#define OCISecuritySignExpansion        ocizxSignExpansion
#define OCISecurityVerify               ocizVerify
#define OCISecurityValidate             ocizValidate
#define OCISecuritySignDetached         ocizsd_SignDetached
#define OCISecuritySignDetExpansion     ocizxsd_SignDetachedExpansion
#define OCISecurityVerifyDetached       ocizved_VerifyDetached
#define OCISecurity_PKEncrypt           ocizkec_PKEncrypt
#define OCISecurityPKEncryptExpansion   ocizxkec_PKEncryptExpansion
#define OCISecurityPKDecrypt            ocizkdc_PKDecrypt
#define OCISecurityEncrypt              ocizEncrypt
#define OCISecurityEncryptExpansion     ocizxEncryptExpansion
#define OCISecurityDecrypt              ocizDecrypt
#define OCISecurityEnvelope             ocizEnvelope
#define OCISecurityDeEnvelope           ocizDeEnvelope
#define OCISecurityKeyedHash            ocizKeyedHash
#define OCISecurityKeyedHashExpansion   ocizxKeyedHashExpansion
#define OCISecurityHash                 ocizHash
#define OCISecurityHashExpansion        ocizxHashExpansion
#define OCISecuritySeedRandom           ocizSeedRandom
#define OCISecurityRandomBytes          ocizrb_RandomBytes
#define OCISecurityRandomNumber         ocizrn_RandomNumber
#define OCISecurityInitBlock            ocizibInitBlock
#define OCISecurityReuseBlock           ocizrbReuseBlock
#define OCISecurityPurgeBlock           ocizpbPurgeBlock
#define OCISecuritySetBlock             ocizsbSetBlock
#define OCISecurityGetIdentity          ocizgi_GetIdentity

#define OCIExtractInit             ocixeini
#define OCIExtractTerm             ocixetrm
#define OCIExtractReset            ocixerst
#define OCIExtractSetNumKeys       ocixesnk
#define OCIExtractSetKey           ocixesk
#define OCIExtractFromFile         ocixeff
#define OCIExtractFromStr          ocixefs
#define OCIExtractToInt            ocixeti
#define OCIExtractToBool           ocixetb
#define OCIExtractToStr            ocixets
#define OCIExtractToOCINum         ocixeton
#define OCIExtractToList           ocixetl
#define OCIExtractFromList         ocixefl

#define OCIDateTimeGetTime         ocidt01_GetTime
#define OCIDateTimeGetDate         ocidt02_GetDate
#define OCIDateTimeGetTimeZoneOffset     ocidt03_GetTZ
#define OCIDateTimeSysTimeStamp    ocidt07_SysTS
#define OCIDateTimeAssign          ocidt08_Assign 
#define OCIDateTimeToText          ocidt09_ToText
#define OCIDateTimeFromText        ocidt10_FromText
#define OCIDateTimeCompare         ocidt11_Compare
#define OCIDateTimeCheck           ocidt12_Check
#define OCIDateTimeConvert         ocidt13_Convert
#define OCIDateTimeSubtract        ocidt14_Subtract
#define OCIDateTimeIntervalAdd     ocidt15_IntervalAdd
#define OCIDateTimeIntervalSub     ocidt16_IntervalSub
#define OCIDateTimeGetTimeZoneName ocidt17_Gettzname
#define OCIDateTimeToArray         ocidt18_ToArray  
#define OCIDateTimeFromArray       ocidt19_FromArray

#define OCIIntervalSubtract        ociint01_Subtract  
#define OCIIntervalAdd             ociint02_Add  
#define OCIIntervalMultiply        ociint03_Multiply  
#define OCIIntervalDivide          ociint04_Divide  
#define OCIIntervalCompare         ociint05_Compare  
#define OCIIntervalFromText        ociint06_FromText  
#define OCIIntervalToText          ociint07_ToText  
#define OCIIntervalToNumber        ociint08_ToNumber  
#define OCIIntervalCheck           ociint09_Check  
#define OCIIntervalAssign          ociint10_Assign  
#define OCIIntervalGetYearMonth    ociint11_GetYearMonth
#define OCIIntervalSetYearMonth    ociint12_SetYearMonth
#define OCIIntervalGetDaySecond    ociint13_GetDaySecond
#define OCIIntervalSetDaySecond    ociint14_SetDaySecond
#define OCIIntervalFromNumber      ociint15_FromNumber
#define OCIIntervalFromTZ          ociint16_FromTZ 

#define OCIFormatInit              ocixs01_Init
#define OCIFormatString            ocixs02_Format
#define OCIFormatTerm              ocixs03_Term
#define OCIFormatTUb1              ocixs04_TUb1
#define OCIFormatTUb2              ocixs05_TUb2
#define OCIFormatTUb4              ocixs06_TUb4
#define OCIFormatTUword            ocixs07_TUword
#define OCIFormatTUbig_ora         ocixs08_TUbig_ora
#define OCIFormatTSb1              ocixs09_TSb1
#define OCIFormatTSb2              ocixs10_TSb2
#define OCIFormatTSb4              ocixs11_TSb4
#define OCIFormatTSword            ocixs12_TSword
#define OCIFormatTSbig_ora         ocixs13_TSbig_ora
#define OCIFormatTEb1              ocixs14_TEb1
#define OCIFormatTEb2              ocixs15_TEb2
#define OCIFormatTEb4              ocixs16_TEb4
#define OCIFormatTEword            ocixs17_TEword
#define OCIFormatTChar             ocixs18_TChar
#define OCIFormatTText             ocixs19_TText
#define OCIFormatTDouble           ocixs20_TDouble
#define OCIFormatTDvoid            ocixs21_TDvoid
#define OCIFormatTEnd              ocixs22_TEnd

#define OCIFileInit                ocifinit
#define OCIFileTerm                ocifterm
#define OCIFileOpen                ocifopen
#define OCIFileClose               ocifclose
#define OCIFileRead                ocifread
#define OCIFileWrite               ocifwrite
#define OCIFileSeek                ocifseek
#define OCIFileExists              ocifexists
#define OCIFileGetLength           ocifglen
#define OCIFileFlush               ocifflush


/* OCIThread short name */
#define OCIThreadProcessInit       ocitt01_ProcessInit
#define OCIThreadInit              ocitt02_Init
#define OCIThreadTerm              ocitt03_Term
#define OCIThreadIsMulti           ocitt04_IsMulti
#define OCIThreadMutexInit         ocitt05_MutexInit
#define OCIThreadMutexDestroy      ocitt06_MutexDestroy
#define OCIThreadMutexAcquire      ocitt07_MutexAcquire
#define OCIThreadMutexRelease      ocitt08_MutexRelease
#define OCIThreadKeyInit           ocitt09_KeyInit
#define OCIThreadKeyDestroy        ocitt10_KeyDestroy
#define OCIThreadKeyGet            ocitt11_KeyGet
#define OCIThreadKeySet            ocitt12_KeySet
#define OCIThreadIdInit            ocitt13_IdInit
#define OCIThreadIdDestroy         ocitt14_IdDestroy
#define OCIThreadIdSet             ocitt15_IdSet
#define OCIThreadIdSetNull         ocitt16_IdSetNull
#define OCIThreadIdGet             ocitt17_IdGet
#define OCIThreadIdSame            ocitt18_IdSame
#define OCIThreadIdNull            ocitt19_IdNull
#define OCIThreadHndInit           ocitt20_HndInit
#define OCIThreadHndDestroy        ocitt21_HndDestroy
#define OCIThreadCreate            ocitt22_Create
#define OCIThreadJoin              ocitt23_Join
#define OCIThreadClose             ocitt24_Close
#define OCIThreadHandleGet         ocitt25_HandleGet

/* Translation between the old and new datatypes */

#define OCISession                 ociusrh
#define OCIBind                    ocibndh
#define OCIDescribe                ocidsch
#define OCIDefine                  ocidfnh
#define OCIEnv                     ocienvh
#define OCIError                   ocierrh

#define OCICPool                   ocicpool

#define OCISPool                   ocispool
#define OCIAuthInfo                ociauthinfo


#define OCILob                     ocilobd
#define OCILobLength               ocillen
#define OCILobMode                 ocilmo
#define OCILobOffset               ociloff

#define OCILobLocator              ocilobd
#define OCIBlobLocator             ociblobl
#define OCIClobLocator             ociclobl
#define OCILobRegion               ocilregd
#define OCIBFileLocator            ocibfilel

#define OCIParam                   ocipard
#define OCIResult                  ocirstd
#define OCISnapshot                ocisnad
#define OCIServer                  ocisrvh
#define OCIStmt                    ocistmh
#define OCISvcCtx                  ocisvch
#define OCITrans                   ocitxnh
#define OCICallbackInBind          ocibicfp
#define OCICallbackOutBind         ocibocfp
#define OCICallbackDefine          ocidcfp
#define OCICallbackLobRead         ocilrfp
#define OCICallbackLobWrite        ocilwfp
#define OCICallbackLobGetDededuplicateRegions ocilgshr
#define OCISecurity                ociossh
#define OCIComplexObject           ocicorh
#define OCIComplexObjectComp       ocicord
#define OCIRowid                   ociridd

#define OCIAQDeq                   ociaqdeq                  
#define OCIAQEnq                   ociaqenq
#define OCIConnectionPoolCreate    ociconpc
#define OCIConnectionPoolDestroy   ociconpd
#define OCIEnvCreate               ocienvct
#define OCILobAssign               ociloass
#define OCILobCharSetForm          ocilocfm
#define OCILobCharSetId            ocilocid
#define OCILobDisableBuffering     ocilodbf
#define OCILobEnableBuffering      ociloebf
#define OCILobFileClose            ocilofcl
#define OCILobFileCloseAll         ocilofca
#define OCILobFileExists           ocilofex
#define OCILobFileGetName          ocilofgn
#define OCILobFileIsOpen           ocifiopn
#define OCILobFileOpen             ocilofop
#define OCILobFileSetName          ocilofsn
#define OCILobFlushBuffer          ocilofbf
#define OCILobIsEqual              ociloieq
#define OCILobLoadFromFile         ocilolff
#define OCILobLocatorIsInit        ocilolii
#define OCILobLocatorAssign        ocilolas
#define OCILogon                   ocilogon
#define OCILogon2                  ocilgon2
#define OCILogoff                  ocilgoff
#endif /* ifdef SLSHORTNAME */

/*--------------------------------------------------------------------------- 
                     PUBLIC TYPES AND CONSTANTS 
  ---------------------------------------------------------------------------*/

/*-----------------------------Handle Types----------------------------------*/
                                           /* handle types range from 1 - 49 */
#define OCI_HTYPE_FIRST          1             /* start value of handle type */
#define OCI_HTYPE_ENV            1                     /* environment handle */
#define OCI_HTYPE_ERROR          2                           /* error handle */
#define OCI_HTYPE_SVCCTX         3                         /* service handle */
#define OCI_HTYPE_STMT           4                       /* statement handle */
#define OCI_HTYPE_BIND           5                            /* bind handle */
#define OCI_HTYPE_DEFINE         6                          /* define handle */
#define OCI_HTYPE_DESCRIBE       7                        /* describe handle */
#define OCI_HTYPE_SERVER         8                          /* server handle */
#define OCI_HTYPE_SESSION        9                  /* authentication handle */
#define OCI_HTYPE_AUTHINFO      OCI_HTYPE_SESSION  /* SessionGet auth handle */
#define OCI_HTYPE_TRANS         10                     /* transaction handle */
#define OCI_HTYPE_COMPLEXOBJECT 11        /* complex object retrieval handle */
#define OCI_HTYPE_SECURITY      12                        /* security handle */
#define OCI_HTYPE_SUBSCRIPTION  13                    /* subscription handle */
#define OCI_HTYPE_DIRPATH_CTX   14                    /* direct path context */
#define OCI_HTYPE_DIRPATH_COLUMN_ARRAY 15        /* direct path column array */
#define OCI_HTYPE_DIRPATH_STREAM       16              /* direct path stream */
#define OCI_HTYPE_PROC                 17                  /* process handle */
#define OCI_HTYPE_DIRPATH_FN_CTX       18    /* direct path function context */
#define OCI_HTYPE_DIRPATH_FN_COL_ARRAY 19          /* dp object column array */
#define OCI_HTYPE_XADSESSION    20                  /* access driver session */
#define OCI_HTYPE_XADTABLE      21                    /* access driver table */
#define OCI_HTYPE_XADFIELD      22                    /* access driver field */
#define OCI_HTYPE_XADGRANULE    23                  /* access driver granule */
#define OCI_HTYPE_XADRECORD     24                   /* access driver record */
#define OCI_HTYPE_XADIO         25                      /* access driver I/O */
#define OCI_HTYPE_CPOOL         26                 /* connection pool handle */
#define OCI_HTYPE_SPOOL         27                    /* session pool handle */
#define OCI_HTYPE_ADMIN         28                           /* admin handle */
#define OCI_HTYPE_EVENT         29                        /* HA event handle */

#define OCI_HTYPE_LAST          29            /* last value of a handle type */

/*---------------------------------------------------------------------------*/


/*-------------------------Descriptor Types----------------------------------*/
                                    /* descriptor values range from 50 - 255 */
#define OCI_DTYPE_FIRST 50                 /* start value of descriptor type */
#define OCI_DTYPE_LOB 50                                     /* lob  locator */
#define OCI_DTYPE_SNAP 51                             /* snapshot descriptor */
#define OCI_DTYPE_RSET 52                           /* result set descriptor */
#define OCI_DTYPE_PARAM 53  /* a parameter descriptor obtained from ocigparm */
#define OCI_DTYPE_ROWID  54                              /* rowid descriptor */
#define OCI_DTYPE_COMPLEXOBJECTCOMP  55
                                      /* complex object retrieval descriptor */
#define OCI_DTYPE_FILE 56                                /* File Lob locator */
#define OCI_DTYPE_AQENQ_OPTIONS 57                        /* enqueue options */
#define OCI_DTYPE_AQDEQ_OPTIONS 58                        /* dequeue options */
#define OCI_DTYPE_AQMSG_PROPERTIES 59                  /* message properties */
#define OCI_DTYPE_AQAGENT 60                                     /* aq agent */
#define OCI_DTYPE_LOCATOR 61                                  /* LOB locator */
#define OCI_DTYPE_INTERVAL_YM 62                      /* Interval year month */
#define OCI_DTYPE_INTERVAL_DS 63                      /* Interval day second */
#define OCI_DTYPE_AQNFY_DESCRIPTOR  64               /* AQ notify descriptor */
#define OCI_DTYPE_DATE 65                            /* Date */
#define OCI_DTYPE_TIME 66                            /* Time */
#define OCI_DTYPE_TIME_TZ 67                         /* Time with timezone */
#define OCI_DTYPE_TIMESTAMP 68                       /* Timestamp */
#define OCI_DTYPE_TIMESTAMP_TZ 69                /* Timestamp with timezone */
#define OCI_DTYPE_TIMESTAMP_LTZ 70             /* Timestamp with local tz */
#define OCI_DTYPE_UCB           71               /* user callback descriptor */
#define OCI_DTYPE_SRVDN         72              /* server DN list descriptor */
#define OCI_DTYPE_SIGNATURE     73                              /* signature */
#define OCI_DTYPE_RESERVED_1    74              /* reserved for internal use */
#define OCI_DTYPE_AQLIS_OPTIONS 75                      /* AQ listen options */
#define OCI_DTYPE_AQLIS_MSG_PROPERTIES 76             /* AQ listen msg props */
#define OCI_DTYPE_CHDES         77     /* Top level change notification desc */
#define OCI_DTYPE_TABLE_CHDES   78      /* Table change descriptor           */
#define OCI_DTYPE_ROW_CHDES     79       /* Row change descriptor            */
#define OCI_DTYPE_CQDES  80                       /* Query change descriptor */
#define OCI_DTYPE_LOB_REGION    81            /* LOB Share region descriptor */
#define OCI_DTYPE_LAST          81        /* last value of a descriptor type */

/*---------------------------------------------------------------------------*/

/*--------------------------------LOB types ---------------------------------*/
#define OCI_TEMP_BLOB 1                /* LOB type - BLOB ------------------ */
#define OCI_TEMP_CLOB 2                /* LOB type - CLOB ------------------ */
/*---------------------------------------------------------------------------*/

/*-------------------------Object Ptr Types----------------------------------*/
#define OCI_OTYPE_NAME 1                                      /* object name */
#define OCI_OTYPE_REF  2                                       /* REF to TDO */
#define OCI_OTYPE_PTR  3                                       /* PTR to TDO */
/*---------------------------------------------------------------------------*/

/*=============================Attribute Types===============================*/
/* 
   Note: All attributes are global.  New attibutes should be added to the end
   of the list. Before you add an attribute see if an existing one can be 
   used for your handle. 

   If you see any holes please use the holes first. 
 
*/
/*===========================================================================*/


#define OCI_ATTR_FNCODE  1                          /* the OCI function code */
#define OCI_ATTR_OBJECT   2 /* is the environment initialized in object mode */
#define OCI_ATTR_NONBLOCKING_MODE  3                    /* non blocking mode */
#define OCI_ATTR_SQLCODE  4                                  /* the SQL verb */
#define OCI_ATTR_ENV  5                            /* the environment handle */
#define OCI_ATTR_SERVER 6                               /* the server handle */
#define OCI_ATTR_SESSION 7                        /* the user session handle */
#define OCI_ATTR_TRANS   8                         /* the transaction handle */
#define OCI_ATTR_ROW_COUNT   9                  /* the rows processed so far */
#define OCI_ATTR_SQLFNCODE 10               /* the SQL verb of the statement */
#define OCI_ATTR_PREFETCH_ROWS  11    /* sets the number of rows to prefetch */
#define OCI_ATTR_NESTED_PREFETCH_ROWS 12 /* the prefetch rows of nested table*/
#define OCI_ATTR_PREFETCH_MEMORY 13         /* memory limit for rows fetched */
#define OCI_ATTR_NESTED_PREFETCH_MEMORY 14   /* memory limit for nested rows */
#define OCI_ATTR_CHAR_COUNT  15 
                    /* this specifies the bind and define size in characters */
#define OCI_ATTR_PDSCL   16                          /* packed decimal scale */
#define OCI_ATTR_FSPRECISION OCI_ATTR_PDSCL   
                                          /* fs prec for datetime data types */
#define OCI_ATTR_PDPRC   17                         /* packed decimal format */
#define OCI_ATTR_LFPRECISION OCI_ATTR_PDPRC 
                                          /* fs prec for datetime data types */
#define OCI_ATTR_PARAM_COUNT 18       /* number of column in the select list */
#define OCI_ATTR_ROWID   19                                     /* the rowid */
#define OCI_ATTR_CHARSET  20                      /* the character set value */
#define OCI_ATTR_NCHAR   21                                    /* NCHAR type */
#define OCI_ATTR_USERNAME 22                           /* username attribute */
#define OCI_ATTR_PASSWORD 23                           /* password attribute */
#define OCI_ATTR_STMT_TYPE   24                            /* statement type */
#define OCI_ATTR_INTERNAL_NAME   25             /* user friendly global name */
#define OCI_ATTR_EXTERNAL_NAME   26      /* the internal name for global txn */
#define OCI_ATTR_XID     27           /* XOPEN defined global transaction id */
#define OCI_ATTR_TRANS_LOCK 28                                            /* */
#define OCI_ATTR_TRANS_NAME 29    /* string to identify a global transaction */
#define OCI_ATTR_HEAPALLOC 30                /* memory allocated on the heap */
#define OCI_ATTR_CHARSET_ID 31                           /* Character Set ID */
#define OCI_ATTR_CHARSET_FORM 32                       /* Character Set Form */
#define OCI_ATTR_MAXDATA_SIZE 33       /* Maximumsize of data on the server  */
#define OCI_ATTR_CACHE_OPT_SIZE 34              /* object cache optimal size */
#define OCI_ATTR_CACHE_MAX_SIZE 35   /* object cache maximum size percentage */
#define OCI_ATTR_PINOPTION 36             /* object cache default pin option */
#define OCI_ATTR_ALLOC_DURATION 37
                                 /* object cache default allocation duration */
#define OCI_ATTR_PIN_DURATION 38        /* object cache default pin duration */
#define OCI_ATTR_FDO          39       /* Format Descriptor object attribute */
#define OCI_ATTR_POSTPROCESSING_CALLBACK 40
                                         /* Callback to process outbind data */
#define OCI_ATTR_POSTPROCESSING_CONTEXT 41
                                 /* Callback context to process outbind data */
#define OCI_ATTR_ROWS_RETURNED 42
               /* Number of rows returned in current iter - for Bind handles */
#define OCI_ATTR_FOCBK        43              /* Failover Callback attribute */
#define OCI_ATTR_IN_V8_MODE   44 /* is the server/service context in V8 mode */
#define OCI_ATTR_LOBEMPTY     45                              /* empty lob ? */
#define OCI_ATTR_SESSLANG     46                  /* session language handle */

#define OCI_ATTR_VISIBILITY             47                     /* visibility */
#define OCI_ATTR_RELATIVE_MSGID         48            /* relative message id */
#define OCI_ATTR_SEQUENCE_DEVIATION     49             /* sequence deviation */

#define OCI_ATTR_CONSUMER_NAME          50                  /* consumer name */
#define OCI_ATTR_DEQ_MODE               51                   /* dequeue mode */
#define OCI_ATTR_NAVIGATION             52                     /* navigation */
#define OCI_ATTR_WAIT                   53                           /* wait */
#define OCI_ATTR_DEQ_MSGID              54             /* dequeue message id */

#define OCI_ATTR_PRIORITY               55                       /* priority */
#define OCI_ATTR_DELAY                  56                          /* delay */
#define OCI_ATTR_EXPIRATION             57                     /* expiration */
#define OCI_ATTR_CORRELATION            58                 /* correlation id */
#define OCI_ATTR_ATTEMPTS               59                  /* # of attempts */
#define OCI_ATTR_RECIPIENT_LIST         60                 /* recipient list */
#define OCI_ATTR_EXCEPTION_QUEUE        61           /* exception queue name */
#define OCI_ATTR_ENQ_TIME               62 /* enqueue time (only OCIAttrGet) */
#define OCI_ATTR_MSG_STATE              63/* message state (only OCIAttrGet) */
                                                   /* NOTE: 64-66 used below */
#define OCI_ATTR_AGENT_NAME             64                     /* agent name */
#define OCI_ATTR_AGENT_ADDRESS          65                  /* agent address */
#define OCI_ATTR_AGENT_PROTOCOL         66                 /* agent protocol */
#define OCI_ATTR_USER_PROPERTY          67                  /* user property */
#define OCI_ATTR_SENDER_ID              68                      /* sender id */
#define OCI_ATTR_ORIGINAL_MSGID         69            /* original message id */

#define OCI_ATTR_QUEUE_NAME             70                     /* queue name */
#define OCI_ATTR_NFY_MSGID              71                     /* message id */
#define OCI_ATTR_MSG_PROP               72             /* message properties */

#define OCI_ATTR_NUM_DML_ERRORS         73       /* num of errs in array DML */
#define OCI_ATTR_DML_ROW_OFFSET         74        /* row offset in the array */

              /* AQ array error handling uses DML method of accessing errors */
#define OCI_ATTR_AQ_NUM_ERRORS          OCI_ATTR_NUM_DML_ERRORS
#define OCI_ATTR_AQ_ERROR_INDEX         OCI_ATTR_DML_ROW_OFFSET

#define OCI_ATTR_DATEFORMAT             75     /* default date format string */
#define OCI_ATTR_BUF_ADDR               76                 /* buffer address */
#define OCI_ATTR_BUF_SIZE               77                    /* buffer size */

/* For values 78 - 80, see DirPathAPI attribute section in this file */

#define OCI_ATTR_NUM_ROWS               81 /* number of rows in column array */
                                  /* NOTE that OCI_ATTR_NUM_COLS is a column
                                   * array attribute too.
                                   */
#define OCI_ATTR_COL_COUNT              82        /* columns of column array
                                                     processed so far.       */
#define OCI_ATTR_STREAM_OFFSET          83  /* str off of last row processed */
#define OCI_ATTR_SHARED_HEAPALLOC       84    /* Shared Heap Allocation Size */

#define OCI_ATTR_SERVER_GROUP           85              /* server group name */

#define OCI_ATTR_MIGSESSION             86   /* migratable session attribute */

#define OCI_ATTR_NOCACHE                87                 /* Temporary LOBs */

#define OCI_ATTR_MEMPOOL_SIZE           88                      /* Pool Size */
#define OCI_ATTR_MEMPOOL_INSTNAME       89                  /* Instance name */
#define OCI_ATTR_MEMPOOL_APPNAME        90               /* Application name */
#define OCI_ATTR_MEMPOOL_HOMENAME       91            /* Home Directory name */
#define OCI_ATTR_MEMPOOL_MODEL          92     /* Pool Model (proc,thrd,both)*/
#define OCI_ATTR_MODES                  93                          /* Modes */

#define OCI_ATTR_SUBSCR_NAME            94           /* name of subscription */
#define OCI_ATTR_SUBSCR_CALLBACK        95            /* associated callback */
#define OCI_ATTR_SUBSCR_CTX             96    /* associated callback context */
#define OCI_ATTR_SUBSCR_PAYLOAD         97             /* associated payload */
#define OCI_ATTR_SUBSCR_NAMESPACE       98           /* associated namespace */

#define OCI_ATTR_PROXY_CREDENTIALS      99         /* Proxy user credentials */
#define OCI_ATTR_INITIAL_CLIENT_ROLES  100       /* Initial client role list */

#define OCI_ATTR_UNK              101                   /* unknown attribute */
#define OCI_ATTR_NUM_COLS         102                   /* number of columns */
#define OCI_ATTR_LIST_COLUMNS     103        /* parameter of the column list */
#define OCI_ATTR_RDBA             104           /* DBA of the segment header */
#define OCI_ATTR_CLUSTERED        105      /* whether the table is clustered */
#define OCI_ATTR_PARTITIONED      106    /* whether the table is partitioned */
#define OCI_ATTR_INDEX_ONLY       107     /* whether the table is index only */
#define OCI_ATTR_LIST_ARGUMENTS   108      /* parameter of the argument list */
#define OCI_ATTR_LIST_SUBPROGRAMS 109    /* parameter of the subprogram list */
#define OCI_ATTR_REF_TDO          110          /* REF to the type descriptor */
#define OCI_ATTR_LINK             111              /* the database link name */
#define OCI_ATTR_MIN              112                       /* minimum value */
#define OCI_ATTR_MAX              113                       /* maximum value */
#define OCI_ATTR_INCR             114                     /* increment value */
#define OCI_ATTR_CACHE            115   /* number of sequence numbers cached */
#define OCI_ATTR_ORDER            116     /* whether the sequence is ordered */
#define OCI_ATTR_HW_MARK          117                     /* high-water mark */
#define OCI_ATTR_TYPE_SCHEMA      118                  /* type's schema name */
#define OCI_ATTR_TIMESTAMP        119             /* timestamp of the object */
#define OCI_ATTR_NUM_ATTRS        120                /* number of sttributes */
#define OCI_ATTR_NUM_PARAMS       121                /* number of parameters */
#define OCI_ATTR_OBJID            122       /* object id for a table or view */
#define OCI_ATTR_PTYPE            123           /* type of info described by */
#define OCI_ATTR_PARAM            124                /* parameter descriptor */
#define OCI_ATTR_OVERLOAD_ID      125     /* overload ID for funcs and procs */
#define OCI_ATTR_TABLESPACE       126                    /* table name space */
#define OCI_ATTR_TDO              127                       /* TDO of a type */
#define OCI_ATTR_LTYPE            128                           /* list type */
#define OCI_ATTR_PARSE_ERROR_OFFSET 129                /* Parse Error offset */
#define OCI_ATTR_IS_TEMPORARY     130          /* whether table is temporary */
#define OCI_ATTR_IS_TYPED         131              /* whether table is typed */
#define OCI_ATTR_DURATION         132         /* duration of temporary table */
#define OCI_ATTR_IS_INVOKER_RIGHTS 133                  /* is invoker rights */
#define OCI_ATTR_OBJ_NAME         134           /* top level schema obj name */
#define OCI_ATTR_OBJ_SCHEMA       135                         /* schema name */
#define OCI_ATTR_OBJ_ID           136          /* top level schema object id */

/* For values 137 - 141, see DirPathAPI attribute section in this file */


#define OCI_ATTR_TRANS_TIMEOUT              142       /* transaction timeout */
#define OCI_ATTR_SERVER_STATUS              143/* state of the server handle */
#define OCI_ATTR_STATEMENT                  144 /* statement txt in stmt hdl */

/* For value 145, see DirPathAPI attribute section in this file */

#define OCI_ATTR_DEQCOND                    146         /* dequeue condition */
#define OCI_ATTR_RESERVED_2                 147                  /* reserved */

  
#define OCI_ATTR_SUBSCR_RECPT               148 /* recepient of subscription */
#define OCI_ATTR_SUBSCR_RECPTPROTO          149    /* protocol for recepient */

/* For values 150 - 151, see DirPathAPI attribute section in this file */

#define OCI_ATTR_LDAP_HOST       153              /* LDAP host to connect to */
#define OCI_ATTR_LDAP_PORT       154              /* LDAP port to connect to */
#define OCI_ATTR_BIND_DN         155                              /* bind DN */
#define OCI_ATTR_LDAP_CRED       156       /* credentials to connect to LDAP */
#define OCI_ATTR_WALL_LOC        157               /* client wallet location */
#define OCI_ATTR_LDAP_AUTH       158           /* LDAP authentication method */
#define OCI_ATTR_LDAP_CTX        159        /* LDAP adminstration context DN */
#define OCI_ATTR_SERVER_DNS      160      /* list of registration server DNs */

#define OCI_ATTR_DN_COUNT        161             /* the number of server DNs */
#define OCI_ATTR_SERVER_DN       162                  /* server DN attribute */

#define OCI_ATTR_MAXCHAR_SIZE               163     /* max char size of data */

#define OCI_ATTR_CURRENT_POSITION           164 /* for scrollable result sets*/

/* Added to get attributes for ref cursor to statement handle */
#define OCI_ATTR_RESERVED_3                 165                  /* reserved */
#define OCI_ATTR_RESERVED_4                 166                  /* reserved */

/* For value 167, see DirPathAPI attribute section in this file */

#define OCI_ATTR_DIGEST_ALGO                168          /* digest algorithm */
#define OCI_ATTR_CERTIFICATE                169               /* certificate */
#define OCI_ATTR_SIGNATURE_ALGO             170       /* signature algorithm */
#define OCI_ATTR_CANONICAL_ALGO             171    /* canonicalization algo. */
#define OCI_ATTR_PRIVATE_KEY                172               /* private key */
#define OCI_ATTR_DIGEST_VALUE               173              /* digest value */
#define OCI_ATTR_SIGNATURE_VAL              174           /* signature value */
#define OCI_ATTR_SIGNATURE                  175                 /* signature */

/* attributes for setting OCI stmt caching specifics in svchp */
#define OCI_ATTR_STMTCACHESIZE              176     /* size of the stm cache */

/* --------------------------- Connection Pool Attributes ------------------ */
#define OCI_ATTR_CONN_NOWAIT               178
#define OCI_ATTR_CONN_BUSY_COUNT            179
#define OCI_ATTR_CONN_OPEN_COUNT            180
#define OCI_ATTR_CONN_TIMEOUT               181
#define OCI_ATTR_STMT_STATE                 182
#define OCI_ATTR_CONN_MIN                   183
#define OCI_ATTR_CONN_MAX                   184
#define OCI_ATTR_CONN_INCR                  185

/* For value 187, see DirPathAPI attribute section in this file */

#define OCI_ATTR_NUM_OPEN_STMTS             188     /* open stmts in session */
#define OCI_ATTR_DESCRIBE_NATIVE            189  /* get native info via desc */

#define OCI_ATTR_BIND_COUNT                 190   /* number of bind postions */
#define OCI_ATTR_HANDLE_POSITION            191 /* pos of bind/define handle */
#define OCI_ATTR_RESERVED_5                 192                 /* reserverd */
#define OCI_ATTR_SERVER_BUSY                193 /* call in progress on server*/

/* For value 194, see DirPathAPI attribute section in this file */

/* notification presentation for recipient */
#define OCI_ATTR_SUBSCR_RECPTPRES           195
#define OCI_ATTR_TRANSFORMATION             196 /* AQ message transformation */

#define OCI_ATTR_ROWS_FETCHED               197 /* rows fetched in last call */

/* --------------------------- Snapshot attributes ------------------------- */
#define OCI_ATTR_SCN_BASE                   198             /* snapshot base */
#define OCI_ATTR_SCN_WRAP                   199             /* snapshot wrap */

/* --------------------------- Miscellanous attributes --------------------- */
#define OCI_ATTR_RESERVED_6                 200                  /* reserved */
#define OCI_ATTR_READONLY_TXN               201           /* txn is readonly */
#define OCI_ATTR_RESERVED_7                 202                  /* reserved */
#define OCI_ATTR_ERRONEOUS_COLUMN           203 /* position of erroneous col */
#define OCI_ATTR_RESERVED_8                 204                  /* reserved */
#define OCI_ATTR_ASM_VOL_SPRT               205     /* ASM volume supported? */

/* For value 206, see DirPathAPI attribute section in this file */

#define OCI_ATTR_INST_TYPE                  207      /* oracle instance type */
/******USED attribute 208 for  OCI_ATTR_SPOOL_STMTCACHESIZE*******************/

#define OCI_ATTR_ENV_UTF16                  209     /* is env in utf16 mode? */
#define OCI_ATTR_RESERVED_9                 210                  /* reserved */
#define OCI_ATTR_RESERVED_10                211                  /* reserved */

/* For values 212 and 213, see DirPathAPI attribute section in this file */

#define OCI_ATTR_RESERVED_12                214                  /* reserved */
#define OCI_ATTR_RESERVED_13                215                  /* reserved */
#define OCI_ATTR_IS_EXTERNAL                216 /* whether table is external */


/* -------------------------- Statement Handle Attributes ------------------ */

#define OCI_ATTR_RESERVED_15                217                  /* reserved */
#define OCI_ATTR_STMT_IS_RETURNING          218 /* stmt has returning clause */
#define OCI_ATTR_RESERVED_16                219                  /* reserved */
#define OCI_ATTR_RESERVED_17                220                  /* reserved */
#define OCI_ATTR_RESERVED_18                221                  /* reserved */

/* --------------------------- session attributes ---------------------------*/
#define OCI_ATTR_RESERVED_19                222                  /* reserved */
#define OCI_ATTR_RESERVED_20                223                  /* reserved */
#define OCI_ATTR_CURRENT_SCHEMA             224            /* Current Schema */
#define OCI_ATTR_RESERVED_21                415                  /* reserved */

/* ------------------------- notification subscription ----------------------*/
#define OCI_ATTR_SUBSCR_QOSFLAGS            225                 /* QOS flags */
#define OCI_ATTR_SUBSCR_PAYLOADCBK          226          /* Payload callback */
#define OCI_ATTR_SUBSCR_TIMEOUT             227                   /* Timeout */
#define OCI_ATTR_SUBSCR_NAMESPACE_CTX       228         /* Namespace context */
#define OCI_ATTR_SUBSCR_CQ_QOSFLAGS         229
                              /* change notification (CQ) specific QOS flags */
#define OCI_ATTR_SUBSCR_CQ_REGID            230
                                      /* change notification registration id */
#define OCI_ATTR_SUBSCR_NTFN_GROUPING_CLASS        231/* ntfn grouping class */
#define OCI_ATTR_SUBSCR_NTFN_GROUPING_VALUE        232/* ntfn grouping value */
#define OCI_ATTR_SUBSCR_NTFN_GROUPING_TYPE         233 /* ntfn grouping type */
#define OCI_ATTR_SUBSCR_NTFN_GROUPING_START_TIME   234/* ntfn grp start time */
#define OCI_ATTR_SUBSCR_NTFN_GROUPING_REPEAT_COUNT 235 /* ntfn grp rep count */
#define OCI_ATTR_AQ_NTFN_GROUPING_MSGID_ARRAY      236 /* aq grp msgid array */
#define OCI_ATTR_AQ_NTFN_GROUPING_COUNT            237  /* ntfns recd in grp */

/* ----------------------- row callback attributes ------------------------- */
#define OCI_ATTR_BIND_ROWCBK                301         /* bind row callback */
#define OCI_ATTR_BIND_ROWCTX                302 /* ctx for bind row callback */
#define OCI_ATTR_SKIP_BUFFER                303  /* skip buffer in array ops */

/*-----  Db Change Notification (CQ) statement handle attributes------------ */
#define OCI_ATTR_CQ_QUERYID               304
/* ------------- DB Change Notification reg handle attributes ---------------*/
#define OCI_ATTR_CHNF_TABLENAMES          401 /* out: array of table names   */
#define OCI_ATTR_CHNF_ROWIDS              402     /* in: rowids needed */ 
#define OCI_ATTR_CHNF_OPERATIONS          403
                                        /* in: notification operation filter*/ 
#define OCI_ATTR_CHNF_CHANGELAG           404
                                           /* txn lag between notifications  */

/* DB Change: Notification Descriptor attributes -----------------------*/
#define OCI_ATTR_CHDES_DBNAME            405    /* source database    */
#define OCI_ATTR_CHDES_NFYTYPE           406    /* notification type flags */
#define OCI_ATTR_CHDES_XID               407    /* XID  of the transaction */
#define OCI_ATTR_CHDES_TABLE_CHANGES     408/* array of table chg descriptors*/

#define OCI_ATTR_CHDES_TABLE_NAME        409    /* table name */
#define OCI_ATTR_CHDES_TABLE_OPFLAGS     410    /* table operation flags */
#define OCI_ATTR_CHDES_TABLE_ROW_CHANGES 411   /* array of changed rows   */
#define OCI_ATTR_CHDES_ROW_ROWID         412   /* rowid of changed row    */
#define OCI_ATTR_CHDES_ROW_OPFLAGS       413   /* row operation flags     */

/* Statement handle attribute for db change notification */
#define OCI_ATTR_CHNF_REGHANDLE          414   /* IN: subscription handle  */
#define OCI_ATTR_NETWORK_FILE_DESC       415   /* network file descriptor */

/* client name for single session proxy */
#define OCI_ATTR_PROXY_CLIENT            416

/* 415 is already taken - see OCI_ATTR_RESERVED_21 */

/* TDE attributes on the Table */
#define OCI_ATTR_TABLE_ENC         417/* does table have any encrypt columns */
#define OCI_ATTR_TABLE_ENC_ALG     418         /* Table encryption Algorithm */
#define OCI_ATTR_TABLE_ENC_ALG_ID  419 /* Internal Id of encryption Algorithm*/

/* -------- Attributes related to Statement cache callback ----------------- */
#define OCI_ATTR_STMTCACHE_CBKCTX           420    /* opaque context on stmt */
#define OCI_ATTR_STMTCACHE_CBK              421 /* callback fn for stmtcache */

/*---------------- Query change descriptor attributes -----------------------*/
#define OCI_ATTR_CQDES_OPERATION 422
#define OCI_ATTR_CQDES_TABLE_CHANGES 423
#define OCI_ATTR_CQDES_QUERYID 424


#define OCI_ATTR_CHDES_QUERIES 425 /* Top level change desc array of queries */
  
/* Please use from 143 */

/* -------- Internal statement attributes ------- */
#define OCI_ATTR_RESERVED_26                422      

/* 424 is used by OCI_ATTR_DRIVER_NAME */
/* --------- Attributes added to support server side session pool ---------- */
#define OCI_ATTR_CONNECTION_CLASS  425
#define OCI_ATTR_PURITY            426

#define OCI_ATTR_PURITY_DEFAULT    0x00
#define OCI_ATTR_PURITY_NEW        0x01
#define OCI_ATTR_PURITY_SELF       0x02

/* -------- Attributes for Times Ten --------------------------*/
#define OCI_ATTR_RESERVED_28               426                   /* reserved */
#define OCI_ATTR_RESERVED_29               427                   /* reserved */
#define OCI_ATTR_RESERVED_30               428                   /* reserved */
#define OCI_ATTR_RESERVED_31               429                   /* reserved */
#define OCI_ATTR_RESERVED_32               430                   /* reserved */
#define OCI_ATTR_RESERVED_41               454                   /* reserved */


/* ----------- Reserve internal attributes for workload replay  ------------ */
#define OCI_ATTR_RESERVED_33               433
#define OCI_ATTR_RESERVED_34               434

/* statement attribute */
#define OCI_ATTR_RESERVED_36               444

/* -------- Attributes for Network Session Time Out--------------------------*/
#define OCI_ATTR_SEND_TIMEOUT               435           /* NS send timeout */
#define OCI_ATTR_RECEIVE_TIMEOUT            436        /* NS receive timeout */

/*--------- Attributes related to LOB prefetch------------------------------ */
#define OCI_ATTR_DEFAULT_LOBPREFETCH_SIZE     438   /* default prefetch size */
#define OCI_ATTR_LOBPREFETCH_SIZE             439           /* prefetch size */
#define OCI_ATTR_LOBPREFETCH_LENGTH           440 /* prefetch length & chunk */

/*--------- Attributes related to LOB Deduplicate Regions ------------------ */
#define OCI_ATTR_LOB_REGION_PRIMARY       442         /* Primary LOB Locator */
#define OCI_ATTR_LOB_REGION_PRIMOFF       443     /* Offset into Primary LOB */ 
#define OCI_ATTR_LOB_REGION_OFFSET        445               /* Region Offset */
#define OCI_ATTR_LOB_REGION_LENGTH        446   /* Region Length Bytes/Chars */
#define OCI_ATTR_LOB_REGION_MIME          447            /* Region mime type */

/*--------------------Attribute to fetch ROWID ------------------------------*/
#define OCI_ATTR_FETCH_ROWID                 448

/* server attribute */
#define OCI_ATTR_RESERVED_37              449

/* DB Change: Event types ---------------*/
#define OCI_EVENT_NONE 0x0                                           /* None */
#define OCI_EVENT_STARTUP 0x1                            /* Startup database */
#define OCI_EVENT_SHUTDOWN 0x2                          /* Shutdown database */
#define OCI_EVENT_SHUTDOWN_ANY 0x3                       /* Startup instance */
#define OCI_EVENT_DROP_DB 0x4                            /* Drop database    */
#define OCI_EVENT_DEREG 0x5                     /* Subscription deregistered */
#define OCI_EVENT_OBJCHANGE 0x6                /* Object change notification */
#define OCI_EVENT_QUERYCHANGE 0x7                     /* query result change */

/* DB Change: Operation types -----------*/
#define OCI_OPCODE_ALLROWS 0x1                      /* all rows invalidated  */
#define OCI_OPCODE_ALLOPS 0x0                /* interested in all operations */
#define OCI_OPCODE_INSERT 0x2                                     /*  INSERT */
#define OCI_OPCODE_UPDATE 0x4                                     /*  UPDATE */
#define OCI_OPCODE_DELETE 0x8                                      /* DELETE */
#define OCI_OPCODE_ALTER 0x10                                       /* ALTER */
#define OCI_OPCODE_DROP 0x20                                   /* DROP TABLE */
#define OCI_OPCODE_UNKNOWN 0x40                           /* GENERIC/ UNKNOWN*/

/* -------- client side character and national character set ids ----------- */
#define OCI_ATTR_ENV_CHARSET_ID   OCI_ATTR_CHARSET_ID   /* charset id in env */
#define OCI_ATTR_ENV_NCHARSET_ID  OCI_ATTR_NCHARSET_ID /* ncharset id in env */

/* ----------------------- ha event callback attributes -------------------- */
#define OCI_ATTR_EVTCBK                     304               /* ha callback */
#define OCI_ATTR_EVTCTX                     305       /* ctx for ha callback */

/* ------------------ User memory attributes (all handles) ----------------- */
#define OCI_ATTR_USER_MEMORY               306     /* pointer to user memory */

/* ------- unauthorised access and user action auditing banners ------------ */
#define OCI_ATTR_ACCESS_BANNER              307             /* access banner */
#define OCI_ATTR_AUDIT_BANNER               308              /* audit banner */

/* ----------------- port no attribute in subscription handle  ------------- */
#define OCI_ATTR_SUBSCR_PORTNO              390  /* port no to listen        */

#define OCI_ATTR_RESERVED_35                437

/*------------- Supported Values for protocol for recepient -----------------*/
#define OCI_SUBSCR_PROTO_OCI                0                         /* oci */
#define OCI_SUBSCR_PROTO_MAIL               1                        /* mail */
#define OCI_SUBSCR_PROTO_SERVER             2                      /* server */
#define OCI_SUBSCR_PROTO_HTTP               3                        /* http */
#define OCI_SUBSCR_PROTO_MAX                4       /* max current protocols */

/*------------- Supported Values for presentation for recepient -------------*/
#define OCI_SUBSCR_PRES_DEFAULT             0                     /* default */
#define OCI_SUBSCR_PRES_XML                 1                         /* xml */
#define OCI_SUBSCR_PRES_MAX                 2   /* max current presentations */

/*------------- Supported QOS values for notification registrations ---------*/
#define OCI_SUBSCR_QOS_RELIABLE             0x01                 /* reliable */
#define OCI_SUBSCR_QOS_PAYLOAD              0x02         /* payload delivery */
#define OCI_SUBSCR_QOS_REPLICATE            0x04    /* replicate to director */
#define OCI_SUBSCR_QOS_SECURE               0x08  /* secure payload delivery */
#define OCI_SUBSCR_QOS_PURGE_ON_NTFN        0x10      /* purge on first ntfn */
#define OCI_SUBSCR_QOS_MULTICBK             0x20  /* multi instance callback */

/* ----QOS flags specific to change notification/ continuous queries CQ -----*/
#define OCI_SUBSCR_CQ_QOS_QUERY  0x01            /* query level notification */
#define OCI_SUBSCR_CQ_QOS_BEST_EFFORT 0x02       /* best effort notification */
#define OCI_SUBSCR_CQ_QOS_CLQRYCACHE 0x04            /* client query caching */

/*------------- Supported Values for notification grouping class ------------*/
#define OCI_SUBSCR_NTFN_GROUPING_CLASS_TIME 1                        /* time */

/*------------- Supported Values for notification grouping type -------------*/
#define OCI_SUBSCR_NTFN_GROUPING_TYPE_SUMMARY 1                   /* summary */
#define OCI_SUBSCR_NTFN_GROUPING_TYPE_LAST    2                      /* last */

/* ----- Temporary attribute value for UCS2/UTF16 character set ID -------- */ 
#define OCI_UCS2ID            1000                        /* UCS2 charset ID */
#define OCI_UTF16ID           1000                       /* UTF16 charset ID */

/*============================== End OCI Attribute Types ====================*/

/*---------------- Server Handle Attribute Values ---------------------------*/

/* OCI_ATTR_SERVER_STATUS */
#define OCI_SERVER_NOT_CONNECTED        0x0 
#define OCI_SERVER_NORMAL               0x1 

/*---------------------------------------------------------------------------*/

/*------------------------- Supported Namespaces  ---------------------------*/
#define OCI_SUBSCR_NAMESPACE_ANONYMOUS   0            /* Anonymous Namespace */
#define OCI_SUBSCR_NAMESPACE_AQ          1                /* Advanced Queues */
#define OCI_SUBSCR_NAMESPACE_DBCHANGE    2            /* change notification */
#define OCI_SUBSCR_NAMESPACE_MAX         3          /* Max Name Space Number */


/*-------------------------Credential Types----------------------------------*/
#define OCI_CRED_RDBMS      1                  /* database username/password */
#define OCI_CRED_EXT        2             /* externally provided credentials */
#define OCI_CRED_PROXY      3                        /* proxy authentication */
#define OCI_CRED_RESERVED_1 4                                    /* reserved */
#define OCI_CRED_RESERVED_2 5                                    /* reserved */
/*---------------------------------------------------------------------------*/

/*------------------------Error Return Values--------------------------------*/
#define OCI_SUCCESS 0                      /* maps to SQL_SUCCESS of SAG CLI */
#define OCI_SUCCESS_WITH_INFO 1             /* maps to SQL_SUCCESS_WITH_INFO */
#define OCI_RESERVED_FOR_INT_USE 200                            /* reserved */ 
#define OCI_NO_DATA 100                               /* maps to SQL_NO_DATA */
#define OCI_ERROR -1                                    /* maps to SQL_ERROR */
#define OCI_INVALID_HANDLE -2                  /* maps to SQL_INVALID_HANDLE */
#define OCI_NEED_DATA 99                            /* maps to SQL_NEED_DATA */
#define OCI_STILL_EXECUTING -3123                   /* OCI would block error */
/*---------------------------------------------------------------------------*/

/*--------------------- User Callback Return Values -------------------------*/
#define OCI_CONTINUE -24200    /* Continue with the body of the OCI function */
#define OCI_ROWCBK_DONE   -24201              /* done with user row callback */
/*---------------------------------------------------------------------------*/

/*------------------DateTime and Interval check Error codes------------------*/

/* DateTime Error Codes used by OCIDateTimeCheck() */
#define   OCI_DT_INVALID_DAY         0x1                          /* Bad day */
#define   OCI_DT_DAY_BELOW_VALID     0x2      /* Bad DAy Low/high bit (1=low)*/
#define   OCI_DT_INVALID_MONTH       0x4                       /*  Bad MOnth */
#define   OCI_DT_MONTH_BELOW_VALID   0x8   /* Bad MOnth Low/high bit (1=low) */
#define   OCI_DT_INVALID_YEAR        0x10                        /* Bad YeaR */
#define   OCI_DT_YEAR_BELOW_VALID    0x20  /*  Bad YeaR Low/high bit (1=low) */
#define   OCI_DT_INVALID_HOUR        0x40                       /*  Bad HouR */
#define   OCI_DT_HOUR_BELOW_VALID    0x80   /* Bad HouR Low/high bit (1=low) */
#define   OCI_DT_INVALID_MINUTE      0x100                     /* Bad MiNute */
#define   OCI_DT_MINUTE_BELOW_VALID  0x200 /*Bad MiNute Low/high bit (1=low) */
#define   OCI_DT_INVALID_SECOND      0x400                    /*  Bad SeCond */
#define   OCI_DT_SECOND_BELOW_VALID  0x800  /*bad second Low/high bit (1=low)*/
#define   OCI_DT_DAY_MISSING_FROM_1582 0x1000     
                                 /*  Day is one of those "missing" from 1582 */
#define   OCI_DT_YEAR_ZERO           0x2000       /* Year may not equal zero */
#define   OCI_DT_INVALID_TIMEZONE    0x4000                 /*  Bad Timezone */
#define   OCI_DT_INVALID_FORMAT      0x8000         /* Bad date format input */


/* Interval Error Codes used by OCIInterCheck() */
#define   OCI_INTER_INVALID_DAY         0x1                       /* Bad day */
#define   OCI_INTER_DAY_BELOW_VALID     0x2  /* Bad DAy Low/high bit (1=low) */
#define   OCI_INTER_INVALID_MONTH       0x4                     /* Bad MOnth */
#define   OCI_INTER_MONTH_BELOW_VALID   0x8 /*Bad MOnth Low/high bit (1=low) */
#define   OCI_INTER_INVALID_YEAR        0x10                     /* Bad YeaR */
#define   OCI_INTER_YEAR_BELOW_VALID    0x20 /*Bad YeaR Low/high bit (1=low) */
#define   OCI_INTER_INVALID_HOUR        0x40                     /* Bad HouR */
#define   OCI_INTER_HOUR_BELOW_VALID    0x80 /*Bad HouR Low/high bit (1=low) */
#define   OCI_INTER_INVALID_MINUTE      0x100                  /* Bad MiNute */
#define   OCI_INTER_MINUTE_BELOW_VALID  0x200 
                                            /*Bad MiNute Low/high bit(1=low) */
#define   OCI_INTER_INVALID_SECOND      0x400                  /* Bad SeCond */
#define   OCI_INTER_SECOND_BELOW_VALID  0x800   
                                            /*bad second Low/high bit(1=low) */
#define   OCI_INTER_INVALID_FRACSEC     0x1000      /* Bad Fractional second */
#define   OCI_INTER_FRACSEC_BELOW_VALID 0x2000  
                                           /* Bad fractional second Low/High */


/*------------------------Parsing Syntax Types-------------------------------*/
#define OCI_V7_SYNTAX 2       /* V815 language - for backwards compatibility */
#define OCI_V8_SYNTAX 3       /* V815 language - for backwards compatibility */
#define OCI_NTV_SYNTAX 1    /* Use what so ever is the native lang of server */
                     /* these values must match the values defined in kpul.h */
/*---------------------------------------------------------------------------*/

/*------------------------(Scrollable Cursor) Fetch Options------------------- 
 * For non-scrollable cursor, the only valid (and default) orientation is 
 * OCI_FETCH_NEXT
 */
#define OCI_FETCH_CURRENT    0x00000001      /* refetching current position  */
#define OCI_FETCH_NEXT       0x00000002                          /* next row */
#define OCI_FETCH_FIRST      0x00000004       /* first row of the result set */
#define OCI_FETCH_LAST       0x00000008    /* the last row of the result set */
#define OCI_FETCH_PRIOR      0x00000010  /* previous row relative to current */
#define OCI_FETCH_ABSOLUTE   0x00000020        /* absolute offset from first */
#define OCI_FETCH_RELATIVE   0x00000040        /* offset relative to current */
#define OCI_FETCH_RESERVED_1 0x00000080                          /* reserved */
#define OCI_FETCH_RESERVED_2 0x00000100                          /* reserved */
#define OCI_FETCH_RESERVED_3 0x00000200                          /* reserved */
#define OCI_FETCH_RESERVED_4 0x00000400                          /* reserved */
#define OCI_FETCH_RESERVED_5 0x00000800                          /* reserved */

/*---------------------------------------------------------------------------*/

/*------------------------Bind and Define Options----------------------------*/
#define OCI_SB2_IND_PTR       0x00000001                           /* unused */
#define OCI_DATA_AT_EXEC      0x00000002             /* data at execute time */
#define OCI_DYNAMIC_FETCH     0x00000002                /* fetch dynamically */
#define OCI_PIECEWISE         0x00000004          /* piecewise DMLs or fetch */
#define OCI_DEFINE_RESERVED_1 0x00000008                         /* reserved */
#define OCI_BIND_RESERVED_2   0x00000010                         /* reserved */
#define OCI_DEFINE_RESERVED_2 0x00000020                         /* reserved */
#define OCI_BIND_SOFT         0x00000040              /* soft bind or define */
#define OCI_DEFINE_SOFT       0x00000080              /* soft bind or define */
#define OCI_BIND_RESERVED_3   0x00000100                         /* reserved */
#define OCI_IOV               0x00000200   /* For scatter gather bind/define */
/*---------------------------------------------------------------------------*/

/*-----------------------------  Various Modes ------------------------------*/
#define OCI_DEFAULT         0x00000000 
                          /* the default value for parameters and attributes */
/*-------------OCIInitialize Modes / OCICreateEnvironment Modes -------------*/
#define OCI_THREADED        0x00000001      /* appl. in threaded environment */
#define OCI_OBJECT          0x00000002  /* application in object environment */
#define OCI_EVENTS          0x00000004  /* application is enabled for events */
#define OCI_RESERVED1       0x00000008                           /* reserved */
#define OCI_SHARED          0x00000010  /* the application is in shared mode */
#define OCI_RESERVED2       0x00000020                           /* reserved */
/* The following *TWO* are only valid for OCICreateEnvironment call */
#define OCI_NO_UCB          0x00000040 /* No user callback called during ini */
#define OCI_NO_MUTEX        0x00000080 /* the environment handle will not be */
                                         /*  protected by a mutex internally */
#define OCI_SHARED_EXT      0x00000100              /* Used for shared forms */
/************************** 0x00000200 free **********************************/
#define OCI_ALWAYS_BLOCKING 0x00000400    /* all connections always blocking */
/************************** 0x00000800 free **********************************/
#define OCI_USE_LDAP        0x00001000            /* allow  LDAP connections */
#define OCI_REG_LDAPONLY    0x00002000              /* only register to LDAP */
#define OCI_UTF16           0x00004000        /* mode for all UTF16 metadata */
#define OCI_AFC_PAD_ON      0x00008000 
                             /* turn on AFC blank padding when rlenp present */
#define OCI_ENVCR_RESERVED3 0x00010000                           /* reserved */
#define OCI_NEW_LENGTH_SEMANTICS  0x00020000   /* adopt new length semantics */
       /* the new length semantics, always bytes, is used by OCIEnvNlsCreate */
#define OCI_NO_MUTEX_STMT   0x00040000           /* Do not mutex stmt handle */
#define OCI_MUTEX_ENV_ONLY  0x00080000  /* Mutex only the environment handle */
#define OCI_SUPPRESS_NLS_VALIDATION   0x00100000  /* suppress nls validation */
  /* nls validation suppression is on by default;
     use OCI_ENABLE_NLS_VALIDATION to disable it */
#define OCI_MUTEX_TRY                 0x00200000    /* try and acquire mutex */
#define OCI_NCHAR_LITERAL_REPLACE_ON  0x00400000 /* nchar literal replace on */
#define OCI_NCHAR_LITERAL_REPLACE_OFF 0x00800000 /* nchar literal replace off*/
#define OCI_ENABLE_NLS_VALIDATION     0x01000000    /* enable nls validation */


/*---------------------------------------------------------------------------*/
/*------------------------OCIConnectionpoolCreate Modes----------------------*/

#define OCI_CPOOL_REINITIALIZE 0x111

/*---------------------------------------------------------------------------*/
/*--------------------------------- OCILogon2 Modes -------------------------*/

#define OCI_LOGON2_SPOOL       0x0001     /* Use session pool */
#define OCI_LOGON2_CPOOL       OCI_CPOOL  /* Use connection pool */
#define OCI_LOGON2_STMTCACHE   0x0004     /* Use Stmt Caching */
#define OCI_LOGON2_PROXY       0x0008     /* Proxy authentiaction */

/*---------------------------------------------------------------------------*/
/*------------------------- OCISessionPoolCreate Modes ----------------------*/

#define OCI_SPC_REINITIALIZE 0x0001   /* Reinitialize the session pool */
#define OCI_SPC_HOMOGENEOUS  0x0002   /* Session pool is homogeneneous */
#define OCI_SPC_STMTCACHE    0x0004   /* Session pool has stmt cache */
#define OCI_SPC_NO_RLB       0x0008 /* Do not enable Runtime load balancing. */ 

/*---------------------------------------------------------------------------*/
/*--------------------------- OCISessionGet Modes ---------------------------*/

#define OCI_SESSGET_SPOOL      0x0001     /* SessionGet called in SPOOL mode */
#define OCI_SESSGET_CPOOL      OCI_CPOOL  /* SessionGet called in CPOOL mode */
#define OCI_SESSGET_STMTCACHE  0x0004                 /* Use statement cache */
#define OCI_SESSGET_CREDPROXY  0x0008     /* SessionGet called in proxy mode */
#define OCI_SESSGET_CREDEXT    0x0010     
#define OCI_SESSGET_SPOOL_MATCHANY 0x0020
#define OCI_SESSGET_PURITY_NEW     0x0040 
#define OCI_SESSGET_PURITY_SELF    0x0080 

/*---------------------------------------------------------------------------*/
/*------------------------ATTR Values for Session Pool-----------------------*/
/* Attribute values for OCI_ATTR_SPOOL_GETMODE */
#define OCI_SPOOL_ATTRVAL_WAIT     0         /* block till you get a session */
#define OCI_SPOOL_ATTRVAL_NOWAIT   1    /* error out if no session avaliable */
#define OCI_SPOOL_ATTRVAL_FORCEGET 2  /* get session even if max is exceeded */

/*---------------------------------------------------------------------------*/
/*--------------------------- OCISessionRelease Modes -----------------------*/

#define OCI_SESSRLS_DROPSESS 0x0001                    /* Drop the Session */
#define OCI_SESSRLS_RETAG    0x0002                   /* Retag the session */

/*---------------------------------------------------------------------------*/
/*----------------------- OCISessionPoolDestroy Modes -----------------------*/

#define OCI_SPD_FORCE        0x0001       /* Force the sessions to terminate. 
                                             Even if there are some busy 
                                             sessions close them */
 
/*---------------------------------------------------------------------------*/
/*----------------------------- Statement States ----------------------------*/

#define OCI_STMT_STATE_INITIALIZED  0x0001
#define OCI_STMT_STATE_EXECUTED     0x0002
#define OCI_STMT_STATE_END_OF_FETCH 0x0003

/*---------------------------------------------------------------------------*/

/*----------------------------- OCIMemStats Modes ---------------------------*/
#define OCI_MEM_INIT        0x01 
#define OCI_MEM_CLN         0x02 
#define OCI_MEM_FLUSH       0x04 
#define OCI_DUMP_HEAP       0x80

#define OCI_CLIENT_STATS    0x10 
#define OCI_SERVER_STATS    0x20 

/*----------------------------- OCIEnvInit Modes ----------------------------*/
/* NOTE: NO NEW MODES SHOULD BE ADDED HERE BECAUSE THE RECOMMENDED METHOD 
 * IS TO USE THE NEW OCICreateEnvironment MODES.
 */
#define OCI_ENV_NO_UCB 0x01         /* A user callback will not be called in
                                       OCIEnvInit() */
#define OCI_ENV_NO_MUTEX 0x08 /* the environment handle will not be protected
                                 by a mutex internally */

/*---------------------------------------------------------------------------*/

/*------------------------ Prepare Modes ------------------------------------*/
#define OCI_NO_SHARING        0x01      /* turn off statement handle sharing */
#define OCI_PREP_RESERVED_1   0x02                               /* reserved */
#define OCI_PREP_AFC_PAD_ON   0x04          /* turn on blank padding for AFC */
#define OCI_PREP_AFC_PAD_OFF  0x08         /* turn off blank padding for AFC */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/

/*----------------------- Execution Modes -----------------------------------*/
#define OCI_BATCH_MODE             0x00000001 /* batch the oci stmt for exec */
#define OCI_EXACT_FETCH            0x00000002  /* fetch exact rows specified */
/* #define                         0x00000004                      available */
#define OCI_STMT_SCROLLABLE_READONLY \
                                   0x00000008 /* if result set is scrollable */
#define OCI_DESCRIBE_ONLY          0x00000010 /* only describe the statement */
#define OCI_COMMIT_ON_SUCCESS      0x00000020  /* commit, if successful exec */
#define OCI_NON_BLOCKING           0x00000040                /* non-blocking */
#define OCI_BATCH_ERRORS           0x00000080  /* batch errors in array dmls */
#define OCI_PARSE_ONLY             0x00000100    /* only parse the statement */
#define OCI_EXACT_FETCH_RESERVED_1 0x00000200                    /* reserved */
#define OCI_SHOW_DML_WARNINGS      0x00000400   
         /* return OCI_SUCCESS_WITH_INFO for delete/update w/no where clause */
#define OCI_EXEC_RESERVED_2        0x00000800                    /* reserved */
#define OCI_DESC_RESERVED_1        0x00001000                    /* reserved */
#define OCI_EXEC_RESERVED_3        0x00002000                    /* reserved */
#define OCI_EXEC_RESERVED_4        0x00004000                    /* reserved */
#define OCI_EXEC_RESERVED_5        0x00008000                    /* reserved */
#define OCI_EXEC_RESERVED_6        0x00010000                    /* reserved */
#define OCI_RESULT_CACHE           0x00020000   /* hint to use query caching */
#define OCI_NO_RESULT_CACHE        0x00040000  /*hint to bypass query caching*/

/*---------------------------------------------------------------------------*/

/*------------------------Authentication Modes-------------------------------*/
#define OCI_MIGRATE         0x00000001            /* migratable auth context */
#define OCI_SYSDBA          0x00000002           /* for SYSDBA authorization */
#define OCI_SYSOPER         0x00000004          /* for SYSOPER authorization */
#define OCI_PRELIM_AUTH     0x00000008      /* for preliminary authorization */
#define OCIP_ICACHE         0x00000010             /* Private OCI cache mode */
#define OCI_AUTH_RESERVED_1 0x00000020                           /* reserved */
#define OCI_STMT_CACHE      0x00000040            /* enable OCI Stmt Caching */
#define OCI_STATELESS_CALL  0x00000080         /* stateless at call boundary */
#define OCI_STATELESS_TXN   0x00000100          /* stateless at txn boundary */
#define OCI_STATELESS_APP   0x00000200    /* stateless at user-specified pts */
#define OCI_AUTH_RESERVED_2 0x00000400                           /* reserved */
#define OCI_AUTH_RESERVED_3 0x00000800                           /* reserved */
#define OCI_AUTH_RESERVED_4 0x00001000                           /* reserved */
#define OCI_AUTH_RESERVED_5 0x00002000                           /* reserved */
#define OCI_SYSASM          0x00008000           /* for SYSASM authorization */
#define OCI_AUTH_RESERVED_6 0x00010000                           /* reserved */

/*---------------------------------------------------------------------------*/

/*------------------------Session End Modes----------------------------------*/
#define OCI_SESSEND_RESERVED_1 0x0001                            /* reserved */
#define OCI_SESSEND_RESERVED_2 0x0002                            /* reserved */
/*---------------------------------------------------------------------------*/

/*------------------------Attach Modes---------------------------------------*/

/* The following attach modes are the same as the UPI modes defined in 
 * UPIDEF.H.  Do not use these values externally.
 */

#define OCI_FASTPATH         0x0010              /* Attach in fast path mode */
#define OCI_ATCH_RESERVED_1  0x0020                              /* reserved */
#define OCI_ATCH_RESERVED_2  0x0080                              /* reserved */
#define OCI_ATCH_RESERVED_3  0x0100                              /* reserved */
#define OCI_CPOOL            0x0200  /* Attach using server handle from pool */
#define OCI_ATCH_RESERVED_4  0x0400                              /* reserved */
#define OCI_ATCH_RESERVED_5  0x2000                              /* reserved */
#define OCI_ATCH_ENABLE_BEQ  0x4000        /* Allow bequeath connect strings */
#define OCI_ATCH_RESERVED_6  0x8000                              /* reserved */
#define OCI_ATCH_RESERVED_7  0x10000                              /* reserved */
#define OCI_ATCH_RESERVED_8  0x20000                             /* reserved */

#define OCI_SRVATCH_RESERVED5 0x01000000                         /* reserved */
#define OCI_SRVATCH_RESERVED6 0x02000000                         /* reserved */

/*---------------------OCIStmtPrepare2 Modes---------------------------------*/
#define OCI_PREP2_CACHE_SEARCHONLY    0x0010                  /* ONly Search */
#define OCI_PREP2_GET_PLSQL_WARNINGS  0x0020         /* Get PL/SQL warnings  */
#define OCI_PREP2_RESERVED_1          0x0040                     /* reserved */

/*---------------------OCIStmtRelease Modes----------------------------------*/
#define OCI_STRLS_CACHE_DELETE      0x0010              /* Delete from Cache */

/*---------------------OCIHanlde Mgmt Misc Modes-----------------------------*/
#define OCI_STM_RESERVED4   0x00100000                           /* reserved */

/*-----------------------------End Various Modes ----------------------------*/

/*------------------------Piece Information----------------------------------*/
#define OCI_PARAM_IN 0x01                                    /* in parameter */
#define OCI_PARAM_OUT 0x02                                  /* out parameter */
/*---------------------------------------------------------------------------*/

/*------------------------ Transaction Start Flags --------------------------*/
/* NOTE: OCI_TRANS_JOIN and OCI_TRANS_NOMIGRATE not supported in 8.0.X       */
#define OCI_TRANS_NEW          0x00000001 /* start a new local or global txn */
#define OCI_TRANS_JOIN         0x00000002     /* join an existing global txn */
#define OCI_TRANS_RESUME       0x00000004    /* resume the global txn branch */
#define OCI_TRANS_PROMOTE      0x00000008 /* promote the local txn to global */
#define OCI_TRANS_STARTMASK    0x000000ff  /* mask for start operation flags */

#define OCI_TRANS_READONLY     0x00000100            /* start a readonly txn */
#define OCI_TRANS_READWRITE    0x00000200          /* start a read-write txn */
#define OCI_TRANS_SERIALIZABLE 0x00000400        /* start a serializable txn */
#define OCI_TRANS_ISOLMASK     0x0000ff00  /* mask for start isolation flags */

#define OCI_TRANS_LOOSE        0x00010000        /* a loosely coupled branch */
#define OCI_TRANS_TIGHT        0x00020000        /* a tightly coupled branch */
#define OCI_TRANS_TYPEMASK     0x000f0000      /* mask for branch type flags */

#define OCI_TRANS_NOMIGRATE    0x00100000      /* non migratable transaction */
#define OCI_TRANS_SEPARABLE    0x00200000  /* separable transaction (8.1.6+) */
#define OCI_TRANS_OTSRESUME    0x00400000      /* OTS resuming a transaction */
#define OCI_TRANS_OTHRMASK     0xfff00000      /* mask for other start flags */


/*---------------------------------------------------------------------------*/

/*------------------------ Transaction End Flags ----------------------------*/
#define OCI_TRANS_TWOPHASE      0x01000000           /* use two phase commit */
#define OCI_TRANS_WRITEBATCH    0x00000001  /* force cmt-redo for local txns */
#define OCI_TRANS_WRITEIMMED    0x00000002              /* no force cmt-redo */
#define OCI_TRANS_WRITEWAIT     0x00000004               /* no sync cmt-redo */
#define OCI_TRANS_WRITENOWAIT   0x00000008   /* sync cmt-redo for local txns */
/*---------------------------------------------------------------------------*/

/*------------------------- AQ Constants ------------------------------------
 * NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE
 * The following constants must match the PL/SQL dbms_aq constants
 * NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE
 */
/* ------------------------- Visibility flags -------------------------------*/
#define OCI_ENQ_IMMEDIATE       1   /* enqueue is an independent transaction */
#define OCI_ENQ_ON_COMMIT       2  /* enqueue is part of current transaction */

/* ----------------------- Dequeue mode flags -------------------------------*/
#define OCI_DEQ_BROWSE          1   /* read message without acquiring a lock */
#define OCI_DEQ_LOCKED          2   /* read and obtain write lock on message */
#define OCI_DEQ_REMOVE          3          /* read the message and delete it */
#define OCI_DEQ_REMOVE_NODATA   4    /* delete message w'o returning payload */
#define OCI_DEQ_GETSIG          5                      /* get signature only */

/* ----------------- Dequeue navigation flags -------------------------------*/
#define OCI_DEQ_FIRST_MSG        1     /* get first message at head of queue */
#define OCI_DEQ_NEXT_MSG         3         /* next message that is available */
#define OCI_DEQ_NEXT_TRANSACTION 2    /* get first message of next txn group */
#define OCI_DEQ_MULT_TRANSACTION 5        /* array dequeue across txn groups */

/* ----------------- Dequeue Option Reserved flags ------------------------- */
#define OCI_DEQ_RESERVED_1      0x000001

/* --------------------- Message states -------------------------------------*/
#define OCI_MSG_WAITING         1 /* the message delay has not yet completed */
#define OCI_MSG_READY           0    /* the message is ready to be processed */
#define OCI_MSG_PROCESSED       2          /* the message has been processed */
#define OCI_MSG_EXPIRED         3    /* message has moved to exception queue */

/* --------------------- Sequence deviation ---------------------------------*/
#define OCI_ENQ_BEFORE          2  /* enqueue message before another message */
#define OCI_ENQ_TOP             3     /* enqueue message before all messages */

/* ------------------------- Visibility flags -------------------------------*/
#define OCI_DEQ_IMMEDIATE       1   /* dequeue is an independent transaction */
#define OCI_DEQ_ON_COMMIT       2  /* dequeue is part of current transaction */

/* ------------------------ Wait --------------------------------------------*/
#define OCI_DEQ_WAIT_FOREVER    -1   /* wait forever if no message available */
#define OCI_NTFN_GROUPING_FOREVER -1  /* send grouping notifications forever */
#define OCI_DEQ_NO_WAIT         0  /* do not wait if no message is available */

#define OCI_FLOW_CONTROL_NO_TIMEOUT      -1
                           /* streaming enqueue: no timeout for flow control */

/* ------------------------ Delay -------------------------------------------*/
#define OCI_MSG_NO_DELAY        0        /* message is available immediately */

/* ------------------------- Expiration -------------------------------------*/
#define OCI_MSG_NO_EXPIRATION -1                /* message will never expire */

#define OCI_MSG_PERSISTENT_OR_BUFFERED   3
#define OCI_MSG_BUFFERED                 2
#define OCI_MSG_PERSISTENT               1

/* ----------------------- Reserved/AQE pisdef flags ------------------------*/
/* see aqeflg defines in kwqp.h */
#define OCI_AQ_RESERVED_1      0x0002
#define OCI_AQ_RESERVED_2      0x0004
#define OCI_AQ_RESERVED_3      0x0008
#define OCI_AQ_RESERVED_4      0x0010

#define OCI_AQ_STREAMING_FLAG  0x02000000

/* ------------------------------ Replay Info -------------------------------*/
#define OCI_AQ_LAST_ENQUEUED     0
#define OCI_AQ_LAST_ACKNOWLEDGED 1

/* -------------------------- END AQ Constants ----------------------------- */

/* --------------------END DateTime and Interval Constants ------------------*/

/*-----------------------Object Types----------------------------------------*/
/*-----------Object Types **** Not to be Used **** --------------------------*/
/* Deprecated */
#define OCI_OTYPE_UNK           0
#define OCI_OTYPE_TABLE         1
#define OCI_OTYPE_VIEW          2
#define OCI_OTYPE_SYN           3
#define OCI_OTYPE_PROC          4
#define OCI_OTYPE_FUNC          5
#define OCI_OTYPE_PKG           6
#define OCI_OTYPE_STMT          7
/*---------------------------------------------------------------------------*/

/*=======================Describe Handle Parameter Attributes ===============*/
/* 
   These attributes are orthogonal to the other set of attributes defined 
   above.  These attrubutes are to be used only for the describe handle. 
*/
/*===========================================================================*/
/* Attributes common to Columns and Stored Procs */
#define OCI_ATTR_DATA_SIZE      1                /* maximum size of the data */
#define OCI_ATTR_DATA_TYPE      2     /* the SQL type of the column/argument */
#define OCI_ATTR_DISP_SIZE      3                        /* the display size */
#define OCI_ATTR_NAME           4         /* the name of the column/argument */
#define OCI_ATTR_PRECISION      5                /* precision if number type */
#define OCI_ATTR_SCALE          6                    /* scale if number type */
#define OCI_ATTR_IS_NULL        7                            /* is it null ? */
#define OCI_ATTR_TYPE_NAME      8
  /* name of the named data type or a package name for package private types */
#define OCI_ATTR_SCHEMA_NAME    9             /* the schema name */
#define OCI_ATTR_SUB_NAME       10      /* type name if package private type */
#define OCI_ATTR_POSITION       11
                    /* relative position of col/arg in the list of cols/args */
/* complex object retrieval parameter attributes */
#define OCI_ATTR_COMPLEXOBJECTCOMP_TYPE         50 
#define OCI_ATTR_COMPLEXOBJECTCOMP_TYPE_LEVEL   51
#define OCI_ATTR_COMPLEXOBJECT_LEVEL            52
#define OCI_ATTR_COMPLEXOBJECT_COLL_OUTOFLINE   53

/* Only Columns */
#define OCI_ATTR_DISP_NAME      100                      /* the display name */
#define OCI_ATTR_ENCC_SIZE      101                   /* encrypted data size */
#define OCI_ATTR_COL_ENC        102                 /* column is encrypted ? */
#define OCI_ATTR_COL_ENC_SALT   103          /* is encrypted column salted ? */

/*Only Stored Procs */
#define OCI_ATTR_OVERLOAD       210           /* is this position overloaded */
#define OCI_ATTR_LEVEL          211            /* level for structured types */
#define OCI_ATTR_HAS_DEFAULT    212                   /* has a default value */
#define OCI_ATTR_IOMODE         213                         /* in, out inout */
#define OCI_ATTR_RADIX          214                       /* returns a radix */
#define OCI_ATTR_NUM_ARGS       215             /* total number of arguments */

/* only named type attributes */
#define OCI_ATTR_TYPECODE                  216       /* object or collection */
#define OCI_ATTR_COLLECTION_TYPECODE       217     /* varray or nested table */
#define OCI_ATTR_VERSION                   218      /* user assigned version */
#define OCI_ATTR_IS_INCOMPLETE_TYPE        219 /* is this an incomplete type */
#define OCI_ATTR_IS_SYSTEM_TYPE            220              /* a system type */
#define OCI_ATTR_IS_PREDEFINED_TYPE        221          /* a predefined type */
#define OCI_ATTR_IS_TRANSIENT_TYPE         222           /* a transient type */
#define OCI_ATTR_IS_SYSTEM_GENERATED_TYPE  223      /* system generated type */
#define OCI_ATTR_HAS_NESTED_TABLE          224 /* contains nested table attr */
#define OCI_ATTR_HAS_LOB                   225        /* has a lob attribute */
#define OCI_ATTR_HAS_FILE                  226       /* has a file attribute */
#define OCI_ATTR_COLLECTION_ELEMENT        227 /* has a collection attribute */
#define OCI_ATTR_NUM_TYPE_ATTRS            228  /* number of attribute types */
#define OCI_ATTR_LIST_TYPE_ATTRS           229    /* list of type attributes */
#define OCI_ATTR_NUM_TYPE_METHODS          230     /* number of type methods */
#define OCI_ATTR_LIST_TYPE_METHODS         231       /* list of type methods */
#define OCI_ATTR_MAP_METHOD                232         /* map method of type */
#define OCI_ATTR_ORDER_METHOD              233       /* order method of type */

/* only collection element */
#define OCI_ATTR_NUM_ELEMS                 234         /* number of elements */

/* only type methods */
#define OCI_ATTR_ENCAPSULATION             235        /* encapsulation level */
#define OCI_ATTR_IS_SELFISH                236             /* method selfish */
#define OCI_ATTR_IS_VIRTUAL                237                    /* virtual */
#define OCI_ATTR_IS_INLINE                 238                     /* inline */
#define OCI_ATTR_IS_CONSTANT               239                   /* constant */
#define OCI_ATTR_HAS_RESULT                240                 /* has result */
#define OCI_ATTR_IS_CONSTRUCTOR            241                /* constructor */
#define OCI_ATTR_IS_DESTRUCTOR             242                 /* destructor */
#define OCI_ATTR_IS_OPERATOR               243                   /* operator */
#define OCI_ATTR_IS_MAP                    244               /* a map method */
#define OCI_ATTR_IS_ORDER                  245               /* order method */
#define OCI_ATTR_IS_RNDS                   246  /* read no data state method */
#define OCI_ATTR_IS_RNPS                   247      /* read no process state */
#define OCI_ATTR_IS_WNDS                   248 /* write no data state method */
#define OCI_ATTR_IS_WNPS                   249     /* write no process state */

#define OCI_ATTR_DESC_PUBLIC               250              /* public object */

/* Object Cache Enhancements : attributes for User Constructed Instances     */
#define OCI_ATTR_CACHE_CLIENT_CONTEXT      251
#define OCI_ATTR_UCI_CONSTRUCT             252
#define OCI_ATTR_UCI_DESTRUCT              253
#define OCI_ATTR_UCI_COPY                  254
#define OCI_ATTR_UCI_PICKLE                255
#define OCI_ATTR_UCI_UNPICKLE              256
#define OCI_ATTR_UCI_REFRESH               257

/* for type inheritance */
#define OCI_ATTR_IS_SUBTYPE                258
#define OCI_ATTR_SUPERTYPE_SCHEMA_NAME     259
#define OCI_ATTR_SUPERTYPE_NAME            260

/* for schemas */
#define OCI_ATTR_LIST_OBJECTS              261  /* list of objects in schema */

/* for database */
#define OCI_ATTR_NCHARSET_ID               262                /* char set id */
#define OCI_ATTR_LIST_SCHEMAS              263            /* list of schemas */
#define OCI_ATTR_MAX_PROC_LEN              264       /* max procedure length */
#define OCI_ATTR_MAX_COLUMN_LEN            265     /* max column name length */
#define OCI_ATTR_CURSOR_COMMIT_BEHAVIOR    266     /* cursor commit behavior */
#define OCI_ATTR_MAX_CATALOG_NAMELEN       267         /* catalog namelength */
#define OCI_ATTR_CATALOG_LOCATION          268           /* catalog location */
#define OCI_ATTR_SAVEPOINT_SUPPORT         269          /* savepoint support */
#define OCI_ATTR_NOWAIT_SUPPORT            270             /* nowait support */
#define OCI_ATTR_AUTOCOMMIT_DDL            271             /* autocommit DDL */
#define OCI_ATTR_LOCKING_MODE              272               /* locking mode */

/* for externally initialized context */
#define OCI_ATTR_APPCTX_SIZE               273 /* count of context to be init*/
#define OCI_ATTR_APPCTX_LIST               274 /* count of context to be init*/
#define OCI_ATTR_APPCTX_NAME               275 /* name  of context to be init*/
#define OCI_ATTR_APPCTX_ATTR               276 /* attr  of context to be init*/
#define OCI_ATTR_APPCTX_VALUE              277 /* value of context to be init*/

/* for client id propagation */
#define OCI_ATTR_CLIENT_IDENTIFIER         278   /* value of client id to set*/

/* for inheritance - part 2 */
#define OCI_ATTR_IS_FINAL_TYPE             279            /* is final type ? */
#define OCI_ATTR_IS_INSTANTIABLE_TYPE      280     /* is instantiable type ? */
#define OCI_ATTR_IS_FINAL_METHOD           281          /* is final method ? */
#define OCI_ATTR_IS_INSTANTIABLE_METHOD    282   /* is instantiable method ? */
#define OCI_ATTR_IS_OVERRIDING_METHOD      283     /* is overriding method ? */

#define OCI_ATTR_DESC_SYNBASE              284   /* Describe the base object */


#define OCI_ATTR_CHAR_USED                 285      /* char length semantics */
#define OCI_ATTR_CHAR_SIZE                 286                /* char length */

/* SQLJ support */
#define OCI_ATTR_IS_JAVA_TYPE              287 /* is java implemented type ? */

/* N-Tier support */
#define OCI_ATTR_DISTINGUISHED_NAME        300        /* use DN as user name */
#define OCI_ATTR_KERBEROS_TICKET           301   /* Kerberos ticket as cred. */
 
/* for multilanguage debugging */
#define OCI_ATTR_ORA_DEBUG_JDWP            302   /* ORA_DEBUG_JDWP attribute */

#define OCI_ATTR_EDITION                   288                /* ORA_EDITION */

#define OCI_ATTR_RESERVED_14               303                   /* reserved */


/*---------------------------End Describe Handle Attributes -----------------*/

/* For values 303 - 307, see DirPathAPI attribute section in this file */

/* ----------------------- Session Pool Attributes ------------------------- */
#define OCI_ATTR_SPOOL_TIMEOUT              308           /* session timeout */
#define OCI_ATTR_SPOOL_GETMODE              309          /* session get mode */
#define OCI_ATTR_SPOOL_BUSY_COUNT           310        /* busy session count */
#define OCI_ATTR_SPOOL_OPEN_COUNT           311        /* open session count */
#define OCI_ATTR_SPOOL_MIN                  312         /* min session count */
#define OCI_ATTR_SPOOL_MAX                  313         /* max session count */
#define OCI_ATTR_SPOOL_INCR                 314   /* session increment count */
#define OCI_ATTR_SPOOL_STMTCACHESIZE        208   /*Stmt cache size of pool  */
/*------------------------------End Session Pool Attributes -----------------*/
/*---------------------------- For XML Types ------------------------------- */
/* For table, view and column */
#define OCI_ATTR_IS_XMLTYPE          315         /* Is the type an XML type? */
#define OCI_ATTR_XMLSCHEMA_NAME      316               /* Name of XML Schema */
#define OCI_ATTR_XMLELEMENT_NAME     317              /* Name of XML Element */
#define OCI_ATTR_XMLSQLTYPSCH_NAME   318    /* SQL type's schema for XML Ele */
#define OCI_ATTR_XMLSQLTYPE_NAME     319     /* Name of SQL type for XML Ele */
#define OCI_ATTR_XMLTYPE_STORED_OBJ  320       /* XML type stored as object? */
#define OCI_ATTR_XMLTYPE_BINARY_XML  422       /* XML type stored as binary? */

/*---------------------------- For Subtypes ------------------------------- */
/* For type */
#define OCI_ATTR_HAS_SUBTYPES        321                    /* Has subtypes? */
#define OCI_ATTR_NUM_SUBTYPES        322               /* Number of subtypes */
#define OCI_ATTR_LIST_SUBTYPES       323                 /* List of subtypes */

/* XML flag */
#define OCI_ATTR_XML_HRCHY_ENABLED   324               /* hierarchy enabled? */

/* Method flag */
#define OCI_ATTR_IS_OVERRIDDEN_METHOD 325           /* Method is overridden? */

/* For values 326 - 335, see DirPathAPI attribute section in this file */

/*------------- Attributes for 10i Distributed Objects ----------------------*/
#define OCI_ATTR_OBJ_SUBS                   336 /* obj col/tab substitutable */

/* For values 337 - 338, see DirPathAPI attribute section in this file */

/*---------- Attributes for 10i XADFIELD (NLS language, territory -----------*/
#define OCI_ATTR_XADFIELD_RESERVED_1        339                  /* reserved */
#define OCI_ATTR_XADFIELD_RESERVED_2        340                  /* reserved */
/*------------- Kerberos Secure Client Identifier ---------------------------*/
#define OCI_ATTR_KERBEROS_CID               341 /* Kerberos db service ticket*/


/*------------------------ Attributes for Rules objects ---------------------*/
#define OCI_ATTR_CONDITION                  342            /* rule condition */
#define OCI_ATTR_COMMENT                    343                   /* comment */
#define OCI_ATTR_VALUE                      344             /* Anydata value */
#define OCI_ATTR_EVAL_CONTEXT_OWNER         345        /* eval context owner */
#define OCI_ATTR_EVAL_CONTEXT_NAME          346         /* eval context name */
#define OCI_ATTR_EVALUATION_FUNCTION        347        /* eval function name */
#define OCI_ATTR_VAR_TYPE                   348             /* variable type */
#define OCI_ATTR_VAR_VALUE_FUNCTION         349   /* variable value function */
#define OCI_ATTR_VAR_METHOD_FUNCTION        350  /* variable method function */
#define OCI_ATTR_ACTION_CONTEXT             351            /* action context */
#define OCI_ATTR_LIST_TABLE_ALIASES         352     /* list of table aliases */
#define OCI_ATTR_LIST_VARIABLE_TYPES        353    /* list of variable types */
#define OCI_ATTR_TABLE_NAME                 356                /* table name */

/* For values 357 - 359, see DirPathAPI attribute section in this file */

#define OCI_ATTR_MESSAGE_CSCN               360              /* message cscn */
#define OCI_ATTR_MESSAGE_DSCN               361              /* message dscn */

/*--------------------- Audit Session ID ------------------------------------*/
#define OCI_ATTR_AUDIT_SESSION_ID           362          /* Audit session ID */

/*--------------------- Kerberos TGT Keys -----------------------------------*/
#define OCI_ATTR_KERBEROS_KEY               363  /* n-tier Kerberos cred key */
#define OCI_ATTR_KERBEROS_CID_KEY           364    /* SCID Kerberos cred key */


#define OCI_ATTR_TRANSACTION_NO             365         /* AQ enq txn number */

/*----------------------- Attributes for End To End Tracing -----------------*/
#define OCI_ATTR_MODULE                     366        /* module for tracing */
#define OCI_ATTR_ACTION                     367        /* action for tracing */
#define OCI_ATTR_CLIENT_INFO                368               /* client info */
#define OCI_ATTR_COLLECT_CALL_TIME          369         /* collect call time */
#define OCI_ATTR_CALL_TIME                  370         /* extract call time */
#define OCI_ATTR_ECONTEXT_ID                371      /* execution-id context */
#define OCI_ATTR_ECONTEXT_SEQ               372  /*execution-id sequence num */


/*------------------------------ Session attributes -------------------------*/
#define OCI_ATTR_SESSION_STATE              373             /* session state */
#define OCI_SESSION_STATELESS  1                             /* valid states */
#define OCI_SESSION_STATEFUL   2

#define OCI_ATTR_SESSION_STATETYPE          374        /* session state type */
#define OCI_SESSION_STATELESS_DEF  0                    /* valid state types */
#define OCI_SESSION_STATELESS_CAL  1
#define OCI_SESSION_STATELESS_TXN  2
#define OCI_SESSION_STATELESS_APP  3

#define OCI_ATTR_SESSION_STATE_CLEARED      376     /* session state cleared */
#define OCI_ATTR_SESSION_MIGRATED           377       /* did session migrate */
#define OCI_ATTR_SESSION_PRESERVE_STATE     388    /* preserve session state */
#define OCI_ATTR_DRIVER_NAME                424               /* Driver Name */

/* -------------------------- Admin Handle Attributes ---------------------- */

#define OCI_ATTR_ADMIN_PFILE                389    /* client-side param file */

/*----------------------- Attributes for End To End Tracing -----------------*/
/* -------------------------- HA Event Handle Attributes ------------------- */

#define OCI_ATTR_HOSTNAME         390                /* SYS_CONTEXT hostname */
#define OCI_ATTR_DBNAME           391                  /* SYS_CONTEXT dbname */
#define OCI_ATTR_INSTNAME         392           /* SYS_CONTEXT instance name */
#define OCI_ATTR_SERVICENAME      393            /* SYS_CONTEXT service name */
#define OCI_ATTR_INSTSTARTTIME    394      /* v$instance instance start time */
#define OCI_ATTR_HA_TIMESTAMP     395                          /* event time */
#define OCI_ATTR_RESERVED_22      396                            /* reserved */
#define OCI_ATTR_RESERVED_23      397                            /* reserved */
#define OCI_ATTR_RESERVED_24      398                            /* reserved */
#define OCI_ATTR_DBDOMAIN         399                           /* db domain */
#define OCI_ATTR_RESERVED_27      425                            /* reserved */

#define OCI_ATTR_EVENTTYPE        400                          /* event type */
#define OCI_EVENTTYPE_HA            0  /* valid value for OCI_ATTR_EVENTTYPE */

#define OCI_ATTR_HA_SOURCE        401
/* valid values for OCI_ATTR_HA_SOURCE */
#define OCI_HA_SOURCE_INSTANCE            0 
#define OCI_HA_SOURCE_DATABASE            1
#define OCI_HA_SOURCE_NODE                2
#define OCI_HA_SOURCE_SERVICE             3
#define OCI_HA_SOURCE_SERVICE_MEMBER      4
#define OCI_HA_SOURCE_ASM_INSTANCE        5
#define OCI_HA_SOURCE_SERVICE_PRECONNECT  6

#define OCI_ATTR_HA_STATUS        402
#define OCI_HA_STATUS_DOWN          0 /* valid values for OCI_ATTR_HA_STATUS */
#define OCI_HA_STATUS_UP            1

#define OCI_ATTR_HA_SRVFIRST      403

#define OCI_ATTR_HA_SRVNEXT       404
/* ------------------------- Server Handle Attributes -----------------------*/

#define OCI_ATTR_TAF_ENABLED      405

/* Extra notification attributes */
#define OCI_ATTR_NFY_FLAGS        406 

#define OCI_ATTR_MSG_DELIVERY_MODE 407        /* msg delivery mode */
#define OCI_ATTR_DB_CHARSET_ID     416       /* database charset ID */
#define OCI_ATTR_DB_NCHARSET_ID    417       /* database ncharset ID */
#define OCI_ATTR_RESERVED_25       418                           /* reserved */

#define OCI_ATTR_FLOW_CONTROL_TIMEOUT       423  /* AQ: flow control timeout */
/*---------------------------------------------------------------------------*/
/* ------------------DirPathAPI attribute Section----------------------------*/
/* All DirPathAPI attributes are in this section of the file.  Existing      */
/* attributes prior to this section being created are assigned values < 2000 */
/* Add new DirPathAPI attributes to this section and their assigned value    */
/* should be whatever the last entry is + 1.                                 */

/*------------- Supported Values for Direct Path Stream Version -------------*/
#define OCI_DIRPATH_STREAM_VERSION_1        100
#define OCI_DIRPATH_STREAM_VERSION_2        200
#define OCI_DIRPATH_STREAM_VERSION_3        300                   /* default */


#define OCI_ATTR_DIRPATH_MODE           78  /* mode of direct path operation */
#define OCI_ATTR_DIRPATH_NOLOG          79               /* nologging option */
#define OCI_ATTR_DIRPATH_PARALLEL       80     /* parallel (temp seg) option */

#define OCI_ATTR_DIRPATH_SORTED_INDEX    137 /* index that data is sorted on */

            /* direct path index maint method (see oci8dp.h) */
#define OCI_ATTR_DIRPATH_INDEX_MAINT_METHOD 138

    /* parallel load: db file, initial and next extent sizes */

#define OCI_ATTR_DIRPATH_FILE               139      /* DB file to load into */
#define OCI_ATTR_DIRPATH_STORAGE_INITIAL    140       /* initial extent size */
#define OCI_ATTR_DIRPATH_STORAGE_NEXT       141          /* next extent size */
            /* direct path index maint method (see oci8dp.h) */
#define OCI_ATTR_DIRPATH_SKIPINDEX_METHOD   145

    /* 8.2 dpapi support of ADTs */
#define OCI_ATTR_DIRPATH_EXPR_TYPE  150        /* expr type of OCI_ATTR_NAME */

/* For the direct path API there are three data formats:
 * TEXT   - used mainly by SQL*Loader, data is in textual form
 * STREAM - used by datapump, data is in stream loadable form
 * OCI    - used by OCI programs utilizing the DpApi, data is in binary form
 */
#define OCI_ATTR_DIRPATH_INPUT         151 
#define OCI_DIRPATH_INPUT_TEXT        0x01                           /* text */
#define OCI_DIRPATH_INPUT_STREAM      0x02              /* stream (datapump) */
#define OCI_DIRPATH_INPUT_OCI         0x04                   /* binary (oci) */
#define OCI_DIRPATH_INPUT_UNKNOWN     0x08

#define OCI_ATTR_DIRPATH_FN_CTX             167  /* fn ctx ADT attrs or args */

#define OCI_ATTR_DIRPATH_OID                187   /* loading into an OID col */
#define OCI_ATTR_DIRPATH_SID                194   /* loading into an SID col */
#define OCI_ATTR_DIRPATH_OBJ_CONSTR         206 /* obj type of subst obj tbl */

/* Attr to allow setting of the stream version PRIOR to calling Prepare */
#define OCI_ATTR_DIRPATH_STREAM_VERSION     212      /* version of the stream*/

#define OCIP_ATTR_DIRPATH_VARRAY_INDEX      213       /* varray index column */

/*------------- Supported Values for Direct Path Date cache -----------------*/
#define OCI_ATTR_DIRPATH_DCACHE_NUM         303        /* date cache entries */
#define OCI_ATTR_DIRPATH_DCACHE_SIZE        304          /* date cache limit */
#define OCI_ATTR_DIRPATH_DCACHE_MISSES      305         /* date cache misses */
#define OCI_ATTR_DIRPATH_DCACHE_HITS        306           /* date cache hits */
#define OCI_ATTR_DIRPATH_DCACHE_DISABLE     307 /* on set: disable datecache 
                                                * on overflow.
                                                * on get: datecache disabled? 
                                                * could be due to overflow
                                                * or others                  */

/*------------- Attributes for 10i Updates to the DirPath API ---------------*/
#define OCI_ATTR_DIRPATH_RESERVED_7         326                 /* reserved */
#define OCI_ATTR_DIRPATH_RESERVED_8         327                 /* reserved */
#define OCI_ATTR_DIRPATH_CONVERT            328 /* stream conversion needed? */
#define OCI_ATTR_DIRPATH_BADROW             329        /* info about bad row */
#define OCI_ATTR_DIRPATH_BADROW_LENGTH      330    /* length of bad row info */
#define OCI_ATTR_DIRPATH_WRITE_ORDER        331         /* column fill order */
#define OCI_ATTR_DIRPATH_GRANULE_SIZE       332   /* granule size for unload */
#define OCI_ATTR_DIRPATH_GRANULE_OFFSET     333    /* offset to last granule */
#define OCI_ATTR_DIRPATH_RESERVED_1         334                  /* reserved */
#define OCI_ATTR_DIRPATH_RESERVED_2         335                  /* reserved */

/*------ Attributes for 10i DirPathAPI conversion (NLS lang, terr, cs) ------*/
#define OCI_ATTR_DIRPATH_RESERVED_3         337                  /* reserved */
#define OCI_ATTR_DIRPATH_RESERVED_4         338                  /* reserved */
#define OCI_ATTR_DIRPATH_RESERVED_5         357                  /* reserved */
#define OCI_ATTR_DIRPATH_RESERVED_6         358                  /* reserved */

#define OCI_ATTR_DIRPATH_LOCK_WAIT          359    /* wait for lock in dpapi */

#define OCI_ATTR_DIRPATH_RESERVED_9        2000                  /* reserved */

/*------ Attribute for 10iR2 for column encryption for Direct Path API ------*/
#define OCI_ATTR_DIRPATH_RESERVED_10       2001                  /* reserved */
#define OCI_ATTR_DIRPATH_RESERVED_11       2002                  /* reserved */

/*------ Attribute to determine last column successfully converted ----------*/
#define OCI_ATTR_CURRENT_ERRCOL            2003      /* current error column */

  /*--Attributes for 11gR1 for multiple subtype support in Direct Path API - */
#define OCI_ATTR_DIRPATH_SUBTYPE_INDEX     2004  /* sbtyp indx for attribute */

#define OCI_ATTR_DIRPATH_RESERVED_12       2005                  /* reserved */
#define OCI_ATTR_DIRPATH_RESERVED_13       2006                  /* reserver */

  /*--Attribute for partitioning constraint optimization in Direct Path API  */
#define OCI_ATTR_DIRPATH_RESERVED_14       2007                  /* reserved */

  /*--Attribute for interval partitioning in Direct Path API  */
#define OCI_ATTR_DIRPATH_RESERVED_15       2008                  /* reserved */

  /*--Attribute for interval partitioning in Direct Path API  */
#define OCI_ATTR_DIRPATH_RESERVED_16       2009                  /* reserved */

/*--Attribute for allowing parallel lob loads in Direct Path API */
#define OCI_ATTR_DIRPATH_RESERVED_17       2010                  /* reserved */

/*--Attribute for process order number of table being loaded/unloaded        */
#define OCI_ATTR_DIRPATH_RESERVED_18       2011                  /* reserved */

#define OCI_ATTR_DIRPATH_RESERVED_19       2012                  /* reserved */

/* Add DirPathAPI attributes above.  Next value to be assigned is 2013      */

/* ------------------End of DirPathAPI attribute Section --------------------*/
/*---------------------------------------------------------------------------*/


/*---------------- Describe Handle Parameter Attribute Values ---------------*/

/* OCI_ATTR_CURSOR_COMMIT_BEHAVIOR */
#define OCI_CURSOR_OPEN   0 
#define OCI_CURSOR_CLOSED 1

/* OCI_ATTR_CATALOG_LOCATION */
#define OCI_CL_START 0
#define OCI_CL_END   1

/* OCI_ATTR_SAVEPOINT_SUPPORT */
#define OCI_SP_SUPPORTED   0
#define OCI_SP_UNSUPPORTED 1

/* OCI_ATTR_NOWAIT_SUPPORT */
#define OCI_NW_SUPPORTED   0
#define OCI_NW_UNSUPPORTED 1

/* OCI_ATTR_AUTOCOMMIT_DDL */
#define OCI_AC_DDL    0
#define OCI_NO_AC_DDL 1

/* OCI_ATTR_LOCKING_MODE */
#define OCI_LOCK_IMMEDIATE 0
#define OCI_LOCK_DELAYED   1

/* ------------------- Instance type attribute values -----------------------*/
#define OCI_INSTANCE_TYPE_UNKNOWN  0
#define OCI_INSTANCE_TYPE_RDBMS    1
#define OCI_INSTANCE_TYPE_OSM      2

/* ---------------- ASM Volume Device Support attribute values --------------*/
#define OCI_ASM_VOLUME_UNSUPPORTED 0
#define OCI_ASM_VOLUME_SUPPORTED   1

/*---------------------------------------------------------------------------*/

/*---------------------------OCIPasswordChange-------------------------------*/
#define OCI_AUTH         0x08        /* Change the password but do not login */


/*------------------------Other Constants------------------------------------*/
#define OCI_MAX_FNS   100                     /* max number of OCI Functions */
#define OCI_SQLSTATE_SIZE 5  
#define OCI_ERROR_MAXMSG_SIZE   1024         /* max size of an error message */
#define OCI_LOBMAXSIZE MINUB4MAXVAL                 /* maximum lob data size */
#define OCI_ROWID_LEN             23 
/*---------------------------------------------------------------------------*/

/*------------------------ Fail Over Events ---------------------------------*/
#define OCI_FO_END          0x00000001
#define OCI_FO_ABORT        0x00000002   
#define OCI_FO_REAUTH       0x00000004
#define OCI_FO_BEGIN        0x00000008 
#define OCI_FO_ERROR        0x00000010
/*---------------------------------------------------------------------------*/

/*------------------------ Fail Over Callback Return Codes ------------------*/
#define OCI_FO_RETRY        25410
/*---------------------------------------------------------------------------*/

/*------------------------- Fail Over Types ---------------------------------*/
#define OCI_FO_NONE           0x00000001
#define OCI_FO_SESSION        0x00000002
#define OCI_FO_SELECT         0x00000004
#define OCI_FO_TXNAL          0x00000008
/*---------------------------------------------------------------------------*/

/*-----------------------Function Codes--------------------------------------*/
#define OCI_FNCODE_INITIALIZE     1                         /* OCIInitialize */
#define OCI_FNCODE_HANDLEALLOC  2                          /* OCIHandleAlloc */
#define OCI_FNCODE_HANDLEFREE  3                            /* OCIHandleFree */
#define OCI_FNCODE_DESCRIPTORALLOC  4                  /* OCIDescriptorAlloc */
#define OCI_FNCODE_DESCRIPTORFREE  5                    /* OCIDescriptorFree */
#define OCI_FNCODE_ENVINIT   6                                 /* OCIEnvInit */
#define OCI_FNCODE_SERVERATTACH   7                       /* OCIServerAttach */
#define OCI_FNCODE_SERVERDETACH   8                       /* OCIServerDetach */
/* unused         9 */ 
#define OCI_FNCODE_SESSIONBEGIN  10                       /* OCISessionBegin */
#define OCI_FNCODE_SESSIONEND   11                          /* OCISessionEnd */
#define OCI_FNCODE_PASSWORDCHANGE   12                  /* OCIPasswordChange */
#define OCI_FNCODE_STMTPREPARE   13                        /* OCIStmtPrepare */
                                                      /* unused       14- 16 */
#define OCI_FNCODE_BINDDYNAMIC   17                        /* OCIBindDynamic */
#define OCI_FNCODE_BINDOBJECT  18                           /* OCIBindObject */
                                                                /* 19 unused */
#define OCI_FNCODE_BINDARRAYOFSTRUCT   20            /* OCIBindArrayOfStruct */
#define OCI_FNCODE_STMTEXECUTE  21                         /* OCIStmtExecute */
                                                             /* unused 22-24 */
#define OCI_FNCODE_DEFINEOBJECT  25                       /* OCIDefineObject */
#define OCI_FNCODE_DEFINEDYNAMIC   26                    /* OCIDefineDynamic */
#define OCI_FNCODE_DEFINEARRAYOFSTRUCT  27         /* OCIDefineArrayOfStruct */
#define OCI_FNCODE_STMTFETCH   28                            /* OCIStmtFetch */
#define OCI_FNCODE_STMTGETBIND   29                    /* OCIStmtGetBindInfo */
                                                            /* 30, 31 unused */
#define OCI_FNCODE_DESCRIBEANY  32                         /* OCIDescribeAny */
#define OCI_FNCODE_TRANSSTART  33                           /* OCITransStart */
#define OCI_FNCODE_TRANSDETACH  34                         /* OCITransDetach */
#define OCI_FNCODE_TRANSCOMMIT  35                         /* OCITransCommit */
                                                                /* 36 unused */
#define OCI_FNCODE_ERRORGET   37                              /* OCIErrorGet */
#define OCI_FNCODE_LOBOPENFILE  38                         /* OCILobFileOpen */
#define OCI_FNCODE_LOBCLOSEFILE  39                       /* OCILobFileClose */
                                             /* 40 was LOBCREATEFILE, unused */
                                         /* 41 was OCILobFileDelete, unused  */
#define OCI_FNCODE_LOBCOPY  42                                 /* OCILobCopy */
#define OCI_FNCODE_LOBAPPEND  43                             /* OCILobAppend */
#define OCI_FNCODE_LOBERASE  44                               /* OCILobErase */
#define OCI_FNCODE_LOBLENGTH  45                          /* OCILobGetLength */
#define OCI_FNCODE_LOBTRIM  46                                 /* OCILobTrim */
#define OCI_FNCODE_LOBREAD  47                                 /* OCILobRead */
#define OCI_FNCODE_LOBWRITE  48                               /* OCILobWrite */
                                                                /* 49 unused */
#define OCI_FNCODE_SVCCTXBREAK 50                                /* OCIBreak */
#define OCI_FNCODE_SERVERVERSION  51                     /* OCIServerVersion */

#define OCI_FNCODE_KERBATTRSET 52                          /* OCIKerbAttrSet */

/* unused 53 */

#define OCI_FNCODE_ATTRGET 54                                  /* OCIAttrGet */
#define OCI_FNCODE_ATTRSET 55                                  /* OCIAttrSet */
#define OCI_FNCODE_PARAMSET 56                                /* OCIParamSet */
#define OCI_FNCODE_PARAMGET 57                                /* OCIParamGet */
#define OCI_FNCODE_STMTGETPIECEINFO   58              /* OCIStmtGetPieceInfo */
#define OCI_FNCODE_LDATOSVCCTX 59                          /* OCILdaToSvcCtx */
                                                                /* 60 unused */
#define OCI_FNCODE_STMTSETPIECEINFO   61              /* OCIStmtSetPieceInfo */
#define OCI_FNCODE_TRANSFORGET 62                          /* OCITransForget */
#define OCI_FNCODE_TRANSPREPARE 63                        /* OCITransPrepare */
#define OCI_FNCODE_TRANSROLLBACK  64                     /* OCITransRollback */
#define OCI_FNCODE_DEFINEBYPOS 65                          /* OCIDefineByPos */
#define OCI_FNCODE_BINDBYPOS 66                              /* OCIBindByPos */
#define OCI_FNCODE_BINDBYNAME 67                            /* OCIBindByName */
#define OCI_FNCODE_LOBASSIGN  68                             /* OCILobAssign */
#define OCI_FNCODE_LOBISEQUAL  69                           /* OCILobIsEqual */
#define OCI_FNCODE_LOBISINIT  70                      /* OCILobLocatorIsInit */

#define OCI_FNCODE_LOBENABLEBUFFERING  71           /* OCILobEnableBuffering */
#define OCI_FNCODE_LOBCHARSETID  72                       /* OCILobCharSetID */
#define OCI_FNCODE_LOBCHARSETFORM  73                   /* OCILobCharSetForm */
#define OCI_FNCODE_LOBFILESETNAME  74                   /* OCILobFileSetName */
#define OCI_FNCODE_LOBFILEGETNAME  75                   /* OCILobFileGetName */
#define OCI_FNCODE_LOGON 76                                      /* OCILogon */
#define OCI_FNCODE_LOGOFF 77                                    /* OCILogoff */
#define OCI_FNCODE_LOBDISABLEBUFFERING 78          /* OCILobDisableBuffering */
#define OCI_FNCODE_LOBFLUSHBUFFER 79                    /* OCILobFlushBuffer */
#define OCI_FNCODE_LOBLOADFROMFILE 80                  /* OCILobLoadFromFile */

#define OCI_FNCODE_LOBOPEN  81                                 /* OCILobOpen */
#define OCI_FNCODE_LOBCLOSE  82                               /* OCILobClose */
#define OCI_FNCODE_LOBISOPEN  83                             /* OCILobIsOpen */
#define OCI_FNCODE_LOBFILEISOPEN  84                     /* OCILobFileIsOpen */
#define OCI_FNCODE_LOBFILEEXISTS  85                     /* OCILobFileExists */
#define OCI_FNCODE_LOBFILECLOSEALL  86                 /* OCILobFileCloseAll */
#define OCI_FNCODE_LOBCREATETEMP  87                /* OCILobCreateTemporary */
#define OCI_FNCODE_LOBFREETEMP  88                    /* OCILobFreeTemporary */
#define OCI_FNCODE_LOBISTEMP  89                        /* OCILobIsTemporary */

#define OCI_FNCODE_AQENQ  90                                     /* OCIAQEnq */
#define OCI_FNCODE_AQDEQ  91                                     /* OCIAQDeq */
#define OCI_FNCODE_RESET  92                                     /* OCIReset */
#define OCI_FNCODE_SVCCTXTOLDA  93                         /* OCISvcCtxToLda */
#define OCI_FNCODE_LOBLOCATORASSIGN 94                /* OCILobLocatorAssign */

#define OCI_FNCODE_UBINDBYNAME 95

#define OCI_FNCODE_AQLISTEN  96                               /* OCIAQListen */

#define OCI_FNCODE_SVC2HST 97                                    /* reserved */
#define OCI_FNCODE_SVCRH   98                                    /* reserved */
                           /* 97 and 98 are reserved for Oracle internal use */

#define OCI_FNCODE_TRANSMULTIPREPARE   99            /* OCITransMultiPrepare */

#define OCI_FNCODE_CPOOLCREATE  100               /* OCIConnectionPoolCreate */
#define OCI_FNCODE_CPOOLDESTROY 101              /* OCIConnectionPoolDestroy */
#define OCI_FNCODE_LOGON2 102                                   /* OCILogon2 */
#define OCI_FNCODE_ROWIDTOCHAR  103                        /* OCIRowidToChar */

#define OCI_FNCODE_SPOOLCREATE  104                  /* OCISessionPoolCreate */
#define OCI_FNCODE_SPOOLDESTROY 105                 /* OCISessionPoolDestroy */
#define OCI_FNCODE_SESSIONGET   106                         /* OCISessionGet */
#define OCI_FNCODE_SESSIONRELEASE 107                   /* OCISessionRelease */
#define OCI_FNCODE_STMTPREPARE2 108                       /* OCIStmtPrepare2 */
#define OCI_FNCODE_STMTRELEASE 109                         /* OCIStmtRelease */
#define OCI_FNCODE_AQENQARRAY  110                          /* OCIAQEnqArray */
#define OCI_FNCODE_AQDEQARRAY  111                          /* OCIAQDeqArray */
#define OCI_FNCODE_LOBCOPY2    112                            /* OCILobCopy2 */
#define OCI_FNCODE_LOBERASE2   113                           /* OCILobErase2 */
#define OCI_FNCODE_LOBLENGTH2  114                       /* OCILobGetLength2 */
#define OCI_FNCODE_LOBLOADFROMFILE2  115              /* OCILobLoadFromFile2 */
#define OCI_FNCODE_LOBREAD2    116                            /* OCILobRead2 */
#define OCI_FNCODE_LOBTRIM2    117                            /* OCILobTrim2 */
#define OCI_FNCODE_LOBWRITE2   118                           /* OCILobWrite2 */
#define OCI_FNCODE_LOBGETSTORAGELIMIT 119           /* OCILobGetStorageLimit */
#define OCI_FNCODE_DBSTARTUP 120                             /* OCIDBStartup */
#define OCI_FNCODE_DBSHUTDOWN 121                           /* OCIDBShutdown */
#define OCI_FNCODE_LOBARRAYREAD       122                 /* OCILobArrayRead */
#define OCI_FNCODE_LOBARRAYWRITE      123                /* OCILobArrayWrite */
#define OCI_FNCODE_AQENQSTREAM        124               /* OCIAQEnqStreaming */
#define OCI_FNCODE_AQGETREPLAY        125              /* OCIAQGetReplayInfo */
#define OCI_FNCODE_AQRESETREPLAY      126            /* OCIAQResetReplayInfo */
#define OCI_FNCODE_ARRAYDESCRIPTORALLOC 127        /*OCIArrayDescriptorAlloc */
#define OCI_FNCODE_ARRAYDESCRIPTORFREE  128       /* OCIArrayDescriptorFree  */
#define OCI_FNCODE_LOBGETOPT        129                /* OCILobGetCptions */
#define OCI_FNCODE_LOBSETOPT        130                /* OCILobSetCptions */
#define OCI_FNCODE_LOBFRAGINS       131           /* OCILobFragementInsert */
#define OCI_FNCODE_LOBFRAGDEL       132           /* OCILobFragementDelete */
#define OCI_FNCODE_LOBFRAGMOV       133             /* OCILobFragementMove */
#define OCI_FNCODE_LOBFRAGREP       134          /* OCILobFragementReplace */
#define OCI_FNCODE_LOBGETDEDUPLICATEREGIONS 135/* OCILobGetDeduplicateRegions */
#define OCI_FNCODE_APPCTXSET        136        /* OCIAppCtxSet */
#define OCI_FNCODE_APPCTXCLEARALL   137         /* OCIAppCtxClearAll */

#define OCI_FNCODE_MAXFCN             137       /* maximum OCI function code */

/*---------------Statement Cache callback modes-----------------------------*/
#define OCI_CBK_STMTCACHE_STMTPURGE  0x01

/*---------------------------------------------------------------------------*/

/*-----------------------Handle Definitions----------------------------------*/
typedef struct OCIEnv           OCIEnv;            /* OCI environment handle */
typedef struct OCIError         OCIError;                /* OCI error handle */
typedef struct OCISvcCtx        OCISvcCtx;             /* OCI service handle */
typedef struct OCIStmt          OCIStmt;             /* OCI statement handle */
typedef struct OCIBind          OCIBind;                  /* OCI bind handle */
typedef struct OCIDefine        OCIDefine;              /* OCI Define handle */
typedef struct OCIDescribe      OCIDescribe;          /* OCI Describe handle */
typedef struct OCIServer        OCIServer;              /* OCI Server handle */
typedef struct OCISession       OCISession;     /* OCI Authentication handle */
typedef struct OCIComplexObject OCIComplexObject;          /* OCI COR handle */
typedef struct OCITrans         OCITrans;          /* OCI Transaction handle */
typedef struct OCISecurity      OCISecurity;          /* OCI Security handle */
typedef struct OCISubscription  OCISubscription;      /* subscription handle */

typedef struct OCICPool         OCICPool;          /* connection pool handle */
typedef struct OCISPool         OCISPool;             /* session pool handle */
typedef struct OCIAuthInfo      OCIAuthInfo;                  /* auth handle */
typedef struct OCIAdmin         OCIAdmin;                    /* admin handle */
typedef struct OCIEvent         OCIEvent;                 /* HA event handle */

/*-----------------------Descriptor Definitions------------------------------*/
typedef struct OCISnapshot      OCISnapshot;      /* OCI snapshot descriptor */
typedef struct OCIResult        OCIResult;      /* OCI Result Set Descriptor */
typedef struct OCILobLocator    OCILobLocator; /* OCI Lob Locator descriptor */
typedef struct OCILobRegion     OCILobRegion;  /* OCI Lob Regions descriptor */
typedef struct OCIParam         OCIParam;        /* OCI PARameter descriptor */
typedef struct OCIComplexObjectComp OCIComplexObjectComp;
                                                       /* OCI COR descriptor */
typedef struct OCIRowid OCIRowid;                    /* OCI ROWID descriptor */

typedef struct OCIDateTime OCIDateTime;           /* OCI DateTime descriptor */
typedef struct OCIInterval OCIInterval;           /* OCI Interval descriptor */

typedef struct OCIUcb           OCIUcb;      /* OCI User Callback descriptor */
typedef struct OCIServerDNs     OCIServerDNs;    /* OCI server DN descriptor */

/*-------------------------- AQ Descriptors ---------------------------------*/
typedef struct OCIAQEnqOptions    OCIAQEnqOptions; /* AQ Enqueue Options hdl */
typedef struct OCIAQDeqOptions    OCIAQDeqOptions; /* AQ Dequeue Options hdl */
typedef struct OCIAQMsgProperties OCIAQMsgProperties;  /* AQ Mesg Properties */
typedef struct OCIAQAgent         OCIAQAgent;         /* AQ Agent descriptor */
typedef struct OCIAQNfyDescriptor OCIAQNfyDescriptor;   /* AQ Nfy descriptor */
typedef struct OCIAQSignature     OCIAQSignature;            /* AQ Siganture */
typedef struct OCIAQListenOpts    OCIAQListenOpts;      /* AQ listen options */
typedef struct OCIAQLisMsgProps   OCIAQLisMsgProps;   /* AQ listen msg props */

/*---------------------------------------------------------------------------*/
 
/* Lob typedefs for Pro*C */
typedef struct OCILobLocator OCIClobLocator;    /* OCI Character LOB Locator */
typedef struct OCILobLocator OCIBlobLocator;       /* OCI Binary LOB Locator */
typedef struct OCILobLocator OCIBFileLocator; /* OCI Binary LOB File Locator */
/*---------------------------------------------------------------------------*/

/* Undefined value for tz in interval types*/
#define OCI_INTHR_UNK 24

  /* These defined adjustment values */
#define OCI_ADJUST_UNK            10
#define OCI_ORACLE_DATE           0
#define OCI_ANSI_DATE             1

/*------------------------ Lob-specific Definitions -------------------------*/

/*
 * ociloff - OCI Lob OFFset
 *
 * The offset in the lob data.  The offset is specified in terms of bytes for
 * BLOBs and BFILes.  Character offsets are used for CLOBs, NCLOBs.
 * The maximum size of internal lob data is 4 gigabytes.  FILE LOB 
 * size is limited by the operating system.
 */
typedef ub4 OCILobOffset;

/*
 * ocillen - OCI Lob LENgth (of lob data)
 *
 * Specifies the length of lob data in bytes for BLOBs and BFILes and in 
 * characters for CLOBs, NCLOBs.  The maximum length of internal lob
 * data is 4 gigabytes.  The length of FILE LOBs is limited only by the
 * operating system.
 */
typedef ub4 OCILobLength;
/*
 * ocilmo - OCI Lob open MOdes
 *
 * The mode specifies the planned operations that will be performed on the
 * FILE lob data.  The FILE lob can be opened in read-only mode only.
 * 
 * In the future, we may include read/write, append and truncate modes.  Append
 * is equivalent to read/write mode except that the FILE is positioned for
 * writing to the end.  Truncate is equivalent to read/write mode except that
 * the FILE LOB data is first truncated to a length of 0 before use.
 */
enum OCILobMode
{
  OCI_LOBMODE_READONLY = 1,                                     /* read-only */
  OCI_LOBMODE_READWRITE = 2             /* read_write for internal lobs only */
};
typedef enum OCILobMode OCILobMode;

/*---------------------------------------------------------------------------*/


/*----------------------------Piece Definitions------------------------------*/

/* if ocidef.h is being included in the app, ocidef.h should precede oci.h */

/* 
 * since clients may  use oci.h, ocidef.h and ocidfn.h the following defines
 * need to be guarded, usually internal clients
 */

#ifndef OCI_FLAGS
#define OCI_FLAGS
#define OCI_ONE_PIECE 0                                         /* one piece */
#define OCI_FIRST_PIECE 1                                 /* the first piece */
#define OCI_NEXT_PIECE 2                          /* the next of many pieces */
#define OCI_LAST_PIECE 3                                   /* the last piece */
#endif
/*---------------------------------------------------------------------------*/

/*--------------------------- FILE open modes -------------------------------*/
#define OCI_FILE_READONLY 1             /* readonly mode open for FILE types */
/*---------------------------------------------------------------------------*/
/*--------------------------- LOB open modes --------------------------------*/
#define OCI_LOB_READONLY 1              /* readonly mode open for ILOB types */
#define OCI_LOB_READWRITE 2                /* read write mode open for ILOBs */
#define OCI_LOB_WRITEONLY     3         /* Writeonly mode open for ILOB types*/
#define OCI_LOB_APPENDONLY    4       /* Appendonly mode open for ILOB types */
#define OCI_LOB_FULLOVERWRITE 5                 /* Completely overwrite ILOB */
#define OCI_LOB_FULLREAD      6                 /* Doing a Full Read of ILOB */

/*----------------------- LOB Buffering Flush Flags -------------------------*/
#define OCI_LOB_BUFFER_FREE   1 
#define OCI_LOB_BUFFER_NOFREE 2
/*---------------------------------------------------------------------------*/

/*---------------------------LOB Option Types -------------------------------*/
#define OCI_LOB_OPT_COMPRESS  1                       /* SECUREFILE Compress */
#define OCI_LOB_OPT_ENCRYPT   2                        /* SECUREFILE Encrypt */
#define OCI_LOB_OPT_DEDUPLICATE 4                  /* SECUREFILE Deduplicate */
#define OCI_LOB_OPT_ALLOCSIZE 8                /* SECUREFILE Allocation Size */
#define OCI_LOB_OPT_MIMETYPE 16                      /* SECUREFILE MIME Type */
#define OCI_LOB_OPT_MODTIME  32              /* SECUREFILE Modification Time */

/*------------------------   LOB Option Values ------------------------------*/
/* Compression */
#define OCI_LOB_COMPRESS_OFF  0                           /* Compression off */
#define OCI_LOB_COMPRESS_ON   1                            /* Compression on */
/* Encryption */
#define OCI_LOB_ENCRYPT_OFF   0                            /* Encryption Off */
#define OCI_LOB_ENCRYPT_ON    2                             /* Encryption On */
/* Deduplciate */
#define OCI_LOB_DEDUPLICATE_OFF 0                         /* Deduplicate Off */
#define OCI_LOB_DEDUPLICATE_ON  4                        /* Deduplicate Lobs */

/*--------------------------- OCI Statement Types ---------------------------*/

#define  OCI_STMT_UNKNOWN 0                             /* Unknown statement */
#define  OCI_STMT_SELECT  1                              /* select statement */
#define  OCI_STMT_UPDATE  2                              /* update statement */
#define  OCI_STMT_DELETE  3                              /* delete statement */
#define  OCI_STMT_INSERT  4                              /* Insert Statement */
#define  OCI_STMT_CREATE  5                              /* create statement */
#define  OCI_STMT_DROP    6                                /* drop statement */
#define  OCI_STMT_ALTER   7                               /* alter statement */
#define  OCI_STMT_BEGIN   8                   /* begin ... (pl/sql statement)*/
#define  OCI_STMT_DECLARE 9                /* declare .. (pl/sql statement ) */
#define  OCI_STMT_CALL    10                      /* corresponds to kpu call */
/*---------------------------------------------------------------------------*/

/*--------------------------- OCI Parameter Types ---------------------------*/
#define OCI_PTYPE_UNK                 0                         /* unknown   */
#define OCI_PTYPE_TABLE               1                         /* table     */
#define OCI_PTYPE_VIEW                2                         /* view      */
#define OCI_PTYPE_PROC                3                         /* procedure */
#define OCI_PTYPE_FUNC                4                         /* function  */
#define OCI_PTYPE_PKG                 5                         /* package   */
#define OCI_PTYPE_TYPE                6                 /* user-defined type */
#define OCI_PTYPE_SYN                 7                         /* synonym   */
#define OCI_PTYPE_SEQ                 8                         /* sequence  */
#define OCI_PTYPE_COL                 9                         /* column    */
#define OCI_PTYPE_ARG                 10                        /* argument  */
#define OCI_PTYPE_LIST                11                        /* list      */
#define OCI_PTYPE_TYPE_ATTR           12    /* user-defined type's attribute */
#define OCI_PTYPE_TYPE_COLL           13        /* collection type's element */
#define OCI_PTYPE_TYPE_METHOD         14       /* user-defined type's method */
#define OCI_PTYPE_TYPE_ARG            15   /* user-defined type method's arg */
#define OCI_PTYPE_TYPE_RESULT         16/* user-defined type method's result */
#define OCI_PTYPE_SCHEMA              17                           /* schema */
#define OCI_PTYPE_DATABASE            18                         /* database */
#define OCI_PTYPE_RULE                19                             /* rule */
#define OCI_PTYPE_RULE_SET            20                         /* rule set */
#define OCI_PTYPE_EVALUATION_CONTEXT  21               /* evaluation context */
#define OCI_PTYPE_TABLE_ALIAS         22                      /* table alias */
#define OCI_PTYPE_VARIABLE_TYPE       23                    /* variable type */
#define OCI_PTYPE_NAME_VALUE          24                  /* name value pair */

/*---------------------------------------------------------------------------*/

/*----------------------------- OCI List Types ------------------------------*/
#define OCI_LTYPE_UNK           0                               /* unknown   */
#define OCI_LTYPE_COLUMN        1                             /* column list */
#define OCI_LTYPE_ARG_PROC      2                 /* procedure argument list */
#define OCI_LTYPE_ARG_FUNC      3                  /* function argument list */
#define OCI_LTYPE_SUBPRG        4                         /* subprogram list */
#define OCI_LTYPE_TYPE_ATTR     5                          /* type attribute */
#define OCI_LTYPE_TYPE_METHOD   6                             /* type method */
#define OCI_LTYPE_TYPE_ARG_PROC 7    /* type method w/o result argument list */
#define OCI_LTYPE_TYPE_ARG_FUNC 8      /* type method w/result argument list */
#define OCI_LTYPE_SCH_OBJ       9                      /* schema object list */
#define OCI_LTYPE_DB_SCH        10                   /* database schema list */
#define OCI_LTYPE_TYPE_SUBTYPE  11                           /* subtype list */
#define OCI_LTYPE_TABLE_ALIAS   12                       /* table alias list */
#define OCI_LTYPE_VARIABLE_TYPE 13                     /* variable type list */
#define OCI_LTYPE_NAME_VALUE    14                        /* name value list */

/*---------------------------------------------------------------------------*/

/*-------------------------- Memory Cartridge Services ---------------------*/
#define OCI_MEMORY_CLEARED  1

/*-------------------------- Pickler Cartridge Services ---------------------*/
typedef struct OCIPicklerTdsCtx OCIPicklerTdsCtx;
typedef struct OCIPicklerTds OCIPicklerTds;
typedef struct OCIPicklerImage OCIPicklerImage;
typedef struct OCIPicklerFdo OCIPicklerFdo;
typedef ub4 OCIPicklerTdsElement;

typedef struct OCIAnyData OCIAnyData;

typedef struct OCIAnyDataSet OCIAnyDataSet;
typedef struct OCIAnyDataCtx OCIAnyDataCtx;

/*---------------------------------------------------------------------------*/

/*--------------------------- User Callback Constants -----------------------*/
#define OCI_UCBTYPE_ENTRY       1                          /* entry callback */
#define OCI_UCBTYPE_EXIT        2                           /* exit callback */
#define OCI_UCBTYPE_REPLACE     3                    /* replacement callback */

/*---------------------------------------------------------------------------*/

/*--------------------- NLS service type and constance ----------------------*/
#define OCI_NLS_DAYNAME1      1                    /* Native name for Monday */
#define OCI_NLS_DAYNAME2      2                   /* Native name for Tuesday */
#define OCI_NLS_DAYNAME3      3                 /* Native name for Wednesday */
#define OCI_NLS_DAYNAME4      4                  /* Native name for Thursday */
#define OCI_NLS_DAYNAME5      5                    /* Native name for Friday */
#define OCI_NLS_DAYNAME6      6              /* Native name for for Saturday */
#define OCI_NLS_DAYNAME7      7                /* Native name for for Sunday */
#define OCI_NLS_ABDAYNAME1    8        /* Native abbreviated name for Monday */
#define OCI_NLS_ABDAYNAME2    9       /* Native abbreviated name for Tuesday */
#define OCI_NLS_ABDAYNAME3    10    /* Native abbreviated name for Wednesday */
#define OCI_NLS_ABDAYNAME4    11     /* Native abbreviated name for Thursday */
#define OCI_NLS_ABDAYNAME5    12       /* Native abbreviated name for Friday */
#define OCI_NLS_ABDAYNAME6    13 /* Native abbreviated name for for Saturday */
#define OCI_NLS_ABDAYNAME7    14   /* Native abbreviated name for for Sunday */
#define OCI_NLS_MONTHNAME1    15                  /* Native name for January */
#define OCI_NLS_MONTHNAME2    16                 /* Native name for February */
#define OCI_NLS_MONTHNAME3    17                    /* Native name for March */
#define OCI_NLS_MONTHNAME4    18                    /* Native name for April */
#define OCI_NLS_MONTHNAME5    19                      /* Native name for May */
#define OCI_NLS_MONTHNAME6    20                     /* Native name for June */
#define OCI_NLS_MONTHNAME7    21                     /* Native name for July */
#define OCI_NLS_MONTHNAME8    22                   /* Native name for August */
#define OCI_NLS_MONTHNAME9    23                /* Native name for September */
#define OCI_NLS_MONTHNAME10   24                  /* Native name for October */
#define OCI_NLS_MONTHNAME11   25                 /* Native name for November */
#define OCI_NLS_MONTHNAME12   26                 /* Native name for December */
#define OCI_NLS_ABMONTHNAME1  27      /* Native abbreviated name for January */
#define OCI_NLS_ABMONTHNAME2  28     /* Native abbreviated name for February */
#define OCI_NLS_ABMONTHNAME3  29        /* Native abbreviated name for March */
#define OCI_NLS_ABMONTHNAME4  30        /* Native abbreviated name for April */
#define OCI_NLS_ABMONTHNAME5  31          /* Native abbreviated name for May */
#define OCI_NLS_ABMONTHNAME6  32         /* Native abbreviated name for June */
#define OCI_NLS_ABMONTHNAME7  33         /* Native abbreviated name for July */
#define OCI_NLS_ABMONTHNAME8  34       /* Native abbreviated name for August */
#define OCI_NLS_ABMONTHNAME9  35    /* Native abbreviated name for September */
#define OCI_NLS_ABMONTHNAME10 36      /* Native abbreviated name for October */
#define OCI_NLS_ABMONTHNAME11 37     /* Native abbreviated name for November */
#define OCI_NLS_ABMONTHNAME12 38     /* Native abbreviated name for December */
#define OCI_NLS_YES           39   /* Native string for affirmative response */
#define OCI_NLS_NO            40                 /* Native negative response */
#define OCI_NLS_AM            41           /* Native equivalent string of AM */
#define OCI_NLS_PM            42           /* Native equivalent string of PM */
#define OCI_NLS_AD            43           /* Native equivalent string of AD */
#define OCI_NLS_BC            44           /* Native equivalent string of BC */
#define OCI_NLS_DECIMAL       45                        /* decimal character */
#define OCI_NLS_GROUP         46                          /* group separator */
#define OCI_NLS_DEBIT         47                   /* Native symbol of debit */
#define OCI_NLS_CREDIT        48                  /* Native sumbol of credit */
#define OCI_NLS_DATEFORMAT    49                       /* Oracle date format */
#define OCI_NLS_INT_CURRENCY  50            /* International currency symbol */
#define OCI_NLS_LOC_CURRENCY  51                   /* Locale currency symbol */
#define OCI_NLS_LANGUAGE      52                            /* Language name */
#define OCI_NLS_ABLANGUAGE    53           /* Abbreviation for language name */
#define OCI_NLS_TERRITORY     54                           /* Territory name */
#define OCI_NLS_CHARACTER_SET 55                       /* Character set name */
#define OCI_NLS_LINGUISTIC_NAME    56                     /* Linguistic name */
#define OCI_NLS_CALENDAR      57                            /* Calendar name */
#define OCI_NLS_DUAL_CURRENCY 78                     /* Dual currency symbol */
#define OCI_NLS_WRITINGDIR    79               /* Language writing direction */
#define OCI_NLS_ABTERRITORY   80                   /* Territory Abbreviation */
#define OCI_NLS_DDATEFORMAT   81               /* Oracle default date format */
#define OCI_NLS_DTIMEFORMAT   82               /* Oracle default time format */
#define OCI_NLS_SFDATEFORMAT  83       /* Local string formatted date format */
#define OCI_NLS_SFTIMEFORMAT  84       /* Local string formatted time format */
#define OCI_NLS_NUMGROUPING   85                   /* Number grouping fields */
#define OCI_NLS_LISTSEP       86                           /* List separator */
#define OCI_NLS_MONDECIMAL    87               /* Monetary decimal character */
#define OCI_NLS_MONGROUP      88                 /* Monetary group separator */
#define OCI_NLS_MONGROUPING   89                 /* Monetary grouping fields */
#define OCI_NLS_INT_CURRENCYSEP 90       /* International currency separator */
#define OCI_NLS_CHARSET_MAXBYTESZ 91     /* Maximum character byte size      */
#define OCI_NLS_CHARSET_FIXEDWIDTH 92    /* Fixed-width charset byte size    */
#define OCI_NLS_CHARSET_ID    93                         /* Character set id */
#define OCI_NLS_NCHARSET_ID   94                        /* NCharacter set id */

#define OCI_NLS_MAXBUFSZ   100 /* Max buffer size may need for OCINlsGetInfo */

#define OCI_NLS_BINARY            0x1           /* for the binary comparison */
#define OCI_NLS_LINGUISTIC        0x2           /* for linguistic comparison */
#define OCI_NLS_CASE_INSENSITIVE  0x10    /* for case-insensitive comparison */

#define OCI_NLS_UPPERCASE         0x20               /* convert to uppercase */
#define OCI_NLS_LOWERCASE         0x40               /* convert to lowercase */

#define OCI_NLS_CS_IANA_TO_ORA   0   /* Map charset name from IANA to Oracle */
#define OCI_NLS_CS_ORA_TO_IANA   1   /* Map charset name from Oracle to IANA */
#define OCI_NLS_LANG_ISO_TO_ORA  2   /* Map language name from ISO to Oracle */
#define OCI_NLS_LANG_ORA_TO_ISO  3   /* Map language name from Oracle to ISO */
#define OCI_NLS_TERR_ISO_TO_ORA  4   /* Map territory name from ISO to Oracle*/
#define OCI_NLS_TERR_ORA_TO_ISO  5   /* Map territory name from Oracle to ISO*/
#define OCI_NLS_TERR_ISO3_TO_ORA 6   /* Map territory name from 3-letter ISO */
                                     /* abbreviation to Oracle               */
#define OCI_NLS_TERR_ORA_TO_ISO3 7   /* Map territory name from Oracle to    */
                                     /* 3-letter ISO abbreviation            */
#define OCI_NLS_LOCALE_A2_ISO_TO_ORA 8
                                      /*Map locale name from A2 ISO to oracle*/
#define OCI_NLS_LOCALE_A2_ORA_TO_ISO 9
                                      /*Map locale name from oracle to A2 ISO*/

typedef struct OCIMsg  OCIMsg;
typedef ub4            OCIWchar;

#define OCI_XMLTYPE_CREATE_OCISTRING 1
#define OCI_XMLTYPE_CREATE_CLOB      2
#define OCI_XMLTYPE_CREATE_BLOB      3

/*------------------------- Kerber Authentication Modes ---------------------*/
#define OCI_KERBCRED_PROXY               1 /* Apply Kerberos Creds for Proxy */
#define OCI_KERBCRED_CLIENT_IDENTIFIER   2/*Apply Creds for Secure Client ID */

/*------------------------- Database Startup Flags --------------------------*/
#define OCI_DBSTARTUPFLAG_FORCE 0x00000001  /* Abort running instance, start */
#define OCI_DBSTARTUPFLAG_RESTRICT 0x00000002      /* Restrict access to DBA */

/*------------------------- Database Shutdown Modes -------------------------*/
#define OCI_DBSHUTDOWN_TRANSACTIONAL      1 /* Wait for all the transactions */
#define OCI_DBSHUTDOWN_TRANSACTIONAL_LOCAL 2  /* Wait for local transactions */
#define OCI_DBSHUTDOWN_IMMEDIATE           3      /* Terminate and roll back */
#define OCI_DBSHUTDOWN_ABORT              4 /* Terminate and don't roll back */
#define OCI_DBSHUTDOWN_FINAL              5              /* Orderly shutdown */

/*------------------------- Version information -----------------------------*/
#define OCI_MAJOR_VERSION  11                       /* Major release version */
#define OCI_MINOR_VERSION  1                        /* Minor release version */

/*---------------------- OCIIOV structure definitions -----------------------*/
struct OCIIOV
{
 void     *bfp;                            /* The Pointer to the data buffer */
 ub4       bfl;                                 /* Length of the Data Buffer */
};
typedef struct OCIIOV OCIIOV;

/*--------------------------------------------------------------------------- 
                     PRIVATE TYPES AND CONSTANTS 
  ---------------------------------------------------------------------------*/
 
/* None */

/*--------------------------------------------------------------------------- 
                           PUBLIC FUNCTIONS 
  ---------------------------------------------------------------------------*/

/* see ociap.h or ocikp.h */
 
/*--------------------------------------------------------------------------- 
                          PRIVATE FUNCTIONS 
  ---------------------------------------------------------------------------*/

/* None */

 
#endif                                              /* OCI_ORACLE */


/* more includes */

#ifndef OCI1_ORACLE
#include <oci1.h>
#endif

#ifndef ORO_ORACLE
#include <oro.h>
#endif

#ifndef ORI_ORACLE
#include <ori.h>
#endif

#ifndef ORL_ORACLE
#include <orl.h>
#endif

#ifndef ORT_ORACLE
#include <ort.h>
#endif

#ifndef OCIEXTP_ORACLE
#include <ociextp.h>
#endif

#include <ociapr.h>
#include <ociap.h>

#ifndef OCIXMLDB_ORACLE
#include <ocixmldb.h>
#endif

#ifndef OCI8DP_ORACLE
#include <oci8dp.h>         /* interface definitions for the direct path api */
#endif

#ifndef OCIEXTP_ORACLE
#include <ociextp.h>
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

