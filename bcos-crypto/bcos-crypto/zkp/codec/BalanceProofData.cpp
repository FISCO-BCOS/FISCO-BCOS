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
 * @brief codec for BalanceProof data
 * @file BalanceProofData.h
 * @date 2022.07.18
 * @author yujiechen
 */
#include "BalanceProofData.h"
#include "bcos-crypto/zkp/Exceptions.h"

using namespace bcos;
using namespace bcos::crypto;

void BalanceProofData::checkBalanceProof() const
{
    if (m_check1.size() < m_scalarLen || m_check2.size() < m_scalarLen ||
        m_m1.size() < m_scalarLen || m_m2.size() < m_scalarLen || m_m3.size() < m_scalarLen ||
        m_m4.size() < m_scalarLen || m_m5.size() < m_scalarLen || m_m6.size() < m_scalarLen)
    {
        BOOST_THROW_EXCEPTION(InvalidBalanceProof() << errinfo_comment(
                                  "InvalidBalanceProof: the scalar data size must be at least " +
                                  std::to_string(m_scalarLen)));
    }
}

bytesPointer BalanceProofData::encode() const
{
    checkBalanceProof();
    // encode the balance proof
    auto encodedData = std::make_shared<bytes>();
    // check1
    encodedData->insert(encodedData->end(), m_check1.data(), m_check1.data() + m_scalarLen);
    // check2
    encodedData->insert(encodedData->end(), m_check2.data(), m_check2.data() + m_scalarLen);
    // m1
    encodedData->insert(encodedData->end(), m_m1.data(), m_m1.data() + m_scalarLen);
    // m2
    encodedData->insert(encodedData->end(), m_m2.data(), m_m2.data() + m_scalarLen);
    // m3
    encodedData->insert(encodedData->end(), m_m3.data(), m_m3.data() + m_scalarLen);
    // m4
    encodedData->insert(encodedData->end(), m_m4.data(), m_m4.data() + m_scalarLen);
    // m5
    encodedData->insert(encodedData->end(), m_m5.data(), m_m5.data() + m_scalarLen);
    // m6
    encodedData->insert(encodedData->end(), m_m6.data(), m_m6.data() + m_scalarLen);
    return encodedData;
}

void BalanceProofData::decode(bytesConstRef _proofData)
{
    if (_proofData.size() < m_proofSize)
    {
        BOOST_THROW_EXCEPTION(
            InvalidBalanceProof() << errinfo_comment(
                "InvalidBalanceProof: the balance proof data size must be at least " +
                std::to_string(m_proofSize)));
    }
    // decode check1
    auto pos = _proofData.data();
    m_check1 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode check2
    m_check2 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m1
    m_m1 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m2
    m_m2 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m3
    m_m3 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m4
    m_m4 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m5
    m_m5 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m6
    m_m6 = bytes(pos, pos + m_scalarLen);
}

CBalanceProof BalanceProofData::toBalanceProof() const
{
    return CBalanceProof{(char*)m_check1.data(), (char*)m_check2.data(), (char*)m_m1.data(),
        (char*)m_m2.data(), (char*)m_m3.data(), (char*)m_m4.data(), (char*)m_m5.data(),
        (char*)m_m6.data(), m_scalarLen};
}
