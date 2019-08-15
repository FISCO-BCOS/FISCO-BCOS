/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file PaillierPrecompiled.h
 *  @author shawnhe
 *  @date 20190808
 */
#include <arpa/inet.h>
#include <string>

#include "PaillierPrecompiled.h"
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <paillier.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::precompiled;

const char* const PAILLIER_METHOD_SET_STR = "paillierAdd(string,string)";

PaillierPrecompiled::PaillierPrecompiled()
{
    name2Selector[PAILLIER_METHOD_SET_STR] = getFuncSelector(PAILLIER_METHOD_SET_STR);
}

bytes PaillierPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    (void)(context), (void)(origin);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("PaillierPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;
    std::string result;

    if (func == name2Selector[PAILLIER_METHOD_SET_STR])
    {
        // paillierAdd(string,string)
        std::string cipher1, cipher2;
        abi.abiOut(data, cipher1, cipher2);

        // check cipher, cipher = pkLen(4 bytes) + PK(pkLen bytes) + ciphertext(2*pkLen bytes)
        if (cipher1.length() != cipher2.length() || cipher1.substr(0, 4) != cipher2.substr(0, 4))
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("PaillierPrecompiled") << LOG_DESC("add invalid ciphers")
                << LOG_KV("cipher1", cipher1) << LOG_KV("cipher2", cipher2);
            getErrorCodeOut(out, CODE_INVALID_CIPHERS);
            return out;
        }

        // parse the length of public-key
        S32 bytelen = 0;
        U8 blen[4] = {0};
        CharToByte(cipher1.c_str(), 4, blen, &bytelen);
        U16 len = 0;
        memcpy((char*)&len, (char*)blen, sizeof(len));
        U16 nLen = len;  // to be written into result
        U32 pkLen = (ntohs(len) + 2) * 2 - 4;
        U32 cipherLen = 3 * pkLen + 4;
        // the length of public key is at least 1024 bits
        if (pkLen < 256 || cipher1.length() != cipherLen)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("PaillierPrecompiled") << LOG_DESC("add invalid ciphers")
                << LOG_KV("cipher1", cipher1) << LOG_KV("cipher2", cipher2);
            getErrorCodeOut(out, CODE_INVALID_CIPHERS);
            return out;
        }

        // Check whether the public-keys are the same
        std::string pk1 = cipher1.substr(4, pkLen);
        std::string pk2 = cipher2.substr(4, pkLen);
        if (pk1 != pk2)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("PaillierPrecompiled") << LOG_DESC("public-keys are different")
                << LOG_KV("pk1", pk1) << LOG_KV("pk2", pk2);
            getErrorCodeOut(out, CODE_DIFFERENT_PUBKEYS);
            return out;
        }

        // format paillier inputs for add
        std::string c1 = cipher1.substr(pkLen + 4);
        std::string c2 = cipher2.substr(pkLen + 4);
        S32 BN_Len = S32(pkLen / 2);
        U8* BN_PK = new U8[BN_Len + 1];
        U8* BN_C1 = new U8[2 * BN_Len + 1];
        U8* BN_C2 = new U8[2 * BN_Len + 1];
        U8* BN_Result = new U8[2 * BN_Len + 1];
        CharToByte(c1.c_str(), 2 * pkLen, BN_C1, &bytelen);
        CharToByte(c2.c_str(), 2 * pkLen, BN_C2, &bytelen);
        CharToByte(pk1.c_str(), pkLen, BN_PK, &bytelen);

        PAI_HomAdd(BN_Result, BN_C1, BN_C2, BN_PK, BN_Len / 4);

        // encode result
        U32 BN_CryptLen = cipherLen / 2;
        U8* BN_CryptResult = new U8[BN_CryptLen + 1];
        memcpy((char*)BN_CryptResult, (char*)&nLen, sizeof(nLen));
        memcpy((char*)(BN_CryptResult + sizeof(nLen)), BN_PK, BN_Len);
        memcpy((char*)(BN_CryptResult + sizeof(nLen) + BN_Len), BN_Result, 2 * BN_Len);
        for (size_t i = 0; i < BN_CryptLen; i++)
        {
            char tmp[3] = {0};
            sprintf(tmp, "%02X", BN_CryptResult[i]);
            result.append(tmp);
        }

        PRECOMPILED_LOG(TRACE) << LOG_BADGE("PaillierPrecompiled")
                               << LOG_DESC("paillierAdd successfully");
        delete[] BN_PK;
        delete[] BN_C1;
        delete[] BN_C2;
        delete[] BN_Result;
        delete[] BN_CryptResult;
        out = abi.abiIn("", result);
        return out;
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("PaillierPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    getErrorCodeOut(out, CODE_UNDEFINED_FUNC);
    return out;
}