#ifndef __HEADER_COMMON_H__
#define __HEADER_COMMON_H__

#include "bn.h"

#define GET_U32(n,b,i)                       \
{                                               \
	(n) = ( (U32) (b)[(i)    ] << 24 )       \
	| ( (U32) (b)[(i) + 1] << 16 )       \
	| ( (U32) (b)[(i) + 2] <<  8 )       \
	| ( (U32) (b)[(i) + 3]       );      \
}

#define PUT_U32(n,b,i)                       \
{                                               \
	(b)[(i)    ] = (U8) ( (n) >> 24 );       \
	(b)[(i) + 1] = (U8) ( (n) >> 16 );       \
	(b)[(i) + 2] = (U8) ( (n) >>  8 );       \
	(b)[(i) + 3] = (U8) ( (n)       );       \
}

#ifdef  __cplusplus
extern "C" {
#endif

S32 ConvertHexChar(S8 ch, U8 *ch_byte);
S32 CharToByte(const S8 *pCharBuf, S32 charlen, U8 *pByteBuf, S32 *bytelen);
S32 ByteToBN(U8 *pByteBuf, S32 bytelen, U32 *pwBN, S32 iBNWordLen);
S32 BNToByte(U32 *pwBN,S32 iBNWordLen,U8 *pByteBuf,S32 *bytelen);

void U8_Print(U8* pwSource,S32 len);
S32  U8_JE(U8* pwX,U8* pwY,S32 len);

#ifdef  __cplusplus
}
#endif

#endif
