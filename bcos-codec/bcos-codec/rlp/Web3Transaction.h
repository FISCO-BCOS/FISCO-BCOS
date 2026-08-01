/**
 * Copyright (C) 2022 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file Web3Transaction.h
 * @brief Web3 typed-tx model + RLP encode/decode (lives in bcos-codec).
 */

#pragma once
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/FixedBytes.h>
#include <magic_enum/magic_enum.hpp>
#include <ostream>

namespace bcos::rpc
{
// EIP-2718 transaction type
enum class TransactionType : uint8_t
{
    Legacy = 0,
    EIP2930 = 1,
    EIP1559 = 2,
    EIP4844 = 3,
};

constexpr auto operator<=>(TransactionType const& ltype, auto rtype)
    requires std::same_as<decltype(rtype), TransactionType> ||
             std::unsigned_integral<decltype(rtype)>
{
    return static_cast<uint8_t>(ltype) <=> static_cast<uint8_t>(rtype);
}

std::ostream& operator<<(std::ostream& _out, const TransactionType& _in);

struct AccessListEntry
{
    Address account;
    std::vector<crypto::HashType> storageKeys;
    friend bool operator==(const AccessListEntry& lhs, const AccessListEntry& rhs) noexcept
    {
        return lhs.account == rhs.account && lhs.storageKeys == rhs.storageKeys;
    }
};

class Web3Transaction
{
public:
    Web3Transaction() = default;
    ~Web3Transaction() = default;
    Web3Transaction(const Web3Transaction&) = delete;
    Web3Transaction(Web3Transaction&&) = default;
    Web3Transaction& operator=(const Web3Transaction&) = delete;
    Web3Transaction& operator=(Web3Transaction&&) = default;

    bcos::bytes encodeForSign() const;
    bcos::crypto::HashType txHash() const;
    bcos::crypto::HashType hashForSign() const;
    uint64_t getSignatureV() const;
    std::string sender() const;
    std::string toString() const noexcept;

    std::optional<uint64_t> chainId{std::nullopt};
    TransactionType type{TransactionType::Legacy};
    std::optional<Address> to;
    bcos::bytes data;
    u256 value{0};
    uint64_t nonce{0};
    uint64_t gasLimit{0};
    std::vector<AccessListEntry> accessList;
    u256 maxPriorityFeePerGas{0};
    u256 maxFeePerGas{0};
    u256 maxFeePerBlobGas{0};
    h256s blobVersionedHashes;
    bcos::bytes signatureR;
    bcos::bytes signatureS;
    uint64_t signatureV{0};
};
}  // namespace bcos::rpc

namespace bcos::codec::rlp
{
Header header(const rpc::AccessListEntry& entry) noexcept;
void encode(bcos::bytes& out, const rpc::AccessListEntry&) noexcept;
size_t length(const rpc::AccessListEntry&) noexcept;

size_t length(const rpc::Web3Transaction&) noexcept;
Header headerForSign(const rpc::Web3Transaction& tx) noexcept;
Header headerTxBase(const rpc::Web3Transaction& tx) noexcept;
Header header(const rpc::Web3Transaction& tx) noexcept;
void encode(bcos::bytes& out, const rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& in, rpc::AccessListEntry&) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& in, rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decodeTransaction(
    bcos::bytesRef& in, rpc::Web3Transaction&, bool withSignature) noexcept;
}  // namespace bcos::codec::rlp
