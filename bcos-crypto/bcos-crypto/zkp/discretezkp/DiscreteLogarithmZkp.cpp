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
 * @file DiscreteLogarithmZkp.cpp
 * @date 2022.01.17
 * @author yujiechen
 */
#include "DiscreteLogarithmZkp.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/utilities/ByteBufferUtils.h"
#include "bcos-crypto/zkp/Common.h"
#include "bcos-crypto/zkp/ristretto/RistrettoGroup.h"
#include <cstring>

using namespace bcos;
using namespace bcos::crypto;

namespace
{
// decodes a 32-byte compressed Ristretto255 point; throws InvalidInputInput if the
// input is shorter than 32 bytes (extra trailing bytes are ignored, as in the wedpr
// wrapper); returns false on invalid/non-canonical encodings
bool decodePoint(bytes const& _data, RistrettoPointBytes* _point)
{
    auto input = bytesToInputBuffer(_data, RISTRETTO_POINT_LEN);
    std::memcpy(_point->data(), input.data, RISTRETTO_POINT_LEN);
    return ristrettoIsValidPoint(_point->data());
}

// challenge = Keccak256(concat of 32-byte compressed points) interpreted as a
// little-endian 256-bit integer reduced mod l, like wedpr's hash_to_scalar
RistrettoScalarBytes hashToScalar(bytes const& _compressedPoints)
{
    auto hash = keccak256Hash(bytesConstRef(_compressedPoints.data(), _compressedPoints.size()));
    return ristrettoScalarReduce(hash.data(), hash.size());
}

void appendPoint(bytes& _buffer, const RistrettoPointBytes& _point)
{
    appendBytes(_buffer, _point);
}

void appendPoint(bytes& _buffer, const std::uint8_t* _point)
{
    appendBytes(_buffer, _point, RISTRETTO_POINT_LEN);
}
}  // namespace

// wedpr_verify_either_equality_relationship_proof
bool DiscreteLogarithmZkp::verifyEitherEqualityProof(bytes const& c1PointData,
    bytes const& c2PointData, bytes const& c3PointData, bytes const& equalityProofData,
    bytes const& basePointData, bytes const& blindingBasePointData)
{
    // points are decoded (and short inputs throw InvalidInputInput) BEFORE the proof-length
    // check, matching the base wedpr evaluation order
    RistrettoPointBytes c1, c2, c3, basePoint, blindingBasePoint;
    if (!decodePoint(c1PointData, &c1) || !decodePoint(c2PointData, &c2) ||
        !decodePoint(c3PointData, &c3) || !decodePoint(basePointData, &basePoint) ||
        !decodePoint(blindingBasePointData, &blindingBasePoint))
    {
        return false;
    }
    constexpr size_t proofLen = 8 * RISTRETTO_SCALAR_LEN;
    if (equalityProofData.size() < proofLen)
    {
        return false;
    }
    const auto* proof = equalityProofData.data();
    auto check1 = ristrettoScalarFromBits(proof);
    auto check2 = ristrettoScalarFromBits(proof + 32);
    auto m1 = ristrettoScalarFromBits(proof + 64);
    auto m2 = ristrettoScalarFromBits(proof + 96);
    auto m3 = ristrettoScalarFromBits(proof + 128);
    auto m4 = ristrettoScalarFromBits(proof + 160);
    auto m5 = ristrettoScalarFromBits(proof + 192);
    auto m6 = ristrettoScalarFromBits(proof + 224);

    auto t1 = ristrettoMultiscalarMul({{check1, c2}, {m1, basePoint}, {m2, blindingBasePoint}});
    auto t2 = ristrettoMultiscalarMul({{check1, c1}, {m1, basePoint}, {m3, blindingBasePoint}});
    auto t3 = ristrettoMultiscalarMul({{check2, c3}, {m4, basePoint}, {m5, blindingBasePoint}});
    auto t4 = ristrettoMultiscalarMul({{check2, c1}, {m4, basePoint}, {m6, blindingBasePoint}});

    bytes hashInput;
    appendPoint(hashInput, t1);
    appendPoint(hashInput, t2);
    appendPoint(hashInput, t3);
    appendPoint(hashInput, t4);
    appendPoint(hashInput, c1PointData.data());
    appendPoint(hashInput, c2PointData.data());
    appendPoint(hashInput, c3PointData.data());
    appendPoint(hashInput, basePointData.data());
    appendPoint(hashInput, blindingBasePointData.data());
    auto check = hashToScalar(hashInput);
    auto checkSum = ristrettoScalarAdd(check1, check2);
    return check == checkSum;
}

// wedpr_verify_knowledge_proof
bool DiscreteLogarithmZkp::verifyKnowledgeProof(bytes const& pointData,
    bytes const& knowledgeProofData, bytes const& basePointData, bytes const& blindingBasePointData)
{
    // points are decoded (and short inputs throw InvalidInputInput) BEFORE the proof-length
    // check, matching the base wedpr evaluation order
    RistrettoPointBytes point, basePoint, blindingBasePoint;
    if (!decodePoint(pointData, &point) || !decodePoint(basePointData, &basePoint) ||
        !decodePoint(blindingBasePointData, &blindingBasePoint))
    {
        return false;
    }
    constexpr size_t proofLen = RISTRETTO_POINT_LEN + 2 * RISTRETTO_SCALAR_LEN;
    if (knowledgeProofData.size() < proofLen)
    {
        return false;
    }
    const auto* proof = knowledgeProofData.data();
    if (!ristrettoIsValidPoint(proof))
    {
        return false;
    }
    auto m1 = ristrettoScalarFromBits(proof + 32);
    auto m2 = ristrettoScalarFromBits(proof + 64);

    bytes hashInput;
    appendPoint(hashInput, proof);
    appendPoint(hashInput, pointData.data());
    appendPoint(hashInput, basePointData.data());
    appendPoint(hashInput, blindingBasePointData.data());
    auto check = hashToScalar(hashInput);

    auto t1 = ristrettoMultiscalarMul({{m1, basePoint}, {m2, blindingBasePoint}, {check, point}});
    return ristrettoEqual(t1, proof);
}

// wedpr_verify_format_proof
bool DiscreteLogarithmZkp::verifyFormatProof(bytes const& c1Point, bytes const& c2Point,
    bytes const& formatProofData, bytes const& c1BasePointData, bytes const& c2BasePointData,
    bytes const& blindingBasePointData)
{
    // points are decoded (and short inputs throw InvalidInputInput) BEFORE the proof-length
    // check, matching the base wedpr evaluation order
    RistrettoPointBytes c1, c2, c1BasePoint, c2BasePoint, blindingBasePoint;
    if (!decodePoint(c1Point, &c1) || !decodePoint(c2Point, &c2) ||
        !decodePoint(c1BasePointData, &c1BasePoint) ||
        !decodePoint(c2BasePointData, &c2BasePoint) ||
        !decodePoint(blindingBasePointData, &blindingBasePoint))
    {
        return false;
    }
    constexpr size_t proofLen = 2 * RISTRETTO_POINT_LEN + 2 * RISTRETTO_SCALAR_LEN;
    if (formatProofData.size() < proofLen)
    {
        return false;
    }
    const auto* proof = formatProofData.data();
    if (!ristrettoIsValidPoint(proof) || !ristrettoIsValidPoint(proof + 32))
    {
        return false;
    }
    auto m1 = ristrettoScalarFromBits(proof + 64);
    auto m2 = ristrettoScalarFromBits(proof + 96);

    bytes hashInput;
    appendPoint(hashInput, proof);
    appendPoint(hashInput, proof + 32);
    appendPoint(hashInput, c1Point.data());
    appendPoint(hashInput, c2Point.data());
    appendPoint(hashInput, c1BasePointData.data());
    appendPoint(hashInput, c2BasePointData.data());
    appendPoint(hashInput, blindingBasePointData.data());
    auto check = hashToScalar(hashInput);

    auto t1 = ristrettoMultiscalarMul({{m1, c1BasePoint}, {m2, blindingBasePoint}, {check, c1}});
    auto t2 = ristrettoMultiscalarMul({{m2, c2BasePoint}, {check, c2}});
    return ristrettoEqual(t1, proof) && ristrettoEqual(t2, proof + 32);
}

namespace
{
struct ArithmeticProof
{
    const std::uint8_t* t1;
    const std::uint8_t* t2;
    const std::uint8_t* t3;
    RistrettoScalarBytes m1;
    RistrettoScalarBytes m2;
    RistrettoScalarBytes m3;
    RistrettoScalarBytes m4;
    RistrettoScalarBytes m5;
};

bool decodeArithmeticProof(bytes const& _proofData, ArithmeticProof* _proof)
{
    constexpr size_t proofLen = 3 * RISTRETTO_POINT_LEN + 5 * RISTRETTO_SCALAR_LEN;
    if (_proofData.size() < proofLen)
    {
        return false;
    }
    const auto* proof = _proofData.data();
    if (!ristrettoIsValidPoint(proof) || !ristrettoIsValidPoint(proof + 32) ||
        !ristrettoIsValidPoint(proof + 64))
    {
        return false;
    }
    _proof->t1 = proof;
    _proof->t2 = proof + 32;
    _proof->t3 = proof + 64;
    _proof->m1 = ristrettoScalarFromBits(proof + 96);
    _proof->m2 = ristrettoScalarFromBits(proof + 128);
    _proof->m3 = ristrettoScalarFromBits(proof + 160);
    _proof->m4 = ristrettoScalarFromBits(proof + 192);
    _proof->m5 = ristrettoScalarFromBits(proof + 224);
    return true;
}
}  // namespace

// wedpr_verify_sum_relationship
bool DiscreteLogarithmZkp::verifySumProof(bytes const& c1Point, bytes const& c2Point,
    bytes const& c3Point, bytes const& arithmeticProofData, bytes const& valueBasePointData,
    bytes const& blindingBasePointData)
{
    // points are decoded (and short inputs throw InvalidInputInput) BEFORE the proof-length
    // check inside decodeArithmeticProof, matching the base wedpr evaluation order
    RistrettoPointBytes c1, c2, c3, valueBasePoint, blindingBasePoint;
    if (!decodePoint(c1Point, &c1) || !decodePoint(c2Point, &c2) || !decodePoint(c3Point, &c3) ||
        !decodePoint(valueBasePointData, &valueBasePoint) ||
        !decodePoint(blindingBasePointData, &blindingBasePoint))
    {
        return false;
    }
    ArithmeticProof proof;
    if (!decodeArithmeticProof(arithmeticProofData, &proof))
    {
        return false;
    }

    // the blinding basepoint is not part of the challenge hash input
    bytes hashInput;
    appendPoint(hashInput, proof.t1);
    appendPoint(hashInput, proof.t2);
    appendPoint(hashInput, proof.t3);
    appendPoint(hashInput, c1Point.data());
    appendPoint(hashInput, c2Point.data());
    appendPoint(hashInput, c3Point.data());
    appendPoint(hashInput, valueBasePointData.data());
    auto check = hashToScalar(hashInput);

    auto t1 = ristrettoMultiscalarMul(
        {{proof.m1, valueBasePoint}, {proof.m2, blindingBasePoint}, {check, c1}});
    auto t2 = ristrettoMultiscalarMul(
        {{proof.m3, valueBasePoint}, {proof.m4, blindingBasePoint}, {check, c2}});
    auto m1PlusM3 = ristrettoScalarAdd(proof.m1, proof.m3);
    auto t3 = ristrettoMultiscalarMul(
        {{m1PlusM3, valueBasePoint}, {proof.m5, blindingBasePoint}, {check, c3}});
    return ristrettoEqual(t1, proof.t1) && ristrettoEqual(t2, proof.t2) &&
           ristrettoEqual(t3, proof.t3);
}

// wedpr_verify_product_relationship
bool DiscreteLogarithmZkp::verifyProductProof(bytes const& c1Point, bytes const& c2Point,
    bytes const& c3Point, bytes const& arithmeticProofData, bytes const& valueBasePointData,
    bytes const& blindingBasePointData)
{
    // points are decoded (and short inputs throw InvalidInputInput) BEFORE the proof-length
    // check inside decodeArithmeticProof, matching the base wedpr evaluation order
    RistrettoPointBytes c1, c2, c3, valueBasePoint, blindingBasePoint;
    if (!decodePoint(c1Point, &c1) || !decodePoint(c2Point, &c2) || !decodePoint(c3Point, &c3) ||
        !decodePoint(valueBasePointData, &valueBasePoint) ||
        !decodePoint(blindingBasePointData, &blindingBasePoint))
    {
        return false;
    }
    ArithmeticProof proof;
    if (!decodeArithmeticProof(arithmeticProofData, &proof))
    {
        return false;
    }

    // the blinding basepoint is not part of the challenge hash input
    bytes hashInput;
    appendPoint(hashInput, proof.t1);
    appendPoint(hashInput, proof.t2);
    appendPoint(hashInput, proof.t3);
    appendPoint(hashInput, c1Point.data());
    appendPoint(hashInput, c2Point.data());
    appendPoint(hashInput, c3Point.data());
    appendPoint(hashInput, valueBasePointData.data());
    auto check = hashToScalar(hashInput);

    auto t1 = ristrettoMultiscalarMul(
        {{proof.m1, valueBasePoint}, {proof.m2, blindingBasePoint}, {check, c1}});
    auto t2 = ristrettoMultiscalarMul(
        {{proof.m3, valueBasePoint}, {proof.m4, blindingBasePoint}, {check, c2}});
    auto m1MulM3 = ristrettoScalarMul(proof.m1, proof.m3);
    auto checkSquared = ristrettoScalarMul(check, check);
    auto checkMulM3 = ristrettoScalarMul(check, proof.m3);
    auto checkMulM1 = ristrettoScalarMul(check, proof.m1);
    auto t3 = ristrettoMultiscalarMul({{m1MulM3, valueBasePoint}, {proof.m5, blindingBasePoint},
        {checkSquared, c3}, {checkMulM3, c1}, {checkMulM1, c2}});
    return ristrettoEqual(t1, proof.t1) && ristrettoEqual(t2, proof.t2) &&
           ristrettoEqual(t3, proof.t3);
}

// wedpr_verify_equality_relationship_proof
bool DiscreteLogarithmZkp::verifyEqualityProof(bytes const& c1Point, bytes const c2Point,
    bytes const& equalityProofData, bytes const& basePoint1Data, bytes const& basePoint2Data)
{
    // points are decoded (and short inputs throw InvalidInputInput) BEFORE the proof-length
    // check, matching the base wedpr evaluation order
    RistrettoPointBytes c1, c2, basePoint1, basePoint2;
    if (!decodePoint(c1Point, &c1) || !decodePoint(c2Point, &c2) ||
        !decodePoint(basePoint1Data, &basePoint1) || !decodePoint(basePoint2Data, &basePoint2))
    {
        return false;
    }
    constexpr size_t proofLen = RISTRETTO_SCALAR_LEN + 2 * RISTRETTO_POINT_LEN;
    if (equalityProofData.size() < proofLen)
    {
        return false;
    }
    const auto* proof = equalityProofData.data();
    auto m1 = ristrettoScalarFromBits(proof);
    if (!ristrettoIsValidPoint(proof + 32) || !ristrettoIsValidPoint(proof + 64))
    {
        return false;
    }

    bytes hashInput;
    appendPoint(hashInput, proof + 32);
    appendPoint(hashInput, proof + 64);
    appendPoint(hashInput, c1Point.data());
    appendPoint(hashInput, c2Point.data());
    appendPoint(hashInput, basePoint1Data.data());
    appendPoint(hashInput, basePoint2Data.data());
    auto check = hashToScalar(hashInput);

    auto t1 = ristrettoMultiscalarMul({{m1, basePoint1}, {check, c1}});
    auto t2 = ristrettoMultiscalarMul({{m1, basePoint2}, {check, c2}});
    return ristrettoEqual(t1, proof + 32) && ristrettoEqual(t2, proof + 64);
}

// wedpr_aggregate_ristretto_point
bytes DiscreteLogarithmZkp::aggregateRistrettoPoint(bytes const& pointSum, bytes const& pointShare)
{
    // with empty pointShare to aggregate
    if (pointShare.size() == 0)
    {
        return pointSum;
    }
    RistrettoPointBytes sum, share;
    if (!decodePoint(pointSum, &sum) || !decodePoint(pointShare, &share))
    {
        BOOST_THROW_EXCEPTION(ZkpExcetpion() << errinfo_comment("aggregateRistrettoPoint failed"));
    }
    auto aggregated = ristrettoAdd(sum, share);
    return bytes(aggregated.begin(), aggregated.end());
}
