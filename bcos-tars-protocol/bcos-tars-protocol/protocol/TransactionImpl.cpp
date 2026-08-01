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
#include <bcos-codec/rlp/Web3Transaction.h>
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <cstring>
#include <exception>
#include <stdexcept>

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
    // Web3: the hash is the canonical txHash = keccak256(rlp(signed tx)), stored in
    // extraTransactionHash (which hash() returns). Recompute it from the signed payload
    // unconditionally -- a wire-supplied value is never believed, even when a caller reaches
    // verify() without clearing it first (e.g. TransactionFactoryImpl::createTransaction skips
    // the hash-match check for non-BCOS types), so no caller discipline is required (FIB-New1).
    // The recompute is a byte splice plus one keccak, cheap enough to always run.
    if (type() == static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        auto const canonicalTxHash =
            recomputeWeb3CanonicalHash(extraTransactionBytes(), signatureData());
        m_inner()->extraTransactionHash.assign(canonicalTxHash.begin(), canonicalTxHash.end());
        return;
    }
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

bcos::u256 bcostars::protocol::TransactionImpl::value() const
{
    return bcos::hex2u(m_inner()->data.value);
}

std::optional<bcos::u256> bcostars::protocol::TransactionImpl::gasPrice() const
{
    if (m_inner()->data.gasPrice.empty())
    {
        return std::nullopt;
    }
    return bcos::hex2u(m_inner()->data.gasPrice);
}

int64_t bcostars::protocol::TransactionImpl::gasLimit() const
{
    return m_inner()->data.gasLimit;
}

std::optional<bcos::u256> bcostars::protocol::TransactionImpl::maxFeePerGas() const
{
    if (m_inner()->data.maxFeePerGas.empty())
    {
        return std::nullopt;
    }
    return bcos::hex2u(m_inner()->data.maxFeePerGas);
}

std::optional<bcos::u256> bcostars::protocol::TransactionImpl::maxPriorityFeePerGas() const
{
    if (m_inner()->data.maxPriorityFeePerGas.empty())
    {
        return std::nullopt;
    }
    return bcos::hex2u(m_inner()->data.maxPriorityFeePerGas);
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
    // FIB-New1: also drop the wire-supplied canonical Web3 txHash (extraTransactionHash) so
    // verify() recomputes it from the signed payload. Both re-verification call sites are
    // untrusted enough to warrant this: the P2P import path (TransactionSync) receives it from an
    // untrusted peer, and the RPC submit path (TxValidator::verify) only pre-wrote a value it can
    // cheaply recompute anyway. Harmless for BCOS transactions (the field is never populated).
    // data.accessList / web3TypedTxKind are NOT cleared: admission
    // (web3TarsFieldsMatchSignedExtra) already rejected forged copies, and execution reads them.
    m_inner()->extraTransactionHash.clear();
    setTainted(true);
}

bcos::crypto::HashType bcostars::protocol::TransactionImpl::recomputeWeb3CanonicalHash(
    bcos::bytesConstRef payload, bcos::bytesConstRef signature)
{
    // Use the shared codec encoder (same path as RPC ingress txHash()) so typed /
    // legacy shapes stay in one place. Signature wire format (tars): r(32)||s(32)||yParity(1).
    if (signature.size() != 65) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "invalid Web3 signature length, expect 65, got " + std::to_string(signature.size())));
    }
    if (payload.empty()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("recompute canonical Web3 txHash: empty payload"));
    }

    bcos::bytes buffer(payload.begin(), payload.end());
    bcos::bytesRef cursor(buffer.data(), buffer.size());
    bcos::rpc::Web3Transaction w3{};
    if (auto const decodeError = bcos::codec::rlp::decodeFromPayload(cursor, w3);
        decodeError != nullptr) [[unlikely]]
    {
        BCOS_LOG(INFO) << LOG_DESC("recompute canonical Web3 txHash: decode failed")
                       << LOG_KV("msg", decodeError->errorMessage());
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("recompute canonical Web3 txHash: decode failed"));
    }

    w3.signatureR.assign(signature.begin(), signature.begin() + 32);
    w3.signatureS.assign(signature.begin() + 32, signature.begin() + 64);
    w3.signatureV = static_cast<uint64_t>(signature[64]);
    return w3.txHash();
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
    std::lock_guard<std::mutex> const lock(*m_web3AccessListCacheMutex);
    if (m_web3AccessListCacheBuilt)
    {
        return;
    }
    m_web3AccessListCache.clear();
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        m_web3AccessListCacheBuilt = true;
        return;
    }
    auto const& entries = m_inner()->data.accessList;
    m_web3AccessListCache.reserve(entries.size());
    for (auto const& entry : entries)
    {
        bcos::protocol::Web3AccessListEntry out;
        try
        {
            out.account = bcos::toAddress(entry.account);
        }
        catch (std::exception const&)
        {
            WEB3_ACCESS_LIST_LOG(WARNING)
                << LOG_DESC("Skip access list entry with invalid account address")
                << LOG_KV("account", entry.account);
            continue;
        }
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
    m_web3AccessListCacheBuilt = true;
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
