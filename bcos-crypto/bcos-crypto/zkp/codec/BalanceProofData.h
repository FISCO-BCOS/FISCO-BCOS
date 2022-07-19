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

#pragma once
#include <bcos-utilities/Common.h>
#include <wedpr-crypto/WedprUtilities.h>

namespace bcos
{
namespace crypto
{
class BalanceProofData
{
public:
    BalanceProofData(size_t _scalarLen) : m_scalarLen(_scalarLen) { m_proofSize = 8 * m_scalarLen; }

    virtual ~BalanceProofData() {}

    virtual bytesPointer encode() const;
    virtual void decode(bytesConstRef _proofData);
    virtual CBalanceProof toBalanceProof() const;

    bytesConstRef check1() const
    {
        return bytesConstRef((byte const*)m_check1.data(), m_check1.size());
    }
    bytesConstRef check2() const
    {
        return bytesConstRef((byte const*)m_check2.data(), m_check2.size());
    }
    bytesConstRef m1() const { return bytesConstRef((byte const*)m_m1.data(), m_m1.size()); }
    bytesConstRef m2() const { return bytesConstRef((byte const*)m_m2.data(), m_m2.size()); }
    bytesConstRef m3() const { return bytesConstRef((byte const*)m_m3.data(), m_m3.size()); }
    bytesConstRef m4() const { return bytesConstRef((byte const*)m_m4.data(), m_m4.size()); }
    bytesConstRef m5() const { return bytesConstRef((byte const*)m_m5.data(), m_m5.size()); }
    bytesConstRef m6() const { return bytesConstRef((byte const*)m_m6.data(), m_m6.size()); }

protected:
    void checkBalanceProof() const;

private:
    size_t m_scalarLen;
    size_t m_proofSize;

    bytes m_check1;
    bytes m_check2;
    bytes m_m1;
    bytes m_m2;
    bytes m_m3;
    bytes m_m4;
    bytes m_m5;
    bytes m_m6;
};
}  // namespace crypto
}  // namespace bcos