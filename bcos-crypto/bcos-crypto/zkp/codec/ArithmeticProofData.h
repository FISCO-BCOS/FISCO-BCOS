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

#pragma once
#include <bcos-utilities/Common.h>
#include <wedpr-crypto/WedprUtilities.h>

namespace bcos
{
namespace crypto
{
class ArithmeticProofData
{
public:
    ArithmeticProofData(size_t _scalarLen, size_t _pointLen)
      : m_scalarLen(_scalarLen), m_pointLen(_pointLen)
    {
        m_proofSize = 3 * m_pointLen + 5 * m_scalarLen;
    }

    virtual ~ArithmeticProofData() {}

    virtual bytesPointer encode() const;
    virtual void decode(bytesConstRef _proofData);
    virtual CArithmeticProof toArithmeticProof() const;

    bytesConstRef t1() const { return bytesConstRef((byte const*)m_t1.data(), m_t1.size()); }
    bytesConstRef t2() const { return bytesConstRef((byte const*)m_t2.data(), m_t2.size()); }
    bytesConstRef t3() const { return bytesConstRef((byte const*)m_t3.data(), m_t3.size()); }

    bytesConstRef m1() const { return bytesConstRef((byte const*)m_m1.data(), m_m1.size()); }
    bytesConstRef m2() const { return bytesConstRef((byte const*)m_m2.data(), m_m2.size()); }
    bytesConstRef m3() const { return bytesConstRef((byte const*)m_m3.data(), m_m3.size()); }
    bytesConstRef m4() const { return bytesConstRef((byte const*)m_m4.data(), m_m4.size()); }
    bytesConstRef m5() const { return bytesConstRef((byte const*)m_m5.data(), m_m5.size()); }

protected:
    void checkArithmeticProof() const;

private:
    size_t m_scalarLen;
    size_t m_pointLen;
    size_t m_proofSize;

    bytes m_t1;
    bytes m_t2;
    bytes m_t3;

    bytes m_m1;
    bytes m_m2;
    bytes m_m3;
    bytes m_m4;
    bytes m_m5;
};
}  // namespace crypto
}  // namespace bcos