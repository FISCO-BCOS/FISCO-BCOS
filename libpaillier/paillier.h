#ifndef __HEADER_PAILLER_H__
#define __HEADER_PAILLER_H__

#include "bn.h"
#include "common.h"
#include "macro.h"

#ifdef  __cplusplus
extern "C" {
#endif

void PAI_HomAdd(U8 *pbBN_Result, U8 *pbBN_c1, U8 *pbBN_c2, U8 *pbBN_n, S32 iBNWordLen);

#ifdef  __cplusplus
}
#endif


#endif

