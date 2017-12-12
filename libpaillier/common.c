#include "common.h"

S32 CharToByte(const S8 *pCharBuf, S32 charlen, U8 *pByteBuf, S32 *bytelen)
{
	S32 i = 0;
	U8 hdata = 0;
	U8 ldata = 0;

	S32 charlen_tmp = charlen;
	if (charlen_tmp & LSBOfWord)
	{
		charlen_tmp += 1;
		*bytelen = charlen_tmp >> 1;
		if (ConvertHexChar(pCharBuf[0], &ldata) == 1)
		{
			pByteBuf[0] = ldata;
		}
		for (i = 1; i < *bytelen; i++)
		{
			if (ConvertHexChar(pCharBuf[2 * i - 1], &hdata) == 1)
			{
				if (ConvertHexChar(pCharBuf[2 * i], &ldata) == 1)
				{
					pByteBuf[i] = (hdata << 4) | ldata;
				}
				else
				{
					return 0;
				}
			}
			else
			{
				return 0;
			}
		}		 
	}
	else
	{
		*bytelen = charlen_tmp >> 1;
		for (i = 0; i < *bytelen; i++)
		{
			if (ConvertHexChar(pCharBuf[2 * i], &hdata) == 1)
			{
				if (ConvertHexChar(pCharBuf[2 * i + 1], &ldata) == 1)
				{
					pByteBuf[i] = (hdata << 4) | ldata;
				}
				else
				{
					return 0;
				}
			}
			else
			{
				return 0;
			}
		}
	}	
    
	return 1;
}

S32 ConvertHexChar(S8 ch, U8 *ch_byte)
{
	if ((ch >= '0') && (ch <= '9'))
	{

		*ch_byte = (U8)(ch - 0x30);
		return 1;

	}
	else
	{
		if ((ch >= 'A') && (ch <= 'F'))
		{
			*ch_byte = (U8)(ch - 'A' + 0x0a);
			return 1;
		}
		else
		{
			if ((ch >= 'a') && (ch <= 'f'))
			{
				*ch_byte = (U8)(ch - 'a' + 0x0a);
				return 1;
			}
		}
	}
    
	return 0;
}

S32 ByteToBN(U8 *pByteBuf, S32 bytelen, U32 *pwBN, S32 iBNWordLen)
{
	S32 ExpLen = bytelen >> 2;
	S32 Rem = bytelen & 0x00000003;

	if (Rem != 0)
	{
		ExpLen += 1; 
	}

	if (ExpLen > iBNWordLen)
	{
		return 0;
	}

	S32 i = bytelen - 1;
	S32 j = 0;
	while (i >= Rem)
	{
		pwBN[j] = ((U32)pByteBuf[i]) | ((U32)pByteBuf[i - 1] << 8) | ((U32)pByteBuf[i - 2] << 16) | ((U32)pByteBuf[i - 3] << 24);
		i -= 4;
		j++;
	}

	i = 0;
	while (i < Rem)
	{
		pwBN[j] = (pwBN[j] << 8) | ((U32)pByteBuf[i]);
		i++;
	}

	return 1;
}

S32 BNToByte(U32 *pwBN,S32 iBNWordLen,U8 *pByteBuf,S32 *bytelen)
{
	U8 * P = pByteBuf;
   	S32 i; 
	for(i = iBNWordLen - 1; i >= 0; i--)	
	{
		U32 W = pwBN[i];
		*P++=(U8) ((W & 0xFF000000) >> 24);
		*P++=(U8) ((W & 0x00FF0000) >> 16);
		*P++=(U8) ((W & 0x0000FF00) >> 8);
		*P++=(U8) (W &  0x000000FF) ;
	}
	*bytelen = iBNWordLen << 2;	

	return 1;
}

void U8_Print(U8* pwSource,S32 len)
{       
	S32 i;
	for(i = 0;i<len;i++)
	{
		printf("%02X",pwSource[i]);
	}
    
	printf("\n");
}

S32  U8_JE(U8* pwX,U8* pwY,S32 len)
{
	S32 i;
	for(i = 0;i<len;i++)
	{
		if(pwX[i]!=pwY[i])
			return 0;
	}
    
	return 1;
}
