#include "paillier.h"

void PAI_HomAdd(U8 *pbBN_Result, U8 *pbBN_c1, U8 *pbBN_c2, U8 *pbBN_n, S32 iBNWordLen)
{
	/****************************/
	U32 BN_N2[BNMAXWordLen];
	U32 BN_N[BNMAXWordLen];
	U32 BN_R[BNMAXWordLen];
	U32 BN_R2[BNMAXWordLen];
	U32 BN_C1[BNMAXWordLen];
	U32 BN_C2[BNMAXWordLen];
	U32 BN_C3[BNMAXWordLen];
	U32 BN_One[BNMAXWordLen];
	U32 wModuleConst = 0;

	U8 bBN_T[8*BNMAXWordLen] = {0};
	S32 i = 0;
	S32 len = 0;
	/****************************/

	BN_Reset(BN_N2, BNMAXWordLen);
	BN_Reset(BN_N, BNMAXWordLen);
	BN_Reset(BN_R, BNMAXWordLen);
	BN_Reset(BN_R2, BNMAXWordLen);
	BN_Reset(BN_C1, BNMAXWordLen);
	BN_Reset(BN_C2, BNMAXWordLen);
	BN_Reset(BN_C3, BNMAXWordLen);
	BN_Reset(BN_One, BNMAXWordLen);
	BN_One[0] = LSBOfWord;

	ByteToBN(pbBN_n, 4*iBNWordLen, BN_N, iBNWordLen);
	ByteToBN(pbBN_c1, 8*iBNWordLen, BN_C1, 2*iBNWordLen);
	ByteToBN(pbBN_c2, 8*iBNWordLen, BN_C2, 2*iBNWordLen);

	//n^2
	BN_Mul(BN_N2, BN_N, BN_N, iBNWordLen);

	wModuleConst = BN_GetMontConst(BN_N2[0], 32);
	BN_GetR(BN_R, BN_N2, 2*iBNWordLen);

	BN_GetR2(BN_R2, BN_R, BN_N2, wModuleConst, 2*iBNWordLen, LogPaiBN2BitLen);
	BN_GetLastRes(BN_R2, BN_N2, 2*iBNWordLen);

	BN_ModMul_Mont(BN_C1, BN_C1, BN_R2, BN_N2, wModuleConst, 2*iBNWordLen);
	BN_ModMul_Mont(BN_C2, BN_C2, BN_R2, BN_N2, wModuleConst, 2*iBNWordLen);
	BN_ModMul_Mont(BN_C3, BN_C1, BN_C2, BN_N2, wModuleConst, 2*iBNWordLen);
	BN_ModMul_Mont(BN_C3, BN_C3, BN_One, BN_N2, wModuleConst, 2*iBNWordLen);
	BN_GetLastRes(BN_C3, BN_N2, 2*iBNWordLen);

	BNToByte(BN_C3, 2*iBNWordLen, pbBN_Result, &len);
}