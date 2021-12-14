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
 * @brief codec for signature data
 * @file SignatureData.h
 * @date 2021.03.10
 * @author yujiechen
 */

#pragma once
#include "../Exceptions.h"
#include <bcos-framework/libutilities/FixedBytes.h>
namespace bcos
{
namespace crypto
{
class SignatureData
{
public:
    using Ptr = std::shared_ptr<SignatureData>;
    SignatureData() = default;
    SignatureData(h256 const& _r, h256 const& _s) : m_r(_r), m_s(_s) {}
    virtual ~SignatureData() {}
    virtual bytesPointer encode() const = 0;
    virtual void decode(bytesConstRef _signatureData) = 0;

    h256 const& r() const { return m_r; }
    h256 const& s() const { return m_s; }

protected:
    virtual void decodeCommonFields(bytesConstRef _signatureData)
    {
        if (_signatureData.size() < m_signatureLen)
        {
            BOOST_THROW_EXCEPTION(
                InvalidSignatureData() << errinfo_comment(
                    "InvalidSignatureData: the signature data size must be at least " +
                    std::to_string(m_signatureLen)));
        }
        m_r = h256(_signatureData.data(), h256::ConstructorType::FromPointer);
        m_s = h256(_signatureData.data() + 32, h256::ConstructorType::FromPointer);
    }
    virtual void encodeCommonFields(bytesPointer _signatureData) const
    {
        _signatureData->resize(64);
        memcpy(_signatureData->data(), m_r.data(), 32);
        memcpy(_signatureData->data() + 32, m_s.data(), 32);
    }

protected:
    size_t m_signatureLen = 64;

private:
    h256 m_r;
    h256 m_s;
};
}  // namespace crypto
}  // namespace bcos
