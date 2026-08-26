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
 * @file EthBlockBody.cpp
 * @brief EthBlock — Ethereum block RLP codec implementation
 * @date 2026/8/18
 */
#include "EthBlockBody.h"

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{
bcos::Error::UniquePtr EthBlock::rlpEncode(bcos::bytes& out) const
{
    // Enforce the same invariants the decoder does (empty or too-short transaction
    // elements) so encode and decode accept the same set — a silently dropped element
    // would produce a wire form that does not represent the object it was given.
    for (auto const& tx : m_data.transactions)
    {
        if (tx.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
                "EthBlock::rlpEncode: empty transaction element");
        }
        if (tx.front() >= LIST_HEAD_BASE)
        {
            // Mirror detail::decodeTx's lower bound: a legacy transaction list has nine
            // fields, so a declared payload shorter than 9 bytes cannot be one. Also
            // require the declared list to span the WHOLE element (payloadLength ==
            // view.size() after the prefix) so two concatenated minimal lists cannot
            // pass as one element — encode-then-decode would then yield a different set.
            bytesRef view(const_cast<bcos::byte*>(tx.data()), tx.size());
            auto [err, header] = bcos::codec::rlp::decodeHeader(view);
            if (err || !header.isList || header.payloadLength < 9 ||
                header.payloadLength != view.size())
            {
                return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedListElements,
                    "EthBlock::rlpEncode: invalid legacy transaction element");
            }
        }
        else
        {
            // Typed arm: mirror detail::decodeTx — the type byte must be 0x01..0x7f and the
            // payload must be long enough to be a real EIP-2718 typed transaction.
            if (tx.size() < 10 || tx.front() == 0 || tx.front() >= BYTES_HEAD_BASE)
            {
                return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
                    "EthBlock::rlpEncode: invalid typed transaction element");
            }
        }
    }
    codec::rlp::encode(out, m_data);
    return nullptr;
}

bcos::Error::UniquePtr EthBlock::rlpDecode(bcos::bytesConstRef data)
{
    // The codec's decode only advances a view cursor and never writes the buffer, so
    // take the view directly; the const_cast is confined to this read-only entry point.
    bytesRef in(const_cast<bcos::byte*>(data.data()), data.size());
    if (auto err = codec::rlp::decode(in, m_data))
    {
        return err;
    }
    // geth's rlp.DecodeBytes rejects trailing bytes (ErrMoreThanOneValue); mirror that so
    // two distinct wire encodings cannot map to the same decoded object.
    if (!in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "trailing bytes after top-level RLP item");
    }
    return nullptr;
}
}  // namespace bcos::protocol
