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
 * @brief implementation for public key/private key
 * @file KeyImpl.h
 * @date 2021.04.01
 * @author yujiechen
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-utilities/DataConvertUtility.h>

namespace bcos::crypto
{
class KeyImpl : public KeyInterface
{
public:
    using Ptr = std::shared_ptr<KeyImpl>;
    KeyImpl(const KeyImpl&) = default;
    KeyImpl(KeyImpl&&) noexcept = default;
    KeyImpl& operator=(const KeyImpl&) = default;
    KeyImpl& operator=(KeyImpl&&) noexcept = default;
    explicit KeyImpl(size_t _keySize) : m_keyData(_keySize) {}
    explicit KeyImpl(bytesConstRef data) : m_keyData(data.toBytes()) {}
    explicit KeyImpl(bytes data) : m_keyData(std::move(data)) {}
    KeyImpl(size_t _keySize, bytesConstRef data)
    {
        if (data.size() < _keySize)
        {
            BOOST_THROW_EXCEPTION(InvalidKey() << errinfo_comment(
                                      "invalidKey, the key size: " + std::to_string(data.size()) +
                                      ", expected size:" + std::to_string(_keySize)));
        }
        m_keyData = data.toBytes();
    }
    KeyImpl(size_t _keySize, const std::shared_ptr<const bytes>& data)
      : KeyImpl(_keySize, ref(*data)) {};

    ~KeyImpl() noexcept override = default;

    const bytes& data() const override { return m_keyData; }
    size_t size() const override { return m_keyData.size(); }
    char* mutableData() override
    {
        // the caller is about to overwrite the key material, so any cached hex is stale
        m_hexCache.clear();
        return (char*)m_keyData.data();
    }
    const char* constData() const override { return (const char*)m_keyData.data(); }
    bytes encode() const override { return m_keyData; }
    void decode(bytesConstRef _data) override
    {
        m_keyData = _data.toBytes();
        m_hexCache.clear();
    }
    void decode(bytes _data) override
    {
        m_keyData = std::move(_data);
        m_hexCache.clear();
    }

    std::string shortHex() override
    {
        auto startIt = m_keyData.begin();
        auto endIt = m_keyData.end();
        if (m_keyData.size() > 4)
        {
            endIt = startIt + 4 * sizeof(byte);
        }
        return toHex(std::span(startIt, endIt)) + "...";
    }

    std::string hex() override
    {
        // Computed once per key and reused by every later call. This must be a MEMBER, not a
        // function-local static: a static would be shared by every KeyImpl instance and would hand
        // the first key's hex to all of them.
        if (m_hexCache.empty())
        {
            m_hexCache = toHex(m_keyData);
        }
        return m_hexCache;
    }

private:
    bytes m_keyData;
    /// Lazily filled by hex(); invalidated by decode() and mutableData().
    std::string m_hexCache;
};
}  // namespace bcos::crypto
