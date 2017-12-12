#ifndef __HEADER_MACRO_H__
#define __HEADER_MACRO_H__

#ifdef __cplusplus
extern "C"{
#endif

#define WRONG						0
#define RIGHT						1

//macro for common bn
#define WordLen						32
#define ByteLen						8
#define WordByteLen					(WordLen/ByteLen)
#define LSBOfWord					0x00000001
#define MSBOfWord					0x80000000
#define Plus						0x00000000
#define Minus						0x00000001

//macro for BN in paillier
#define PaiBNBitLen					1024
#define PaiBNWordLen				32
#define PaiPrimeWordLen				16
#define MAXPrimeWordLen				32
#define Ext_PaiBNWordLen			(PaiBNWordLen + 2)
#define BNMAXWordLen				(2 * PaiBNWordLen + 2)
#define LogPaiBNBitLen				10
#define LogPaiBN2BitLen				11


#ifdef __cplusplus 
}
#endif

#endif
