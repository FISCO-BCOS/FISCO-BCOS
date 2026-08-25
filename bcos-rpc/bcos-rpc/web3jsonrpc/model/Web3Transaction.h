/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file Web3Transaction.h
 * @author: kyonGuo
 * @date 2024/4/8
 */

#pragma once
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-utilities/FixedBytes.h>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <ostream>
#include <string_view>
namespace bcos
{
namespace rpc
{
// EIP-2718 transaction type
// https://github.com/ethereum/eth1.0-specs/tree/master/lists/signature-types
enum class TransactionType : uint8_t
{
    Legacy = 0,
    EIP2930 = 1,     // https://eips.ethereum.org/EIPS/eip-2930
    EIP1559 = 2,     // https://eips.ethereum.org/EIPS/eip-1559
    EIP4844 = 3,     // https://eips.ethereum.org/EIPS/eip-4844
    EIP7702 = 4,     // https://eips.ethereum.org/EIPS/eip-7702
    Deposit = 0x7e,  // deposit-only system tx (OP Stack)
};

constexpr auto operator<=>(TransactionType const& ltype, auto rtype)
    requires std::same_as<decltype(rtype), TransactionType> ||
             std::unsigned_integral<decltype(rtype)>
{
    return static_cast<uint8_t>(ltype) <=> static_cast<uint8_t>(rtype);
}

std::ostream& operator<<(std::ostream& _out, const TransactionType& _in);

// EIP-2930: Access lists
struct AccessListEntry
{
    Address account;
    std::vector<crypto::HashType> storageKeys;
    friend bool operator==(const AccessListEntry& lhs, const AccessListEntry& rhs) noexcept
    {
        return lhs.account == rhs.account && lhs.storageKeys == rhs.storageKeys;
    }
};

// EIP-7702: authorization entry (set_code transactions, Prague+).
struct AuthorizationListEntry
{
    // chainId is a 256-bit value (EIP-7702). It is part of the signed payload
    // (encodeForSign) and of the canonical tx hash, so it must NOT be narrowed:
    // EEST tests chain id 2**256-1 and a truncated uint64 changes the signing
    // hash -> wrong sender recovery.
    u256 chainId{0};
    Address address;  // delegation target
    uint64_t nonce{0};
    uint8_t yParity{0};
    u256 r{0};
    u256 s{0};
    friend bool operator==(
        const AuthorizationListEntry& lhs, const AuthorizationListEntry& rhs) noexcept
    {
        return lhs.chainId == rhs.chainId && lhs.address == rhs.address && lhs.nonce == rhs.nonce &&
               lhs.yParity == rhs.yParity && lhs.r == rhs.r && lhs.s == rhs.s;
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

    // encode for sign, rlp(tx_payload)
    bcos::bytes encodeForSign() const;
    // full RLP (with type byte) — delegates to handlerFor(type).encode
    bcos::bytes encode() const;
    // Decode — delegates to handlerFor(type).decode, propagating decode errors
    bcos::Error::UniquePtr decode(bcos::bytesRef& in, bool withSig = true);
    // tx hash = keccak256(rlp(tx_payload,v,r,s))
    bcos::crypto::HashType txHash() const;
    // hash for sign = keccak256(rlp(tx_payload))
    bcos::crypto::HashType hashForSign() const;
    bcostars::Transaction takeToTarsTransaction();
    uint64_t getSignatureV() const;
    std::string sender() const;
    std::string toString() const noexcept;

    std::optional<uint64_t> chainId{std::nullopt};  // nullopt means a pre-EIP-155 transaction
    TransactionType type{TransactionType::Legacy};
    std::optional<Address> to;
    bcos::bytes data;
    u256 value{0};
    uint64_t nonce{0};
    uint64_t gasLimit{0};
    // EIP-2930: Optional access lists
    std::vector<AccessListEntry> accessList;
    // EIP-1559: Fee market change for ETH 1.0 chain
    u256 maxPriorityFeePerGas{0};  // for legacy tx, it stands for gasPrice
    u256 maxFeePerGas{0};
    // EIP-4844: Shard Blob Transactions
    u256 maxFeePerBlobGas{0};
    h256s blobVersionedHashes;
    // deposit-only (0x7e)
    h256 sourceHash;
    Address from;
    u256 mint{0};
    bool isSystemTx{false};
    // TODO)) blob
    // EIP-7702: Set Code Transactions (Prague+)
    std::vector<AuthorizationListEntry> authorizationList;
    bcos::bytes signatureR;
    bcos::bytes signatureS;
    uint64_t signatureV{0};
};
}  // namespace rpc
namespace codec::rlp
{
Header header(const rpc::AccessListEntry& entry) noexcept;
void encode(bcos::bytes& out, const rpc::AccessListEntry&) noexcept;
size_t length(const rpc::AccessListEntry&) noexcept;

Header header(const rpc::AuthorizationListEntry& entry) noexcept;
void encode(bcos::bytes& out, const rpc::AuthorizationListEntry&) noexcept;
size_t length(const rpc::AuthorizationListEntry&) noexcept;

size_t length(const rpc::Web3Transaction&) noexcept;
void encode(bcos::bytes& out, const rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& in, rpc::AccessListEntry&) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& in, rpc::AuthorizationListEntry&) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& in, rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decodeTransaction(
    bcos::bytesRef& in, rpc::Web3Transaction&, bool withSignature) noexcept;
}  // namespace codec::rlp
}  // namespace bcos
