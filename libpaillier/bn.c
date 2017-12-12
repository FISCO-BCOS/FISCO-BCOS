#include "bn.h"

void BN_Print(U32 *pwBN, S32 iBNWordLen)
{
	S32 i;
	for (i = iBNWordLen - 1; i >= 0; i--)
	{
		printf("%08X", pwBN[i]);
	}

	printf("\n");
}

void BN_Reset(U32 *pwBN,S32 iBNWordLen)
{
	S32 i;
	for (i = 0; i < iBNWordLen; i++)
		pwBN[i] = 0x0;
}

void BN_Assign(U32 *pwDest, U32 *pwSource, S32  iBNWordLen)
{
	S32 i;
	for (i = 0; i < iBNWordLen; i++)
		pwDest[i] = pwSource[i];
}

S32 BN_JA(U32 *pwX, U32 *pwY, S32 iBNWordLen)
{  
	S32 i;
	for (i = iBNWordLen - 1; i >= 0; i--)
	{
		if (pwX[i] > pwY[i])
		{
			return 1;
		}
		else
		{
			if (pwX[i] < pwY[i])
			{
				return 0;
			}
		}
	}
    
	return 0;
}

U32 BN_Add( U32 *pwSum, U32 *pwX, U32 *pwY,S32  iBNWordLen)
{
    U64 carry = 0;
	  
	S32 i; 
	for (i = 0; i < iBNWordLen; i++)
    {
        carry = (U64)pwX[i] + (U64)pwY[i] + carry;
        pwSum[i] = (U32)carry;
        carry = carry >> 32;
    }	
    
    return (U32)carry;
}

U32 BN_Sub(U32 *pwDiff, U32 *pwX, U32 *pwY, S32  iBNWordLen)
{
    U64 borrow = 0;
	
    S32 i; 
    for (i = 0; i < iBNWordLen; i++)
    {
        borrow = (U64)pwX[i] - (U64)pwY[i] + borrow;
        pwDiff[i] = (U32)borrow;
        borrow = (U64)(((S64)borrow) >> 32);
    }
    
    return (U32)borrow;
}

void BN_Mul(U32 *pwPro, U32 *pwX, U32 *pwY, S32  iBNWordLen)
{
	U64 carry = 0;

	S32 i = iBNWordLen << 1;
	BN_Reset(pwPro, i);
    
	for (i = 0; i < iBNWordLen; i++)
	{
		carry = 0;
		S32 j;
		for (j = 0; j < iBNWordLen; j++)
		{
			carry = (U64)pwPro[i + j] + (U64)pwX[j] * (U64)pwY[i] + carry;
			pwPro[i + j] = (U32)carry;
			carry >>= WordLen;;
		}
		pwPro[i + iBNWordLen] = (U32)(carry);
	}
}

void BN_ModAdd(U32 *pwResult, U32 *pwX, U32 *pwY, U32 *pwModule, S32  iBNWordLen)
{
    U32 c = BN_Add(pwResult, pwX, pwY,iBNWordLen);
    if (c == 0)
        return;

    do
    {
        c = BN_Sub(pwResult, pwResult, pwModule,iBNWordLen);
    } while (c==0);	
}

void BN_ModSub(U32 *pwResult, U32 *pwX, U32 *pwY, U32 *pwModule, S32  iBNWordLen)
{
    U32 c = BN_Sub(pwResult, pwX, pwY,iBNWordLen);
    if (c == 0)
        return;

    do
    {
        c = BN_Add(pwResult, pwResult, pwModule,iBNWordLen);
    } while (c == 0);	
}

U32 BN_GetMontConst(U32 nLastU32, S32 nRadix)
{
	U64 y = 1;
	U64 flag_2_i = 1;
	U64 flag_last_i = 1;
    
        S32 i; 
	for (i = 2; i <= nRadix; i++ )
	{
		flag_2_i = flag_2_i << 1;
		flag_last_i = (flag_last_i << 1) | 0x01;

        U64 tmp = nLastU32 * y;
		tmp = tmp & flag_last_i;
		if ( tmp > flag_2_i)
		{
			y = y + flag_2_i;
		}
	}
	flag_2_i = flag_2_i << 1;

	return (U32)(flag_2_i - y);
}

void BN_ModMul_Mont(U32 *pwResult, U32 *pwX, U32 *pwY, U32 *pwModule, U32 wModuleConst, S32 iBNWordLen)
{
	U32 D[BNMAXWordLen + 2];
	BN_Reset(D, BNMAXWordLen + 2);
	
	int i;
	for (i = 0; i < iBNWordLen; i++)
	{
		U64 carry = 0;
		int j;
		for (j = 0; j < iBNWordLen; j++)
		{
			carry = (U64)D[j] + (U64)pwX[j] * (U64)pwY[i] + carry;
			D[j] = (U32)carry;
			carry = carry >> 32;
		}
		
		carry = (U64)D[iBNWordLen] + carry;
		D[iBNWordLen] = (U32)carry;
		D[iBNWordLen + 1] = (U32)(carry >> 32);
		
		carry = (U64)D[0] * (U64)wModuleConst;
		U32 U = (U32)carry;
		carry = (U64)D[0] + (U64)U * (U64)pwModule[0];
		carry = carry >> 32;
		
		for (j = 1; j < iBNWordLen; j++)
		{
			carry = (U64)D[j] + (U64)U * (U64)pwModule[j] + carry;
			D[j - 1] = (U32)carry;
			carry = carry >> 32;
		}
		carry = (U64)D[iBNWordLen] + carry;
		D[iBNWordLen - 1] = (U32)carry;
		D[iBNWordLen] = D[iBNWordLen + 1] + (U32)(carry >> 32);
	}
    
	if (D[iBNWordLen] == 0)
		BN_Assign(pwResult,D, iBNWordLen);
	else
		BN_Sub(pwResult, D, pwModule, iBNWordLen);
}

void BN_Random(U32 *pwBN, S32 iBNWordLen)
{
    	S32 i; 
	for (i = 0; i < iBNWordLen; i++)
	{
		U8 B0 = (U8)rand();
		U8 B1 = (U8)rand();
		U8 B2 = (U8)rand();
		U8 B3 = (U8)rand();
		pwBN[i] = ((U32)B3 << 24) | ((U32)B3 << 16) | ((U32)B3 << 8) | ((U32)B3);
	}
}

void BN_GetLastRes(U32 *pwBN, U32 *pwMod, S32 iBNWordLen)
{
	if ( BN_JA(pwMod, pwBN, iBNWordLen) == 0)
	{
		BN_Sub(pwBN, pwBN, pwMod, iBNWordLen);
	}
}

void BN_GetR(U32 *pwR, U32 *pwModule, S32 iBNWordLen)
{
	U32 BNT[BNMAXWordLen];

	BN_Reset(BNT, BNMAXWordLen);

	BN_Sub(pwR, BNT, pwModule, iBNWordLen);//R = 0-N;
}

void BN_GetR2(U32 *pwR2, U32 *pwR, U32 *pwModule, U32 wModuleConst, S32 iBNWordLen, S32 iLogLogBNWordLen)
{
	U32 BN_T1[BNMAXWordLen];
	U32 BN_T2[BNMAXWordLen];

	BN_Reset(BN_T1, BNMAXWordLen);
	BN_Reset(BN_T2, BNMAXWordLen);

	BN_ModAdd(BN_T1, pwR, pwR, pwModule, iBNWordLen);//BN_T1 = 2*R

    	S32 i; 
	for (i = 0; i < iLogLogBNWordLen; i ++ )
	{
		BN_ModMul_Mont(BN_T2, BN_T1, BN_T1, pwModule, wModuleConst, iBNWordLen);//
		BN_Assign(BN_T1, BN_T2, iBNWordLen);
	}

	BN_Assign(pwR2, BN_T2, iBNWordLen);
}
