#ifndef __HEADER_BN_H__
#define __HEADER_BN_H__

#include "typedef.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef  __cplusplus
extern "C" {
#endif

void BN_Print(U32 *pwBN,S32 iBNWordLen);
void BN_Reset(U32 *pwBN,S32 iBNWordLen);
void BN_Assign(U32 *pwDest, U32 *pwSource, S32  iBNWordLen);

S32 BN_JE(U32 *pwX, U32 *pwY, S32 iBNWordLen);
S32 BN_JA(U32 *pwX, U32 *pwY, S32 iBNWordLen);

U32 BN_Add( U32 *pwSum, U32 *pwX, U32 *pwY,S32  iBNWordLen);
U32 BN_Sub(U32 *pwDiff, U32 *pwX, U32 *pwY, S32  iBNWordLen);
void BN_Mul(U32 *pwPro, U32 *pwX, U32 *pwY, S32  iBNWordLen);

void BN_ModAdd(U32 *pwResult, U32 *pwX, U32 *pwY, U32 *pwModule, S32  iBNWordLen);
void BN_ModSub(U32 *pwResult, U32 *pwX, U32 *pwY, U32 *pwModule, S32  iBNWordLen);
void BN_ModMul_Mont(U32 *pwResult, U32 *pwX, U32 *pwY, U32 *pwModule,U32 wModuleConst,S32 iBNWordLen);

U32 BN_GetMontConst(U32 nLastWord, S32 nRadix);
void BN_GetR(U32 *pwR, U32 *pwX, S32 iBNWordLen);
void BN_GetR2(U32 *pwR2, U32 *pwR, U32 *pwModule, U32 wModuleConst, S32 iBNWordLen, S32 iLogLogBNWordLen);

void BN_Random(U32 *pwBN, S32 iBNWordLen);
void BN_GetLastRes(U32 *pwBN, U32 *pwMod, S32 iBNWordLen);
	
	
#ifdef  __cplusplus
}
#endif


#endif

