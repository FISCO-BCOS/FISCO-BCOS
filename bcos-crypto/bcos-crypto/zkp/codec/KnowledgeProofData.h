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
 * @brief codec for KnowledgeProof data
 * @file KnowledgeProofData.h
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
class KnowledgeProofData
{
public:
    KnowledgeProofData(size_t _scalarLen, size_t _pointLen)
      : m_scalarLen(_scalarLen), m_pointLen(_pointLen)
    {
        m_proofSize = m_pointLen + 2 * m_scalarLen;
    }

    virtual ~KnowledgeProofData() {}

    virtual bytesPointer encode() const;
    virtual void decode(bytesConstRef _proofData);
    virtual CKnowledgeProof toKnowledgeProof() const;

    bytesConstRef t1() const { return bytesConstRef((byte const*)m_t1.data(), m_t1.size()); }
    bytesConstRef m1() const { return bytesConstRef((byte const*)m_m1.data(), m_m1.size()); }
    bytesConstRef m2() const { return bytesConstRef((byte const*)m_m2.data(), m_m2.size()); }

protected:
    void checkKnowledgeProof() const;

private:
    size_t m_scalarLen;
    size_t m_pointLen;
    size_t m_proofSize;

    bytes m_t1;
    bytes m_m1;
    bytes m_m2;
};
}  // namespace crypto
}  // namespace bcos