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
 * (c) 2016-2019 fisco-dev contributors.
 *
 *  @file CryptoPrecompiled.cpp
 *  @author yujiechen
 *  @date 20201202
 */
#include "CryptoPrecompiled.h"
#include "ffi_vrf.h"
#include "libdevcrypto/Hash.h"
#include "libdevcrypto/SM2Signature.h"
#include "libdevcrypto/SM3Hash.h"
#include "libethcore/ABI.h"

using namespace dev;
using namespace dev::eth;
using namespace dev::crypto;
using namespace dev::precompiled;
using namespace dev::blockverifier;

// precompiled interfaces related to hash calculation
const char* const CRYPTO_METHOD_SM3_STR = "sm3(bytes)";
const char* const CRYPTO_METHOD_KECCAK256_STR = "keccak256(bytes)";
// precompiled interfaces related to verify
// sm2 verify: (message, v, r, s)
const char* const CRYPTO_METHOD_SM2_VERIFY_STR = "sm2Verify(bytes32,bytes,bytes32,bytes32)";
// precompiled interfaces related to VRF verify
// the params are (vrfInput, vrfPublicKey, vrfProof)
const char* const CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR =
    "curve25519VRFVerify(string,string,string)";

CryptoPrecompiled::CryptoPrecompiled()
{
    name2Selector[CRYPTO_METHOD_SM3_STR] = getFuncSelector(CRYPTO_METHOD_SM3_STR);
    name2Selector[CRYPTO_METHOD_KECCAK256_STR] = getFuncSelector(CRYPTO_METHOD_KECCAK256_STR);
    name2Selector[CRYPTO_METHOD_SM2_VERIFY_STR] = getFuncSelector(CRYPTO_METHOD_SM2_VERIFY_STR);
    name2Selector[CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR] =
        getFuncSelector(CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR);
}

PrecompiledExecResult::Ptr CryptoPrecompiled::call(
    std::shared_ptr<ExecutiveContext>, bytesConstRef _param, Address const&, Address const&)
{
    auto funcSelector = getParamFunc(_param);
    auto paramData = getParamData(_param);
    ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    callResult->gasPricer()->setMemUsed(_param.size());
    do
    {
        if (funcSelector == name2Selector[CRYPTO_METHOD_SM3_STR])
        {
            bytes inputData;
            abi.abiOut(paramData, inputData);
            auto sm3Hash = sm3(inputData);
            PRECOMPILED_LOG(TRACE)
                << LOG_DESC("CryptoPrecompiled: sm3") << LOG_KV("input", toHex(inputData))
                << LOG_KV("result", toHex(sm3Hash));
            callResult->setExecResult(abi.abiIn("", dev::eth::toString32(sm3Hash)));
            break;
        }
        if (funcSelector == name2Selector[CRYPTO_METHOD_KECCAK256_STR])
        {
            bytes inputData;
            abi.abiOut(paramData, inputData);
            auto keccak256Hash = keccak256(inputData);
            PRECOMPILED_LOG(TRACE)
                << LOG_DESC("CryptoPrecompiled: keccak256") << LOG_KV("input", toHex(inputData))
                << LOG_KV("result", toHex(keccak256Hash));
            callResult->setExecResult(abi.abiIn("", dev::eth::toString32(keccak256Hash)));
            break;
        }
        if (funcSelector == name2Selector[CRYPTO_METHOD_SM2_VERIFY_STR])
        {
            sm2Verify(paramData, callResult);
            break;
        }
        if (funcSelector == name2Selector[CRYPTO_METHOD_CURVE25519_VRF_VERIFY_STR])
        {
            curve25519VRFVerify(paramData, callResult);
            break;
        }
        // no defined function
        callResult->setExecResult(abi.abiIn("", u256(CODE_UNKNOW_FUNCTION_CALL)));
    } while (0);
    return callResult;
}

void CryptoPrecompiled::sm2Verify(bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    ContractABI abi;
    try
    {
        string32 message;
        bytes v;
        string32 r;
        string32 s;
        abi.abiOut(_paramData, message, v, r, s);
        std::shared_ptr<SM2Signature> sm2Signature =
            std::make_shared<SM2Signature>(fromString32(r), fromString32(s), h512(v));
        Address account;
        bool verifySucc = true;
        if (!sm2Signature->isValid())
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_DESC("CryptoPrecompiled: sm2Verify failed for invalid signature");
            _callResult->setExecResult(abi.abiIn("", false, account));
            return;
        }
        auto publicKey = sm2Recover(sm2Signature, fromString32(message));
        if (publicKey == dev::h512())
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_DESC("CryptoPrecompiled: sm2Verify failed for recover public key failed");
            _callResult->setExecResult(abi.abiIn("", false, account));
            return;
        }
        account = right160(sm3(publicKey));
        PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: sm2Verify")
                               << LOG_KV("verifySucc", verifySucc)
                               << LOG_KV("publicKey", toHex(publicKey))
                               << LOG_KV("account", account);
        _callResult->setExecResult(abi.abiIn("", verifySucc, account));
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("CryptoPrecompiled: sm2Verify exception")
                                 << LOG_KV("e", boost::diagnostic_information(e));
        Address emptyAccount;
        _callResult->setExecResult(abi.abiIn("", false, emptyAccount));
    }
}

void CryptoPrecompiled::curve25519VRFVerify(
    bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    PRECOMPILED_LOG(TRACE) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify");
    ContractABI abi;
    try
    {
        std::string vrfPublicKey;
        std::string vrfInput;
        std::string vrfProof;
        abi.abiOut(_paramData, vrfInput, vrfPublicKey, vrfProof);
        u256 randomValue = 0;
        // check the public key and verify the proof
        if (0 == curve25519_vrf_is_valid_pubkey(vrfPublicKey.c_str()) &&
            0 == curve25519_vrf_verify(vrfPublicKey.c_str(), vrfInput.c_str(), vrfProof.c_str()))
        {
            // get the random hash
            auto hexVRFHash = curve25519_vrf_proof_to_hash(vrfProof.c_str());
            randomValue = (u256)(h256(hexVRFHash));
            _callResult->setExecResult(abi.abiIn("", true, randomValue));
            PRECOMPILED_LOG(DEBUG) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify succ")
                                   << LOG_KV("vrfHash", hexVRFHash);
            return;
        }
        _callResult->setExecResult(abi.abiIn("", false, randomValue));
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify failed");
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("CryptoPrecompiled: curve25519VRFVerify exception")
                                 << LOG_KV("e", boost::diagnostic_information(e));
        _callResult->setExecResult(abi.abiIn("", false, u256(0)));
    }
}
