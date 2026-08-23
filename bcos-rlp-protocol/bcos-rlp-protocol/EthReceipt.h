/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EthReceipt.h
 * @brief Ethereum transaction-receipt RLP codec (EIP-2718 typed + legacy status/postState)
 * @date 2026/8/18
 */
#pragma once

#include "EthLog.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <optional>
#include <vector>

namespace bcos::protocol
{
// Ethereum transaction receipt.
//
// Serialization (EIP-2718):
//   - typed receipt:  <type> || rlp([status, cumulativeGasUsed, logsBloom, logs])
//     where <type> is a single byte 0x01..0x04 (0x7e on OP-stack L2 deposit receipts).
//   - legacy receipt: rlp([status, cumulativeGasUsed, logsBloom, logs])
//     (Byzantium+, EIP-658) or, pre-Byzantium, rlp([postStateRoot, cumulativeGasUsed,
//     logsBloom, logs]).
//
// The receiptsRoot trie commits to these encodings, so any divergence here changes the
// receipts trie root and invalidates the block hash on real Ethereum chains.
struct EthReceiptData
{
    // EIP-2718 transaction type of the receipt; 0 = legacy (no type prefix).
    uint8_t type{0};
    // Pre-Byzantium (pre-EIP-658) legacy receipts carry the post-transaction state root
    // (32 bytes) as the first item instead of a status byte. Exactly one of postState /
    // status is meaningful: postState is only set for pre-Byzantium legacy receipts.
    std::optional<bcos::h256> postState;
    // Transaction execution status: 0 = failure, 1 = success (EIP-658).
    uint8_t status{0};
    bcos::u256 cumulativeGasUsed{0};
    // 256-byte bloom filter over the receipt's logs (EIP-234/2997 semantics).
    bcos::Bloom logsBloom{};
    std::vector<EthLogData> logs;

    bool operator==(const EthReceiptData& rhs) const
    {
        return type == rhs.type && postState == rhs.postState && status == rhs.status &&
               cumulativeGasUsed == rhs.cumulativeGasUsed && logsBloom == rhs.logsBloom &&
               logs == rhs.logs;
    }
    bool operator!=(const EthReceiptData& rhs) const { return !(*this == rhs); }
};

// Class wrapper following the EthBlockHeader pattern: rlpEncode/rlpDecode plus the
// codec::rlp overloads that let EthReceiptData live inside lists (block bodies, ...).
class EthReceipt
{
public:
    EthReceipt() = default;
    explicit EthReceipt(EthReceiptData data) : m_data(std::move(data)) {}

    void rlpEncode(bcos::bytes& out) const;
    // Decodes a single receipt item (possibly with an EIP-2718 type prefix) from `data`.
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    const EthReceiptData& data() const { return m_data; }
    EthReceiptData& data() { return m_data; }

private:
    EthReceiptData m_data;
};

/// Convert a BCOS transaction receipt (FISCO TransactionStatus semantics) plus its EIP-2718
/// transaction type into the Ethereum receipt data struct — the value that feeds the
/// receiptsRoot trie on Ethereum-compatible (executor_version >= 2) chains. The bcos status
/// maps None (0, success) -> 1 (EIP-658 success) and every other status -> 0.
EthReceiptData toEthReceiptData(
    TransactionReceipt const& receipt, uint8_t txType);
}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
// Overloads so EthReceiptData works as an item inside the generic list/vector codecs.
// Implementation lives in EthReceipt.cpp (it needs the type-prefix and status/postState
// disambiguation logic, which is not header-inline).
size_t length(const protocol::EthReceiptData& _receipt) noexcept;
void encode(bcos::bytes& _out, const protocol::EthReceiptData& _receipt) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& _in, protocol::EthReceiptData& _receipt) noexcept;
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
// ADL-visible delegators (see EthLog.h): let EthReceiptData participate in
// std::vector<EthReceiptData> / variadic-list encode/decode.
inline size_t length(const EthReceiptData& _receipt) noexcept
{
    return codec::rlp::length(_receipt);
}
inline void encode(bcos::bytes& _out, const EthReceiptData& _receipt) noexcept
{
    codec::rlp::encode(_out, _receipt);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, EthReceiptData& _receipt) noexcept
{
    return codec::rlp::decode(_in, _receipt);
}
}  // namespace bcos::protocol
