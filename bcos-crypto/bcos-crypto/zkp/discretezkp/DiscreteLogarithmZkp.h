/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief implementation for discrete-logarithm-proof
 * @file DiscreteLogarithmZkp.h
 * @date 2022.01.17
 * @author yujiechen
 */
#pragma once
#include <bcos-utilities/Common.h>
#include <wedpr-crypto/WedprDiscreteLogarithmProof.h>
#include <memory>
namespace bcos
{
namespace crypto
{
class DiscreteLogarithmZkp
{
public:
    using Ptr = std::shared_ptr<DiscreteLogarithmZkp>;
    DiscreteLogarithmZkp(size_t _pointLen) : m_pointLen(_pointLen) {}
    DiscreteLogarithmZkp() {}

    virtual ~DiscreteLogarithmZkp() {}

    // wedpr_verify_either_equality_relationship_proof
    bool verifyEitherEqualityProof(bytes const& c1Point, bytes const& c2Point, bytes const& c3Point,
        bytes const& equalityProof, bytes const& basePoint, bytes const& blindingBasePoint);

    // wedpr_verify_knowledge_proof
    bool verifyKnowledgeProof(bytes const& pointData, bytes const& knowledgeProof,
        bytes const& basePointData, bytes const& blindingBasePoint);

    // wedpr_verify_format_proof
    bool verifyFormatProof(bytes const& c1Point, bytes const& c2Point, bytes const& formatProof,
        bytes const& c1BasePoint, bytes const& c2BasePoint, bytes const& blindingBasePoint);

    // wedpr_verify_sum_relationship
    bool verifySumProof(bytes const& c1Point, bytes const& c2Point, bytes const& c3Point,
        bytes const& arithmeticProof, bytes const& valueBasePoint, bytes const& blindingBasePoint);

    // wedpr_verify_product_relationship
    bool verifyProductProof(bytes const& c1Point, bytes const& c2Point, bytes const& c3Point,
        bytes const& arithmeticProof, bytes const& valueBasePoint, bytes const& blindingBasePoint);

    // wedpr_verify_equality_relationship_proof
    bool verifyEqualityProof(bytes const& c1Point, bytes const c2Point, bytes const& equalityProof,
        bytes const& basePoint1, bytes const& basePoint2);

    // wedpr_aggregate_ristretto_point
    bytes aggregateRistrettoPoint(bytes const& pointSum, bytes const& pointShare);

private:
    size_t m_pointLen = 32;
};
}  // namespace crypto
}  // namespace bcos