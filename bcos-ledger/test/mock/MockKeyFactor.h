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
 * @file MockKeyFactor.h
 * @author: kyonRay
 * @date 2021-05-14
 */

#pragma once

#include <bcos-framework/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
class MockKey : public bcos::crypto::KeyInterface
{
public:
    using Ptr = std::shared_ptr<MockKey>;
    explicit MockKey(size_t _keySize) : m_keyData(std::make_shared<bytes>(_keySize)) {}
    explicit MockKey(bytesConstRef _data) : m_keyData(std::make_shared<bytes>()) { decode(_data); }
    explicit MockKey(bytes const& _data) : MockKey(ref(_data)) {}
    explicit MockKey(size_t _keySize, std::shared_ptr<const bytes> _data)
      : m_keyData(std::make_shared<bytes>())
    {
        if (_data->size() < _keySize)
        {
            BOOST_THROW_EXCEPTION(InvalidKey() << errinfo_comment(
                                      "invalidKey, the key size: " + std::to_string(_data->size()) +
                                      ", expected size:" + std::to_string(_keySize)));
        }
        *m_keyData = *_data;
    }

    bool operator==(MockKey const& _comparedKey) { return (*m_keyData == _comparedKey.data()); }
    bool operator!=(MockKey const& _comparedKey) { return !operator==(_comparedKey); }

    ~MockKey() override {}

    const bytes& data() const override { return *m_keyData; }
    size_t size() const override { return m_keyData->size(); }
    char* mutableData() override { return (char*)m_keyData->data(); }
    const char* constData() const override { return (const char*)m_keyData->data(); }
    std::shared_ptr<bytes> encode() const override { return m_keyData; }
    void decode(bytesConstRef _data) override { *m_keyData = _data.toBytes(); }

    void decode(bytes&& _data) override { *m_keyData = std::move(_data); }

    std::string shortHex() override
    {
        auto startIt = m_keyData->begin();
        auto endIt = m_keyData->end();
        if (m_keyData->size() > 4)
        {
            endIt = startIt + 4 * sizeof(byte);
        }
        return *toHexString(startIt, endIt) + "...";
    }

    std::string hex() override { return *toHexString(*m_keyData); }

private:
    std::shared_ptr<bytes> m_keyData;
};

class MockKeyFactory : public bcos::crypto::KeyFactory
{
public:
    using Ptr = std::shared_ptr<MockKeyFactory>;
    MockKeyFactory() = default;
    ~MockKeyFactory() override {}

    KeyInterface::Ptr createKey(bytesConstRef _keyData) override
    {
        return std::make_shared<MockKey>(_keyData);
    }

    KeyInterface::Ptr createKey(bytes const& _keyData) override
    {
        return std::make_shared<MockKey>(_keyData);
    }
};

}  // namespace test
}  // namespace bcos
