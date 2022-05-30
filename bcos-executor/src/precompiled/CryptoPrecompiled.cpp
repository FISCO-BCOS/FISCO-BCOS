/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file CryptoPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "CryptoPrecompiled.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/Signature.h>
#include <bcos-crypto/signature/ed25519/Ed25519Crypto.h>
#include <bcos-crypto/signature/sm2.h>

using namespace bcos;
using namespace bcos::codec;
using namespace bcos::crypto;
using namespace bcos::executor;
using namespace bcos::precompiled;

// precompiled interfaces related to hash calculation
const char* const CRYPTO_METHOD_SM3_STR = "sm3(bytes)";
// Note: the interface here can't be keccak256k1 for naming conflict
const char* const CRYPTO_METHOD_KECCAK256_STR = "keccak256Hash(bytes)";
// precompiled interfaces related to verify
// sm2 verify: (message, sign)
const char* const CRYPTO_METHOD_SM2_VERIFY_STR = "sm2Verify(bytes,bytes)";
// FIXME: add precompiled interfaces related to VRF verify
// the params are (vrfInput, vrfPublicKey, vrfProof)
// const char* const CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR =
// "curve25519VRFVerify(string,string,string)";

CryptoPrecompiled::CryptoPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[CRYPTO_METHOD_SM3_STR] = getFuncSelector(CRYPTO_METHOD_SM3_STR, _hashImpl);
    name2Selector[CRYPTO_METHOD_KECCAK256_STR] =
        getFuncSelector(CRYPTO_METHOD_KECCAK256_STR, _hashImpl);
    name2Selector[CRYPTO_METHOD_SM2_VERIFY_STR] =
        getFuncSelector(CRYPTO_METHOD_SM2_VERIFY_STR, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> CryptoPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    auto funcSelector = getParamFunc(_callParameters->input());
    auto paramData = _callParameters->params();
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(paramData.size());
    if (funcSelector == name2Selector[CRYPTO_METHOD_SM3_STR])
    {
        bytes inputData;
        codec.decode(paramData, inputData);

        auto sm3Hash = crypto::sm3Hash(ref(inputData));
        PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: sm3")
                               << LOG_KV("input", toHexString(inputData))
                               << LOG_KV("result", toHexString(sm3Hash));
        _callParameters->setExecResult(codec.encode(codec::toString32(sm3Hash)));
    }
    else if (funcSelector == name2Selector[CRYPTO_METHOD_KECCAK256_STR])
    {
        bytes inputData;
        codec.decode(paramData, inputData);
        auto keccak256Hash = crypto::keccak256Hash(ref(inputData));
        PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: keccak256")
                               << LOG_KV("input", toHexString(inputData))
                               << LOG_KV("result", toHexString(keccak256Hash));
        _callParameters->setExecResult(codec.encode(codec::toString32(keccak256Hash)));
    }
    else if (funcSelector == name2Selector[CRYPTO_METHOD_SM2_VERIFY_STR])
    {
        sm2Verify(_executive, paramData, _callParameters);
    }
    else
    {
        // no defined function
        PRECOMPILED_LOG(ERROR) << LOG_DESC("CryptoPrecompiled: undefined method")
                               << LOG_KV("funcSelector", std::to_string(funcSelector));
        _callParameters->setExecResult(codec.encode(u256((int)CODE_UNKNOW_FUNCTION_CALL)));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}

void CryptoPrecompiled::sm2Verify(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    try
    {
        bytes message;
        bytes sm2Sign;
        codec.decode(_paramData, message, sm2Sign);
        auto msgHash = HashType(message.data(), message.size());
        Address account;
        bool verifySuccess = true;
        auto publicKey = crypto::sm2Recover(msgHash, ref(sm2Sign));
        if (!publicKey)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_DESC("CryptoPrecompiled: sm2Verify failed for recover public key failed");
            _callResult->setExecResult(codec.encode(false, account));
            return;
        }

        account = right160(
            crypto::sm3Hash(bytesConstRef(publicKey->data().data(), publicKey->data().size())));
        PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: sm2Verify")
                               << LOG_KV("verifySuccess", verifySuccess)
                               << LOG_KV("publicKey", publicKey->hex())
                               << LOG_KV("account", account);
        _callResult->setExecResult(codec.encode(verifySuccess, account));
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("CryptoPrecompiled: sm2Verify exception")
                                 << LOG_KV("e", boost::diagnostic_information(e));
        Address emptyAccount;
        _callResult->setExecResult(codec.encode(false, emptyAccount));
    }
}
