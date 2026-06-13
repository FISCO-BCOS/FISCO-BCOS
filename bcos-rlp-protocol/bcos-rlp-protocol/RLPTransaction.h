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
 * @file RLPTransaction.h
 */
#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/Web3AccessList.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace bcos::rlp
{

/// Ethereum EIP-2718 transaction types.
enum class Web3TxType : uint8_t
{
    Legacy = 0,
    EIP2930 = 1,
    EIP1559 = 2,
    EIP4844 = 3,
};

/// Ethereum-style access list entry for RLP encode/decode.
struct EthAccessListEntry
{
    Address account;
    std::vector<bcos::crypto::HashType> storageKeys;
};

}  // namespace bcos::rlp

// RLP decode support for EthAccessListEntry.
// The inline wrapper in codec::rlp enables ADL from RLPDecode.h templates;
// the real implementation is in bcos::rlp::rlpDecodeEthAccessListEntry.
namespace bcos::rlp
{
bcos::Error::UniquePtr rlpDecodeEthAccessListEntry(
    bcos::bytesRef& in, EthAccessListEntry& out) noexcept;
}  // namespace bcos::rlp

namespace bcos::codec::rlp
{
inline bcos::Error::UniquePtr decode(
    bcos::bytesRef& in, bcos::rlp::EthAccessListEntry& out) noexcept
{
    return bcos::rlp::rlpDecodeEthAccessListEntry(in, out);
}
}  // namespace bcos::codec::rlp

namespace bcos::rlp
{

/// Pure RLP-based Transaction implementation for Ethereum Web3 transactions.
///
/// This class stores all transaction data as native Ethereum types (u256, uint64_t,
/// Address, etc.) and uses RLP encoding for serialization. It does NOT use TARS.
///
/// All Transaction interface getters returning string_view point to internal
/// pre-formatted string caches that are refreshed on construction and after decode().
class RLPTransaction : public bcos::protocol::Transaction
{
public:
    RLPTransaction() { refreshStringCaches(); }
    RLPTransaction(const RLPTransaction& other);
    RLPTransaction& operator=(const RLPTransaction&) = delete;
    ~RLPTransaction() noexcept override = default;

    // --- Transaction interface ---

    void decode(bcos::bytesConstRef _txData) override;
    void encode(bcos::bytes& txData) const override;

    bcos::crypto::HashType hash() const override;
    void calculateHash(const bcos::crypto::Hash& hashImpl) override;
    bcos::bytesConstRef extraTransactionBytes() const override;

    uint8_t web3TypedTxKind() const override { return m_web3TypedTxKind; }
    bcos::protocol::Web3AccessList const& web3AccessList() const override;

    int32_t version() const override { return m_version; }
    std::string_view chainId() const override { return m_chainIdStr; }
    std::string_view groupId() const override { return {}; }
    int64_t blockLimit() const override { return m_blockLimit; }
    std::string_view nonce() const override { return m_nonceStr; }
    void setNonce(std::string nonce) override;
    std::string_view to() const override { return m_toStr; }
    std::string_view abi() const override { return {}; }

    std::string_view value() const override { return m_valueStr; }
    std::string_view gasPrice() const override { return m_gasPriceStr; }
    int64_t gasLimit() const override { return static_cast<int64_t>(m_gasLimit); }
    std::string_view maxFeePerGas() const override { return m_maxFeePerGasStr; }
    std::string_view maxPriorityFeePerGas() const override { return m_maxPriorityFeePerGasStr; }

    bcos::bytesConstRef extension() const override { return {}; }
    std::string_view extraData() const override { return m_extraData; }
    std::string_view sender() const override
    {
        return {reinterpret_cast<const char*>(m_sender.data()), m_sender.size()};
    }

    bcos::bytesConstRef input() const override { return {m_input.data(), m_input.size()}; }
    int64_t importTime() const override { return m_importTime; }
    void setImportTime(int64_t _importTime) override { m_importTime = _importTime; }
    uint8_t type() const override
    {
        return static_cast<uint8_t>(bcos::protocol::TransactionType::RLPWeb3Transaction);
    }

    void forceSender(const bcos::bytes& _sender) override;
    void clearSenderAndHash() override;
    bcos::bytesConstRef signatureData() const override
    {
        refreshSignatureCache();
        return {m_cachedSignature.data(), m_cachedSignature.size()};
    }

    int32_t attribute() const override { return m_attribute; }
    void setAttribute(int32_t attribute) override { m_attribute |= attribute; }

    size_t size() const override;

    // --- RLP-specific accessors ---

    std::optional<uint64_t> const& chainIdValue() const { return m_chainId; }
    void setChainId(std::optional<uint64_t> id);
    Web3TxType web3TxType() const { return static_cast<Web3TxType>(m_web3TypedTxKind); }
    void setWeb3TxType(Web3TxType t);
    std::optional<Address> const& toAddress() const { return m_to; }
    void setToAddress(std::optional<Address> addr);
    bcos::bytes const& inputData() const { return m_input; }
    void setInputData(bcos::bytes data);
    u256 const& valueU256() const { return m_value; }
    void setValueU256(u256 v);
    uint64_t nonceU64() const { return m_nonce; }
    void setNonceU64(uint64_t n);
    uint64_t gasLimitU64() const { return m_gasLimit; }
    void setGasLimitU64(uint64_t g);
    u256 const& maxFeePerGasU256() const { return m_maxFeePerGas; }
    void setMaxFeePerGasU256(u256 v);
    u256 const& maxPriorityFeePerGasU256() const { return m_maxPriorityFeePerGas; }
    void setMaxPriorityFeePerGasU256(u256 v);
    u256 const& maxFeePerBlobGasU256() const { return m_maxFeePerBlobGas; }
    void setMaxFeePerBlobGasU256(u256 v);
    h256s const& blobVersionedHashes() const { return m_blobVersionedHashes; }
    void setBlobVersionedHashes(h256s hashes);
    bcos::bytes const& signatureR() const { return m_signatureR; }
    void setSignatureR(bcos::bytes r);
    bcos::bytes const& signatureS() const { return m_signatureS; }
    void setSignatureS(bcos::bytes s);
    uint64_t signatureV() const { return m_signatureV; }
    void setSignatureV(uint64_t v);
    std::vector<EthAccessListEntry> const& accessListEth() const { return m_accessList; }
    void setAccessListEth(std::vector<EthAccessListEntry> list);
    bcos::bytes const& senderBytes() const { return m_sender; }

    /// Compute the signable RLP payload (same as extraTransactionBytes).
    bcos::bytes encodeForSign() const;
    /// Compute the full transaction hash = keccak256(rlp(tx_with_sig)).
    bcos::crypto::HashType txHash() const;
    /// Compute hash for signing = keccak256(rlp(payload)).
    bcos::crypto::HashType hashForSign() const;

private:
    /// Rebuild all string caches from raw fields.
    void refreshStringCaches();
    /// Rebuild the signature cache (r||s||v) in FISCO BCOS format.
    void refreshSignatureCache() const;
    /// Rebuild web3 access list cache.
    void ensureWeb3AccessListCache() const;
    /// Encode the RLP payload (without signature) for signing.
    void encodePayload(bcos::bytes& out) const;
    /// Compute the total payload length of base fields (no header, no signature).
    size_t basePayloadLength() const;
    /// Write base fields into out. Call basePayloadLength() first to reserve.
    void encodeBaseFields(bcos::bytes& out) const;

    // --- Core Ethereum fields ---
    std::optional<uint64_t> m_chainId;
    uint8_t m_web3TypedTxKind{0};
    std::optional<Address> m_to;
    bcos::bytes m_input;
    u256 m_value{0};
    uint64_t m_nonce{0};
    uint64_t m_gasLimit{0};
    u256 m_maxFeePerGas{0};
    u256 m_maxPriorityFeePerGas{0};
    u256 m_maxFeePerBlobGas{0};
    h256s m_blobVersionedHashes;
    std::vector<EthAccessListEntry> m_accessList;
    bcos::bytes m_signatureR;
    bcos::bytes m_signatureS;
    uint64_t m_signatureV{0};

    // --- Additional fields for Transaction interface ---
    bcos::bytes m_sender;
    int64_t m_importTime{0};
    int32_t m_attribute{0};
    std::string m_extraData;

    // --- String caches for string_view getters ---
    std::string m_nonceStr;
    std::string m_chainIdStr;
    std::string m_toStr;
    std::string m_valueStr;
    std::string m_gasPriceStr;
    std::string m_maxFeePerGasStr;
    std::string m_maxPriorityFeePerGasStr;
    int32_t m_version{0};
    int64_t m_blockLimit{std::numeric_limits<int64_t>::max()};

    // --- Mutable caches ---
    mutable bcos::protocol::Web3AccessList m_web3AccessListCache;
    mutable std::unique_ptr<std::mutex> m_accessListMutex{std::make_unique<std::mutex>()};
    mutable bool m_web3AccessListBuilt{false};

    mutable bcos::crypto::HashType m_cachedTxHash;
    mutable bcos::crypto::HashType m_cachedHashForSign;
    mutable bool m_hashDirty{true};
    mutable bool m_hashForSignDirty{true};

    mutable bcos::bytes m_encodedForSign;
    mutable bool m_encodedForSignBuilt{false};

    mutable bcos::bytes m_cachedSignature;
    mutable bool m_cachedSignatureDirty{true};
};

// Guard: RLPTransaction must fit inside the AnyTransaction fixed-size buffer.
// If this assertion fires, update the size constant in
// bcos-framework/bcos-framework/protocol/Transaction.h (using AnyTransaction = AnyHolder<..., N>).
// Also update the same guard in TransactionImpl.h.
static_assert(sizeof(RLPTransaction) <= 1024,
    "RLPTransaction exceeds AnyTransaction buffer (1024 bytes); "
    "update the size constant in bcos-framework/protocol/Transaction.h");

}  // namespace bcos::rlp
