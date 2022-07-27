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
 * @file ZkpPrecompiled.cpp
 * @author: yujiechen
 * @date 2022-07-18
 */
#include "ZkpPrecompiled.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <wedpr-crypto/WedprDiscreteLogarithmProof.h>

using namespace bcos;
using namespace bcos::precompiled;
// wedpr_verify_either_equality_relationship_proof(c1_point, c2_point, c3_point, proof, c_basepoint,
// blinding_basepoint)
const char* const VERIFY_EITHER_EQUALITY_PROOF_STR =
    "verifyEitherEqualityProof(bytes,bytes,bytes,bytes,bytes,bytes)";
// wedpr_verify_knowledge_proof(c_point, proof, c_basepoint, blinding_basepoint)
const char* const VERIFY_KNOWLEDGE_PROOF_STR = "verifyKnowledgeProof(bytes,bytes,bytes,bytes)";
// wedpr_verify_format_proof(c1_point, c2_point, proof, c1_basepoint, c2_basepoint,
// blinding_basepoint)
const char* const VERIFY_FORMAT_PROOF = "verifyFormatProof(bytes,bytes,bytes,bytes,bytes,bytes)";
// wedpr_verify_sum_relationship(c1_point, c2_point, c3_point, proof, value_basepoint,
// blinding_basepoint)
const char* const VERIFY_SUM_PROOF = "verifySumProof(bytes,bytes,bytes,bytes,bytes,bytes)";
// wedpr_verify_product_relationship(c1_point, c2_point, c3_point, proof, value_basepoint,
// blinding_basepoint)
const char* const VERIFY_PRODUCT_PROOF =
    "verifyProductProof(bytes, bytes, bytes, bytes, bytes, bytes)";
// wedpr_verify_equality_relationship_proof(c1_point, c2_point, proof, basepoint1, basepoint2)
const char* const VERIFY_EQUALITY_PROOF = "verifyEqualityProof(bytes,bytes,bytes,bytes,bytes)";
// wedpr_aggregate_ristretto_point(*point_sum, *point_share,result);
const char* const AGGREGATE_POINT = "aggregatePoint(bytes,bytes)";

ZkpPrecompiled::ZkpPrecompiled(bcos::crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    m_zkpImpl = std::make_shared<bcos::crypto::DiscreteLogarithmZkp>();

    name2Selector[VERIFY_EITHER_EQUALITY_PROOF_STR] =
        getFuncSelector(VERIFY_EITHER_EQUALITY_PROOF_STR, _hashImpl);
    name2Selector[VERIFY_KNOWLEDGE_PROOF_STR] =
        getFuncSelector(VERIFY_KNOWLEDGE_PROOF_STR, _hashImpl);
    name2Selector[VERIFY_FORMAT_PROOF] = getFuncSelector(VERIFY_FORMAT_PROOF, _hashImpl);
    name2Selector[VERIFY_SUM_PROOF] = getFuncSelector(VERIFY_SUM_PROOF, _hashImpl);
    name2Selector[VERIFY_PRODUCT_PROOF] = getFuncSelector(VERIFY_PRODUCT_PROOF, _hashImpl);
    name2Selector[VERIFY_EQUALITY_PROOF] = getFuncSelector(VERIFY_EQUALITY_PROOF, _hashImpl);
    name2Selector[AGGREGATE_POINT] = getFuncSelector(AGGREGATE_POINT, _hashImpl);
}
std::shared_ptr<PrecompiledExecResult> ZkpPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    auto funcSelector = getParamFunc(_callParameters->input());
    auto paramData = _callParameters->params();
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(paramData.size());

    if (name2Selector[VERIFY_EITHER_EQUALITY_PROOF_STR] == funcSelector)
    {
        verifyEitherEqualityProof(codec, paramData, _callParameters);
    }
    else if (name2Selector[VERIFY_KNOWLEDGE_PROOF_STR] == funcSelector)
    {
        // verifyKnowledgeProof
        verifyKnowledgeProof(codec, paramData, _callParameters);
    }
    else if (name2Selector[VERIFY_FORMAT_PROOF] == funcSelector)
    {
        // verifyFormatProof
        verifyFormatProof(codec, paramData, _callParameters);
    }
    else if (name2Selector[VERIFY_SUM_PROOF] == funcSelector)
    {
        // verifySumProof
        verifySumProof(codec, paramData, _callParameters);
    }
    else if (name2Selector[VERIFY_PRODUCT_PROOF] == funcSelector)
    {
        // verifyProductProof
        verifyProductProof(codec, paramData, _callParameters);
    }
    else if (name2Selector[VERIFY_EQUALITY_PROOF] == funcSelector)
    {
        // verifyEqualityProof
        verifyEqualityProof(codec, paramData, _callParameters);
    }
    else if (name2Selector[AGGREGATE_POINT] == funcSelector)
    {
        // aggregateRistrettoPoint
        aggregateRistrettoPoint(codec, paramData, _callParameters);
    }
    else
    {
        // no defined function
        PRECOMPILED_LOG(INFO) << LOG_DESC("ZkpPrecompiled: undefined method")
                              << LOG_KV("funcSelector", std::to_string(funcSelector));
        BOOST_THROW_EXCEPTION(
            bcos::protocol::PrecompiledError("ZkpPrecompiled call undefined function!"));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}

// verifyEitherEqualityProof
void ZkpPrecompiled::verifyEitherEqualityProof(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    bool verifyResult = false;
    try
    {
        bytes c1Point;
        bytes c2Point;
        bytes c3Point;
        bytes equalityProof;
        bytes basePoint;
        bytes blindingBasePoint;
        _codec.decode(
            _paramData, c1Point, c2Point, c3Point, equalityProof, basePoint, blindingBasePoint);
        verifyResult = m_zkpImpl->verifyEitherEqualityProof(
            c1Point, c2Point, c3Point, equalityProof, basePoint, blindingBasePoint);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("verifyEitherEqualityProof exception")
                               << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(verifyResult));
}

void ZkpPrecompiled::verifyKnowledgeProof(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    bool verifyResult = false;
    try
    {
        bytes pointData;
        bytes knowledgeProof;
        bytes basePoint;
        bytes blindingBasePoint;
        _codec.decode(_paramData, pointData, knowledgeProof, basePoint, blindingBasePoint);
        verifyResult = m_zkpImpl->verifyKnowledgeProof(
            pointData, knowledgeProof, basePoint, blindingBasePoint);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("verifyKnowledgeProof exception")
                               << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(verifyResult));
}

void ZkpPrecompiled::verifyFormatProof(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    bool verifyResult = false;
    try
    {
        bytes c1Point;
        bytes c2Point;
        bytes formatProof;
        bytes c1BasePoint;
        bytes c2BasePoint;
        bytes blindingBasePoint;
        _codec.decode(
            _paramData, c1Point, c2Point, formatProof, c1BasePoint, c2BasePoint, blindingBasePoint);
        verifyResult = m_zkpImpl->verifyFormatProof(
            c1Point, c2Point, formatProof, c1BasePoint, c2BasePoint, blindingBasePoint);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("verifyFormatProof exception")
                               << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(verifyResult));
}

void ZkpPrecompiled::verifySumProof(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    bool verifyResult = false;
    try
    {
        bytes c1Point;
        bytes c2Point;
        bytes c3Point;
        bytes arithmeticProof;
        bytes valueBasePoint;
        bytes blindingBasePoint;
        _codec.decode(_paramData, c1Point, c2Point, c3Point, arithmeticProof, valueBasePoint,
            blindingBasePoint);
        verifyResult = m_zkpImpl->verifySumProof(
            c1Point, c2Point, c3Point, arithmeticProof, valueBasePoint, blindingBasePoint);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("verifySumProof exception")
                               << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(verifyResult));
}

void ZkpPrecompiled::verifyProductProof(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    bool verifyResult = false;
    try
    {
        bytes c1Point;
        bytes c2Point;
        bytes c3Point;
        bytes arithmeticProof;
        bytes valueBasePoint;
        bytes blindingBasePoint;
        _codec.decode(_paramData, c1Point, c2Point, c3Point, arithmeticProof, valueBasePoint,
            blindingBasePoint);
        verifyResult = m_zkpImpl->verifyProductProof(
            c1Point, c2Point, c3Point, arithmeticProof, valueBasePoint, blindingBasePoint);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("verifyProductProof exception")
                               << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(verifyResult));
}

void ZkpPrecompiled::verifyEqualityProof(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    bool verifyResult = false;
    try
    {
        bytes c1Point;
        bytes c2Point;
        bytes equalityProof;
        bytes basePoint1;
        bytes basePoint2;
        _codec.decode(_paramData, c1Point, c2Point, equalityProof, basePoint1, basePoint2);
        verifyResult =
            m_zkpImpl->verifyEqualityProof(c1Point, c2Point, equalityProof, basePoint1, basePoint2);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("verifyEqualityProof exception")
                               << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(verifyResult));
}

void ZkpPrecompiled::aggregateRistrettoPoint(
    CodecWrapper const& _codec, bytesConstRef _paramData, PrecompiledExecResult::Ptr _callResult)
{
    int retCode = 0;
    bytes result;
    try
    {
        bytes sumPoint;
        bytes pointShare;
        _codec.decode(_paramData, sumPoint, pointShare);
        result = m_zkpImpl->aggregateRistrettoPoint(sumPoint, pointShare);
    }
    catch (std::exception const& e)
    {
        retCode = -1;
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("aggregateRistrettoPoint exception")
                               << LOG_KV("error", boost::diagnostic_information(e));
    }
    _callResult->setExecResult(_codec.encode(retCode, result));
}