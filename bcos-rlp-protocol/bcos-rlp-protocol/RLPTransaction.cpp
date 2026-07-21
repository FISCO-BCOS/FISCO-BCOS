/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @brief RLP-based implementation for Ethereum Web3 transactions
 * @file RLPTransaction.cpp
 */

#include "RLPTransaction.h"

#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <cstring>
#include <magic_enum/magic_enum.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <sstream>
#include <stdexcept>

#define RLP_TX_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("RLPTransaction")

// RLP decode support for EthAccessListEntry
// Real implementation; called via inline wrapper in codec::rlp::decode.
namespace bcos::rlp
{
bcos::Error::UniquePtr rlpDecodeEthAccessListEntry(
    bcos::bytesRef& in, EthAccessListEntry& out) noexcept
{
    namespace rlp = bcos::codec::rlp;
    // Use the variadic decode which handles the outer list header automatically
    return rlp::decode(in, out.account, out.storageKeys);
}
}  // namespace bcos::rlp

using namespace bcos;
using namespace bcos::rlp;

// ============================================================================
// Internal helpers
// ============================================================================

namespace
{

/// Trim leading zeros from bytes for compact RLP encoding.
bytesConstRef trimLeadingZeros(bytesConstRef input)
{
    const auto* it = ::ranges::find_if(input, [](byte b) { return b != 0; });
    if (it == input.end())
    {
        return {input.data(), static_cast<size_t>(0)};
    }
    return {it, input.size() - static_cast<size_t>(it - input.begin())};
}

/// Format uint64_t as hex quantity string ("0x...").
std::string toQuantityStr(uint64_t value)
{
    if (value == 0)
    {
        return "0x0";
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << value;
    return oss.str();
}

}  // anonymous namespace

// ============================================================================
// Copy constructor (needed for Factory::createTransaction(Transaction&))
// ============================================================================

RLPTransaction::RLPTransaction(const RLPTransaction& other)
  : Transaction(other),
    m_chainId(other.m_chainId),
    m_web3TypedTxKind(other.m_web3TypedTxKind),
    m_to(other.m_to),
    m_input(other.m_input),
    m_value(other.m_value),
    m_nonce(other.m_nonce),
    m_gasLimit(other.m_gasLimit),
    m_maxFeePerGas(other.m_maxFeePerGas),
    m_maxPriorityFeePerGas(other.m_maxPriorityFeePerGas),
    m_maxFeePerBlobGas(other.m_maxFeePerBlobGas),
    m_blobVersionedHashes(other.m_blobVersionedHashes),
    m_accessList(other.m_accessList),
    m_signatureR(other.m_signatureR),
    m_signatureS(other.m_signatureS),
    m_signatureV(other.m_signatureV),
    m_sender(other.m_sender),
    m_importTime(other.m_importTime),
    m_attribute(other.m_attribute),
    m_extraData(other.m_extraData),
    m_nonceStr(other.m_nonceStr),
    m_chainIdStr(other.m_chainIdStr),
    m_toStr(other.m_toStr),
    m_version(other.m_version),
    m_blockLimit(other.m_blockLimit),
    m_cachedHashForSign(other.m_cachedHashForSign),
    m_hashForSignDirty(other.m_hashForSignDirty),
    m_cachedSignature(other.m_cachedSignature)
{}

// ============================================================================
// String cache management
// ============================================================================

void RLPTransaction::refreshStringCaches()
{
    if (m_chainId.has_value())
    {
        m_chainIdStr = std::to_string(m_chainId.value());
    }
    else
    {
        m_chainIdStr.clear();
    }

    m_nonceStr = toQuantityStr(m_nonce);

    if (m_to.has_value())
    {
        m_toStr = m_to.value().hexPrefixed();
    }
    else
    {
        m_toStr.clear();
    }
}

void RLPTransaction::refreshSignatureCache() const
{
    std::lock_guard<std::mutex> lock(*m_cacheMutex);
    if (!m_cachedSignature.empty())
    {
        return;
    }
    m_cachedSignature.reserve(65);  // r(32) + s(32) + v(1)
    // Left-pad r to 32 bytes (RLP decode strips leading zeros)
    if (m_signatureR.size() < 32)
    {
        m_cachedSignature.insert(m_cachedSignature.end(), 32 - m_signatureR.size(), 0);
    }
    m_cachedSignature.insert(m_cachedSignature.end(), m_signatureR.begin(), m_signatureR.end());
    // Left-pad s to 32 bytes (RLP decode strips leading zeros)
    if (m_signatureS.size() < 32)
    {
        m_cachedSignature.insert(m_cachedSignature.end(), 32 - m_signatureS.size(), 0);
    }
    m_cachedSignature.insert(m_cachedSignature.end(), m_signatureS.begin(), m_signatureS.end());
    m_cachedSignature.push_back(static_cast<byte>(m_signatureV & 0xFF));
}

// ============================================================================
// Base field encoding (shared by encodePayload and encode)
// ============================================================================
// Base field length / encoding (shared by encodePayload and encode)
// ============================================================================

size_t RLPTransaction::basePayloadLength() const
{
    namespace rlp = bcos::codec::rlp;

    size_t len = 0;

    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::Legacy))
    {
        len += rlp::length(m_nonce);
        len += rlp::length(m_maxFeePerGas);  // gasPrice
    }
    else
    {
        len += rlp::length(m_chainId.value_or(0));
        len += rlp::length(m_nonce);
        if (m_web3TypedTxKind != static_cast<uint8_t>(Web3TxType::EIP2930))
        {
            len += rlp::length(m_maxPriorityFeePerGas);
        }
        len += rlp::length(m_maxFeePerGas);
    }

    len += rlp::length(m_gasLimit);
    len += m_to.has_value() ? (Address::SIZE + 1) : 1;
    len += rlp::length(m_value);
    len += rlp::length(m_input);

    if (m_web3TypedTxKind != static_cast<uint8_t>(Web3TxType::Legacy))
    {
        size_t alLen = 0;
        for (auto const& entry : m_accessList)
        {
            size_t keysLen = 0;
            for (auto const& key : entry.storageKeys)
            {
                keysLen += rlp::length(key.ref());
            }
            size_t entryPayloadLen = Address::SIZE + 1 + rlp::lengthOfLength(keysLen) + keysLen;
            alLen += rlp::lengthOfLength(entryPayloadLen) + entryPayloadLen;
        }
        len += rlp::lengthOfLength(alLen) + alLen;
    }

    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::EIP4844))
    {
        len += rlp::length(m_maxFeePerBlobGas);
        size_t bLen = 0;
        for (auto const& bh : m_blobVersionedHashes)
        {
            bLen += rlp::length(bh.ref());
        }
        len += rlp::lengthOfLength(bLen) + bLen;
    }

    return len;
}

size_t RLPTransaction::signedPayloadLength() const
{
    namespace rlp = bcos::codec::rlp;

    size_t payloadLen = basePayloadLength();

    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::Legacy))
    {
        uint64_t recoveredV =
            m_chainId.has_value() ? (m_chainId.value() * 2) + 35 + m_signatureV : m_signatureV + 27;
        payloadLen += rlp::length(recoveredV);
        payloadLen += rlp::length(trimLeadingZeros(ref(m_signatureR)));
        payloadLen += rlp::length(trimLeadingZeros(ref(m_signatureS)));
    }
    else
    {
        payloadLen += 1;  // yParity
        payloadLen += rlp::length(trimLeadingZeros(ref(m_signatureR)));
        payloadLen += rlp::length(trimLeadingZeros(ref(m_signatureS)));
    }
    return payloadLen;
}

void RLPTransaction::encodeBaseFields(bcos::bytes& out) const
{
    namespace rlp = bcos::codec::rlp;

    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::Legacy))
    {
        rlp::encode(out, m_nonce);
        rlp::encode(out, m_maxFeePerGas);
    }
    else
    {
        rlp::encode(out, m_chainId.value_or(0));
        rlp::encode(out, m_nonce);
        if (m_web3TypedTxKind != static_cast<uint8_t>(Web3TxType::EIP2930))
        {
            rlp::encode(out, m_maxPriorityFeePerGas);
        }
        rlp::encode(out, m_maxFeePerGas);
    }

    rlp::encode(out, m_gasLimit);
    if (m_to.has_value())
    {
        rlp::encode(out, m_to.value().ref());
    }
    else
    {
        out.push_back(rlp::BYTES_HEAD_BASE);
    }
    rlp::encode(out, m_value);
    rlp::encode(out, m_input);

    if (m_web3TypedTxKind != static_cast<uint8_t>(Web3TxType::Legacy))
    {
        size_t alLen = 0;
        for (auto const& entry : m_accessList)
        {
            size_t keysLen = 0;
            for (auto const& key : entry.storageKeys)
            {
                keysLen += rlp::length(key.ref());
            }
            size_t entryPayloadLen = Address::SIZE + 1 + rlp::lengthOfLength(keysLen) + keysLen;
            alLen += rlp::lengthOfLength(entryPayloadLen) + entryPayloadLen;
        }
        rlp::encodeHeader(out, {.isList = true, .payloadLength = alLen});
        for (auto const& entry : m_accessList)
        {
            size_t keysLen = 0;
            for (auto const& key : entry.storageKeys)
            {
                keysLen += rlp::length(key.ref());
            }
            size_t entryPayloadLen = Address::SIZE + 1 + rlp::lengthOfLength(keysLen) + keysLen;
            rlp::encodeHeader(out, {.isList = true, .payloadLength = entryPayloadLen});
            rlp::encode(out, entry.account.ref());
            rlp::encodeHeader(out, {.isList = true, .payloadLength = keysLen});
            for (auto const& key : entry.storageKeys)
            {
                rlp::encode(out, key.ref());
            }
        }
    }

    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::EIP4844))
    {
        rlp::encode(out, m_maxFeePerBlobGas);
        size_t bLen = 0;
        for (auto const& bh : m_blobVersionedHashes)
        {
            bLen += rlp::length(bh.ref());
        }
        rlp::encodeHeader(out, {.isList = true, .payloadLength = bLen});
        for (auto const& bh : m_blobVersionedHashes)
        {
            rlp::encode(out, bh.ref());
        }
    }
}

// ============================================================================
// encodePayload: base fields + EIP-155 extras (legacy only)
// ============================================================================

void RLPTransaction::encodePayload(bcos::bytes& out) const
{
    namespace rlp = bcos::codec::rlp;

    // 1. Compute total payload length
    size_t payloadLen = basePayloadLength();
    size_t eip155Extra = 0;
    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::Legacy) && m_chainId.has_value())
    {
        eip155Extra = rlp::length(m_chainId.value()) + 2;
    }
    payloadLen += eip155Extra;

    // 2. Reserve and write header, then fields
    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::Legacy))
    {
        out.reserve(out.size() + rlp::lengthOfLength(payloadLen) + payloadLen);
        rlp::encodeHeader(out, {.isList = true, .payloadLength = payloadLen});
        encodeBaseFields(out);
        if (m_chainId.has_value())
        {
            rlp::encode(out, m_chainId.value());
            rlp::encode(out, 0U);
            rlp::encode(out, 0U);
        }
    }
    else
    {
        out.reserve(out.size() + 1 + rlp::lengthOfLength(payloadLen) + payloadLen);
        out.push_back(static_cast<byte>(m_web3TypedTxKind));
        rlp::encodeHeader(out, {.isList = true, .payloadLength = payloadLen});
        encodeBaseFields(out);
    }
}

// ============================================================================
// Transaction interface: encode (full RLP with signature)
// ============================================================================

void RLPTransaction::encode(bcos::bytes& txData) const
{
    namespace rlp = bcos::codec::rlp;
    refreshSignatureCache();

    size_t payloadLen = signedPayloadLength();

    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::Legacy))
    {
        uint64_t recoveredV =
            m_chainId.has_value() ? (m_chainId.value() * 2) + 35 + m_signatureV : m_signatureV + 27;

        txData.reserve(txData.size() + rlp::lengthOfLength(payloadLen) + payloadLen);
        rlp::encodeHeader(txData, {.isList = true, .payloadLength = payloadLen});
        encodeBaseFields(txData);
        rlp::encode(txData, recoveredV);
        rlp::encode(txData, trimLeadingZeros(ref(m_signatureR)));
        rlp::encode(txData, trimLeadingZeros(ref(m_signatureS)));
    }
    else
    {
        txData.reserve(txData.size() + 1 + rlp::lengthOfLength(payloadLen) + payloadLen);
        txData.push_back(static_cast<byte>(m_web3TypedTxKind));
        rlp::encodeHeader(txData, {.isList = true, .payloadLength = payloadLen});
        encodeBaseFields(txData);
        if (m_signatureV == 0)
        {
            txData.push_back(rlp::BYTES_HEAD_BASE);
        }
        else
        {
            rlp::encode(txData, static_cast<uint8_t>(m_signatureV));
        }
        rlp::encode(txData, trimLeadingZeros(ref(m_signatureR)));
        rlp::encode(txData, trimLeadingZeros(ref(m_signatureS)));
    }
}

void RLPTransaction::decode(bcos::bytesConstRef _txData)
{
    namespace rlp = bcos::codec::rlp;

    bcos::bytes dataCopy(_txData.begin(), _txData.end());
    bcos::bytesRef ref(dataCopy.data(), dataCopy.size());

    if (ref.empty())
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: empty decode data"));
    }

    auto const firstByte = ref[0];
    if (firstByte > 0 && firstByte < 0x80)
    {
        // --- EIP-2718 Typed Transaction ---
        auto txType = magic_enum::enum_cast<Web3TxType>(firstByte);
        if (!txType.has_value())
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: unsupported tx type " +
                                      std::to_string(static_cast<int>(firstByte))));
        }
        m_web3TypedTxKind = static_cast<uint8_t>(txType.value());
        ref = ref.getCroppedData(1);

        auto&& [err, header] = rlp::decodeHeader(ref);
        if (err != nullptr || !header.isList)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: invalid typed tx header"));
        }

        uint64_t chainId = 0;
        if (auto error = rlp::decodeItems(ref, chainId, m_nonce, m_maxPriorityFeePerGas))
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: decode typed fields failed"));
        }
        m_chainId.emplace(chainId);

        if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::EIP2930))
        {
            m_maxFeePerGas = m_maxPriorityFeePerGas;
        }
        else if (auto error = rlp::decode(ref, m_maxFeePerGas))
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: decode maxFeePerGas failed"));
        }

        if (auto error = rlp::decode(ref, m_gasLimit))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: decode gasLimit failed"));
        }

        // Guard against truncated tx (ref[0] OOB)
        if (ref.empty())
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: truncated tx at to field"));
        }
        if (ref[0] == rlp::BYTES_HEAD_BASE)
        {
            m_to = std::nullopt;
            ref = ref.getCroppedData(1);
        }
        else
        {
            Address addr;
            if (auto error = rlp::decode(ref, addr))
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: decode to failed"));
            }
            m_to.emplace(addr);
        }

        if (auto error = rlp::decodeItems(ref, m_value, m_input))
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: decode value/data failed"));
        }

        // Manually decode access list (EthAccessListEntry), bypassing ADL issues
        // with the std::vector decode template.
        m_accessList.clear();
        {
            auto&& [alErr, alHeader] = rlp::decodeHeader(ref);
            if (alErr || !alHeader.isList)
            {
                BOOST_THROW_EXCEPTION(
                    std::invalid_argument("RLPTransaction: bad accessList header"));
            }
            auto alView = ref.getCroppedData(0, alHeader.payloadLength);
            while (!alView.empty())
            {
                EthAccessListEntry entry;
                if (auto e = rlpDecodeEthAccessListEntry(alView, entry))
                {
                    BOOST_THROW_EXCEPTION(
                        std::invalid_argument("RLPTransaction: decode access entry failed"));
                }
                m_accessList.emplace_back(std::move(entry));
            }
            ref = ref.getCroppedData(alHeader.payloadLength);
        }

        if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::EIP4844))
        {
            if (auto error = rlp::decodeItems(ref, m_maxFeePerBlobGas, m_blobVersionedHashes))
            {
                BOOST_THROW_EXCEPTION(
                    std::invalid_argument("RLPTransaction: decode EIP-4844 fields failed"));
            }
            // Enforce EIP-4844 invariants
            if (!m_to.has_value())
            {
                BOOST_THROW_EXCEPTION(
                    std::invalid_argument("RLPTransaction: EIP-4844 blob tx must have 'to'"));
            }
            if (m_blobVersionedHashes.empty())
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument(
                    "RLPTransaction: EIP-4844 blobVersionedHashes must be non-empty"));
            }
            for (auto const& h : m_blobVersionedHashes)
            {
                if (h.ref()[0] != 0x01)
                {
                    BOOST_THROW_EXCEPTION(std::invalid_argument(
                        "RLPTransaction: EIP-4844 versioned hash must start with 0x01"));
                }
            }
        }

        if (auto error = rlp::decodeItems(ref, m_signatureV, m_signatureR, m_signatureS))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: decode signature failed"));
        }

        // Validate yParity for typed txs
        if (m_signatureV > 1)
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: typed tx yParity must be 0 or 1, got " +
                                      std::to_string(m_signatureV)));
        }
    }
    else
    {
        // --- Legacy ---
        m_web3TypedTxKind = static_cast<uint8_t>(Web3TxType::Legacy);

        auto&& [err, header] = rlp::decodeHeader(ref);
        if (err != nullptr || !header.isList)
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: invalid legacy tx header"));
        }

        if (auto error = rlp::decodeItems(ref, m_nonce, m_maxPriorityFeePerGas))
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: decode legacy fields failed"));
        }
        m_maxFeePerGas = m_maxPriorityFeePerGas;

        if (auto error = rlp::decode(ref, m_gasLimit))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: decode gasLimit failed"));
        }

        // Guard against truncated tx (ref[0] OOB)
        if (ref.empty())
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: truncated tx at to field"));
        }
        if (ref[0] == rlp::BYTES_HEAD_BASE)
        {
            m_to = std::nullopt;
            ref = ref.getCroppedData(1);
        }
        else
        {
            Address addr;
            if (auto error = rlp::decode(ref, addr))
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: decode to failed"));
            }
            m_to.emplace(addr);
        }

        if (auto error = rlp::decodeItems(ref, m_value, m_input))
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: decode value/data failed"));
        }

        if (auto error = rlp::decodeItems(ref, m_signatureV, m_signatureR, m_signatureS))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument("RLPTransaction: decode signature failed"));
        }

        // EIP-155: extract chainId from v
        auto v = m_signatureV;
        if (v == 27 || v == 28)
        {
            m_chainId = std::nullopt;
            m_signatureV = v - 27;
        }
        else if (v == 0 || v == 1)
        {
            m_chainId = std::nullopt;
        }
        else if (v >= 35)
        {
            m_signatureV = (v - 35) % 2;
            m_chainId = (v - 35) >> 1;
        }
        else
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: invalid legacy v value: " +
                                      std::to_string(v) +
                                      " (expected 0,1,27,28 or >=35)"));
        }
    }

    // Verify full consumption: reject trailing garbage
    if (!ref.empty())
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("RLPTransaction: trailing bytes after decode, remaining=" +
                                  std::to_string(ref.size())));
    }

    setTainted(true);
    m_hashForSignDirty = true;
    m_web3AccessListBuilt = false;
    refreshStringCaches();

    // Eagerly compute mutable caches to avoid data races on const getters
    refreshSignatureCache();
    {
        std::lock_guard<std::mutex> lock(*m_cacheMutex);
        m_encodedForSign = encodeForSign();
    }
}

// ============================================================================
// Hash operations
// ============================================================================

bcos::crypto::HashType RLPTransaction::hash() const
{
    bcos::bytes encoded;
    encode(encoded);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::crypto::HashType RLPTransaction::txHash() const
{
    bcos::bytes encoded;
    encode(encoded);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::crypto::HashType RLPTransaction::hashForSign() const
{
    auto payload = encodeForSign();
    return bcos::crypto::keccak256Hash(bcos::ref(payload));
}

void RLPTransaction::calculateHash(const bcos::crypto::Hash& /*hashImpl*/)
{
    // hash() always computes fresh (no caching); nothing to precompute.
}

bcos::bytes RLPTransaction::encodeForSign() const
{
    bcos::bytes out;
    encodePayload(out);
    return out;
}

void RLPTransaction::verify(crypto::Hash& hashImpl, crypto::SignatureCrypto& signatureImpl)
{
    if (!tainted())
    {
        return;
    }

    auto const txHash = hash();
    auto const signature = signatureData();
    auto [recovered, sender] = signatureImpl.recoverAddress(hashImpl, txHash, signature);
    if (!recovered) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("RLPTransaction: recover sender address from signature failed"));
    }
    forceSender(sender);
    setTainted(false);
}

bcos::bytesConstRef RLPTransaction::extraTransactionBytes() const
{
    std::lock_guard<std::mutex> lock(*m_cacheMutex);
    if (m_encodedForSign.empty())
    {
        m_encodedForSign = encodeForSign();
    }
    return {m_encodedForSign.data(), m_encodedForSign.size()};
}

// ============================================================================
// Access list
// ============================================================================

void RLPTransaction::ensureWeb3AccessListCache() const
{
    std::lock_guard<std::mutex> lock(*m_accessListMutex);
    if (m_web3AccessListBuilt)
    {
        return;
    }
    m_web3AccessListCache.clear();
    m_web3AccessListCache.reserve(m_accessList.size());
    for (auto const& entry : m_accessList)
    {
        m_web3AccessListCache.push_back(
            bcos::protocol::Web3AccessListEntry{entry.account, entry.storageKeys});
    }
    m_web3AccessListBuilt = true;
}

bcos::protocol::Web3AccessList const& RLPTransaction::web3AccessList() const
{
    ensureWeb3AccessListCache();
    return m_web3AccessListCache;
}

// ============================================================================
// Setters
// ============================================================================

void RLPTransaction::setNonce(std::string nonce)
{
    try
    {
        if (nonce.starts_with("0x") || nonce.starts_with("0X"))
        {
            m_nonce = std::stoull(nonce.substr(2), nullptr, 16);
        }
        else
        {
            m_nonce = std::stoull(nonce);
        }
    }
    catch (...)
    {
        m_nonce = 0;
    }
    refreshStringCaches();  // Use formatted hex, not raw input
    m_hashForSignDirty = true;
    {
        std::lock_guard<std::mutex> lock(*m_cacheMutex);
        m_encodedForSign.clear();
    }
}

void RLPTransaction::forceSender(const bcos::bytes& _sender)
{
    if (!tainted())
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("sender of clean transaction is immutable"));
    }
    m_sender = _sender;
    // Clear cached signature since sender changed (may need re-verify)
    {
        std::lock_guard<std::mutex> lock(*m_cacheMutex);
        m_cachedSignature.clear();
    }
}

void RLPTransaction::clearSenderAndHash()
{
    m_sender.clear();
    m_cachedHashForSign = {};
    m_hashForSignDirty = true;
    setTainted(true);
}

void RLPTransaction::setChainId(std::optional<uint64_t> id)
{
    m_chainId = id;
    refreshStringCaches();
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setWeb3TxType(Web3TxType t)
{
    m_web3TypedTxKind = static_cast<uint8_t>(t);
    refreshStringCaches();
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setToAddress(std::optional<Address> addr)
{
    m_to = addr;
    refreshStringCaches();
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setInputData(bcos::bytes data)
{
    m_input = std::move(data);
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setValueU256(u256 v)
{
    m_value = std::move(v);
    refreshStringCaches();
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setNonceU64(uint64_t n)
{
    m_nonce = n;
    refreshStringCaches();
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setGasLimitU64(uint64_t g)
{
    m_gasLimit = g;
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setMaxFeePerGasU256(u256 v)
{
    m_maxFeePerGas = std::move(v);
    refreshStringCaches();
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setMaxPriorityFeePerGasU256(u256 v)
{
    m_maxPriorityFeePerGas = std::move(v);
    refreshStringCaches();
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setMaxFeePerBlobGasU256(u256 v)
{
    m_maxFeePerBlobGas = std::move(v);
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setBlobVersionedHashes(h256s hashes)
{
    m_blobVersionedHashes = std::move(hashes);
    m_hashForSignDirty = true;
    { std::lock_guard<std::mutex> lock(*m_cacheMutex); m_encodedForSign.clear(); }
}

void RLPTransaction::setSignatureR(bcos::bytes r)
{
    m_signatureR = std::move(r);
    std::lock_guard<std::mutex> lock(*m_cacheMutex);
    m_cachedSignature.clear();
}

void RLPTransaction::setSignatureS(bcos::bytes s)
{
    m_signatureS = std::move(s);
    std::lock_guard<std::mutex> lock(*m_cacheMutex);
    m_cachedSignature.clear();
}

void RLPTransaction::setSignatureV(uint64_t v)
{
    m_signatureV = v;
    std::lock_guard<std::mutex> lock(*m_cacheMutex);
    m_cachedSignature.clear();
}

void RLPTransaction::setAccessListEth(std::vector<EthAccessListEntry> list)
{
    std::lock_guard<std::mutex> lock(*m_accessListMutex);
    m_accessList = std::move(list);
    m_web3AccessListBuilt = false;
    m_hashForSignDirty = true;
    {
        std::lock_guard<std::mutex> cacheLock(*m_cacheMutex);
        m_encodedForSign.clear();
    }
}

// ============================================================================
// Size estimation
// ============================================================================

size_t RLPTransaction::size() const
{
    namespace rlp = bcos::codec::rlp;

    size_t payloadLen = signedPayloadLength();

    if (m_web3TypedTxKind == static_cast<uint8_t>(Web3TxType::Legacy))
    {
        return rlp::lengthOfLength(payloadLen) + payloadLen;
    }

    return 1 + rlp::lengthOfLength(payloadLen) + payloadLen;  // type byte + header + payload
}
