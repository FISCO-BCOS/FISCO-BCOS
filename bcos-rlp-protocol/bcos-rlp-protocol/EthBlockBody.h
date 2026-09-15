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
 * @file EthBlockBody.h
 * @brief Ethereum block-body RLP codec
 *        (rlp([header, transactions, ommers/uncles, withdrawals?]) — EIP-4895 for withdrawals)
 * @date 2026/8/18
 */
#pragma once

#include "EthBlockHeader.h"
#include "EthWithdrawal.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <optional>
#include <vector>

namespace bcos::protocol
{
// Ethereum block (geth's types.Block): rlp([header, transactions, ommers]) — the
// ENTIRE block, header included. It is NOT the header-less devp2p BlockBodies element
// ([transactions, ommers, withdrawals?]); a body codec for that wire form belongs with
// the devp2p eth protocol.
// RLP form (yellow paper, Appendix B):
//   pre-Shanghai: rlp([header, transactions, ommers])
//   Shanghai+  :  rlp([header, transactions, ommers, withdrawals])   (EIP-4895)
// `transactions` holds the raw EIP-2718 encodings (opaque bytes); `ommers` are uncle headers
// (always empty on PoS chains); `withdrawals` is nullopt for pre-Shanghai bodies and an
// (possibly empty) list for Shanghai+ bodies — the presence of the field itself is fork
// significant, so it is an optional<vector>, not a bare vector.
struct EthBlockData
{
    EthBlockHeaderData header;
    std::vector<bcos::bytes> transactions;   // opaque EIP-2718 encoded transactions
    std::vector<EthBlockHeaderData> ommers;  // uncle headers (empty on PoS)
    std::optional<std::vector<EthWithdrawalData>> withdrawals;  // nullopt = pre-Shanghai

    bool operator==(const EthBlockData& rhs) const
    {
        return header == rhs.header && transactions == rhs.transactions && ommers == rhs.ommers &&
               withdrawals == rhs.withdrawals;
    }
    bool operator!=(const EthBlockData& rhs) const { return !(*this == rhs); }
};

// Class wrapper following the EthBlockHeader pattern: rlpEncode/rlpDecode plus the
// codec::rlp overloads that let EthBlockData work as an item in larger structures.
class EthBlock
{
public:
    EthBlock() = default;
    explicit EthBlock(EthBlockData data) : m_data(std::move(data)) {}

    // Encode the block via EthBlock::rlpEncode — that entry point enforces the decoder's
    // invariants (empty / too-short / invalid-type elements rejected, via thrown
    // codec::rlp::RlpEncodeException) so no element is silently dropped into a hash input.
    // The codec::rlp::encode overload below performs no such check.
    void rlpEncode(bcos::bytes& out) const;
    // Decodes a single block body (a 3- or 4-element list) from `data`.
    // Throws codec::rlp::RlpDecodeException on malformed input.
    void rlpDecode(bcos::bytesConstRef data);

    const EthBlockData& data() const { return m_data; }
    EthBlockData& data() { return m_data; }

private:
    EthBlockData m_data;
};
}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
namespace detail
{
// Decode one transaction from a block-body transactions list into its opaque
// EIP-2718 bytes: a typed tx (RLP string) loses its string prefix (0xNN||payload
// is returned, matching what encode wraps); a legacy tx (RLP list) is taken
// whole. Malformed input (e.g. a bare type byte 0x00) is rejected by a thrown
// RlpDecodeException.
void decodeTx(bcos::bytesRef& _in, bcos::bytes& _out);
}  // namespace detail

// Overloads so EthBlockData works as an item inside the generic list/vector codecs.
// The header codec (EthBlockHeaderData) is the single source of truth for the header
// field order; the transactions list is built by hand so legacy txs stay raw lists.
// decode throws RlpDecodeException on malformed input.
size_t length(const protocol::EthBlockData& _body) noexcept;
void encode(bcos::bytes& _out, const protocol::EthBlockData& _body) noexcept;
void decode(bcos::bytesRef& _in, protocol::EthBlockData& _body);
}  // namespace bcos::codec::rlp
