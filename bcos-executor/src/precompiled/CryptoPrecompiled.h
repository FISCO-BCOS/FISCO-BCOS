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
 * @file CryptoPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-30
 */

#pragma once
#include "../vm/Precompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"

namespace bcos
{
namespace precompiled
{
#if 0
contract Crypto
{
    function sm3(bytes data) public view returns(bytes32);
    function keccak256Hash(bytes data) public view returns(bytes32);
    function sm2Verify(bytes32 message, bytes publicKey, bytes32 r, bytes32 s) public view returns(bool, address);
    function curve25519VRFVerify(string input, string vrfPublicKey, string vrfProof) public view returns(bool,uint256);
}
#endif
class CryptoPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<CryptoPrecompiled>;
    CryptoPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~CryptoPrecompiled() {}
    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender, int64_t gasLeft) override;

private:
    void sm2Verify(
        bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult, CodecWrapper::Ptr _codec);
};
}  // namespace precompiled
}  // namespace bcos
