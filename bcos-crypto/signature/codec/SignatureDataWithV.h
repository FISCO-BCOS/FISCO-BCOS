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
 * @brief codec for signature data with v (for secp256k1)
 * @file SignatureDataWithV.h
 * @date 2021.03.10
 * @author yujiechen
 */

#pragma once
#include "SignatureData.h"
#include <bcos-framework/interfaces/crypto/Signature.h>

namespace bcos
{
namespace crypto
{
class SignatureDataWithV : public SignatureData
{
public:
    using Ptr = std::shared_ptr<SignatureDataWithV>;
    explicit SignatureDataWithV(bytesConstRef _data) { decode(_data); }
    SignatureDataWithV(h256 const& _r, h256 const& _s, byte const& _v)
      : SignatureData(_r, _s), m_v(_v)
    {}

    ~SignatureDataWithV() override {}

    byte const& v() { return m_v; }

    bytesPointer encode() const override
    {
        auto encodedData = std::make_shared<bytes>();
        encodeCommonFields(encodedData);
        encodedData->emplace_back(m_v);
        return encodedData;
    }
    void decode(bytesConstRef _signatureData) override
    {
        if (_signatureData.size() < m_signatureLen + 1)
        {
            BOOST_THROW_EXCEPTION(
                InvalidSignatureData() << errinfo_comment(
                    "InvalidSignatureData: the signature data size must be at least " +
                    std::to_string(m_signatureLen + 1)));
        }
        decodeCommonFields(_signatureData);
        m_v = (byte)(_signatureData[m_signatureLen]);
    }

private:
    byte m_v;
};
}  // namespace crypto
}  // namespace bcos