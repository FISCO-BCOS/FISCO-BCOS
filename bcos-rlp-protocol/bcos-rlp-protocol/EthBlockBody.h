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
#include <bcos-utilities/Error.h>
#include <optional>
#include <vector>

namespace bcos::protocol
{
// Ethereum block body (the part of a block not committed to by the header hash).
// RLP form (yellow paper, Appendix B):
//   pre-Shanghai: rlp([header, transactions, ommers])
//   Shanghai+  :  rlp([header, transactions, ommers, withdrawals])   (EIP-4895)
// `transactions` holds the raw EIP-2718 encodings (opaque bytes); `ommers` are uncle headers
// (always empty on PoS chains); `withdrawals` is nullopt for pre-Shanghai bodies and an
// (possibly empty) list for Shanghai+ bodies — the presence of the field itself is fork
// significant, so it is an optional<vector>, not a bare vector.
struct EthBlockBodyData
{
    EthBlockHeaderData header;
    std::vector<bcos::bytes> transactions;           // opaque EIP-2718 encoded transactions
    std::vector<EthBlockHeaderData> ommers;          // uncle headers (empty on PoS)
    std::optional<std::vector<EthWithdrawalData>> withdrawals;  // nullopt = pre-Shanghai

    bool operator==(const EthBlockBodyData& rhs) const
    {
        return header == rhs.header && transactions == rhs.transactions &&
               ommers == rhs.ommers && withdrawals == rhs.withdrawals;
    }
    bool operator!=(const EthBlockBodyData& rhs) const { return !(*this == rhs); }
};

// Class wrapper following the EthBlockHeader pattern: rlpEncode/rlpDecode plus the
// codec::rlp overloads that let EthBlockBodyData work as an item in larger structures.
class EthBlockBody
{
public:
    EthBlockBody() = default;
    explicit EthBlockBody(EthBlockBodyData data) : m_data(std::move(data)) {}

    void rlpEncode(bcos::bytes& out) const;
    // Decodes a single block body (a 3- or 4-element list) from `data`.
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    const EthBlockBodyData& data() const { return m_data; }
    EthBlockBodyData& data() { return m_data; }

private:
    EthBlockBodyData m_data;
};
}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
// Overloads so EthBlockBodyData works as an item inside the generic list/vector codecs.
inline size_t length(const protocol::EthBlockBodyData& _body) noexcept
{
    if (_body.withdrawals.has_value())
    {
        return length(_body.header, _body.transactions, _body.ommers, *_body.withdrawals);
    }
    return length(_body.header, _body.transactions, _body.ommers);
}
inline void encode(bcos::bytes& _out, const protocol::EthBlockBodyData& _body) noexcept
{
    if (_body.withdrawals.has_value())
    {
        encode(_out, _body.header, _body.transactions, _body.ommers, *_body.withdrawals);
        return;
    }
    encode(_out, _body.header, _body.transactions, _body.ommers);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, protocol::EthBlockBodyData& _body) noexcept
{
    // The optional-withdrawals argument doubles as the pre-Shanghai marker: when the body
    // list is exhausted after header/transactions/ommers, the optional decode resets it to
    // nullopt; otherwise the fourth item is decoded as the withdrawals list.
    return decode(_in, _body.header, _body.transactions, _body.ommers, _body.withdrawals);
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
// ADL-visible delegators (see EthLog.h): let EthBlockBodyData participate in
// variadic-list encode/decode.
inline size_t length(const EthBlockBodyData& _body) noexcept
{
    return codec::rlp::length(_body);
}
inline void encode(bcos::bytes& _out, const EthBlockBodyData& _body) noexcept
{
    codec::rlp::encode(_out, _body);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, EthBlockBodyData& _body) noexcept
{
    return codec::rlp::decode(_in, _body);
}
}  // namespace bcos::protocol
