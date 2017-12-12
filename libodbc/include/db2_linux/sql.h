/******************************************************************************
** 
** Source File Name: SQL
** 
** (C) COPYRIGHT International Business Machines Corp. 1987, 2006
** All Rights Reserved
** Licensed Materials - Property of IBM
** 
** US Government Users Restricted Rights - Use, duplication or
** disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
** 
** Function = Include File defining:
**              System Constants
**              National Language Support Information
**              SQLCA / SQLDA Constants
**              Interface to BINDER and PRECOMPILER
**              Error Message Retrieval Interface
**              Authorization Constants
** 
** Operating System: LINUX
** 
*******************************************************************************/
#ifndef SQL_H_SQL
#define SQL_H_SQL

#ifdef __cplusplus
extern "C" {
#endif


/* Note: _SQLOLDCHAR defaults to 'char'.  See sqlsystm.h for details.         */

#include <stddef.h>                    
#include "sqlsystm.h"                  /* System dependent defines  */

#ifndef SQLCODE
#include "sqlca.h"                     /* Required include file    */
#endif


/* Release Identifier Constants                                               */

#define SQL_RELPRE6            0       /* Pre Version 6.1.0.0                 */
#define SQL_REL6100            6010000 /* V6.1.0.0                            */
#define SQL_REL7100            7010000 /* V7.1.0.0                            */
#define SQL_REL7102            7010200 /* V7.1.2.0                            */
#define SQL_REL7200            7020100 /* V7.2.0.0                            */
#define SQL_REL7201            7020200 /* V7.2.1.0                            */
#define SQL_REL7204            7020400 /* V7.2.4.0                            */
#define SQL_REL8100            8010000 /* V8.1.0.0                            */
#define SQL_REL8101            8010100 /* V8.1.1.0                            */
#define SQL_REL8102            8010200 /* V8.1.2.0                            */
#define SQL_REL8103            8010300 /* V8.1.3.0                            */
#define SQL_REL8104            8010400 /* V8.1.4.0                            */
#define SQL_REL8105            8010500 /* V8.1.5.0                            */
#define SQL_REL8106            8010600 /* V8.1.6.0                            */
#define SQL_REL8200            8020000 /* V8.2.0.0                            */
#define SQL_REL8201            8020100 /* V8.2.1.0                            */
#define SQL_REL8202            8020200 /* V8.2.2.0                            */
#define SQL_REL9000            9000000 /* V9.0.0.0                            */
#define SQL_REL9100            9010000 /* V9.1.0.0                            */
#define SQL_FUTUREL            9999999 /* Future Release                      */

/* System Constants                                                           */

#ifndef   SQL_RC_OK
#define SQL_RC_OK              0       /* successful execution                */
#endif

#define SQL_KEYPMAX            64      /* Maximum nbr of key parts in Index   */
#define SQL_KEYLMAX            8192    /* Maximum key length                  */
#define SQL_KEYLMAX_4KPAGE     1024    /* Maximum key length for 4K page      */
#define SQL_KEYLMAX_8KPAGE     2048    /* Maximum key length for 8K page      */
#define SQL_KEYLMAX_16KPAGE    4096    /* Maximum key length for 16K page     */
#define SQL_KEYLMAX_32KPAGE    8192    /* Maximum key length for 32K page     */
#define SQL_SORTFLDLMT         32677   /* Maximum size of field for sort      */
#define SQL_MAXRECL_4K         4005    /* Maximum record length on a 4K page  */
#define SQL_MAXRECL_8K         8101    /* Maximum record length on a 8K page  */
#define SQL_MAXRECL            32677   /* Maximum record length               */
#define SQL_MAXTABLES          15      /* Maximum nbr of tables in a SELECT   */
#define SQL_MAXVARS_STMT       32767   /* Maximum nbr of Host Vars per stmt   */
#define SQL_MAXCOLS            3000    /* Internal max nbr of columns in a    */
                                       /* table                               */
#define SQL_MAXCOLS_EXT        1012    /* External max nbr of columns in a    */
                                       /* table                               */
#define SQL_MAXSEL_ITEMS       1012    /* Maximum nbr of items in a SELECT    */
#define SQL_MAXPARMS           90      /* Maximum nbr of parms in a function  */
#define SQL_MAX_STMT_SIZ       2097152 /* Maximum statement size              */

#define SQL_SMALL_LENGTH       2       /* Size of a SMALLINT                  */
#define SQL_MAXSMALLVAL        32767   /* Maximum value of a SMALLINT         */
#define   SQL_MINSMALLVAL (-(SQL_MAXSMALLVAL)-1) /* Minimum value of a SMALLINT */
#define SQL_INT_LENGTH         4       /* Size of an INTEGER                  */
#define SQL_MAXINTVAL          2147483647 /* Maximum value of an INTEGER      */
#define   SQL_MININTVAL (-(SQL_MAXINTVAL)-1) /* Minimum value of an INTEGER  */

#define SQL_BIGINT_LENGTH      8       /*           Size of a BIGINT          */
#ifndef SQL_NO_NATIVE_BIGINT_SUPPORT
#define SQL_MAXBIGINTVAL DB2_CONSTRUCT_BIGINT_CONSTANT(9223372036854775807)  /* Maximum value of a BIGINT */
#define SQL_MINBIGINTVAL (-(SQL_MAXBIGINTVAL)-1) /* Minimum value of a BIGINT */
#endif

#define SQL_FLOAT_LENGTH       8       /* Size of a FLOAT                     */
#define SQL_FLOAT4_LENGTH      4       /* Size of a 4-byte FLOAT              */
#define SQL_MAXSFLTPREC        24      /* Maximum prec for small float        */
#define SQL_MINSFLTPREC        1       /* Minimum prec for small float        */
#define SQL_MAXFLOATPREC       53      /* Minimum prec for any float          */
#define SQL_DEFDEC_PRECISION   5       /* Default precision for DECIMAL       */
#define SQL_DEFDEC_SCALE       0       /* Default scale for DECIMAL           */
#define SQL_MAXDECIMAL         31      /* Maximum scale/prec. for DECIMAL     */
#define SQL_DEFCHAR            1       /* Default length for a CHAR           */
#define SQL_DEFWCHAR           1       /* Default length for a graphic        */
#define SQL_MAXCHAR            254     /* Maximum length of a CHAR            */
#define SQL_MAXLSTR            255     /* Maximum length of an LSTRING        */
#define SQL_MAXVCHAR           (SQL_MAXRECL - 5) /* Maximum length of a       */
                                       /* VARCHAR                             */
#define SQL_MAXVGRAPH          SQL_MAXVCHAR/2 /* Maximum length of a          */
                                       /* VARGRAPHIC                          */
#define SQL_MAXBLOB            2147483647 /* Max. length of a BLOB host var   */
#define SQL_MAXCLOB            2147483647 /* Max. length of a CLOB host var   */
#define SQL_MAXDBCLOB          1073741823 /* Max. length of an DBCLOB host    */
                                       /* var                                 */
#define SQL_LOBLOCATOR_LEN     4       /* Length of a LOB locator host var    */
#define SQL_LOBFILE_LEN        267     /* Length of a LOB file host var       */
#define SQL_VCHAROH            4       /* Overhead for VARCHAR in record      */
#define SQL_VARCOL_OH          4       /* Overhead for variable length type   */
                                       /* in record                           */
#define SQL_VARKEY_OH          2       /* Overhead for variable keyparts      */
#define SQL_LONGMAX            32700   /* Maximum length of a LONG VARCHAR    */
#define SQL_LONGGRMAX          16350   /* Max. length of a LONG VARGRAPHIC    */
#define SQL_LVCHAROH           24      /* Overhead for LONG VARCHAR in        */
                                       /* record                              */
#define SQL_LOBCHAROH          312     /* Overhead for LOB in record          */
#define SQL_BLOB_MAXLEN        2147483647 /* BLOB maximum length, in bytes    */
#define SQL_CLOB_MAXLEN        2147483647 /* CLOB maximum length, in chars    */
#define SQL_DBCLOB_MAXLEN      1073741823 /* maxlen for dbcs lobs             */
#define SQL_TIME_LENGTH        3       /* Size of a TIME field                */
#define SQL_TIME_STRLEN        8       /* Size of a TIME field output         */
#define SQL_TIME_MINSTRLEN     5       /* Size of a non-USA TIME field        */
                                       /* output without seconds              */
#define SQL_DATE_LENGTH        4       /* Size of a DATE field                */
#define SQL_DATE_STRLEN        10      /* Size of a DATE field output         */
#define SQL_STAMP_LENGTH       10      /* Size of a TIMESTAMP field           */
#define SQL_STAMP_STRLEN       26      /* Size of a TIMESTAMP field output    */
#define SQL_STAMP_MINSTRLEN    19      /* Size of a TIMESTAMP field output    */
                                       /* without microseconds                */
#define SQL_BOOLEAN_LENGTH     1       /* Size of a BOOLEAN field             */
#define SQL_DATALINK_LENGTH    254     /* Size of a DATALINK field            */
#define SQL_IND_LENGTH         2       /* Size of an indicator value          */

#define SQL_DECFLOAT16_LENGTH  8       /* Size of a DECFLOAT16 field          */
#define SQL_DECFLOAT34_LENGTH  16      /* Size of a DECFLOAT34 field          */
#define SQL_MAXDECFLOAT        34      /* Maximum precision for DECFLOAT      */

#define SQL_MAX_PNAME_LENGTH   254     /* Max size of Stored Proc Name        */
#define SQL_MAX_IDENT          128     /* Maximum length of Identifer         */
#define SQL_LG_IDENT           18      /* Maximum length of Long Identifer    */
#define SQL_SH_IDENT           8       /* Maximum length of Short Identifer   */
#define SQL_MN_IDENT           1       /* Minimum length of Identifiers       */
#define SQL_MAX_VAR_NAME       255     /* Max size of Host Variable Name      */
#define SQL_PDB_MAP_SIZE       4096    /* Number of partitions in a pmap      */
#define SQL_MAX_NUM_PART_KEYS  500     /* Max # of Partition Keys             */
#define SQL_ZONEDDECIMAL_FORMAT 0x20   /* decimal columns for sqlugrpn are    */
                                       /* in zoneddecimal format              */
#define SQL_IMPLIEDDECIMAL_FORMAT 0x10 /* decimal columns for sqlugrpn are    */
                                       /* in implieddecimal format            */
#define SQL_BINARYNUMERICS_FORMAT 0x4  /* numeric columns for sqlugrpn are    */
                                       /* in binary format                    */
#define SQL_PACKEDDECIMAL_FORMAT 0x8   /* decimal columns for sqlugrpn are    */
                                       /* in packeddecimal format             */
#define SQL_CHARSTRING_FORMAT  0x0     /* numeric/decimal columns for         */
                                       /* sqlugrpn are in character string    */
                                       /* format                              */
#define SQL_KILO_VALUE         1024    /* # of bytes in a kilobyte            */
#define SQL_MEGA_VALUE         1048576 /* # of bytes in a megabyte            */
#define SQL_GIGA_VALUE         1073741824 /* # of bytes in a gigabyte         */

#define SQLB_MAX_CONTAIN_NAME_SZ 256   /* size of container name for api's    */
                                       /* (includes 1 byte for C NULL         */
                                       /* terminator)                         */
/* System types                                                               */
typedef signed short SQL_PDB_NODE_TYPE;/* Datatype of PDB node               */
typedef int          SQL_PDB_PORT_TYPE;/* Datatype of PDB port               */
#define SQL_PDB_MAX_NUM_NODE   1000    /* limit for max. # of nodes           */
/* information related to logical node name                                   */
#define SQL_PDB_NODE_NUM_DIGIT 4       /* no. of digits for node number in    */
                                       /* node name                           */
#define SQL_PDB_NODE_NAME_LEN  8       /* length of logical node name         */
#define SQL_NODE_NUM_TO_NAME(name_p,node_num) \
        0 <= node_num ? sprintf(name_p,"NODE%.*d", SQL_PDB_NODE_NUM_DIGIT, node_num) : \
                        sprintf(name_p,"NODE%.*d", SQL_PDB_NODE_NUM_DIGIT-1, node_num)
/* Codepages                                                                  */
#define SQL_CP_367             367     /* Codepage 367 - EUC single byte      */
#define SQL_CP_420             420     /* CCSID x01A4, (CP420, ST4)           */
#define SQL_CP_424             424     /* CCSID x01A8, (CP424, ST4)           */
#define SQL_CP_425             425     /* CCSID x01A9, (CP420, ST5)           */
#define SQL_CP_437             437     /* Codepage 437 - US, Europe           */
#define SQL_CP_737             737     /* Codepage 737 - WIN Greece           */
#define SQL_CP_806             806     /* Codepage 806 - ISCII, India         */
#define SQL_CP_813             813     /* Codepage 813 - AIX Greece           */
#define SQL_CP_819             819     /* Codepage 819 - ISO 8859-1           */
#define SQL_CP_850             850     /* Codepage 850 - International PC     */
#define SQL_CP_855             855     /* Codepage 855 - OS2 Cyrillic         */
#define SQL_CP_852             852     /* Codepage 852 - OS2 Latin2           */
#define SQL_CP_856             856     /* Codepage 856 - Hebrew               */
#define SQL_CP_857             857     /* Codepage 857 - OS2 Turkey           */
#define SQL_CP_860             860     /* Codepage 860 - Portuguese           */
#define SQL_CP_862             862     /* Codepage 862 - OS2 Hebrew           */
#define SQL_CP_863             863     /* Codepage 863 - Canadian-French      */
#define SQL_CP_864             864     /* Codepage 864 - OS2 Arabic           */
#define SQL_CP_865             865     /* Codepage 865 - Norway, Denmark      */
#define SQL_CP_866             866     /* Codepage 866 - Russia               */
#define SQL_CP_867             867     /* Codepage 867 - OS2 Hebrew           */
#define SQL_CP_869             869     /* Codepage 869 - OS2 Greece           */
#define SQL_CP_874             874     /* Codepage 874 - OS2/AIX Thailand     */
#define SQL_CP_878             878     /* Codepage 878 - KOI-8R Russia        */
#define SQL_CP_891             891     /* Codepage 891 - Korean               */
#define SQL_CP_897             897     /* Codepage 897 - Japanese             */
#define SQL_CP_903             903     /* Codepage 903 - Chinese              */
#define SQL_CP_904             904     /* Codepage 904 - Taiwan               */
#define SQL_CP_912             912     /* Codepage 912 - AIX Latin2           */
#define SQL_CP_915             915     /* Codepage 915 - AIX Cyrillic         */
#define SQL_CP_916             916     /* Codepage 916 - AIX Hebrew           */
#define SQL_CP_920             920     /* Codepage 920 - AIX Turkey           */
#define SQL_CP_921             921     /* Codepage 921 - Latvia, Lithuania    */
#define SQL_CP_922             922     /* Codepage 922 - Estonia              */
#define SQL_CP_923             923     /* Codepage 923 - ISO 8859-15          */
#define SQL_CP_1004            1004    /* Codepage 1004 - MS-WINDOWS          */
#define SQL_CP_1040            1040    /* Codepage 1040 - Extended Korean     */
#define SQL_CP_1041            1041    /* Codepage 1041 - Extended Japanese   */
#define SQL_CP_1042            1042    /* Codepage 1042 - Extended Chinese    */
#define SQL_CP_1043            1043    /* Codepage 1043 - Extended Taiwan     */
#define SQL_CP_1046            1046    /* Codepage 1046 - AIX Arabic          */
#define SQL_CP_1051            1051    /* Codepage 1051 - HP Roman8           */
#define SQL_CP_1088            1088    /* Codepage 1088 - Korea Std           */
#define SQL_CP_1089            1089    /* Codepage 1089 - AIX Arabic          */
#define SQL_CP_1114            1114    /* Codepage 1114 - Big-5 & GBK         */
#define SQL_CP_1115            1115    /* Codepage 1115 - China GB            */
#define SQL_CP_1124            1124    /* Codepage 1124 - AIX Ukraine         */
#define SQL_CP_1125            1125    /* Codepage 1125 - OS/2 Ukraine        */
#define SQL_CP_1126            1126    /* Codepage 1126 - Windows Korean Std  */
#define SQL_CP_1129            1129    /* Codepage 1129 - Vietnamese          */
#define SQL_CP_1131            1131    /* Codepage 1131 - OS/2 Belarus        */
#define SQL_CP_1163            1163    /* Codepage 1163 - Vietnamese          */
#define SQL_CP_1167            1167    /* KOI8-RU - Belarus                   */
#define SQL_CP_1168            1168    /* KOI8-U - Ukraine                    */
#define SQL_CP_1250            1250    /* Codepage 1250 - Windows Latin-2     */
#define SQL_CP_1251            1251    /* Codepage 1251 - Windows Cyrillic    */
#define SQL_CP_1252            1252    /* Codepage 1252 - Windows Latin-1     */
#define SQL_CP_1253            1253    /* Codepage 1253 - Windows Greek       */
#define SQL_CP_1254            1254    /* Codepage 1254 - Windows Turkish     */
#define SQL_CP_1255            1255    /* Codepage 1255 - Windows Hebrew      */
#define SQL_CP_1256            1256    /* Codepage 1256 - Windows Arabic      */
#define SQL_CP_1257            1257    /* Codepage 1257 - Windows Baltic      */
#define SQL_CP_1258            1258    /* Codepage 1258 - Windows Vietnamese  */
#define SQL_CP_1275            1275    /* Codepage 1275 - Mac Latin-1         */
#define SQL_CP_1280            1280    /* Codepage 1280 - Mac Greek           */
#define SQL_CP_1281            1281    /* Codepage 1281 - Mac Turkish         */
#define SQL_CP_1282            1282    /* Codepage 1282 - Mac Latin-2         */
#define SQL_CP_1283            1283    /* Codepage 1283 - Mac Cyrillic        */
#define SQL_CP_62208           62208   /* CCSID xF300, (CP856, ST4)           */
#define SQL_CP_62209           62209   /* CCSID xF301, (CP862, ST4)           */
#define SQL_CP_62210           62210   /* CCSID xF302, (CP916, ST4)           */
#define SQL_CP_62213           62213   /* CCSID xF305, (CP862, ST5)           */
#define SQL_CP_62220           62220   /* CCSID xF30C, (CP856, ST6)           */
#define SQL_CP_62221           62221   /* CCSID xF30D, (CP862, ST6)           */
#define SQL_CP_62222           62222   /* CCSID xF30E, (CP916, ST6)           */
#define SQL_CP_62223           62223   /* CCSID xF30F, (CP1255, ST6)          */
#define SQL_CP_62225           62225   /* CCSID xF311, (CP864, ST6)           */
#define SQL_CP_62226           62226   /* CCSID xF312, (CP1046, ST6)          */
#define SQL_CP_62227           62227   /* CCSID xF313, (CP1089, ST6)          */
#define SQL_CP_62228           62228   /* CCSID xF314, (CP1256, ST6)          */
#define SQL_CP_62230           62230   /* CCSID xF316, (CP856, ST8)           */
#define SQL_CP_62231           62231   /* CCSID xF317, (CP862, ST8)           */
#define SQL_CP_62232           62232   /* CCSID xF318, (CP916, ST8)           */
#define SQL_CP_62236           62236   /* CCSID xF31C, (CP856, ST10)          */
#define SQL_CP_62238           62238   /* CCSID xF31E, (CP916, ST10)          */
#define SQL_CP_62239           62239   /* CCSID xF31F, (CP1255, ST10)         */
#define SQL_CP_62241           62241   /* CCSID xF321, (CP856, ST11)          */
#define SQL_CP_62242           62242   /* CCSID xF322, (CP862, ST11)          */
#define SQL_CP_62243           62243   /* CCSID xF323, (CP916, ST11)          */
#define SQL_CP_62244           62244   /* CCSID xF324, (CP1255, ST11)         */
#define SQL_CP_UNKNOWN         57344   /* CCSID xE000, (Unknown or            */
                                       /* unsupported)                        */
#define SQL_CP_1162            1162    /* CCSID 1162 - Windows Thailand       */
                                       /* (with Euro)                         */
#define SQL_CP_5222            5222    /* CCSID 5222 - Windows Korea          */
#define SQL_CP_5346            5346    /* CCSID 5346 - Windows Latin-2 (v2    */
                                       /* with Euro)                          */
#define SQL_CP_5347            5347    /* CCSID 5347 - Windows Cyrillic (v2   */
                                       /* with Euro)                          */
#define SQL_CP_5348            5348    /* CCSID 5348 - Windows Latin-1 (v2    */
                                       /* with Euro)                          */
#define SQL_CP_5349            5349    /* CCSID 5349 - Windows Greece (v2     */
                                       /* with Euro)                          */
#define SQL_CP_5350            5350    /* CCSID 5350 - Windows Turkey (v2     */
                                       /* with Euro)                          */
#define SQL_CP_5351            5351    /* CCSID 5351 - Windows Hebrew ST5     */
                                       /* (v2 with Euro)                      */
#define SQL_CP_5352            5352    /* CCSID 5352 - Windows Arabic ST5     */
                                       /* (v2 with Euro)                      */
#define SQL_CP_5353            5353    /* CCSID 5353 - Windows Baltic (v2     */
                                       /* with Euro)                          */
#define SQL_CP_5354            5354    /* CCSID 5354 - Windows Vietnam (v2    */
                                       /* with Euro)                          */
#define SQL_CP_62215           62215   /* CCSID 62215 - Windows Hebrew ST4    */
#define SQL_CP_62237           62237   /* CCSID 62237 - Windows Hebrew ST8    */
#define SQL_CP_895             895     /* CCSID 895 - Japan 7-bit Latin       */
#define SQL_CP_901             901     /* CCSID 901 - Baltic 8-bit (with      */
                                       /* Euro)                               */
#define SQL_CP_902             902     /* CCSID 902 - Estonia ISO-8 (with     */
                                       /* Euro)                               */
#define SQL_CP_1008            1008    /* CCSID 1008 - Arabic 8-bit ISO       */
                                       /* ASCII                               */
#define SQL_CP_1155            1155    /* CCSID 1155 - Turkey Latin-5 (with   */
                                       /* Euro)                               */
#define SQL_CP_4909            4909    /* CCSID 4909 - Greece, Latin ISO-8    */
                                       /* (with Euro)                         */
#define SQL_CP_5104            5104    /* CCSID 5104 - Arabic 8-bit ISO       */
                                       /* ASCII (with Euro)                   */
#define SQL_CP_9005            9005    /* CCSID 9005 - Greece, Latin ISO      */
                                       /* 8859-7:2003 (with Euro)             */
#define SQL_CP_21427           21427   /* CCSID 21427 - Taiwan IBM Big-5      */
                                       /* (with 13493 CNS, 566, 6204 UDC,     */
                                       /* Euro)                               */
#define SQL_CP_62212           62212   /* CCSID 62212 - CP867 Hebrew ST10     */
#define SQL_CP_62214           62214   /* CCSID 62214 - CP867 Hebrew ST5      */
#define SQL_CP_62216           62216   /* CCSID 62216 - CP867 Hebrew ST6      */
#define SQL_CP_62217           62217   /* CCSID 62217 - CP867 Hebrew ST8      */
#define SQL_CP_62219           62219   /* CCSID 62219 - CP867 Hebrew ST11     */
#define SQL_CP_62240           62240   /* CCSID 62240 - CP856 Hebrew ST11     */

/* DBCS Codepages                                                             */
#define SQL_CP_926             926     /* Codepage 926 - Korean               */
#define SQL_CP_951             951     /* Codepage 951 - New Korean           */
#define SQL_CP_301             301     /* Codepage 301 - Japanese             */
#define SQL_CP_928             928     /* Codepage 928 - Chinese              */
#define SQL_CP_927             927     /* Codepage 927 - Taiwan               */
#define SQL_CP_941             941     /* Codepage 941 - Japanese             */
#define SQL_CP_947             947     /* Codepage 947 - Taiwan Big-5         */
#define SQL_CP_971             971     /* Codepage 971 - Korean 5601          */
#define SQL_CP_1351            1351    /* Codepage 1351 - Japanese            */
#define SQL_CP_1362            1362    /* Codepage 1362 - Korean Windows      */
#define SQL_CP_1380            1380    /* Codepage1380 - China GB             */
#define SQL_CP_1382            1382    /* Codepage1382 - Simp Chinese GB      */
#define SQL_CP_1385            1385    /* Codepage1385 - Simp Chinese GBK     */
#define SQL_CP_1393            1393    /* Codepage 1393 - Japanese            */

/* Combined Codepages                                                         */
#define SQL_CP_934             934     /* Codepage 891 + 926 - Korean         */
#define SQL_CP_949             949     /* CP 1088 + 951 - Korean Std          */
#define SQL_CP_932             932     /* Codepage 897 + 301 - Japanese       */
#define SQL_CP_936             936     /* Codepage 903 + 928 - Chinese        */
#define SQL_CP_938             938     /* Codepage 904 + 927 - Taiwan         */
#define SQL_CP_944             944     /* Codepage 1040 + 926 - Ext.Korean    */
#define SQL_CP_942             942     /* Codepage 1041 + 301 - Ext.Japanese  */
#define SQL_CP_943             943     /* Codepage  897 + 941 - Ext.Japanese  */
#define SQL_CP_946             946     /* Codepage 1042 + 928 - Ext.Chinese   */
#define SQL_CP_948             948     /* Codepage 1043 + 927 - Ext.Taiwan    */
#define SQL_CP_950             950     /* Codepage 1114 + 947 - Taiwan Big5   */
#define SQL_CP_954             954     /* Codepage 954 + 13488 - euc Japan    */
#define SQL_CP_964             964     /* Codepage 964 + 13488 - euc Taiwan   */
#define SQL_CP_970             970     /* Codepage  367 + 971 - Korean 5601   */
#define SQL_CP_1363            1363    /* Codepage 1363 - Korean Windows      */
#define SQL_CP_1381            1381    /* Codepage 1115 +1380 - China GB      */
#define SQL_CP_1383            1383    /* Codepage  367 +1382 - Chinese GB    */
#define SQL_CP_1386            1386    /* Codepage  1114 +1385 - Chinese GBK  */
#define SQL_CP_5488            5488    /* Codepage  - Chinese GB18030         */
#define SQL_CP_1392            1392    /* Codepage  - Chinese GB18030         */
#define SQL_CP_1394            1394    /* Codepage  - Japanese Shift          */
                                       /* JISX0213                            */
#define SQL_CP_5039            5039    /* Codepage  1041 + 1351 - Japanese    */
#define SQL_CP_8612            8612    /* CCSID x21A4, (CP420, ST5)           */
#define SQL_CP_62218           62218   /* CCSID xF30A, (CP864, ST8)           */
#define SQL_CP_62224           62224   /* CCSID xF310, (CP420, ST6)           */
#define SQL_CP_62233           62233   /* CCSID xF319, (CP420, ST8)           */
#define SQL_CP_62234           62234   /* CCSID xF31A, (CP1255, ST9)          */
#define SQL_CP_62246           62246   /* CCSID xF326, (CP1046, ST8)          */
#define SQL_CP_62247           62247   /* CCSID xF327, (CP1046, ST9)          */
#define SQL_CP_62248           62248   /* CCSID xF328, (CP1046, ST4)          */
#define SQL_CP_62249           62249   /* CCSID xF329, (CP1046, ST2)          */
#define SQL_CP_62250           62250   /* CCSID xF32A, (CP420, ST12)          */

/* Unicode CCSIDs                                                             */
#define SQL_CP_1200            1200    /* Codepage1200 - UCS-2 (big-endian)   */
#define SQL_CP_1202            1202    /* Codepage1202 - UCS-2 (little        */
                                       /* endian)                             */
#define SQL_CP_1204            1204    /* Codepage1204 - UCS-2 (BOM)          */
#define SQL_CP_1208            1208    /* Codepage1208 - UTF-8                */
#define SQL_CP_1232            1232    /* Codepage1232 - UTF-32 (big-endian)  */
#define SQL_CP_1234            1234    /* Codepage1234 - UTF-32 (little       */
                                       /* endian)                             */
#define SQL_CP_UTF32BE         SQL_CP_1232 /* Big-endian UTF-32               */
#define SQL_CP_UTF32LE         SQL_CP_1234 /* Little-endian UTF-32            */
#define SQL_CP_1236            1236    /* Codepage1236 - UTF-32 (BOM)         */
#define SQL_CP_13488           13488   /* Codepg13488 - UCS-2 (Unicode v2,    */
                                       /* big-endian)                         */
#define SQL_CP_13490           13490   /* Codepg13490 - UCS-2 (Unicode v2,    */
                                       /* little-endiant)                     */
#define SQL_CP_17584           17584   /* Codepg17584 - UCS-2 (Unicode v3,    */
                                       /* big-endian)                         */
#define SQL_CP_17586           17586   /* Codepg17586 - UCS-2 (Unicode v3,    */
                                       /* little-endiant)                     */
#define SQL_CP_UTF8            SQL_CP_1208 /* UTF-8                           */
#define SQL_CP_UTF16BE         SQL_CP_1200 /* Big-endian UTF-16               */
#define SQL_CP_UTF16LE         SQL_CP_1202 /* Little-endian UTF-16            */

/* EBCDIC, PCDATA and ECECP CCSIDs                                            */
#define SQL_CP_37              37      /* CCSID 37 - EBCDIC - Common          */
                                       /* European                            */
#define SQL_CP_273             273     /* CCSID 273 - EBCDIC Austria,         */
                                       /* Germany                             */
#define SQL_CP_274             274     /* CCSID 274 - EBCDIC Belgium          */
#define SQL_CP_277             277     /* CCSID 277 - EBCDIC Denmark, Norway  */
#define SQL_CP_278             278     /* CCSID 278 - EBCDIC Finland, Sweden  */
#define SQL_CP_280             280     /* CCSID 280 - EBCDIC Italy            */
#define SQL_CP_284             284     /* CCSID 284 - EBCDIC Spain, Latin     */
                                       /* America                             */
#define SQL_CP_285             285     /* CCSID 285 - EBCDIC UK               */
#define SQL_CP_290             290     /* CCSID 290 - EBCDIC Japan            */
#define SQL_CP_297             297     /* CCSID 297 - EBCDIC France           */
#define SQL_CP_300             300     /* CCSID 300 - EBCDIC Japan DBCS       */
#define SQL_CP_423             423     /* CCSID 423 - EBCDIC Greece           */
#define SQL_CP_500             500     /* CCSID 500 - EBCDIC Latin-1          */
#define SQL_CP_803             803     /* CCSID 803 - EBCDIC Hebrew Set-A,    */
                                       /* ST4                                 */
#define SQL_CP_833             833     /* CCSID 833 - EBCDIC Korea Extended   */
                                       /* SBCS                                */
#define SQL_CP_834             834     /* CCSID 834 - EBCDIC Korea DBCS       */
                                       /* (with 1880 UDC)                     */
#define SQL_CP_835             835     /* CCSID 835 - EBCDIC Taiwan DBCS      */
                                       /* (with 6204 UDC)                     */
#define SQL_CP_836             836     /* CCSID 836 - EBCDIC China SBCS       */
#define SQL_CP_837             837     /* CCSID 837 - EBCDIC China DBCS       */
                                       /* (with 1880 UDC)                     */
#define SQL_CP_838             838     /* CCSID 838 - EBCDIC Thailand         */
                                       /* Extended SBCS                       */
#define SQL_CP_870             870     /* CCSID 870 - EBCDIC Latin-2          */
#define SQL_CP_871             871     /* CCSID 871 - EBCDIC Iceland          */
#define SQL_CP_875             875     /* CCSID 875 - EBCDIC Greece           */
#define SQL_CP_924             924     /* CCSID 924 - EBCDIC Latin-9          */
#define SQL_CP_930             930     /* CCSID 930 - EBCDIC Japan mix (with  */
                                       /* 4370 UDC, Extended SBCS)            */
#define SQL_CP_933             933     /* CCSID 933 - EBCDIC Korea mix (with  */
                                       /* 1880 UDC, Extended SBCS)            */
#define SQL_CP_935             935     /* CCSID 935 - EBCDIC China mix (with  */
                                       /* 1880 UDC, Extended SBCS)            */
#define SQL_CP_937             937     /* CCSID 937 - EBCDIC Taiwan mix       */
                                       /* (with 6204 UDC, Extended SBCS)      */
#define SQL_CP_939             939     /* CCSID 939 - EBCDIC Japan mix (with  */
                                       /* 4370 UDC, Extended SBCS)            */
#define SQL_CP_1025            1025    /* CCSID 1025 - EBCDIC Cyrillic        */
#define SQL_CP_1026            1026    /* CCSID 1026 - EBCDIC Turkey Latin-5  */
#define SQL_CP_1027            1027    /* CCSID 1027 - EBCDIC Japan Extended  */
                                       /* SBCS                                */
#define SQL_CP_1047            1047    /* CCSID 1047 - EBCDIC Open Systems    */
                                       /* Latin-1                             */
#define SQL_CP_1112            1112    /* CCSID 1112 - EBCDIC Baltic          */
#define SQL_CP_1122            1122    /* CCSID 1122 - EBCDIC Estonia         */
#define SQL_CP_1123            1123    /* CCSID 1123 - EBCDIC Ukraine         */
#define SQL_CP_1130            1130    /* CCSID 1130 - EBCDIC Vietnam         */
#define SQL_CP_1137            1137    /* CCSID 1137 - EBCDIC Devangari       */
                                       /* (Based on Unicode)                  */
#define SQL_CP_1153            1153    /* CCSID 1153 - EBCDIC Latin-2 (with   */
                                       /* Euro)                               */
#define SQL_CP_1154            1154    /* CCSID 1154 - EBCDIC Cyrillic (with  */
                                       /* Euro)                               */
#define SQL_CP_1156            1156    /* CCSID 1156 - EBCDIC Baltic          */
#define SQL_CP_1157            1157    /* CCSID 1157 - EBCDIC Estonia         */
#define SQL_CP_1158            1158    /* CCSID 1158 - EBCDIC Ukraine         */
#define SQL_CP_1159            1159    /* CCSID 1159 - EBCDIC Taiwan          */
                                       /* Extended SBCS (with Euro)           */
#define SQL_CP_1160            1160    /* CCSID 1160 - EBCDIC Thailanf (with  */
                                       /* Euro)                               */
#define SQL_CP_1164            1164    /* CCSID 1164 - EBCDIC Vietnam (with   */
                                       /* Euro)                               */
#define SQL_CP_1364            1364    /* CCSID 1364 - EBCDIC Korea mix       */
                                       /* (with Full Hangul)                  */
#define SQL_CP_1371            1371    /* CCSID 1371 - EBCDIC Taiwan mix      */
                                       /* (with 6204 UDC, Extended SBCS)      */
#define SQL_CP_1388            1388    /* CCSID 1388 - EBCDIC China mix       */
#define SQL_CP_1390            1390    /* CCSID 1390 - EBCDIC Japan mix       */
                                       /* (with 6205 UDC, Extended SBCS,      */
                                       /* Euro)                               */
#define SQL_CP_1399            1399    /* CCSID 1399 - EBCDIC Japan max       */
                                       /* (with 6205 UDC, Exnteded SBCS,      */
                                       /* Euro)                               */
#define SQL_CP_4369            4369    /* CCSID 4369 - EBCDIC Austria,        */
                                       /* German (DP94)                       */
#define SQL_CP_4396            4396    /* CCSID 4396 - EBCDIC Japan DBCS      */
                                       /* (with 1880 UCD)                     */
#define SQL_CP_4899            4899    /* CCSID 4899 - EBCDIC Hebrew Set-A,   */
                                       /* ST4 (with Euro, Sheqel)             */
#define SQL_CP_4930            4930    /* CCSID 4930 - EBCDIC Korea DBCS      */
                                       /* (with Full Hangul)                  */
#define SQL_CP_4933            4933    /* CCSID 4933 - EBCDIC China DBCS      */
                                       /* (with all GBK)                      */
#define SQL_CP_4971            4971    /* CCSID 4971 - EBCDIC Greece (with    */
                                       /* Euro)                               */
#define SQL_CP_5026            5026    /* CCSID 5026 - EBCDIC Japan mix       */
                                       /* (with 1880 UDC, Extended SBCS)      */
#define SQL_CP_5035            5035    /* CCSID 5035 - EBCDIC Japan mix       */
                                       /* (with 1880 UDC, Extended SBCS)      */
#define SQL_CP_5123            5123    /* CCSID 5123 - EBCDIC Japan Latin     */
                                       /* (with Extended SBCS, Euro)          */
#define SQL_CP_8482            8482    /* CCSID 8482 - EBCDIC Japan SBCS      */
                                       /* (with Euro)                         */
#define SQL_CP_8616            8616    /* CCSID 8616 - EBCDIC Hebrew subset   */
                                       /* ST10                                */
#define SQL_CP_9027            9027    /* CCSID 9027 - EBCDIC Taiwan (with    */
                                       /* 6204 UDC, Euro)                     */
#define SQL_CP_12708           12708   /* CCSID 12708 - EBCDIC Arabic ST7     */
#define SQL_CP_12712           12712   /* CCSID 12712 - EBCDIC Hebrew ST10    */
                                       /* (with Euro, Sheqel)                 */
#define SQL_CP_13121           13121   /* CCSID 13121 - EBCDIC Korea (with    */
                                       /* Extended SBCS)                      */
#define SQL_CP_13124           13124   /* CCSID 13124 - EBCDIC China SBCS     */
#define SQL_CP_16684           16684   /* CCSID 16684 - EBCDIC Japan (with    */
                                       /* 6295 UDC, Euro)                     */
#define SQL_CP_16804           16804   /* CCSID 16804 - EBCDIC Arabic ST4     */
                                       /* (with Euro)                         */
#define SQL_CP_28709           28709   /* CCSID 28709 - EBCDIC Taiwan SBCS    */
#define SQL_CP_62211           62211   /* CCSID 62211 - EBCDIC Hebrew ST5     */
#define SQL_CP_62229           62229   /* CCSID 62229 - EBCDIC Hebrew ST8     */
#define SQL_CP_62235           62235   /* CCSID 62235 - EBCDIC Hebrew ST6     */
#define SQL_CP_62245           62245   /* CCSID 62245 - EBCDIC Hebrew ST10    */
#define SQL_CP_62251           62251   /* CCSID 62251 - zOS Arabic/Latin ST6  */
#define SQL_CP_808             808     /* CCSID 808 - PCDATA Cyrillic         */
                                       /* (Russian with Euro)                 */
#define SQL_CP_848             848     /* CCSID 848 - PCDATA Uktaine (with    */
                                       /* Euro)                               */
#define SQL_CP_849             849     /* CCSID 849 - PCDATA Belarus (with    */
                                       /* Euro)                               */
#define SQL_CP_858             858     /* CCSID 858 - PCDATA Latin-1E (with   */
                                       /* Euro)                               */
#define SQL_CP_872             872     /* CCSID 872 - PCDATA Cyrillic (with   */
                                       /* Euro)                               */
#define SQL_CP_1161            1161    /* CCSID 1161 - PCDATA Thailand (with  */
                                       /* Euero)                              */
#define SQL_CP_1370            1370    /* CCSID 1370 - PCDATA Taiwan mix      */
                                       /* (with Euro)                         */
#define SQL_CP_5210            5210    /* CCSID 5210 - PCDATA China SBCS      */
#define SQL_CP_9044            9044    /* CCSID 9044 - PCDATA Latin-2 (with   */
                                       /* Euro)                               */
#define SQL_CP_9048            9048    /* CCSID 9048 - PCDATA Hebrew ST5      */
                                       /* (with Euro, Sheqel)                 */
#define SQL_CP_9049            9049    /* CCSID 9049 - PCDATA Turkey Latin-5  */
                                       /* (with Euro)                         */
#define SQL_CP_9061            9061    /* CCSID 9061 - PCDATA Greece (with    */
                                       /* Euro)                               */
#define SQL_CP_9238            9238    /* CCSID 9238 - PCDATA Arabic ST5      */
                                       /* (with Euro)                         */
#define SQL_CP_17248           17248   /* CCSID 17248 - PCDATA Arabic ST5     */
                                       /* (with Euro)                         */
#define SQL_CP_1140            1140    /* CCSID 1140 - ECECP Common           */
                                       /* European, US, Canada                */
#define SQL_CP_1141            1141    /* CCSID 1141 - ECECP Austria,         */
                                       /* Germany                             */
#define SQL_CP_1142            1142    /* CCSID 1142 - ECECP Denmakr, Norway  */
#define SQL_CP_1143            1143    /* CCSID 1143 - ECECP Finalnd, Sweden  */
#define SQL_CP_1144            1144    /* CCSID 1144 - ECECP Italy            */
#define SQL_CP_1145            1145    /* CCSID 1145 - ECECP Spanish, Latin   */
                                       /* America                             */
#define SQL_CP_1146            1146    /* CCSID 1146 - ECECP UK               */
#define SQL_CP_1147            1147    /* CCSID 1147 - ECECP France           */
#define SQL_CP_1148            1148    /* CCSID 1148 - ECECP International 1  */
#define SQL_CP_1149            1149    /* CCSID 1149 - ECECP Iceland          */
#define SQL_CP_65535           65535   /* CCSID 65535 - Reserved              */

/* Datastream Types                                                           */
#define SQL_SBC_PC             0       /* Single byte PC                      */
#define SQL_JPN_PC             1       /* Japanese-PC                         */
#define SQL_CHN_PC             2       /* Chinese-PC                          */
#define SQL_KOR_PC             3       /* Korean-PC                           */
#define SQL_KSC_PC             4       /* New Korean-PC                       */
#define SQL_KR5_PC             5       /* Korean 5601                         */
#define SQL_TWN_PC             6       /* Taiwan Big-5                        */
#define SQL_CGB_PC             7       /* China GB                            */
#define SQL_CGBA_PC            8       /* China GB on AIX                     */
#define SQL_EUCJP_PC           9       /* Japan euc                           */
#define SQL_EUCTW_PC           10      /* Taiwan euc                          */
#define SQL_UCS2_PC            11      /* UCS-2                               */
#define SQL_KSC2_PC            12      /* Korean Windows                      */
#define SQL_CGBK_PC            13      /* China GBK                           */
#define SQL_UTF8_PC            14      /* UTF-8                               */
#define SQL_CGB18030_PC        15      /* China GB18030                       */
#define SQL_UNKN_PC            255     /* Unknown                             */

/* OEM codeset & locale lengths                                               */
#define SQL_CODESET_LEN        17
#define SQL_LOCALE_LEN         33

/* Codeset & locale lengths for sqle_db_territory_info struct                 */
#define SQL_CODESET_SIZE       17
#define SQL_LOCALE_SIZE        33

/* SQLCA Constants                                                            */
#ifndef SQL_RC_INVALID_SQLCA
#endif

/* Size of SQLCA                                                              */
#define SQLCA_SIZE             sizeof(struct sqlca)

/* SQL Error message token delimiter                                          */
#define SQL_ERRMC_PRES ((char) 0xFF) /* delimiter for string entry */

/* Offset in SQLERRD - Diagnostic information                                 */
#define SQL_ERRD_RC            0       /* return code                         */
#define SQL_ERRD_REAS          1       /* reason code                         */
#define SQL_ERRD_CNT           2       /* nbr rows inserted/updated/deleted   */
#define SQL_ERRD_OPT_CARD      2       /* optimizer estimate of # rows        */
#define SQL_ERRD_OPTM          3       /* obsolete -- do not use --           */
#define SQL_ERRD_OPT_TOTCOST   3       /* optimzer estimate of total cost     */
#define SQL_ERRD_DCNT          4       /* nbr of cascaded deletes/updates     */
#define SQL_ERRD_LINE          4       /* line number for recompile error     */
#define SQL_ERRD_AUTHTYPE      4       /* authentication type returned for    */
                                       /* CONNECT/ATTACH                      */
#define SQL_ERRD_DIAG          5       /* diagnostics                         */

/* Indexes in SQLWARN - Warning flags                                         */
#define SQL_WARN_ANY           0       /* composite - set if any warnings     */
#define SQL_WARN_TRUNC         1       /* string column truncated             */
#define SQL_WARN_NUL           2       /* null values eliminated              */
#define SQL_WARN_MISM          3       /* nbr of columns/host vars mismatch   */
#define SQL_WARN_ALLR          4       /* no WHERE clause in update/delete    */
#define SQL_WARN_ETO           5       /* error is tolerated                  */
#define SQL_WARN_DATE          6       /* date has been truncated             */
#define SQL_WARN_SUB           8       /* character conversion substitution   */
#define SQL_WARN_NUL2          9       /* arithmetic error nulls eliminated   */
#define SQL_WARN_SQLCA         10      /* SQLCA conversion error              */

/* Values for Warning flags in SQLWARN                                        */
#define SQL_WARNING            'W'     /* warning indicator                   */
#define SQL_NULL_TRN           'N'     /* null terminator truncated warning   */
#define SQL_TRN_APP_LEN        'X'     /* truncation warning with             */
                                       /* application context length          */
                                       /* returned in sqlind                  */
#define SQL_NO_WARN            ' '     /* no warning indicator                */
#define SQL_ETO                'E'     /* error has been tolerated            */

#define SQL_PREPARE_ESTIMATE_WARNING 'P' /* Compiler estimate warning         */
                                       /* indicator                           */
/* SQLDA Constants                                                            */

/* Increment for type with null indicator                                     */
#define SQL_TYP_NULINC         1

/* Variable Types                                                             */
#define SQL_TYP_DATE           384     /* DATE                                */
#define SQL_TYP_NDATE          (SQL_TYP_DATE+SQL_TYP_NULINC)

#define SQL_TYP_TIME           388     /* TIME                                */
#define SQL_TYP_NTIME          (SQL_TYP_TIME+SQL_TYP_NULINC)

#define SQL_TYP_STAMP          392     /* TIMESTAMP                           */
#define SQL_TYP_NSTAMP         (SQL_TYP_STAMP+SQL_TYP_NULINC)

#define SQL_TYP_DATALINK       396     /* DATALINK                            */
#define SQL_TYP_NDATALINK      (SQL_TYP_DATALINK+SQL_TYP_NULINC)

#define SQL_TYP_CGSTR          400     /* C NUL-terminated graphic str        */
#define SQL_TYP_NCGSTR         (SQL_TYP_CGSTR+SQL_TYP_NULINC)

#define SQL_TYP_BLOB           404     /* BLOB - varying length string        */
#define SQL_TYP_NBLOB          (SQL_TYP_BLOB+SQL_TYP_NULINC)

#define SQL_TYP_CLOB           408     /* CLOB - varying length string        */
#define SQL_TYP_NCLOB          (SQL_TYP_CLOB+SQL_TYP_NULINC)

#define SQL_TYP_DBCLOB         412     /* DBCLOB - varying length string      */
#define SQL_TYP_NDBCLOB        (SQL_TYP_DBCLOB+SQL_TYP_NULINC)

#define SQL_TYP_VARCHAR        448     /* VARCHAR(i) - varying length string  */
                                       /* (2 byte length)                     */
#define SQL_TYP_NVARCHAR       (SQL_TYP_VARCHAR+SQL_TYP_NULINC)

#define SQL_TYP_CHAR           452     /* CHAR(i) - fixed length string       */
#define SQL_TYP_NCHAR          (SQL_TYP_CHAR+SQL_TYP_NULINC)

#define SQL_TYP_LONG           456     /* LONG VARCHAR - varying length       */
                                       /* string                              */
#define SQL_TYP_NLONG          (SQL_TYP_LONG+SQL_TYP_NULINC)

#define SQL_TYP_CSTR           460     /* varying length string for C (null   */
                                       /* terminated)                         */
#define SQL_TYP_NCSTR          (SQL_TYP_CSTR+SQL_TYP_NULINC)

#define SQL_TYP_VARGRAPH       464     /* VARGRAPHIC(i) - varying length      */
                                       /* graphic string (2 byte length)      */
#define SQL_TYP_NVARGRAPH      (SQL_TYP_VARGRAPH+SQL_TYP_NULINC)

#define SQL_TYP_GRAPHIC        468     /* GRAPHIC(i) - fixed length graphic   */
                                       /* string                              */
#define SQL_TYP_NGRAPHIC       (SQL_TYP_GRAPHIC+SQL_TYP_NULINC)

#define SQL_TYP_LONGRAPH       472     /* LONG VARGRAPHIC(i) - varying        */
                                       /* length graphic string               */
#define SQL_TYP_NLONGRAPH      (SQL_TYP_LONGRAPH+SQL_TYP_NULINC)

#define SQL_TYP_LSTR           476     /* varying length string for Pascal    */
                                       /* (1-byte length)                     */
#define SQL_TYP_NLSTR          (SQL_TYP_LSTR+SQL_TYP_NULINC)

#define SQL_TYP_FLOAT          480     /* FLOAT - 4 or 8 byte floating point  */
#define SQL_TYP_NFLOAT         (SQL_TYP_FLOAT+SQL_TYP_NULINC)

#define SQL_TYP_DECIMAL        484     /* DECIMAL (m,n)                       */
#define SQL_TYP_NDECIMAL       (SQL_TYP_DECIMAL+SQL_TYP_NULINC)

#define SQL_TYP_ZONED          488     /* Zoned Decimal -> DECIMAL (m,n)      */
#define SQL_TYP_NZONED         (SQL_TYP_ZONED+SQL_TYP_NULINC)

#define SQL_TYP_BIGINT         492     /* BIGINT - 8-byte signed integer      */
#define SQL_TYP_NBIGINT        (SQL_TYP_BIGINT+SQL_TYP_NULINC)

#define SQL_TYP_INTEGER        496     /* INTEGER - 4-byte signed integer     */
#define SQL_TYP_NINTEGER       (SQL_TYP_INTEGER+SQL_TYP_NULINC)

#define SQL_TYP_SMALL          500     /* SMALLINT - 2-byte signed integer    */
#define SQL_TYP_NSMALL         (SQL_TYP_SMALL+SQL_TYP_NULINC)

#define SQL_TYP_NUMERIC        504     /* NUMERIC -> DECIMAL (m,n)            */
#define SQL_TYP_NNUMERIC       (SQL_TYP_NUMERIC+SQL_TYP_NULINC)

#define SQL_TYP_BLOB_FILE_OBSOLETE 804 /* Obsolete Value                      */
#define SQL_TYP_NBLOB_FILE_OBSOLETE (SQL_TYP_BLOB_FILE_OBSOLETE+SQL_TYP_NULINC)

#define SQL_TYP_CLOB_FILE_OBSOLETE 808 /* Obsolete Value                      */
#define SQL_TYP_NCLOB_FILE_OBSOLETE (SQL_TYP_CLOB_FILE_OBSOLETE+SQL_TYP_NULINC)

#define SQL_TYP_DBCLOB_FILE_OBSOLETE 812 /* Obsolete Value                    */
#define SQL_TYP_NDBCLOB_FILE_OBSOLETE (SQL_TYP_DBCLOB_FILE_OBSOLETE+SQL_TYP_NULINC)

#define SQL_TYP_VARBINARY      908     /* Variable Binary                     */
#define SQL_TYP_NVARBINARY     (SQL_TYP_VARBINARY+SQL_TYP_NULINC)

#define SQL_TYP_BINARY         912     /* Fixed Binary                        */
#define SQL_TYP_NBINARY        (SQL_TYP_BINARY+SQL_TYP_NULINC)

#define SQL_TYP_BLOB_FILE      916     /* BLOB File - Binary Large Object     */
                                       /* File                                */
#define SQL_TYP_NBLOB_FILE     (SQL_TYP_BLOB_FILE+SQL_TYP_NULINC)

#define SQL_TYP_CLOB_FILE      920     /* CLOB File - Char Large Object File  */
#define SQL_TYP_NCLOB_FILE     (SQL_TYP_CLOB_FILE+SQL_TYP_NULINC)

#define SQL_TYP_DBCLOB_FILE    924     /* DBCLOB File - Double Byte Char      */
                                       /* Large Object File                   */
#define SQL_TYP_NDBCLOB_FILE   (SQL_TYP_DBCLOB_FILE+SQL_TYP_NULINC)

#define SQL_TYP_BLOB_LOCATOR   960     /* BLOB locator                        */
#define SQL_TYP_NBLOB_LOCATOR  (SQL_TYP_BLOB_LOCATOR+SQL_TYP_NULINC)

#define SQL_TYP_CLOB_LOCATOR   964     /* CLOB locator                        */
#define SQL_TYP_NCLOB_LOCATOR  (SQL_TYP_CLOB_LOCATOR+SQL_TYP_NULINC)

#define SQL_TYP_DBCLOB_LOCATOR 968     /* DBCLOB locator                      */
#define SQL_TYP_NDBCLOB_LOCATOR (SQL_TYP_DBCLOB_LOCATOR+SQL_TYP_NULINC)

#define SQL_TYP_XML            988     /* XML                                 */
#define SQL_TYP_NXML           (SQL_TYP_XML+SQL_TYP_NULINC)

#define SQL_TYP_DECFLOAT       996     /* Decimal Float (16/34)               */
#define SQL_TYP_NDECFLOAT      (SQL_TYP_DECFLOAT+SQL_TYP_NULINC)

#define SQL_LOBTOKEN_LEN       SQL_LOBLOCATOR_LEN
#define SQL_TYP_BLOB_TOKEN     SQL_TYP_BLOB_LOCATOR
#define SQL_TYP_NBLOB_TOKEN    SQL_TYP_NBLOB_LOCATOR
#define SQL_TYP_CLOB_TOKEN     SQL_TYP_CLOB_LOCATOR
#define SQL_TYP_NCLOB_TOKEN    SQL_TYP_NCLOB_LOCATOR
#define SQL_TYP_DBCLOB_TOKEN   SQL_TYP_DBCLOB_LOCATOR
#define SQL_TYP_NDBCLOB_TOKEN  SQL_TYP_NDBCLOB_LOCATOR
#define SQL_NCLOB_MAXLEN       SQL_DBCLOB_MAXLEN
#define SQL_LOBHANDLE_LEN      SQL_LOBTOKEN_LEN
#define SQL_TYP_BLOB_HANDLE    SQL_TYP_BLOB_TOKEN
#define SQL_TYP_NBLOB_HANDLE   SQL_TYP_NBLOB_TOKEN
#define SQL_TYP_CLOB_HANDLE    SQL_TYP_CLOB_TOKEN
#define SQL_TYP_NCLOB_HANDLE   SQL_TYP_NCLOB_TOKEN
#define SQL_TYP_DBCLOB_HANDLE  SQL_TYP_DBCLOB_TOKEN
#define SQL_TYP_NDBCLOB_HANDLE SQL_TYP_NDBCLOB_TOKEN

/* Values for 30th byte of sqlname                                            */
#define SQL_SQLNAME_SYSGEN     ((char) 0xFF) /* sqlname is system generated   */
#define SQL_SQLNAME_NOT_SYSGEN ((char) 0x00) /* sqlname is directly derived   */
                                             /* from a single column or       */
                                             /* specified in the AS clause    */

/* Return Codes for sqlabndx, sqlaprep and sqlarbnd                           */
#define SQLA_RC_OPT_IGNORED    20      /* The option(s) specified are not     */
                                       /* supported by the target  database   */
                                       /* and will be ignored                 */
#define SQLA_RC_BINDWARN       25      /* Bind execution succeeded with       */
                                       /* warnings.                           */
#define SQLA_RC_PREPWARN       25      /* Precompilation succeeded with       */
                                       /* warnings.                           */
#define SQLA_RC_BINDERROR      -1      /* Bind execution failed               */
#define SQLA_RC_PREPERROR      -1      /* Precompilation failed               */
#define SQLA_RC_BAD_BINDNAME   -2      /* Invalid bind file                   */
#define SQLA_RC_BAD_DBNAME     -3      /* Invalid database                    */
#define SQLA_RC_BAD_MSGNAME    -5      /* Invalid message file                */
#define SQLA_RC_BAD_FORMAT     -6      /* Invalid format                      */
#define SQLA_RC_OPEN_ERROR     -31     /* Error opening list file             */
#define SQLA_RC_MFILE_OPEN_ERR -35     /* Error opening message file          */
#define SQLA_RC_FILE_NAME_BAD  -36     /* Source file name is invalid         */
#define SQLA_RC_BAD_BNDFILE    -39     /* Bind file corrupted                 */
#define SQLA_RC_LIST_ERROR     -40     /* Bind list errors                    */
#define SQLA_RC_INTERRUPT      -94     /* Interrupt                           */
#define SQLA_RC_OSERROR        -1086   /* System error                        */
#define SQLA_RC_PREP_BIND_BUSY -1392   /* Prep/Bind already in use            */
#define SQLA_RC_OPTION_LEN_BAD -4903   /* Invalid parm. length                */
#define SQLA_RC_OPTION_PTR_BAD -4904   /* Invalid parm. ptr                   */
#define SQLA_RC_OPTION_SIZE_BAD -4905  /* Invalid parm. size                  */
#define SQLA_RC_OPTION_DATA_BAD -4917  /* Invalid parm. data                  */
#define SQLA_RC_OPTION_INVALID -30104  /* Invalid option or option value      */
#define SQLA_RC_SDK_LICENSE    -8005   /* No SDK/6000 license                 */

/* Values used for the date/time format parameter of sqlabind() ** OBSOLETE   */
/* **                                                                         */
#define SQL_FMT_DEF            "DEF"   /* FORMAT = Default for Country Code   */
#define SQL_FMT_USA            "USA"   /* FORMAT = USA                        */
#define SQL_FMT_EUR            "EUR"   /* FORMAT = EUR                        */
#define SQL_FMT_ISO            "ISO"   /* FORMAT = ISO                        */
#define SQL_FMT_JIS            "JIS"   /* FORMAT = JIS                        */
#define SQL_FMT_LOC            "LOC"   /* FORMAT = LOCAL                      */

/* The size of a date/time format buffer                                      */
#define SQL_FMT_LENGTH         3

/* Structures used system wide                                                */

#ifndef SQL_SQLDBCHAR_DEFN
#define SQL_SQLDBCHAR_DEFN
  typedef unsigned short sqldbchar;
#endif

/******************************************************************************
** sqlchar data structure
** This structure is used to pass variable length data to the database manager.
** 
** Table: Fields in the SQLCHAR Structure
** ----------------------------------------------------------------------------
** |Field Name           |Data Type      |Description                         |
** |---------------------|---------------|------------------------------------|
** |LENGTH               |SMALLINT       |Length of the character string      |
** |                     |               |pointed to by DATA.                 |
** |---------------------|---------------|------------------------------------|
** |DATA                 |CHAR(n)        |An array of characters of length    |
** |                     |               |LENGTH.                             |
** |---------------------|---------------|------------------------------------|
** 
** COBOL Structure
** 
** This is not defined in any header file. The following is an example that
** shows how to define the structure in COBOL:
** 
** * Replace maxlen with the appropriate value:
** 01 SQLCHAR.
** 49 SQLCHAR-LEN  PIC S9(4) COMP-5.
** 49 SQLCHAR-DATA PIC X(maxlen).
*******************************************************************************/
SQL_STRUCTURE sqlchar                  /* General-purpose VARCHAR for         */
                                       /* casting                             */
{
        short                  length;
        _SQLOLDCHAR            data[1];
};

/******************************************************************************
** sqlgraphic data structure
*******************************************************************************/
SQL_STRUCTURE sqlgraphic               /* General-purpose VARGRAPHIC for      */
                                       /* casting                             */
{
        short                  length;
#ifdef SQL_WCHART_CONVERT
        wchar_t                data[1];
#else
        sqldbchar              data[1];
#endif
};

/******************************************************************************
** sqllob data structure
** This structure is used to represent a LOB data type in a host
** programming language.
** 
** Table: Fields in the sqllob structure
** ----------------------------------------------------------------
** |Field name    |Data type    |Description                      |
** |--------------|-------------|---------------------------------|
** |length        |sqluint32    |Length in bytes of the data      |
** |              |             |parameter.                       |
** |--------------|-------------|---------------------------------|
** |data          |char(1)      |Data being passed in.            |
** |--------------|-------------|---------------------------------|
** 
*******************************************************************************/
SQL_STRUCTURE sqllob                   /* General-purpose LOB for casting     */
{
        sqluint32              length;
        char                   data[1];
};

/******************************************************************************
** sqldbclob data structure
*******************************************************************************/
SQL_STRUCTURE sqldbclob                /* General-purpose DBCLOB for casting  */
{
        sqluint32              length;
#ifdef SQL_WCHART_CONVERT
        wchar_t                data[1];
#else
        sqldbchar              data[1];
#endif
};

/******************************************************************************
** sqlfile data structure
*******************************************************************************/
SQL_STRUCTURE sqlfile                  /* File reference structure for LOBs   */
{
        sqluint32              name_length;
        sqluint32              data_length;
        sqluint32              file_options;
        char                   name[255];
};

SQL_STRUCTURE sqlfile2                 /* Extended file reference structure   */
                                       /* for LOBs                            */
{
        sqluint32              name_length;
        sqluint32              data_length;
        sqluint32              file_options;
        char                   name[255];
        char                   reserved[5];
        sqluint64              offset;
};

/* Values used for SQLVAR.SQLLEN with sqlfile structures                      */
#define SQL_SQLFILE_LEN        SQL_LOBFILE_LEN /* Length of an sqlfile LOB    */
                                       /* file host var                       */
#define SQL_SQLFILE2_LEN       280     /* Length of an sqlfile2 LOB file      */
                                       /* host var                            */

/* Values used for file_options in the sqlfile structures                     */
#define SQL_FILE_READ          2       /* Input file to read from             */
#define SQL_FILE_CREATE        8       /* Output file - new file to be        */
                                       /* created                             */
#define SQL_FILE_OVERWRITE     16      /* Output file - overwrite existing    */
                                       /* file or create a new file if it     */
                                       /* doesn't exist                       */
#define SQL_FILE_APPEND        32      /* Output file - append to an          */
                                       /* existing file or create a new file  */
                                       /* if it doesn't exist                 */

/* Structure used to store binder options when calling sqlabndr()             */
/* or sqlabndx(), or to store precompile options when calling                 */
/* sqlaprep(), or to store rebind options when calling sqlarbnd().            */

#define sqlbindopt sqloptions

#ifndef SQL_BIND_OPTS

/******************************************************************************
** sqloptheader data structure
** Table: Fields in the SQLOPTHEADER Structure
** |----------------------------------------------------------------|
** |Field Name|Data Type|Description                                |
** |----------|---------|-------------------------------------------|
** |ALLOCATED |INTEGER  |Number of elements in the option array     |
** |          |         |of the sqlopt structure.                   |
** |----------|---------|-------------------------------------------|
** |USED      |INTEGER  |Number of elements in the option array     |
** |          |         |of the sqlopt structure actually used.     |
** |          |         |This is the number of option pairs (TYPE   |
** |          |         |and VAL) supplied.                         |
** |----------|---------|-------------------------------------------|
*******************************************************************************/
SQL_STRUCTURE sqloptheader     /* Header for sqlopt structure */
{
   sqluint32     allocated; /* Number of options allocated */
   sqluint32     used;      /* Number of options used */
};

/******************************************************************************
** sqloptions data structure
** Table: Fields in the SQLOPTIONS Structure
** |----------------------------------------------------------------|
** |Field Name| Data Type | Description                             |
** |----------------------------------------------------------------|
** |TYPE      | INTEGER   | Bind/precompile/rebind option type.     |
** |VAL       | INTEGER   | Bind/precompile/rebind option value.    |
** |----------------------------------------------------------------|
** |Note:                                                           |
** |The TYPE and VAL fields are repeated for each bind/precompile/  |
** |rebind option specified.                                        |
** |----------------------------------------------------------------|
*******************************************************************************/
SQL_STRUCTURE sqloptions       /* bind/prep/rebind option          */
{
   sqluint32     type; /* Type  of bind/prep/rebind option */
   sqluintptr    val;  /* Value of bind/prep/rebind option */
};

/******************************************************************************
** sqlopt data structure
** This structure is used to pass bind options to the sqlabndx API,
** precompile options to the sqlaprep API, and rebind options to the
** sqlarbnd API.

** Table: Fields in the SQLOPT Structure
** |----------------------------------------------------------------|
** |Field Name| Data Type|Description                               |
** |----------|----------|------------------------------------------|
** |HEADER    | Structure|An sqloptheader structure.                |
** |----------|----------|------------------------------------------|
** |OPTION    | Array    |An array of sqloptions structures. The    |
** |          |          |number of elements in this array is       |
** |          |          |determined by the value of the allocated  |
** |          |          |field of the header.                      |
** |----------|----------|------------------------------------------|

** COBOL Structure

** * File: sql.cbl
** 01 SQLOPT.
**     05 SQLOPTHEADER.
**         10 ALLOCATED   PIC 9(9) COMP-5.
**         10 USED        PIC 9(9) COMP-5.
**     05 SQLOPTIONS OCCURS 1 TO 50 DEPENDING ON ALLOCATED.
**         10 SQLOPT-TYPE        PIC 9(9) COMP-5.
**         10 SQLOPT-VAL         PIC 9(9) COMP-5.
**         10 SQLOPT-VAL-PTR     REDEFINES SQLOPT-VAL
** * 
*******************************************************************************/
SQL_STRUCTURE sqlopt
{
   SQL_STRUCTURE sqloptheader header;     /* Header for sqlopt structure */
   SQL_STRUCTURE sqloptions   option[1];  /* Array of bind/prep/rebind options  */
};

#define SQL_BIND_OPTS
#endif

/* Values used for option[n].type in the sqloptions structure                 */
/* of sqlabndx(), sqlaprep() and sqlarbnd().                                  */
#define SQL_DATETIME_OPT       1       /* Option for date/time format - bind  */
                                       /* and precompile option               */
#define SQL_STANDARDS_OPT      2       /* Option for standards level          */
                                       /* compliance - precompile option      */
                                       /* only                                */
#define SQL_ISO_OPT            4       /* Option for isolation level - bind   */
                                       /* and precompile option               */
#define SQL_BLOCK_OPT          5       /* Option for record blocking - bind   */
                                       /* and precompile option               */
#define SQL_GRANT_OPT          6       /* Option for granting privileges -    */
                                       /* bind option only                    */
#define SQL_FLAG_OPT           8       /* Option for the Flagger -            */
                                       /* precompile option only              */
#define SQL_GRANT_USER_OPT     9       /* Option for granting privileges to   */
                                       /* a user - bind option only           */
#define SQL_GRANT_GROUP_OPT    10      /* Option for granting privileges to   */
                                       /* a group - bind option only          */
#define SQL_CNULREQD_OPT       11      /* Option for adding NULLs to strings  */
                                       /* - bind option only                  */
#define SQL_GENERIC_OPT        12      /* Generic option for DRDA servers -   */
                                       /* bind option only                    */
#define SQL_DEFERRED_PREPARE_OPT 15    /* Option for Deferred Prepare -       */
                                       /* precompile option only              */
#define SQL_CONNECT_OPT        16      /* Specifies whether one or multiple   */
                                       /* connections are allowed             */
                                       /* similtaneously within a unit of     */
                                       /* work.                               */
#define SQL_RULES_OPT          17      /* Specifies the set of rules used     */
                                       /* for connection to multiple          */
                                       /* databases within a single unit of   */
                                       /* work                                */
#define SQL_DISCONNECT_OPT     18      /* Specifies which of multiple         */
                                       /* databases connected to will be      */
                                       /* disconnected when a COMMIT or       */
                                       /* ROLLBACK is issued.                 */
#define SQL_SYNCPOINT_OPT      19      /* Specifies what syncpoint option     */
                                       /* (for example one phase or two       */
                                       /* phase) will be used                 */
#define SQL_BIND_OPT           20      /* Option to create a bind file -      */
                                       /* precompile option only              */
#define SQL_SAA_OPT            21      /* Option specifies SAA/non-SAA        */
                                       /* compatibility - FORTRAN precompile  */
                                       /* option only                         */
#define SQL_PKG_OPT            23      /* Option to create a package with a   */
                                       /* specific name - precompile option   */
                                       /* only                                */
#define SQL_OPTIM_OPT          24      /* Option to specify SQLDA             */
                                       /* optimization - precompile option    */
                                       /* only                                */
#define SQL_SYNTAX_OPT         25      /* Option to not create a package or   */
                                       /* bind file - precompile option only  */
#define SQL_SQLERROR_OPT       SQL_SYNTAX_OPT /* Indicates under what         */
                                       /* conditions a package will be be     */
                                       /* created - bind and precompile       */
                                       /* option                              */
#define SQL_LINEMACRO_OPT      26      /* Option to suppress #line macro      */
                                       /* generation in modified source file  */
                                       /* - C precompile option only          */
#define SQL_NO_OPT             27      /* 'No-op' option - ignore this entry  */
                                       /* in the option array - bind,         */
                                       /* precompile and rebind option        */
#define SQL_LEVEL_OPT          30      /* Level of a module - precompile      */
                                       /* option only                         */
#define SQL_COLLECTION_OPT     31      /* Package collection identifier -     */
                                       /* precompile option only              */
#define SQL_VERSION_OPT        32      /* Package version identifier -        */
                                       /* precompile and rebind option only   */
#define SQL_OWNER_OPT          33      /* Package owner authorization         */
                                       /* identifier - bind and precompile    */
                                       /* option                              */
#define SQL_SCHEMA_OPT         SQL_OWNER_OPT /* Synonym for owner - bind and  */
                                       /* precompile option                   */
#define SQL_QUALIFIER_OPT      34      /* Authorization identifier that is    */
                                       /* to be used as a qualifier for       */
                                       /* unqualified objects - bind and      */
                                       /* precompile option                   */
#define SQL_CATALOG_OPT        SQL_QUALIFIER_OPT /* Synonym for qualifier -   */
                                       /* bind and precompile option          */
#define SQL_TEXT_OPT           35      /* Package description - bind and      */
                                       /* precompile option                   */
#define SQL_VALIDATE_OPT       40      /* Indicates when object validation    */
                                       /* occurs - bind and precompile        */
                                       /* option                              */
#define SQL_EXPLAIN_OPT        41      /* Determines whether information      */
                                       /* will be produced about how the SQL  */
                                       /* statements in a package will be     */
                                       /* executed - bind and precompile      */
                                       /* option                              */
#define SQL_ACTION_OPT         42      /* Indicates whether a package is to   */
                                       /* be added or replaced - bind and     */
                                       /* precompile option                   */
#define SQL_REPLVER_OPT        44      /* Replaces a specific version of a    */
                                       /* package - bind and precompile       */
                                       /* option                              */
#define SQL_RETAIN_OPT         45      /* Indicates whether EXECUTE           */
                                       /* authorities are to be preserved     */
                                       /* when a package is replaced - bind   */
                                       /* and precompile option               */
#define SQL_RELEASE_OPT        46      /* Indicates whether resources are     */
                                       /* released at each COMMIT or when     */
                                       /* the application  terminates - bind  */
                                       /* and precompile option               */
#define SQL_DEGREE_OPT         47      /* Specifies whether or not the query  */
                                       /* is executed using I/O parallelism   */
                                       /*  bind and precompile option         */
#define SQL_STRDEL_OPT         50      /* Designates whether an apostrophe    */
                                       /* or quote will be used as a string   */
                                       /* delimiter - bind and precompile     */
                                       /* option                              */
#define SQL_DECDEL_OPT         51      /* Designates whether a period or      */
                                       /* comma will be used as a decimal     */
                                       /* point indicator - bind and          */
                                       /* precompile option                   */
#define SQL_CHARSUB_OPT        55      /* Designates default character        */
                                       /* subtype that is to be used for      */
                                       /* column definitions in the CREATE    */
                                       /* and ALTER TABLE SQL statements -    */
                                       /* bind and precompile option          */
#define SQL_CCSIDS_OPT         56      /* Designates what CCSID will be used  */
                                       /* for single byte characters for      */
                                       /* character column definitions        */
                                       /* without a specific CCSID clause in  */
                                       /* the CREATE and ALTER TABLE SQL      */
                                       /* statements - bind and precompile    */
                                       /* option                              */
#define SQL_CCSIDM_OPT         57      /* Designates what CCSID will be used  */
                                       /* for mixed byte characters for       */
                                       /* character column definitions        */
                                       /* without a specific CCSID clause in  */
                                       /* the CREATE and ALTER TABLE SQL      */
                                       /* statements - bind and precompile    */
                                       /* option                              */
#define SQL_CCSIDG_OPT         58      /* Designates what CCSID will be used  */
                                       /* for double byte characters for      */
                                       /* character column definitions        */
                                       /* without a specific CCSID clause in  */
                                       /* the CREATE and ALTER TABLE SQL      */
                                       /* statements - bind and precompile    */
                                       /* option                              */
#define SQL_DEC_OPT            59      /* Specifies maximum precision to be   */
                                       /* used in decimal arithmetic          */
                                       /* operations - bind and precompile    */
                                       /* option                              */
#define SQL_WCHAR_OPT          60      /* Specifies handling of graphic       */
                                       /* vars. - precompile only             */
#define SQL_DYNAMICRULES_OPT   61      /* Specifies which authorization       */
                                       /* identifier to use when dynamic SQL  */
                                       /* in a package is executed - bind     */
                                       /* and precompile option               */
#define SQL_INSERT_OPT         62      /* Buffers VALUE inserts - bind and    */
                                       /* precompile option for DB2/PE        */
                                       /* servers only                        */
#define SQL_EXPLSNAP_OPT       63      /* Capture explain snapshot - bind     */
                                       /* and precompile option               */
#define SQL_FUNCTION_PATH      64      /* Path for user-defined function      */
                                       /* resolution - bind and precompile    */
                                       /* option                              */
#define SQL_SQLWARN_OPT        65      /* Disable prepare-time SQL warnings   */
                                       /*  bind and precompile option         */
#define SQL_QUERYOPT_OPT       66      /* Set query optimization class -      */
                                       /* bind and precompile option          */
#define SQL_TARGET_OPT         67      /* Target compiler - precompile        */
                                       /* option                              */
#define SQL_PREP_OUTPUT_OPT    68      /* Name of precompiler output file -   */
                                       /* precompile option                   */
#define SQL_PREPROCESSOR_OPT   69      /* Preprocessor command - precompile   */
                                       /* option only                         */
#define SQL_RESOLVE_OPT        70      /* Indicates whether function and      */
                                       /* type resolution should or should    */
                                       /* not use conservative binding        */
                                       /* semantics - rebind option only      */
#define SQL_CLIPKG_OPT         71      /* CLIPKG option - bind option only    */
#define SQL_FEDERATED_OPT      72      /* FEDERATED option - bind and         */
                                       /* precompile option                   */
#define SQL_TRANSFORMGROUP_OPT 73      /* Transform Group - precompile and    */
                                       /* bind option                         */
#define SQL_LONGERROR_OPT      74      /* Option to treat long host variable  */
                                       /* declarations as errors -            */
                                       /* precompile option only              */
#define SQL_DECTYPE_OPT        75      /* DECTYPE Option to convert decimals  */
                                       /*  precompile option only             */
#define SQL_KEEPDYNAMIC_OPT    76      /* Specifies whether dynamic SQL       */
                                       /* statements are to be kept after     */
                                       /* commit points - bind and            */
                                       /* precompile option                   */
#define SQL_DBPROTOCOL_OPT     77      /* Specifies what protocol to use      */
                                       /* when connecting to a remote site    */
                                       /* that is identified by a three-part  */
                                       /* name statement - bind and           */
                                       /* precompile option                   */
#define SQL_OPTHINT_OPT        78      /* Controls whether query              */
                                       /* optimization hints are used for     */
                                       /* static SQL - bind and precompile    */
                                       /* option                              */
#define SQL_IMMEDWRITE_OPT     79      /* Tells whether immediate writes      */
                                       /* will be done for updates made to    */
                                       /* group buffer pool dependent         */
                                       /* pagesets or partitions  - bind and  */
                                       /* precompile option                   */
#define SQL_ENCODING_OPT       80      /* Specifies the encoding for all      */
                                       /* host variables in static            */
                                       /* statements in the plan or package   */
                                       /* (bind and precompile option)        */
#define SQL_OS400NAMING_OPT    81      /* Specifies which naming option is    */
                                       /* to be used when accessing DB2 UDB   */
                                       /* for iSeries data - bind and         */
                                       /* precompile option                   */
#define SQL_SORTSEQ_OPT        82      /* Specifies which sort sequence       */
                                       /* table to use on the iSeries system  */
                                       /* - bind and precompile option        */
#define SQL_REOPT_OPT          83      /* Specifies whether to have DB2       */
                                       /* determine an access path at run     */
                                       /* time using values for host          */
                                       /* variables, parameter markers, and   */
                                       /* special registers - bind and        */
                                       /* precompile option                   */
#define SQL_PSM_OPT            84      /* PSM option - bind option            */
#define SQL_CALL_RES_OPT       85      /* Specifies whether to use immediate  */
                                       /* ordeferred procedure                */
                                       /* resolutionprecompile option         */
#define SQL_TIMESTAMP_OPT      86      /* Option for date/time format - bind  */
                                       /* and precompile option               */
#define SQL_STATICREADONLY_OPT 87      /* Specifies whether static cursors    */
                                       /* will be treated as read-only if     */
                                       /* they are ambiguous - precompile     */
                                       /* and bind option                     */
#define SQL_OPTPROFILE_OPT     88      /* Specifies a two-part name of the    */
                                       /* form [schemaname.]basename where    */
                                       /* basename is a character string of   */
                                       /* up to 128 chars in length used to   */
                                       /* uniquely identify the optimization  */
                                       /* profile within a particular         */
                                       /* schema.  Schemaname is a character  */
                                       /* string identifier of up to 30       */
                                       /* bytes used to explicitelyqualify    */
                                       /* an optimization profile schema.     */
#define SQL_FEDASYNC_OPT       89      /* Specifies whether or not the query  */
                                       /* is executed using asynchrony        */
#define SQL_NODB_OPT           90      /* Specifies that a database should    */
                                       /* not be used during precompile.      */
                                       /* The BINDFILE and VALIDATE RUN       */
                                       /* options will be implicitely         */
                                       /* specified.                          */

#define SQL_NUM_OPTS           90      /* # of PREP/BIND/REBIND options       */

/* Values used for option[n].val when option[n].type is                       */
/* SQL_DATETIME_OPT. These can also be used for the date/time                 */
/* format parameter of sqlabind().                                            */

#define SQL_DATETIME_DEF       48      /* FORMAT = Default for Country Code   */
#define SQL_DATETIME_USA       49      /* FORMAT = USA                        */
#define SQL_DATETIME_EUR       50      /* FORMAT = EUR                        */
#define SQL_DATETIME_ISO       51      /* FORMAT = ISO                        */
#define SQL_DATETIME_JIS       52      /* FORMAT = JIS                        */
#define SQL_DATETIME_LOC       53      /* FORMAT = LOCAL                      */

/* The following constants are here for backwards compatbility with earlier   */
/* releases.                                                                  */

#define SQL_FRMT_OPT           SQL_DATETIME_OPT
#define SQL_FMT_0              SQL_DATETIME_DEF
#define SQL_FMT_1              SQL_DATETIME_USA
#define SQL_FMT_2              SQL_DATETIME_EUR
#define SQL_FMT_3              SQL_DATETIME_ISO
#define SQL_FMT_4              SQL_DATETIME_JIS
#define SQL_FMT_5              SQL_DATETIME_LOC

/* Values used for option[n].val when option[n].type is SQL_STANDARDS_OPT.    */
#define SQL_SAA_COMP           0       /* SAA Level 1 Database CPI            */
#define SQL_MIA_COMP           1       /* MIA                                 */
#define SQL_SQL92E_COMP        2       /* SQL92 Entry                         */

/* Values used for option[n].val when option[n].type is SQL_ISO_OPT           */
#define SQL_REP_READ           0       /* Repeatable read level               */
#define SQL_CURSOR_STAB        1       /* Cursor stability level              */
#define SQL_UNCOM_READ         2       /* Uncommitted read level              */
#define SQL_READ_STAB          3       /* Read stability level                */
#define SQL_NO_COMMIT          4       /* No Commit level      l              */

/* Values used for option[n].val when option[n].type is SQL_BLOCK_OPT         */
#define SQL_BL_UNAMBIG         0       /* Block Unambiguous cursors           */
#define SQL_BL_ALL             1       /* Block All cursors                   */
#define SQL_NO_BL              2       /* Block No cursors                    */

/* Values used for option[n].val when option[n].type is SQL_FLAG_OPT          */
#define SQL_MVSDB2V23_SYNTAX   4       /* Flagger check against MVS           */
#define SQL_MVSDB2V31_SYNTAX   5       /* DB2 V2.3, V3.1 or V4.1 SQL          */
#define SQL_MVSDB2V41_SYNTAX   6       /* syntax                              */
#define SQL_SQL92E_SYNTAX      7       /* FIPS flagger SQL92E syntax          */

/* Values used for option[n].val when option[n].type is SQL_CNULREQD_OPT      */
#define SQL_CNULREQD_NO        0       /* C NULL value not required           */
#define SQL_CNULREQD_YES       1       /* C NULL value required               */

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* GENERIC_OPT                                                                */
#define SQL_MAX_GENERIC        1023

/* Values used for option[n].val when option[n].type is SQL_SAA_OPT.          */
#define SQL_SAA_NO             0       /* SQLCA definition not SAA            */
                                       /* compatible                          */
#define SQL_SAA_YES            1       /* SQLCA definition is SAA compatible  */

/* Values used for option[n].val when option[n].type is SQL_OPTIM_OPT.        */
#define SQL_DONT_OPTIMIZE      0       /* Do not optimize SQLDA               */
                                       /* initialization                      */
#define SQL_OPTIMIZE           1       /* Optimize SQLDA initialization       */

/* Values used for option[n].val when option[n].type is SQL_SYNTAX_OPT.       */
#define SQL_NO_SYNTAX_CHECK    0       /* Create a package and/or a bind      */
                                       /* file                                */
#define SQL_SYNTAX_CHECK       1       /* Do not create a package or bind     */
                                       /* file                                */

/* Values used for option[n].val when option[n].type is SQL_LINEMACRO_OPT.    */
#define SQL_NO_LINE_MACROS     0       /* Do not generate #line macros in     */
                                       /* modified source file                */
#define SQL_LINE_MACROS        1       /* Generate #line macros in modified   */
                                       /* source file                         */

/* Values used for option[n].val when option[n].type is SQL_WCHAR_OPT.        */
#define SQL_WCHAR_NOCONVERT    0       /* graphic variable not converted      */
#define SQL_WCHAR_CONVERT      1       /* graphic variable converted          */

/* Maximum sqlchar length for option[n].val when option[n].type is SQL_LEVEL  */
/* OPT                                                                        */
#define SQL_MAX_LEVEL          8

/* Values used for option[n].val when option[n].type is SQL_CONNECT_OPT       */
#define SQL_DEFERRED_PREPARE_YES 1     /* Dynamic SQL statements will be      */
                                       /* chained.                            */
#define SQL_DEFERRED_PREPARE_NO 2      /* Dynamic SQL statements will not be  */
                                       /* chained.                            */
#define SQL_DEFERRED_PREPARE_ALL 3     /* Dynamic SQL statements will be      */
                                       /* chained in all cases.  The          */
                                       /* application must not allocate host  */
                                       /* vars a FETCH SQLDA until after the  */
                                       /* OPEN statement for the cursor.      */

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* COLLECTION_OPT                                                             */
#define SQL_MAX_COLLECTION     30

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* VERSION_OPT                                                                */
#define SQL_MAX_VERSION        254

/* Maximum sqlchar length for option[n].val when option[n].type is SQL_OWNER  */
/* OPT                                                                        */
#define SQL_MAX_OWNER          30

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* SCHEMA_OPT                                                                 */
#define SQL_MAX_SCHEMA         SQL_MAX_OWNER

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* QUALIFIER_OPT                                                              */
#define SQL_MAX_QUALIFIER      30

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* CATALOG_OPT                                                                */
#define SQL_MAX_CATALOG        SQL_MAX_QUALIFIER

/* Maximum sqlchar length for option[n].val when option[n].type is SQL_TEXT   */
/* OPT                                                                        */
#define SQL_MAX_TEXT           255

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* PREPROCESSOR_OPT                                                           */
#define SQL_MAX_PREPROCESSOR   1024

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* TRANSFORMGROUP_OPT                                                         */
#define SQL_MAX_TRANSFORMGROUP 18

/* Values used for option[n].val when option[n].type is SQL_VALIDATE_OPT      */
#define SQL_VALIDATE_BIND      0       /* Validate objects during BIND        */
#define SQL_VALIDATE_RUN       1       /* Validate objects during execution   */

/* Values used for option[n].val when option[n].type is SQL_EXPLAIN_OPT       */
#define SQL_EXPLAIN_NO         0       /* No Explain output saved             */
#define SQL_EXPLAIN_YES        1       /* Explain output saved                */
#define SQL_EXPLAIN_ALL        2       /* Explain output saved for all        */
                                       /* static and dynamic statements       */
#define SQL_EXPLAIN_REOPT      3       /* Explain output saved for static     */
                                       /* reoptimizable statements            */

/* Values used for option[n].val when option[n].type is SQL_ACTION_OPT        */
#define SQL_ACTION_ADD         0       /* Package is to be added              */
#define SQL_ACTION_REPLACE     1       /* Package is to be replaced           */

/* Max/Min value of CLIPKG for option[n].val when option[n].type is SQL       */
/* CLIPKG_OPT                                                                 */
#define SQL_MIN_CLIPKG         3
#define SQL_MAX_CLIPKG         30

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* REPLVER_OPT                                                                */
#define SQL_MAX_REPLVER        254

/* Values used for option[n].val when option[n].type is SQL_SQLERROR_OPT      */
#define SQL_SQLERROR_NOPACKAGE SQL_NO_SYNTAX_CHECK /* Do not create a         */
                                       /* package if errors are encountered   */
#define SQL_SQLERROR_CHECK     SQL_SYNTAX_CHECK /* Do not create a package    */
#define SQL_SQLERROR_CONTINUE  2       /* Create a package even if errors     */
                                       /* are encountered                     */

/* Values used for option[n].val when option[n].type is SQL_RETAIN_OPT        */
#define SQL_RETAIN_NO          0       /* Do not preserve EXECUTE             */
                                       /* authorities when a package is       */
                                       /* replaced                            */
#define SQL_RETAIN_YES         1       /* Preserve EXECUTE authorities when   */
                                       /* a package is replaced               */

/* Values used for option[n].val when option[n].type is SQL_RELEASE_OPT       */
#define SQL_RELEASE_COMMIT     0       /* Release resources at COMMIT         */
#define SQL_RELEASE_DEALLOCATE 1       /* Release resources when the program  */
                                       /* terminates                          */

/* Values used for option[n].val when option[n].type is SQL_STRDEL_OPT        */
#define SQL_STRDEL_APOSTROPHE  0       /* Apostrophe string delimiter         */
#define SQL_STRDEL_QUOTE       1       /* Quote string delimiter              */

/* Values used for option[n].val when option[n].type is SQL_DECDEL_OPT        */
#define SQL_DECDEL_PERIOD      0       /* Period is used as a decimal point   */
                                       /* indicator in decimal and floating   */
                                       /* point literals                      */
#define SQL_DECDEL_COMMA       1       /* Comma is used as a decimal point    */
                                       /* indicator in decimal and floating   */
                                       /* point literals                      */

/* Values used for option[n].val when option[n].type is SQL_CHARSUB_OPT       */
#define SQL_CHARSUB_DEFAULT    0       /* Use the target system defined       */
                                       /* default for all new character       */
                                       /* columns for which an explicit       */
                                       /* subtype is not specified            */
#define SQL_CHARSUB_BIT        1       /* Use the BIT character subtype for   */
                                       /* all new character columns for       */
                                       /* which an explicit subtype is not    */
                                       /* specified                           */
#define SQL_CHARSUB_SBCS       2       /* Use the SBCS character subtype for  */
                                       /* all new character columns for       */
                                       /* which an explicit subtype is not    */
                                       /* specified                           */
#define SQL_CHARSUB_MIXED      3       /* Use the mixed character subtype     */
                                       /* for all new character columns for   */
                                       /* which an explicit subtype is not    */
                                       /* specified                           */

/* Values used for option[n].val when option[n].type is SQL_DEC_OPT           */
#define SQL_DEC_15             15      /* 15 bit precision is used in         */
                                       /* decimal arithmetic operations       */
#define SQL_DEC_31             31      /* 31 bit precision is used in         */
                                       /* decimal arithmetic operations       */

/* Values used for option[n].val when option[n].type is SQL_DEGREE_OPT        */
#define SQL_DEGREE_1           1       /* Prohibits parallel I/O operations   */
#define SQL_DEGREE_ANY         0       /* Allows the target database system   */
                                       /* to determine the degree of          */
                                       /* parallel I/O operations             */
#define SQL_MAX_DEGREE_VAL     32767   /* Maximum value                       */

/* Values used for option[n].val when option[n].type is SQL_VERSION_OPT       */
#define SQL_VERSION_AUTO       "AUTO"  /* Use the timestamp to generate the   */
                                       /* package VERSION                     */

/* The next four option values (for CONNECT type, SQLRULES, DISCONNECT and    */
/* SYNCPOINT) are used not only by the precompiler but also by the sqlesetc   */
/* and sqleqryc APIs.                                                         */

/* Values used for option[n].val when option[n].type is SQL_CONNECT_OPT       */
#define SQL_CONNECT_1          1       /* Indicates that only one connection  */
                                       /* to a database can exist at any      */
                                       /* given time.                         */
#define SQL_CONNECT_2          2       /* Indicates that multiple             */
                                       /* connections can exist               */
                                       /* simultaneously, with one being      */
                                       /* active and the others dormant.      */

/* Values used for option[n].val when option[n].type is SQL_RULES_OPT         */
#define SQL_RULES_DB2          1       /* Indicates that CONNECT TO can be    */
                                       /* used to make a dormant connection   */
                                       /* the current connection.             */
#define SQL_RULES_STD          2       /* Indicates that CONNECT TO is not    */
                                       /* valid for making a dormant          */
                                       /* connection current, and SET         */
                                       /* CONNECTION must be used instead.    */

/* Values used for option[n].val when option[n].type is SQL_DISCONNECT_OPT    */
#define SQL_DISCONNECT_EXPL    1       /* Indicates that all connections      */
                                       /* marked by the RELEASE statement     */
                                       /* will be the only connections        */
                                       /* released when a COMMIT is issued.   */
#define SQL_DISCONNECT_COND    2       /* Indicates that all connections      */
                                       /* that do not have open WITH HOLD     */
                                       /* cursors will be the only            */
                                       /* connections released when a COMMIT  */
                                       /* is issued.                          */
#define SQL_DISCONNECT_AUTO    3       /* Indicates that all connections      */
                                       /* will be released when a COMMIT is   */
                                       /* issued.                             */

/* Values used for option[n].val when option[n].type is SQL_SYNCPOINT_OPT     */
#define SQL_SYNC_ONEPHASE      1       /* Do not use a transaction manager    */
                                       /* to perform two phase commit, but    */
                                       /* enforce that there is only one      */
                                       /* database is updated when multiple   */
                                       /* databases are accessed within a     */
                                       /* single transaction.                 */
#define SQL_SYNC_TWOPHASE      2       /* Use a transaction manager to        */
                                       /* coordinate two phase commit.        */
#define SQL_SYNC_NONE          0       /* No update enforcement or two phase  */
                                       /* commit protocol will be used.       */

/* The next option value (for SQL_CONNECT_NODE) is used only by the sqlesetc  */
/* and sqleqryc APIs.                                                         */
#define SQL_CONN_CATALOG_NODE  0xfffe  /* Connect to the catalog node         */

/* Values used for option[n].val when option[n].type is SQL_DYNAMICRULES_OPT  */
#define SQL_DYNAMICRULES_RUN   0       /* Dynamic SQL in package will use     */
                                       /* authid of person running the        */
                                       /* package                             */
#define SQL_DYNAMICRULES_BIND  1       /* Dynamic SQL in package will use     */
                                       /* authid of person who owns the       */
                                       /* package                             */
#define SQL_DYNAMICRULES_INVOKERUN 2   /* Dynamic SQL in a routine will use   */
                                       /* authid of invoker of routine        */
#define SQL_DYNAMICRULES_DEFINERUN 3   /* Dynamic SQL in a routine will use   */
                                       /* authid of definer of routine        */
#define SQL_DYNAMICRULES_INVOKEBIND 4  /* Dynamic SQL in a routine will use   */
                                       /* authid of invoker of routine        */
#define SQL_DYNAMICRULES_DEFINEBIND 5  /* Dynamic SQL in a routine will use   */
                                       /* authid of definer of routine        */

/* Values used for option[n].val when option[n].type is SQL_INSERT_OPT        */
#define SQL_INSERT_DEF         0       /* Do not buffer VALUE inserts         */
#define SQL_INSERT_BUF         1       /* Buffer VALUE inserts                */

/* Values used for option[n].val when option[n].type is SQL_EXPLSNAP_OPT      */
#define SQL_EXPLSNAP_NO        0       /* No Explain snapshot saved           */
#define SQL_EXPLSNAP_YES       1       /* Explain snapshot saved              */
#define SQL_EXPLSNAP_ALL       2       /* Explain snapshot saved for all      */
                                       /* static and dynamic statements       */
#define SQL_EXPLSNAP_REOPT     3       /* Explain snapshot saved for static   */
                                       /* reoptimizable statements            */

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* FUNCTION_PATH                                                              */
#define SQL_MAX_FUNCPATH       254

/* Values used for option[n].val when option[n].type is SQL_SQLWARN_OPT       */
#define SQL_SQLWARN_NO         0       /* Suppress prepare-time warning       */
                                       /* SQLCODEs                            */
#define SQL_SQLWARN_YES        1       /* Permit prepare-time warning         */
                                       /* SQLCODEs                            */

/* Values used for option[n].val when option[n].type is SQL_QUERYOPT_OPT      */
#define SQL_QUERYOPT_0         0       /* Class 0 query optimization          */
#define SQL_QUERYOPT_1         1       /* Class 1 query optimization          */
#define SQL_QUERYOPT_2         2       /* Class 2 query optimization          */
#define SQL_QUERYOPT_3         3       /* Class 3 query optimization          */
#define SQL_QUERYOPT_5         5       /* Class 5 query optimization          */
#define SQL_QUERYOPT_7         7       /* Class 7 query optimization          */
#define SQL_QUERYOPT_9         9       /* Class 9 query optimization          */

/* Maximum sqlchar length for option[n].val when option[n].type is SQL        */
/* TARGET_OPT                                                                 */
#define SQL_MAX_TARGET_LEN     32

/* Values used for option[n].val when option[n].type is SQL_RESOLVE_OPT       */
#define SQL_RESOLVE_ANY        0       /* Conservative binding semantics are  */
                                       /* not used                            */
#define SQL_RESOLVE_CONSERVATIVE 1     /* Conservative binding semantics are  */
                                       /* used                                */

/* Values used for option[n].val when option[n].type is SQL_FEDERATED_OPT     */
#define SQL_FEDERATED_NO       0       /* Federated systems are not           */
                                       /* supported                           */
#define SQL_FEDERATED_YES      1       /* Federated systems are supported     */
/* Values used for option[n].val when option[n].type is SQL_PSM_OPT           */
#define SQL_PSM_NO             0       /* PSM no                              */
#define SQL_PSM_YES            1       /* PSM yes                             */

/* Values used for option[n].val when option[n].type is SQL_LONGERROR_OPT.    */
#define SQL_LONGERROR_NO       0       /* Do not generate errors for the use  */
                                       /* of long host variable declarations  */
#define SQL_LONGERROR_YES      1       /* Generate errors for the use of      */
                                       /* long host variable declarations     */

/* Values used for option[n].val when option[n].type is SQL_DECTYPE_OPT.      */
#define SQL_DECTYPE_NOCONVERT  0       /* Decimal type not converted          */
#define SQL_DECTYPE_CONVERT    1       /* Decimal type converted              */

/* Values used for option[n].val when option[n].type is SQL_KEEPDYNAMIC_OPT   */
#define SQL_KEEPDYNAMIC_NO     0       /* Do not keep dynamic SQL statements  */
                                       /* after commit points                 */
#define SQL_KEEPDYNAMIC_YES    1       /* Keep dynamic SQL statements after   */
                                       /* commit points                       */

/* Values used for option[n].val when option[n].type is SQL_DBPROTOCOL_OPT    */
#define SQL_DBPROTOCOL_DRDA    0       /* Use DRDA protocol when connecting   */
                                       /* to a remote site that is            */
                                       /* identified by a three-part name     */
                                       /* statement                           */
#define SQL_DBPROTOCOL_PRIVATE 1       /* Use a private protocol when         */
                                       /* connecting to a remote site that    */
                                       /* is identified by a three-part name  */
                                       /* statement                           */

/* Values used for option[n].val when option[n].type is SQL_IMMEDWRITE_OPT    */
#define SQL_IMMEDWRITE_NO      0       /* Updated pages are written at or     */
                                       /* before phase two of commit          */
#define SQL_IMMEDWRITE_YES     1       /* Updated pages are written as soon   */
                                       /* as the buffer update completes      */
#define SQL_IMMEDWRITE_PH1     2       /* Updated pages are written at or     */
                                       /* before phase one of commit          */

/* Values used for option[n].val when option[n].type is SQL_ENCODING_OPT      */
#define SQL_ENCODING_ASCII     0       /* Host variables in static            */
                                       /* statements are encoded in ascii     */
#define SQL_ENCODING_EBCDIC    1       /* Host variables in static            */
                                       /* statements are encoded in ebcdic    */
#define SQL_ENCODING_UNICODE   2       /* Host variables in static            */
                                       /* statements are encoded in unicode   */

/* Values used for option[n].val when option[n].type is SQL_OS400NAMING_OPT   */
#define SQL_OS400NAMING_SYSTEM 0       /* Use the iSeries system naming       */
                                       /* option when accessing DB2 UDB for   */
                                       /* iSeries data                        */
#define SQL_OS400NAMING_SQL    1       /* Use the SQL naming option when      */
                                       /* accessing DB2 UDB for iSeries data  */

/* Values used for option[n].val when option[n].type is SQL_SORTSEQ_OPT       */
#define SQL_SORTSEQ_JOBRUN     0       /* Use the NLSS sort sequence table    */
                                       /* of the DRDA job on the iSeries      */
                                       /* system                              */
#define SQL_SORTSEQ_HEX        1       /* Use the EBCDIC sort sequence table  */
                                       /* of the DRDA job on the iSeries      */
                                       /* system                              */

/* Values used for option[n].val when option[n].type is SQL_REOPT_OPT         */
#define SQL_REOPT_NO           0       /* Do not determine an access path at  */
                                       /* run time using values for host      */
                                       /* variables, parameter markers, and   */
                                       /* special registers                   */
#define SQL_REOPT_YES          1       /* Re-determine an access path at run  */
                                       /* time using values for host          */
                                       /* variables, parameter markers, and   */
                                       /* special registers                   */
#define SQL_REOPT_NONE         2       /* The access path is determined       */
                                       /* based on the default estimates for  */
                                       /* host variables, parameter markers,  */
                                       /* and special registers               */
#define SQL_REOPT_ONCE         3       /* Re-determine an access path only    */
                                       /* once at run time using values for   */
                                       /* host variables, parameter markers,  */
                                       /* and special registers               */
#define SQL_REOPT_ALWAYS       4       /* Re-determine an access path at      */
                                       /* every execution values for host     */
                                       /* variables, parameter markers, and   */
                                       /* special registers                   */

/* Values used for option[n].val when option[n].type is SQL_CALL_RES_OPT      */
#define SQL_CALL_RES_IMMED     0       /* Immediate SP call resolution        */
#define SQL_CALL_RES_DEFERRED  1       /* Deferred SP call resolution         */

/* Values used for option[n].val when option[n].type is SQL_STATICREADONLY    */
/* OPT                                                                        */
#define SQL_STATICRO_NO        0       /* Static cursors take on attributes   */
                                       /* as wouldnormally be generated       */
                                       /* given the statementtext and         */
                                       /* setting of the LANGLEVEL option.    */
#define SQL_STATICRO_YES       1       /* Any static cursor that does not     */
                                       /* contain theFOR UPDATE or FOR READ   */
                                       /* ONLY clause will beconsidered READ  */
                                       /* ONLY.                               */

/* Values used for option[n].val when option[n].type is SQL_NODB_OPT.         */
#define SQL_USEDB              0       /* Use a database to precompiler.      */
#define SQL_NODB               1       /* Do not use a database to            */
                                       /* precompile.                         */

/* Values used for option[n].val when option[n].type is SQL_FEDASYNC_OPT      */
#define SQL_FEDASYNC_MIN       0       /* No asynchrony is used               */
#define SQL_FEDASYNC_ANY       -1      /* Allows the optimizer to determine   */
                                       /* the level of asynchrony             */
#define SQL_FEDASYNC_MAX       64000   /* Maximum value                       */

/* Binder Interface Parameters/Return Codes                                   */

/******************************************************************************
** sqlabndx API
** Invokes the bind utility, which prepares SQL statements stored in the
** bind file generated by the precompiler, and creates a package that is
** stored in the database.
** 
** Scope
** 
** This API can be called from any database partition server in db2nodes.cfg. It
** updates the database catalogs on the catalog partition. Its effects are
** visible to all database partition servers.
** 
** Authorization
** 
** One of the following:
** - sysadm or dbadm authority
** - BINDADD privilege if a package does not exist and one of:
** -- IMPLICIT_SCHEMA authority on the database if the schema name
** of the package does not exist
** -- CREATEIN privilege on the schema if the schema name of the
** package exists
** - ALTERIN privilege on the schema if the package exists
** - BIND privilege on the package if it exists.
** 
** The user also needs all privileges required to compile any static SQL
** statements in the application. Privileges granted to groups are not used for
** authorization checking of static statements. If the user has sysadm
** authority, but not explicit privileges to complete the bind, the database
** manager grants explicit dbadm authority automatically.
** 
** Required connection
** 
** Database
** 
** API include file
** 
** sql.h 
** 
** sqlabndx API parameters
** 
** pBindFileName
** Input. A string containing the name of the bind file, or the name of a file
** containing a list of bind file names. The bind file names must contain the
** extension .bnd. A path for these files can be specified.
** 
** Precede the name of a bind list file with the at sign (@). For example, a
** fully qualified bind list file name might be: /u/user1/bnd/@all.lst
** 
** The bind list file should contain one or more bind file names, and must have
** the extension .lst.
** 
** Precede all but the first bind file name with a plus symbol (+). The bind
** file names may be on one or more lines. For example, the bind list file
** all.lst might contain:
** mybind1.bnd+mybind2.bnd+
** mybind3.bnd+
** mybind4.bnd
** 
** Path specifications on bind file names in the list file can be used.
** If no path is specified, the database manager takes path information
** from the bind list file.
** 
** pMsgFileName
** Input. A string containing the destination for error, warning, and
** informational messages. Can be the path and the name of an operating system
** file, or a standard device. If a file already exists, it is overwritten.
** If it does not exist, a file is created.
** 
** pBindOptions
** Input. A structure used to pass bind options to the API. For more information
** about this structure, see SQLOPT.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
** 
** Usage notes
** 
** Binding can be done as part of the precompile process for an application
** program source file, or as a separate step at a later time. Use BIND when
** binding is performed as a separate process.
** 
** The name used to create the package is stored in the bind file, and is
** based on the source file name from which it was generated (existing paths
** or extensions are discarded). For example, a precompiled source file
** called myapp.sqc generates a default bind file called myapp.bnd and a
** default package name of MYAPP. (However,
**  the bind file name and the
** package name can be overridden at precompile time by using the
** SQL_BIND_OPT and the SQL_PKG_OPT options of sqlaprep.)
** 
** BIND executes under the transaction that the user has started. After
** performing the bind, BIND issues a COMMIT (if bind is successful) or a
** ROLLBACK (if bind is unsuccessful) operation to terminate the current
** transaction and start another one.
** 
** Binding halts if a fatal error or more than 100 errors occur. If a
** fatal error occurs during binding, BIND stops binding, attempts to
** close all files, and discards the package.
** 
** Binding application programs have prerequisite requirements and restrictions
** beyond the scope of this manual. For example, an application cannot be bound
** from a V8 client to a V8 server, and then executed against a V7 server.
** 
** The Bind option types and values are defined in sql.
** 
** REXX API syntax
** 
** This API can be called from REXX through the SQLDB2 interface.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Bind                            */
  sqlabndx (
        _SQLOLDCHAR * pBindFileName,       /* bind file name                  */
        _SQLOLDCHAR * pMsgFileName,        /* message file                    */
        struct sqlopt * pBindOptions,      /* bind options                    */
        struct sqlca * pSqlca);            /* SQLCA                           */

/******************************************************************************
** sqlarbnd API
** Allows the user to recreate a package stored in the database without the need
** for a bind file.
** 
** Authorization
** 
** One of the following:
** - sysadm or dbadm authority
** - ALTERIN privilege on the schema
** - BIND privilege on the package.
** 
** The authorization ID logged in the BOUNDBY column of the SYSCAT.PACKAGES
** system catalog table, which is the ID of the most recent binder of the
** package, is used as the binder authorization ID for the rebind, and for
** the default schema for table references in the package. Note that this
** default qualifier may be different from the authorization ID of the
** user executing the rebind request. REBIND will use the same bind
** options that were specified when the package was created.
** 
** Required connection
** 
** Database
** 
** API include file
** 
** sql.h
** 
** sqlarbnd API parameters
** 
** pPackageName
** Input. A string containing the qualified or unqualified name that designates
** the package to be rebound. An unqualified package-name is implicitly
** qualified by the current authorization ID. This name does not include the
** package version. When specifying a package that has a version that is not
** the empty string, then the version-id must be specified using the
** SQL_VERSION_OPT rebind option.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
** 
** pRebindOptions
** Input. A pointer to the SQLOPT structure, used to pass rebind options to the
** API. For more information about this structure, see SQLOPT.
** 
** Usage notes
** 
** REBIND does not automatically commit the transaction following a successful
** rebind. The user must explicitly commit the transaction. This enables
** "what if " analysis, in which the user updates certain statistics, and
** then tries to rebind the package to see what changes. It also permits
** multiple rebinds within a unit of work.
** 
** This API:
** 
** - Provides a quick way to recreate a package. This enables the user to take
** advantage of a change in the system without a need for the original
** bind file.fs. For example, if it is likely that a particular SQL
** statement can take advantage of a newly created index, REBIND can be
** used to recreate the package. REBIND can also be used to recreate
** packages after db2Runstats has been executed, thereby taking advantage
** of the new statistics.
** 
** - Provides a method to recreate inoperative packages. Inoperative packages
** must be explicitly rebound by invoking either the bind utility or the rebind
** utility. A package will be marked inoperative (the VALID column of the
** SYSCAT.PACKAGES system catalog will be set to X) if a function instance on
** which the package depends is dropped. The rebind conservative option is
** not supported for inoperative packages.
** 
** - Gives users control over the rebinding of invalid packages. Invalid
** packages will be automatically (or implicitly) rebound by the database
** manager when they are executed. This may result in a noticeable delay
** in the execution of the first SQL request for the invalid package. It
** may be desirable to explicitly rebind invalid packages, rather than
** allow the system to automatically rebind them, in order to eliminate
** the initial delay and to prevent unexpected SQL error messages which
** may be returned in case the implicit rebind fails. For example,
** following migration, all packages stored in the database will be
** invalidated by the DB2 Version 5 migration process. Given that this
** may involve a large number of packages, it may be desirable to
** explicitly rebind all of the invalid packages at one time. This
** explicit rebinding can be accomplished using BIND, REBIND, or the
** db2rbind tool.
** 
** The choice of whether to use BIND or REBIND to explicitly rebind a package
** depends on the circumstances. It is recommended that REBIND be used
** whenever the situation does not specifically require the use of BIND,
** since the performance of REBIND is significantly better than that of
** BIND. BIND must be used, however:
** 
** - When there have been modifications to the program (for example, when
** SQL statements have been added or deleted, or when the package does
** not match the executable for the program).
** 
** - When the user wishes to modify any of the bind options as part of the
** rebind. REBIND does not support any bind options. For example, if the user
** wishes to have privileges on the package granted as part of the bind
** process, BIND must be used, since it has an SQL_GRANT_OPT option.
** 
** - When the package does not currently exist in the database.
** 
** - When detection of all bind errors is desired. REBIND only returns the first
** error it detects, and then ends, whereas the BIND command returns the
** first 100 errors that occur during binding.
** 
** REBIND is supported by DB2 Connect.
** 
** If REBIND is executed on a package that is in use by another user, the rebind
** will not occur until the other user's logical unit of work ends, because an
** exclusive lock is held on the package's record in the SYSCAT.PACKAGES system
** catalog table during the rebind.
** 
** When REBIND is executed, the database manager recreates the package from
** the SQL statements stored in the SYSCAT.STATEMENTS system catalog table.
** If many versions with the same package number and creator exist, only one
** version can be bound at once. If not specified using the SQL_VERSION_OPT
** rebind option, the VERSION defaults to be "". Even if there is only one
** package with a name and creator that matches the name and creator specified
** in the rebind request, it will not rebound unless its VERSION matches the
** VERSION specified explicitly or implicitly.
** 
** If REBIND encounters an error, processing stops, and an error message is
** returned.
** 
** The Explain tables are populated during REBIND if either SQL_EXPLSNAP_OPT
** or SQL_EXPLAIN_OPT have been set to YES or ALL (check
** EXPLAIN_SNAPSHOT and EXPLAIN_MODE columns in the catalog). The Explain
** tables used are those of the REBIND requester, not the original binder.
** The Rebind option types and values are defined in sql.h.
** 
** REXX API syntax
** 
** This API can be called from REXX through the SQLDB2 interface.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Rebind                          */
  sqlarbnd (
        char * pPackageName,               /* package name                    */
        struct sqlca * pSqlca,             /* SQLCA                           */
        struct sqlopt * pRebindOptions);   /* rebind options                  */

/******************************************************************************
** sqlaprep API
** Processes an application program source file containing embedded SQL
** statements. A modified source file is produced containing host language calls
** for the SQL statements and, by default, a package is created in the database.
** 
** Scope
** 
** This API can be called from any database partition server in db2nodes.cfg. It
** updates the database catalogs on the catalog partition. Its effects
** are visible to all database partition servers.
** 
** Authorization
** 
** One of the following:
** - sysadm or dbadm authority
** - BINDADD privilege if a package does not exist and one of:
** -- IMPLICIT_SCHEMA authority on the database if the schema name
** of the package does not exist
** -- CREATEIN privilege on the schema if the schema name of the
** package exists
** - ALTERIN privilege on the schema if the package exists
** - BIND privilege on the package if it exists.
** 
** The user also needs all privileges required to compile any static SQL
** statements in the application. Privileges granted to groups are not
** used for authorization checking of static statements. If the user
** has sysadm authority, but not explicit privileges to complete the
** bind, the database manager grants explicit dbadm authority
** automatically.
** 
** Required connection
** 
** Database
** 
** API include file
** 
** sql.h
** 
** sqlaprep API parameters
** 
** pProgramName
** Input. A string containing the name of the application to be precompiled. Use
** the following extensions:
** - .sqb - for COBOL applications
** - .sqc - for C applications
** - .sqC - for UNIX C++ applications
** - .sqf - for FORTRAN applications
** - .sqx - for C++ applications
** 
** When the TARGET option is used, the input file name extension does not have
** to be from this predefined list.
** 
** The preferred extension for C++ applications containing embedded SQL on UNIX
** based systems is sqC; however, the sqx convention, which was invented for
** systems that are not case sensitive, is tolerated by UNIX based systems.
** 
** pMsgFileName
** Input. A string containing the destination for error, warning, and
** informational messages. Can be the path and the name of an operating
** system file, or a standard device. If a file already exists, it is
** overwritten. If it does not exist, a file is created.
** 
** pPrepOptions
** Input. A structure used to pass precompile options to the API. For more
** information about this structure, see SQLOPT.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
** 
** Usage notes
** 
** A modified source file is produced, which contains host language
** equivalents to the SQL statements. By default, a package is created
** in the database to which a connection has been established. The name
** of the package is the same as the program file name (minus the
** extension and folded to uppercase), up to a maximum of 8 characters.
** 
** Following connection to a database, sqlaprep executes under the
** transaction that was started. PRECOMPILE PROGRAM then issues a COMMIT
** or a ROLLBACK operation to terminate the current transaction and start
** another one.
** 
** Precompiling stops if a fatal error or more than 100 errors occur. If a fatal
** error does occur, PRECOMPILE PROGRAM stops precompiling, attempts to
** close all files, and discards the package.
** 
** The Precompile option types and values are defined in sql.h.
** 
** REXX API syntax
** 
** This API can be called from REXX through the SQLDB2 interface.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Precompile Program              */
  sqlaprep (
        _SQLOLDCHAR * pProgramName,        /* source file name                */
        _SQLOLDCHAR * pMsgFileName,        /* message file name               */
        struct sqlopt * pPrepOptions,      /* precompile options              */
        struct sqlca * pSqlca);            /* SQLCA                           */

/* Generic Interfaces to the Binder and Precompiler                           */

/******************************************************************************
** sqlgbndx API
** sqlgbndx API-specific parameters
** 
** pMsgFileName
** Input. A string containing the destination for error, warning, and
** informational messages. Can be the path and the name of an operating
** system file, or a standard device. If a file already exists, it is
** overwritten. If it does not exist, a file is created.
** 
** BindFileNameLen
** Input. Length in bytes of the pBindFileName parameter.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Bind                            */
  sqlgbndx (
        unsigned short MsgFileNameLen,     /* message file name length        */
        unsigned short BindFileNameLen,    /* bind file name length           */
        struct sqlca * pSqlca,             /* SQLCA                           */
        struct sqlopt * pBindOptions,      /* binder options                  */
        _SQLOLDCHAR * pMsgFileName,        /* message file                    */
        _SQLOLDCHAR * pBindFileName);      /* bind file name                  */

/******************************************************************************
** sqlgrbnd API
** sqlgrbnd API-specific parameters
** 
** PackageNameLen
** Input. Length in bytes of the pPackageName parameter.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Rebind                          */
  sqlgrbnd (
        unsigned short PackageNameLen,     /* package name length             */
        char * pPackageName,               /* package name                    */
        struct sqlca * pSqlca,             /* SQLCA                           */
        struct sqlopt * pRebindOptions);   /* rebind options                  */

/******************************************************************************
** sqlgprep API
** sqlgprep API-specific parameters
** 
** MsgFileNameLen
** Input. Length in bytes of the pMsgFileName parameter.
** 
** ProgramNameLen
** Input. Length in bytes of the pProgramName parameter.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Precompile Program              */
  sqlgprep (
        unsigned short MsgFileNameLen,     /* message file name length        */
        unsigned short ProgramNameLen,     /* source file name length         */
        struct sqlca * pSqlca,             /* SQLCA                           */
        struct sqlopt * pPrepOptions,      /* precompile options              */
        _SQLOLDCHAR * pMsgFileName,        /* message file name               */
        _SQLOLDCHAR * pProgramName);       /* source file name                */

/* Application Context apis                                                   */


#define SQL_CTX_ORIGINAL 0                 /* serial access                   */
#define SQL_CTX_MULTI_MANUAL 1             /* concurrent access               */
#define SQL_CTX_TRUSTED_ROUTINE 2          /* trusted routine (internal)      */
/******************************************************************************
** sqleSetTypeCtx API
** Sets the application context type. This API should be the first database API
** called inside an application.
** 
** Scope
** 
** The scope of this API is limited to the immediate process.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqleSetTypeCtx API parameters
** 
** lOptions
** Input. Valid values are:
** 
** - SQL_CTX_ORIGINAL
** All threads will use the same context, and concurrent access will be blocked.
** This is the default if none of these APIs is called.
** 
** - SQL_CTX_MULTI_MANUAL
** All threads will use separate contexts, and it is up to the application to
** manage the context for each thread. See
** - sqleBeginCtx API
** - sqleAttachToCtx API 
** - sqleDetachFromCtx API
** - sqleEndCtx API
** 
** The following restrictions/changes apply when this option is used:
** - When termination is normal, automatic COMMIT at process termination is
** disabled. All outstanding transactions are rolled back, and all COMMITs must
** be done explicitly.
** - sqleintr API interrupts all contexts. To interrupt a specific context, use
** sqleInterruptCtx.
** 
** Usage notes
** 
** This API must be called before any other database call, and only the
** first call is effective.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Set Application Context         */
  sqleSetTypeCtx (
        sqlint32 lOptions);                /* options                         */


#define SQL_CTX_BEGIN_ALL 0                /* create & attach                 */
#define SQL_CTX_CREATE_ONLY 1              /* create only                     */
/******************************************************************************
** sqleBeginCtx API
** Creates an application context, or creates and then attaches to an
** application context. More than one application context can be
** created. Each context has its own commit scope. Different threads
** can attach to different contexts (see the sqleAttachToCtx API).
** Any database API calls made by such threads will not be serialized
** with one another.
** 
** Scope
** 
** The scope of this API is limited to the immediate process.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqleBeginCtx API parameters
** 
** ppCtx
** Output. A data area allocated out of private memory for the storage
** of context information.
** 
** lOptions
** Input. Valid values are:
** - SQL_CTX_CREATE_ONLY
** The context memory will be allocated, but there will be no attachment.
** - SQL_CTX_BEGIN_ALL
** The context memory will be allocated, and then a call to sqleAttachToCtx will
** be made for the current thread. If this option is used, the ppCtx
** parameter can be NULL. If the thread is already attached to a context,
** the call will fail.
** 
** reserved
** Reserved for future use. Must be set to NULL.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Create Application Context      */
  sqleBeginCtx (
        void ** ppCtx,                     /* pointer to a pointer to ctx     */
        sqlint32 lOptions,                 /* lOptions                        */
        void * reserved,                   /* reserved                        */
        struct sqlca * pSqlca);            /* SQLCA                           */


#define SQL_CTX_END_ALL 0                  /* detach & free                   */
#define SQL_CTX_FREE_ONLY 1                /* free only                       */
/******************************************************************************
** sqleEndCtx API
** Frees all memory associated with a given context.
** 
** Scope
** 
** The scope of this API is limited to the immediate process.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqleEndCtx API parameters
** 
** ppCtx
** Output. A data area in private memory (used for the storage of context
** information) that is freed.
** 
** lOptions
** Input. Valid values are:
** - SQL_CTX_FREE_ONLY
** The context memory will be freed only if a prior detach has been done.
** Note:
** pCtx must be a valid context previously allocated by sqleBeginCtx.
** - SQL_CTX_END_ALL
** If necessary, a call to sqleDetachFromCtx will be made before the memory is
** freed.
** Note:
** A detach will be done even if the context is still in use. If this option is
** used, the ppCtx parameter can be NULL, but if passed, it must be a valid
** context previously allocated by sqleBeginCtx. A call to
** sqleGetCurrentCtx will be made, and the current context freed from there.
** 
** reserved
** Reserved for future use. Must be set to NULL.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
** 
** Usage notes
** 
** If a database connection exists, or the context has been attached by another
** thread, this call will fail.
** Note:
** If a context calls an API that establishes an instance attachment
** (for example, db2CfgGet, it is necessary to detach from the instance
** using sqledtin before calling sqleEndCtx.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Destroy Application Context     */
  sqleEndCtx (
        void ** ppCtx,                     /* pointer to a pointer to ctx     */
        sqlint32 lOptions,                 /* lOptions                        */
        void * reserved,                   /* reserved                        */
        struct sqlca * pSqlca);            /* SQLCA                           */


/******************************************************************************
** sqleAttachToCtx API
** Makes the current thread use a specified context. All subsequent
** database calls made on this thread will use this context. If more
** than one thread is attached to a given context, access is serialized
** for these threads, and they share a commit scope.
** 
** Scope
** 
** The scope of this API is limited to the immediate process.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqleAttachToCtx API parameters
** 
** pCtx
** Input. A valid context previously allocated by sqleBeginCtx.
** 
** reserved
** Reserved for future use. Must be set to NULL.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Attach to Application Context   */
  sqleAttachToCtx (
        void * pCtx,                       /* pointer to ctx                  */
        void * reserved,                   /* reserved                        */
        struct sqlca * pSqlca);            /* SQLCA                           */


/******************************************************************************
** sqleDetachFromCtx API
** Detaches the context being used by the current thread. The context will be
** detached only if an attach to that context has previously been made.
** 
** Scope
** 
** The scope of this API is limited to the immediate process.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqleDetachFromCtx API parameters
** 
** pCtx
** Input. A valid context previously allocated by sqleBeginCtx.
** 
** reserved
** Reserved for future use. Must be set to NULL.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Detach Application Context      */
  sqleDetachFromCtx (
        void * pCtx,                       /* pointer to ctx                  */
        void * reserved,                   /* reserved                        */
        struct sqlca * pSqlca);            /* SQLCA                           */


/******************************************************************************
** sqleGetCurrentCtx API
** Returns the current context associated with a thread.
** 
** Scope
** 
** The scope of this API is limited to the immediate process.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqleGetCurrentCtx API parameters
** 
** ppCtx
** Output. A data area allocated out of private memory for the storage
** of context information.
** 
** reserved
** Reserved for future use. Must be set to NULL.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Return Application Context      */
  sqleGetCurrentCtx (
        void ** ppCtx,                     /* pointer to a pointer to ctx     */
        void * reserved,                   /* reserved                        */
        struct sqlca * pSqlca);            /* SQLCA                           */


/******************************************************************************
** sqleInterruptCtx API
** Interrupts the specified context.
** 
** Scope
** 
** The scope of this API is limited to the immediate process.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** Database
** 
** API include file
** 
** sql.h
** 
** sqleInterruptCtx API parameters
** 
** pCtx
** Input. A valid context previously allocated by sqleBeginCtx.
** 
** reserved
** Reserved for future use. Must be set to NULL.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
** 
** Usage notes
** 
** During processing, this API:
** - Switches to the context that has been passed in
** - Sends an interrupt
** - Switches to the original context
** - Exits.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Interrupt Context               */
  sqleInterruptCtx (
        void * pCtx,                       /* pointer to ctx                  */
        void * reserved,                   /* reserved                        */
        struct sqlca * pSqlca);            /* SQLCA                           */


/******************************************************************************
** Error/SQLSTATE Message Retrieval Interface Parameters/Return Codes
*******************************************************************************/

/* Get Error Message Macro                                                    */
#define  sqlaintp(msgbuf,bufsize,linesize,sqlcaptr) \
sqlaintp_api(msgbuf,bufsize,linesize, \
(char *)"db2sql.mo", sqlcaptr)
/******************************************************************************
** sqlaintp API
** Retrieves the message associated with an error condition specified by the
** sqlcode field of the sqlca structure.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqlaintp API parameters
** 
** pBuffer
** Output. A pointer to a string buffer where the message text is placed. If the
** message must be truncated to fit in the buffer, the truncation allows for the
** null string terminator character.
** 
** BufferSize
** Input. Size, in bytes, of a string buffer to hold the retrieved message text.
** 
** LineWidth
** Input. The maximum line width for each line of message text. Lines are broken
** on word boundaries. A value of zero indicates that the message text
** is returned without line breaks.
** 
** pSqlca
** Output. A pointer to the sqlca structure.
** 
** Usage notes
** 
** One message is returned per call.
** 
** A new line (line feed, LF, or carriage return/line feed, CR/LF) sequence is
** placed at the end of each message.
** 
** If a positive line width is specified, new line sequences are inserted
** between words so that the lines do not exceed the line width.
** 
** If a word is longer than a line width, the line is filled with as many
** characters as will fit, a new line is inserted, and the remaining characters
** are placed on the next line.
** 
** In a multi-threaded application, sqlaintp must be attached to a
** valid context; otherwise, the message text for SQLCODE - 1445 cannot
** be obtained
** 
** Return codes
** 
** Code
** 
** Message
** 
** +i
** Positive integer indicating the number of bytes in the formatted message. If
** this is greater than the buffer size input by the caller, the message is
** truncated.
** 
** -1
** Insufficient memory available for message formatting services to
** function. The requested message is not returned.
** 
** -2
** No error. The sqlca did not contain an error code (SQLCODE = 0).
** 
** -3
** Message file inaccessible or incorrect.
** 
** -4
** Line width is less than zero.
** 
** -5
** Invalid sqlca, bad buffer address, or bad buffer length.
** 
** If the return code is -1 or -3, the message buffer will contain additional
** information about the problem.
** 
** REXX API syntax
** 
** GET MESSAGE INTO :msg [LINEWIDTH width]
** 
** REXX API parameters
** 
** msg
** REXX variable into which the text message is placed.
** 
** width 
** Maximum line width for each line in the text message. The line is broken on
** word boundaries. If width is not given or set to 0, the message text returns
** without line breaks.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Get Error Message               */
  sqlaintp_api (
        char * pBuffer,                    /* buffer for message text         */
        short BufferSize,                  /* buffer size                     */
        short LineWidth,                   /* line width                      */
        const char * pMsgFileName,         /* message file                    */
        struct sqlca * pSqlca);            /* SQLCA                           */

/* Generic Interface to Error Message Retrieval                               */
/******************************************************************************
** sqlgintp API
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Get Error Message               */
  sqlgintp (
        short BufferSize,                  /* buffer size                     */
        short LineWidth,                   /* line width                      */
        struct sqlca * pSqlca,             /* SQLCA                           */
        _SQLOLDCHAR * pBuffer);            /* buffer                          */

/******************************************************************************
** sqlogstt API
** Retrieves the message text associated with an SQLSTATE value.
** 
** Authorization
** 
** None
** 
** Required connection
** 
** None
** 
** API include file
** 
** sql.h
** 
** sqlogstt API parameters
** 
** pBuffer
** Output. A pointer to a string buffer where the message text is to be
** placed. If the message must be truncated to fit in the buffer, the
** truncation allows for the null string terminator character.
** 
** BufferSize
** Input. Size, in bytes, of a string buffer to hold the retrieved message text.
** 
** LineWidth
** Input. The maximum line width for each line of message text. Lines are broken
** on word boundaries. A value of zero indicates that the message text is
** returned without line breaks.
** 
** pSqlstate
** Input. A string containing the SQLSTATE for which the message text is to be
** retrieved. This field is alphanumeric and must be either five-digit (specific
** SQLSTATE) or two-digit (SQLSTATE class, first two digits of an
** SQLSTATE). This field does not need to be NULL-terminated if 5 digits
** are being passed in, but must be NULL-terminated if 2 digits are being
** passed.
** 
** Usage notes
** 
** One message is returned per call.
** 
** A LF/NULL sequence is placed at the end of each message.
** 
** If a positive line width is specified, LF/NULL sequences are inserted between
** words so that the lines do not exceed the line width.
** 
** If a word is longer than a line width, the line is filled with as many
** characters as will fit, a LF/NULL is inserted, and the remaining
** characters are placed on the next line.
** 
** Return codes
** 
** Code
** 
** Message
** 
** +i
** Positive integer indicating the number of bytes in the formatted message. If
** this is greater than the buffer size input by the caller, the message is
** truncated.
** 
** -1
** Insufficient memory available for message formatting services to
** function. The requested message is not returned.
** 
** -2
** The SQLSTATE is in the wrong format. It must be alphanumeric and be
** either 2 or 5 digits in length.
** 
** -3
** Message file inaccessible or incorrect.
** 
** -4
** Line width is less than zero.
** 
** -5
** Invalid sqlca, bad buffer address, or bad buffer length.
** 
** If the return code is -1 or -3, the message buffer will contain further
** information about the problem.
** 
** REXX API syntax
** 
** GET MESSAGE FOR SQLSTATE sqlstate INTO :msg [LINEWIDTH width]
** 
** REXX API parameters
** 
** sqlstate
** The SQLSTATE for which the message text is to be retrieved.
** 
** msg
** REXX variable into which the message is placed.
** 
** width
** Maximum line width for each line of the message text. The line is broken on
** word boundaries. If a value is not specified, or this parameter is set to 0,
** the message text returns without line breaks.
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Sqlstate Message Retrieval      */
  sqlogstt (
        char * pBuffer,                    /* buffer for message text         */
        short BufferSize,                  /* buffer size                     */
        short LineWidth,                   /* line width                      */
        char * pSqlstate);                 /* sqlstate                        */

/* Generic Interface to SQLSTATE Message Retrieval                            */
/******************************************************************************
** sqlggstt API
*******************************************************************************/
SQL_API_RC SQL_API_FN                      /* Sqlstate Message Retrieval      */
  sqlggstt (
        short BufferSize,                  /* buffer size                     */
        short LineWidth,                   /* line width                      */
        char * pSqlstate,                  /* sqlstate                        */
        char * pBuffer);                   /* buffer                          */

/* Return Codes for sqlaintp/sqlogstt                                         */
#define SQLA_ERR_BADCC         -1      /* insufficient memory for msg file    */
#define SQLA_ERR_NOCOD         -2      /* no error code in SQLCA              */
#define SQLA_ERR_NOMSG         -3      /* message file inaccessible or        */
                                       /* incorrect                           */
#define SQLA_ERR_BADLL         -4      /* specified line length negative      */
#define SQLA_ERR_BADCA         -5      /* invalid sqlca/buffer addr/length    */

/* Administrative/Database Authorizations returned from Get Administrative    */
/* Authorizations function                                                    */
/* Authorizations granted explicitly to user                                  */
#define SQL_SYSADM             0x1     /* SYSADM Authority                    */
#define SQL_DBADM              0x2     /* DBADM Authority                     */
#define SQL_CREATETAB          0x4     /* CREATETAB Privilege                 */
#define SQL_BINDADD            0x8     /* BINDADD Privilege                   */
#define SQL_CONNECT            0x10    /* CONNECT Privilege                   */
#define SQL_CREATE_NOT_FENC    0x20    /* CREATE_NOT_FENCED Privilege         */
#define SQL_SYSCTRL            0x40    /* SYSCTRL Authority                   */
#define SQL_SYSMAINT           0x80    /* SYSMAINT Authority                  */
#define SQL_IMPLICIT_SCHEMA    0x10000 /* IMPLICIT_SCHEMA Privilege           */
#define SQL_LOAD               0x20000 /* LOAD Privilege                      */
#define SQL_CREATE_EXT_RT      0x40000 /* CREATE_EXTERNAL_ROUTINE Privilege   */
#define SQL_LIBADM             0x80000 /* LIBRARYADM Privilege                */
#define SQL_QUIESCE_CONN       0x100000 /* QUIESCE_CONNECT Privilege          */
#define SQL_SECADM             0x200000 /* SECURITYADM Privilege              */
#define SQL_SYSQUIESCE         0x400000 /* SYSQUIESCE Authority               */
#define SQL_SYSMON             0x800000 /* SYSMON Authority                   */

/* Composite of authorizations granted explicitly to user, to groups of       */
/* which user is a member, and to PUBLIC                                      */
#define SQL_SYSADM_GRP         0x100
#define SQL_DBADM_GRP          0x200
#define SQL_CREATETAB_GRP      0x400
#define SQL_BINDADD_GRP        0x800
#define SQL_CONNECT_GRP        0x1000
#define SQL_CREATE_NOT_FENC_GRP 0x2000
#define SQL_SYSCTRL_GRP        0x4000
#define SQL_SYSMAINT_GRP       0x8000
#define SQL_IMPLICIT_SCHEMA_GRP 0x1000000
#define SQL_LOAD_GRP           0x2000000
#define SQL_CREATE_EXT_RT_GRP  0x4000000
#define SQL_LIBADM_GRP         0x8000000
#define SQL_QUIESCE_CONN_GRP   0x10000000
#define SQL_SECADM_GRP         0x20000000
#define SQL_SYSQUIESCE_GRP     0x40000000
#define SQL_SYSMON_GRP         0x80000000

/* Table/View Authorizations/Dependencies Bit definitions in                  */
/* SYSTABAUTH.TABAUTH and SYSPLANDEP.TABAUTH                                  */
#define SQL_TAB_CTL            0x1     /* Control Authority                   */
#define SQL_TAB_ALT            0x2     /* Alter Privilege                     */
#define SQL_TAB_DEL            0x4     /* Delete Privilege/Dependency         */
#define SQL_TAB_IDX            0x8     /* Index Privilege                     */
#define SQL_TAB_INS            0x10    /* Insert Privilege/Dependency         */
#define SQL_TAB_SEL            0x20    /* Select Privilege/Dependency         */
#define SQL_TAB_UPD            0x40    /* Update Privilege/Dependency         */
#define SQL_TAB_REF            0x80    /* Reference Privilege                 */
#define SQL_TAB_KEY            0x2000  /* Key Dependency                      */
#define SQL_TAB_CAS            0x4000  /* Cascade Dependency                  */

/* Bit definitions for SYSTABAUTH.TABAUTH indicating the specified table or   */
/* view privilege is grantable.                                               */
#define SQL_TAB_ALT_G          0x200   /* Alter Privilege Grantable           */
#define SQL_TAB_DEL_G          0x400   /* Delete Privilege Grantable          */
#define SQL_TAB_IDX_G          0x800   /* Index Privilege Grantable           */
#define SQL_TAB_INS_G          0x1000  /* Insert Privilege Grantable          */
#define SQL_TAB_SEL_G          0x2000  /* Select Privilege Grantable          */
#define SQL_TAB_UPD_G          0x4000  /* Update Privilege Grantable          */
#define SQL_TAB_REF_G          0x8000  /* References Privilege Grantable      */

/* Definitions for application remote interface                               */
#define SQLZ_DISCONNECT_PROC   1       /* Unload Progam                       */
#define SQLZ_HOLD_PROC         2       /* Keep program loaded                 */
/* The following functions and symbols are obsolete and may not be supported  */
/* in future releases. The obsolete functions are provided for backward       */
/* compatibility and exported from DB2API.LIB. All applications should be     */
/* migrated to use new APIs.                                                  */
#define SQLA_RC_BAD_PASSWD     -4      /* Invalid password                    */
#define SQL_MAXSTMTS           32767   /* Maximum statements (see SQL         */
                                       /* reference)                          */
#define SQL_MAXVARS            32767   /* Maximum host variables per          */
                                       /* precompile unit (see SQL            */
                                       /* reference)                          */
#define SQL_DYNAMICRULES_INVOKE SQL_DYNAMICRULES_INVOKERUN /* Dynamic SQL in  */
                                       /* UDF or stored procedure will use    */
                                       /* authid of invoker of UDF or stored  */
                                       /* procedure                           */
#define SQL_DYNAMICRULES_DEFINE SQL_DYNAMICRULES_DEFINERUN /* Dynamic SQL in  */
                                       /* UDF or stored procedure will use    */
                                       /* authid of definer of UDF or stored  */
                                       /* procedure                           */

#ifdef __cplusplus 
}
#endif

#endif /* SQL_H_SQL */
