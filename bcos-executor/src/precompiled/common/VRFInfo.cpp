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
 * @file VRFInfo.cpp
 * @author: kyonGuo
 * @date 2023/2/13
 */

#include "VRFInfo.h"
#include "bcos-framework/sealer/VrfCurveType.h"
#include <bcos-crypto/vrf/Curve25519Vrf.h>
#include <bcos-crypto/vrf/Secp256k1Vrf.h>
#include <cstring>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::precompiled;

VRFInfo::VRFInfo(bcos::bytes _vrfProof, bcos::bytes _vrfPk, bcos::bytes _vrfInput,
    bcos::sealer::VrfCurveType vrfCurveType)
  : m_vrfProof(std::move(_vrfProof)),
    m_vrfPublicKey(std::move(_vrfPk)),
    m_vrfInput(std::move(_vrfInput)),
    m_vrfCurveType(vrfCurveType)
{}

bcos::bytes const& VRFInfo::vrfProof() const
{
    return m_vrfProof;
}

bcos::bytes const& VRFInfo::vrfPublicKey() const
{
    return m_vrfPublicKey;
}

bcos::bytes const& VRFInfo::vrfInput() const
{
    return m_vrfInput;
}

bcos::sealer::VrfCurveType VRFInfo::vrfCurveType() const
{
    return m_vrfCurveType;
}

bool VRFInfo::isValidVRFPublicKey()
{
    auto publicKeyRef = bytesConstRef(m_vrfPublicKey.data(), m_vrfPublicKey.size());
    switch (m_vrfCurveType)
    {
    case sealer::VrfCurveType::CURVE25519:
        return curve25519VrfIsValidPublicKey(publicKeyRef);
    case sealer::VrfCurveType::SECKP256K1:
        return secp256k1VrfIsValidPublicKey(publicKeyRef);
    default:
        return false;
    }
}

bool VRFInfo::verifyProof()
{
    auto publicKeyRef = bytesConstRef(m_vrfPublicKey.data(), m_vrfPublicKey.size());
    auto inputRef = bytesConstRef(m_vrfInput.data(), m_vrfInput.size());
    auto proofRef = bytesConstRef(m_vrfProof.data(), m_vrfProof.size());
    switch (m_vrfCurveType)
    {
    case sealer::VrfCurveType::CURVE25519:
        return curve25519VrfVerify(publicKeyRef, inputRef, proofRef);
    case sealer::VrfCurveType::SECKP256K1:
        return secp256k1VrfVerify(publicKeyRef, inputRef, proofRef);
    default:
        return false;
    }
}

HashType VRFInfo::getHashFromProof()
{
    auto proofRef = bytesConstRef(m_vrfProof.data(), m_vrfProof.size());
    bcos::bytes hash;
    switch (m_vrfCurveType)
    {
    case sealer::VrfCurveType::CURVE25519:
        hash = curve25519VrfProofToHash(proofRef);
        break;
    case sealer::VrfCurveType::SECKP256K1:
        hash = secp256k1VrfProofToHash(proofRef);
        break;
    default:
        break;
    }
    if (hash.size() != HashType::SIZE)
    {
        return HashType{};
    }
    HashType vrfHash;
    std::memcpy(vrfHash.data(), hash.data(), HashType::SIZE);
    return vrfHash;
}
