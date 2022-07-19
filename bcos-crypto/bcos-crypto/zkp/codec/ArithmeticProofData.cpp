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
 * @brief codec for ArithmeticProof data
 * @file ArithmeticProofData.h
 * @date 2022.07.18
 * @author yujiechen
 */
#include "ArithmeticProofData.h"
#include "bcos-crypto/zkp/Exceptions.h"

using namespace bcos;
using namespace bcos::crypto;

void ArithmeticProofData::checkArithmeticProof() const
{
    if (m_t1.size() < m_pointLen || m_t2.size() < m_pointLen || m_t3.size() < m_pointLen ||
        m_m1.size() < m_scalarLen || m_m2.size() < m_scalarLen || m_m3.size() < m_scalarLen ||
        m_m4.size() < m_scalarLen || m_m5.size() < m_scalarLen)
    {
        BOOST_THROW_EXCEPTION(InvalidArithmeticProof() << errinfo_comment(
                                  "InvalidArithmeticProof: the scalar data size must be at least " +
                                  std::to_string(m_scalarLen)));
    }
}

bytesPointer ArithmeticProofData::encode() const
{
    checkArithmeticProof();
    // encode the balance proof
    auto encodedData = std::make_shared<bytes>();
    // m_t1
    encodedData->insert(encodedData->end(), m_t1.data(), m_t1.data() + m_pointLen);
    // m_t2
    encodedData->insert(encodedData->end(), m_t2.data(), m_t2.data() + m_pointLen);
    // m_t3
    encodedData->insert(encodedData->end(), m_t3.data(), m_t3.data() + m_pointLen);
    // m_m1
    encodedData->insert(encodedData->end(), m_m1.data(), m_m1.data() + m_scalarLen);
    // m_m2
    encodedData->insert(encodedData->end(), m_m2.data(), m_m2.data() + m_scalarLen);
    // m_m3
    encodedData->insert(encodedData->end(), m_m3.data(), m_m3.data() + m_scalarLen);
    // m_m4
    encodedData->insert(encodedData->end(), m_m4.data(), m_m4.data() + m_scalarLen);
    // m_m5
    encodedData->insert(encodedData->end(), m_m5.data(), m_m5.data() + m_scalarLen);
    return encodedData;
}

void ArithmeticProofData::decode(bytesConstRef _proofData)
{
    if (_proofData.size() < m_proofSize)
    {
        BOOST_THROW_EXCEPTION(
            InvalidArithmeticProof() << errinfo_comment(
                "InvalidArithmeticProof: the arithmetic proof data size must be at least " +
                std::to_string(m_proofSize)));
    }
    // decode m_t1
    auto pos = _proofData.data();
    m_t1 = bytes(pos, pos + m_pointLen);
    pos += m_pointLen;
    // decode m_t2
    m_t2 = bytes(pos, pos + m_pointLen);
    pos += m_pointLen;
    // decode m_t3
    m_t3 = bytes(pos, pos + m_pointLen);
    pos += m_pointLen;
    // decode m_m1
    m_m1 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m_m2
    m_m2 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m_m3
    m_m3 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m_m4
    m_m4 = bytes(pos, pos + m_scalarLen);
    pos += m_scalarLen;
    // decode m_m5
    m_m5 = bytes(pos, pos + m_scalarLen);
}

CArithmeticProof ArithmeticProofData::toArithmeticProof() const
{
    return CArithmeticProof{(char*)m_t1.data(), (char*)m_t2.data(), (char*)m_t3.data(),
        (char*)m_m1.data(), (char*)m_m2.data(), (char*)m_m3.data(), (char*)m_m4.data(),
        (char*)m_m5.data(), m_scalarLen, m_pointLen};
}
