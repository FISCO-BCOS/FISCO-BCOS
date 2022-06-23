/*
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
 * @brief implement for TransactionMetaData
 * @file TransactionMetaDataImpl.h
 * @author: yujiechen
 * @date: 2021-09-07
 */
#pragma once

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "bcos-tars-protocol/tars/TransactionMetaData.h"
#include <bcos-framework//protocol/TransactionMetaData.h>

namespace bcostars
{
namespace protocol
{
class TransactionMetaDataImpl : public bcos::protocol::TransactionMetaData
{
public:
    using Ptr = std::shared_ptr<TransactionMetaDataImpl>;
    using ConstPtr = std::shared_ptr<const TransactionMetaDataImpl>;

    TransactionMetaDataImpl()
      : m_inner([inner = bcostars::TransactionMetaData()]() mutable { return &inner; })
    {}

    TransactionMetaDataImpl(bcos::crypto::HashType hash, std::string to) : TransactionMetaDataImpl()
    {
        setHash(std::move(hash));
        setTo(std::move(to));
    }

    explicit TransactionMetaDataImpl(std::function<bcostars::TransactionMetaData*()> inner)
      : m_inner(std::move(inner))
    {}

    ~TransactionMetaDataImpl() override {}

    bcos::crypto::HashType hash() const override
    {
        auto const& hashBytes = m_inner()->hash;
        if (hashBytes.size() == bcos::crypto::HashType::SIZE)
        {
            bcos::crypto::HashType hash(reinterpret_cast<const bcos::byte*>(hashBytes.data()),
                bcos::crypto::HashType::SIZE);
            return hash;
        }
        return bcos::crypto::HashType();
    }
    void setHash(bcos::crypto::HashType _hash) override
    {
        m_inner()->hash.assign(_hash.begin(), _hash.end());
    }

    std::string_view to() const override { return m_inner()->to; }
    void setTo(std::string _to) override { m_inner()->to = std::move(_to); }

    uint32_t attribute() const override { return m_inner()->attribute; }
    void setAttribute(uint32_t attribute) override { m_inner()->attribute = attribute; }

    std::string_view source() const override { return m_inner()->source; }
    void setSource(std::string source) override { m_inner()->source = std::move(source); }

    const bcostars::TransactionMetaData& inner() const { return *m_inner(); }
    bcostars::TransactionMetaData& mutableInner() { return *m_inner(); }
    bcostars::TransactionMetaData takeInner() { return std::move(*m_inner()); }
    void setInner(bcostars::TransactionMetaData inner) { *m_inner() = std::move(inner); }

private:
    std::function<bcostars::TransactionMetaData*()> m_inner;
};
}  // namespace protocol
}  // namespace bcostars
