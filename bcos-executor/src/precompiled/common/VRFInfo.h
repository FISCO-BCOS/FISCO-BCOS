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
 * @file VRFInfo.h
 * @author: kyonGuo
 * @date 2023/2/13
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-utilities/Common.h"
#include <cstdint>
namespace bcos::precompiled
{
class VRFInfo
{
public:
    VRFInfo(const VRFInfo&) = default;
    VRFInfo(VRFInfo&&) = default;
    VRFInfo& operator=(const VRFInfo&) = default;
    VRFInfo& operator=(VRFInfo&&) = default;
    VRFInfo(bcos::bytes _vrfProof, bcos::bytes _vrfPk, bcos::bytes _vrfInput, uint8_t vrfCurveType = 0)
      : m_vrfProof(std::move(_vrfProof)),
        m_vrfPublicKey(std::move(_vrfPk)),
        m_vrfInput(std::move(_vrfInput)),
        m_vrfCurveType(vrfCurveType)
    {}

    virtual ~VRFInfo() = default;
    virtual bool verifyProof();
    virtual bcos::crypto::HashType getHashFromProof();
    virtual bool isValidVRFPublicKey();

    bcos::bytes const& vrfProof() const { return m_vrfProof; }
    bcos::bytes const& vrfPublicKey() const { return m_vrfPublicKey; }
    bcos::bytes const& vrfInput() const { return m_vrfInput; }
    uint8_t vrfCurveType() const { return m_vrfCurveType; }

private:
    bcos::bytes m_vrfProof;
    bcos::bytes m_vrfPublicKey;
    bcos::bytes m_vrfInput;
    uint8_t m_vrfCurveType = 0;
};
}  // namespace bcos::precompiled