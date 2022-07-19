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
 * @file ZkpPrecompiled.h
 * @author: yujiechen
 * @date 2022-07-18
 */

#pragma once

#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include <bcos-crypto/zkp/discretezkp/DiscreteLogarithmZkp.h>

namespace bcos
{
namespace precompiled
{
#if 0
contract ZkpPrecompiled
{
function verifyEitherEqualityProof(bytes c1_point, bytes c2_point, bytes c3_point, bytes proof, bytes c_basepoint, bytes blinding_basepoint) public view returns(bool);
function verifyKnowledgeProof(bytes c_point, bytes proof, bytes c_basepoint, bytes blinding_basepoint) public view returns(bool);
function verifyFormatProof(bytes c1_point, bytes c2_point, bytes proof, bytes c1_basepoint, bytes c2_basepoint, bytes blinding_basepoint) public view returns(bool);
function verifySumProof(bytes c1_point, bytes c2_point, bytes c3_point, bytes proof, bytes value_basepoint, bytes blinding_basepoint)public view returns(bool);
function verifyProductProof(bytes c1_point, bytes c2_point, bytes c3_point, bytes proof, bytes value_basepoint, bytes blinding_basepoint) public view returns(bool);
function verifyEqualityProof(bytes c1_point, bytes c2_point, bytes proof, bytes basepoint1, bytes basepoint2)public view returns(bool);
}
#endif

class ZkpPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<ZkpPrecompiled>;
    ZkpPrecompiled(bcos::crypto::Hash::Ptr _hashImpl);
    ~ZkpPrecompiled() override{};

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    void verifyEitherEqualityProof(CodecWrapper const& _codec, bytesConstRef _paramData,
        PrecompiledExecResult::Ptr _callResult);

    void verifyKnowledgeProof(CodecWrapper const& _codec, bytesConstRef _paramData,
        PrecompiledExecResult::Ptr _callResult);

    void verifyFormatProof(CodecWrapper const& _codec, bytesConstRef _paramData,
        PrecompiledExecResult::Ptr _callResult);

    void verifySumProof(CodecWrapper const& _codec, bytesConstRef _paramData,
        PrecompiledExecResult::Ptr _callResult);

    void verifyProductProof(CodecWrapper const& _codec, bytesConstRef _paramData,
        PrecompiledExecResult::Ptr _callResult);


    void verifyEqualityProof(CodecWrapper const& _codec, bytesConstRef _paramData,
        PrecompiledExecResult::Ptr _callResult);

private:
    bcos::crypto::DiscreteLogarithmZkp::Ptr m_zkpImpl;
};
}  // namespace precompiled
}  // namespace bcos
