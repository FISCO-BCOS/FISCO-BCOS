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
 * @brief tars implementation for Transaction
 * @file TransactionImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#include "../impl/TarsHashable.h"

#include "bcos-concepts/Hash.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <memory>

namespace bcostars::protocol
{

class TransactionImpl : public bcos::protocol::Transaction
{
public:
    explicit TransactionImpl(std::function<bcostars::Transaction*()> inner)
      : m_inner(std::move(inner))
    {}
    ~TransactionImpl() override = default;

    friend class TransactionFactoryImpl;

    bool operator==(const Transaction& rhs) const { return this->hash() == rhs.hash(); }

    void decode(bcos::bytesConstRef _txData) override;
    void encode(bcos::bytes& txData) const override;

    bcos::crypto::HashType hash() const override;
    
    template <bcos::crypto::hasher::Hasher Hasher>
    void calculateHash()
    {
        bcos::concepts::hash::calculate<Hasher>(*m_inner(), m_inner()->dataHash);
    }

    int32_t version() const override { return m_inner()->data.version; }
    std::string_view chainId() const override { return m_inner()->data.chainID; }
    std::string_view groupId() const override { return m_inner()->data.groupID; }
    int64_t blockLimit() const override { return m_inner()->data.blockLimit; }
    bcos::u256 nonce() const override;
    std::string_view to() const override { return m_inner()->data.to; }
    std::string_view abi() const override { return m_inner()->data.abi; }
    bcos::bytesConstRef input() const override;
    int64_t importTime() const override { return m_inner()->importTime; }
    void setImportTime(int64_t _importTime) override { m_inner()->importTime = _importTime; }
    bcos::bytesConstRef signatureData() const override
    {
        return {reinterpret_cast<const bcos::byte*>(m_inner()->signature.data()),
            m_inner()->signature.size()};
    }
    std::string_view sender() const override
    {
        return {m_inner()->sender.data(), m_inner()->sender.size()};
    }
    void forceSender(bcos::bytes _sender) const override
    {
        m_inner()->sender.assign(_sender.begin(), _sender.end());
    }

    void setSignatureData(bcos::bytes& signature)
    {
        m_inner()->signature.assign(signature.begin(), signature.end());
    }

    int32_t attribute() const override { return m_inner()->attribute; }
    void setAttribute(int32_t attribute) override { m_inner()->attribute = attribute; }

    std::string_view extraData() const override { return m_inner()->extraData; }
    void setExtraData(std::string const& _extraData) override { m_inner()->extraData = _extraData; }

    const bcostars::Transaction& inner() const { return *m_inner(); }
    bcostars::Transaction& mutableInner() { return *m_inner(); }
    void setInner(bcostars::Transaction inner) { *m_inner() = std::move(inner); }

private:
    std::function<bcostars::Transaction*()> m_inner;
    mutable bcos::u256 m_nonce;
};
}  // namespace bcostars::protocol