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
 * @brief codec for signature data with pub
 * @file SignatureDataWithPub.h
 * @date 2021.03.10
 * @author yujiechen
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/Signature.h>
#include <bcos-crypto/signature/codec/SignatureData.h>
namespace bcos
{
namespace crypto
{
class SignatureDataWithPub : public SignatureData
{
public:
    using Ptr = std::shared_ptr<SignatureDataWithPub>;
    explicit SignatureDataWithPub(bytesConstRef _data) : m_pub(std::make_shared<bytes>())
    {
        decode(_data);
    }
    SignatureDataWithPub(h256 const& _r, h256 const& _s, bytesConstRef _pub)
      : SignatureData(_r, _s), m_pub(std::make_shared<bytes>(_pub.begin(), _pub.end()))
    {}

    SignatureDataWithPub(h256 const& _r, h256 const& _s, bytesPointer _pub)
      : SignatureData(_r, _s), m_pub(_pub)
    {}

    ~SignatureDataWithPub() override {}

    bytesPointer pub() const { return m_pub; }
    bytesPointer encode() const override
    {
        auto encodedData = std::make_shared<bytes>();
        encodeCommonFields(encodedData);
        encodedData->insert(encodedData->end(), m_pub->begin(), m_pub->end());
        return encodedData;
    }

    void decode(bytesConstRef _signatureData) override
    {
        m_pub->clear();
        decodeCommonFields(_signatureData);
        if (_signatureData.size() > m_signatureLen)
        {
            m_pub->insert(
                m_pub->end(), _signatureData.data() + m_signatureLen, _signatureData.end());
        }
    }

private:
    bytesPointer m_pub;
};
}  // namespace crypto
}  // namespace bcos
