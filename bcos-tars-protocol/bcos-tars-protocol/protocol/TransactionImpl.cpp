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
 * @file TransactionImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */

#include "TransactionImpl.h"
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/endian/conversion.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <cstring>
#include <range/v3/view/any_view.hpp>

DERIVE_BCOS_EXCEPTION(EmptyTransactionHash);

#define WEB3_ACCESS_LIST_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("WEB3_ACCESS_LIST")

bcostars::protocol::TransactionImpl::TransactionImpl(std::function<bcostars::Transaction*()> inner)
  : m_inner(std::move(inner))
{}
bcostars::protocol::TransactionImpl::TransactionImpl()
  : m_inner([m_transaction = bcostars::Transaction()]() mutable {
        return std::addressof(m_transaction);
    })
{}

bool bcostars::protocol::TransactionImpl::operator==(const Transaction& rhs) const
{
    return this->hash() == rhs.hash();
}

void bcostars::protocol::TransactionImpl::decode(bcos::bytesConstRef _txData)
{
    bcos::concepts::serialize::decode(_txData, *m_inner());
}

void bcostars::protocol::TransactionImpl::encode(bcos::bytes& txData) const
{
    bcos::concepts::serialize::encode(*m_inner(), txData);
}

bcos::crypto::HashType bcostars::protocol::TransactionImpl::hash() const
{
    if (m_inner()->dataHash.empty() && m_inner()->extraTransactionHash.empty())
    {
        throwTrace(EmptyTransactionHash{});
    }

    if (type() == static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        bcos::crypto::HashType hashResult((bcos::byte*)m_inner()->extraTransactionHash.data(),
            m_inner()->extraTransactionHash.size());
        return hashResult;
    }
    bcos::crypto::HashType hashResult(
        (bcos::byte*)m_inner()->dataHash.data(), m_inner()->dataHash.size());

    return hashResult;
}

void bcostars::protocol::TransactionImpl::calculateHash(const bcos::crypto::Hash& hashImpl)
{
    bcos::concepts::hash::calculate(*m_inner(), hashImpl.hasher(), m_inner()->dataHash);
}

std::string_view bcostars::protocol::TransactionImpl::nonce() const
{
    return m_inner()->data.nonce;
}

bcos::bytesConstRef bcostars::protocol::TransactionImpl::input() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.input.data()),
        m_inner()->data.input.size()};
}
int32_t bcostars::protocol::TransactionImpl::version() const
{
    return m_inner()->data.version;
}
std::string_view bcostars::protocol::TransactionImpl::chainId() const
{
    return m_inner()->data.chainID;
}
std::string_view bcostars::protocol::TransactionImpl::groupId() const
{
    return m_inner()->data.groupID;
}
int64_t bcostars::protocol::TransactionImpl::blockLimit() const
{
    return m_inner()->data.blockLimit;
}
void bcostars::protocol::TransactionImpl::setNonce(std::string nonce)
{
    m_inner()->data.nonce = std::move(nonce);
}
std::string_view bcostars::protocol::TransactionImpl::to() const
{
    return m_inner()->data.to;
}
std::string_view bcostars::protocol::TransactionImpl::abi() const
{
    return m_inner()->data.abi;
}

std::string_view bcostars::protocol::TransactionImpl::value() const
{
    return m_inner()->data.value;
}

std::string_view bcostars::protocol::TransactionImpl::gasPrice() const
{
    return m_inner()->data.gasPrice;
}

int64_t bcostars::protocol::TransactionImpl::gasLimit() const
{
    return m_inner()->data.gasLimit;
}

std::string_view bcostars::protocol::TransactionImpl::maxFeePerGas() const
{
    return m_inner()->data.maxFeePerGas;
}

std::string_view bcostars::protocol::TransactionImpl::maxPriorityFeePerGas() const
{
    return m_inner()->data.maxPriorityFeePerGas;
}

bcos::bytesConstRef bcostars::protocol::TransactionImpl::extension() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.extension.data()),
        m_inner()->data.extension.size()};
}

int64_t bcostars::protocol::TransactionImpl::importTime() const
{
    return m_inner()->importTime;
}
void bcostars::protocol::TransactionImpl::setImportTime(int64_t _importTime)
{
    m_inner()->importTime = _importTime;
}
bcos::bytesConstRef bcostars::protocol::TransactionImpl::signatureData() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->signature.data()),
        m_inner()->signature.size()};
}
std::string_view bcostars::protocol::TransactionImpl::sender() const
{
    return {m_inner()->sender.data(), m_inner()->sender.size()};
}
void bcostars::protocol::TransactionImpl::forceSender(const bcos::bytes& _sender)
{
    if (!tainted())
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("sender of clean transaction is immutable"));
    }
    m_inner()->sender.assign(_sender.begin(), _sender.end());
}
void bcostars::protocol::TransactionImpl::clearSenderAndHash()
{
    m_inner()->sender.clear();
    m_inner()->dataHash.clear();
    setTainted(true);
}
void bcostars::protocol::TransactionImpl::setSignatureData(bcos::bytes& signature)
{
    m_inner()->signature.assign(signature.begin(), signature.end());
}
int32_t bcostars::protocol::TransactionImpl::attribute() const
{
    return m_inner()->attribute;
}
void bcostars::protocol::TransactionImpl::setAttribute(int32_t attribute)
{
    m_inner()->attribute |= attribute;
}
std::string_view bcostars::protocol::TransactionImpl::extraData() const
{
    return m_inner()->extraData;
}
uint8_t bcostars::protocol::TransactionImpl::type() const
{
    return static_cast<uint8_t>(m_inner()->type);
}
bcos::bytesConstRef bcostars::protocol::TransactionImpl::extraTransactionBytes() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->extraTransactionBytes.data()),
        m_inner()->extraTransactionBytes.size()};
}

uint8_t bcostars::protocol::TransactionImpl::web3TypedTxKind() const
{
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return 0;
    }
    return static_cast<uint8_t>(m_inner()->web3TypedTxKind);
}

void bcostars::protocol::TransactionImpl::ensureWeb3AccessListCache() const
{
    if (m_web3AccessListCacheBuilt)
    {
        return;
    }
    m_web3AccessListCacheBuilt = true;
    m_web3AccessListCache.clear();
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return;
    }
    auto const& entries = m_inner()->data.accessList;
    m_web3AccessListCache.reserve(entries.size());
    for (auto const& entry : entries)
    {
        bcos::protocol::Web3AccessListEntry out;
        out.accountHex = entry.account;
        out.storageKeys.reserve(entry.storageKeys.size());
        for (auto const& keyBytes : entry.storageKeys)
        {
            if (keyBytes.size() != bcos::h256::SIZE)
            {
                WEB3_ACCESS_LIST_LOG(WARNING)
                    << LOG_DESC("Skip access list storage key with invalid length")
                    << LOG_KV("account", entry.account) << LOG_KV("keySize", keyBytes.size())
                    << LOG_KV("expected", bcos::h256::SIZE);
                continue;
            }
            bcos::h256 key;
            std::memcpy(key.data(), keyBytes.data(), bcos::h256::SIZE);
            out.storageKeys.emplace_back(key);
        }
        m_web3AccessListCache.emplace_back(std::move(out));
    }
}

bcos::protocol::Web3AccessList const& bcostars::protocol::TransactionImpl::web3AccessList() const
{
    ensureWeb3AccessListCache();
    return m_web3AccessListCache;
}

const bcostars::Transaction& bcostars::protocol::TransactionImpl::inner() const
{
    return *m_inner();
}
bcostars::Transaction& bcostars::protocol::TransactionImpl::mutableInner()
{
    return *m_inner();
}
void bcostars::protocol::TransactionImpl::setInner(bcostars::Transaction inner)
{
    *m_inner() = std::move(inner);
}

size_t bcostars::protocol::TransactionImpl::size() const
{
    size_t size = 0;
    size += m_inner()->data.nonce.size();
    size += m_inner()->data.to.size();
    size += m_inner()->data.input.size();
    size += m_inner()->data.abi.size();
    size += m_inner()->data.value.size();
    size += m_inner()->data.gasPrice.size();
    size += m_inner()->data.maxFeePerGas.size();
    size += m_inner()->data.maxPriorityFeePerGas.size();
    size += m_inner()->data.extension.size();
    for (auto const& entry : m_inner()->data.accessList)
    {
        size += entry.account.size();
        for (auto const& key : entry.storageKeys)
        {
            size += key.size();
        }
    }
    size += m_inner()->signature.size();
    size += m_inner()->sender.size();
    size += m_inner()->extraData.size();
    size += m_inner()->extraTransactionBytes.size();
    size += m_inner()->extraTransactionHash.size();
    return size;
}
