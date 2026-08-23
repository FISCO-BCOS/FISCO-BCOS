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
 * @file EthReceipt.cpp
 * @brief EthReceipt — Ethereum transaction-receipt RLP codec implementation
 * @date 2026/8/18
 */
#include "EthReceipt.h"
#include <bcos-protocol/TransactionStatus.h>
#include <boost/lexical_cast.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::codec::rlp
{
// log bloom serializes as a raw 256-byte string (0xb9 0x01 0x00 || 256 bytes); the shared
// codec has no overload for std::array<byte,256> on the encode/length side (only on decode),
// so convert to a bytesConstRef explicitly, mirroring EthBlockHeader::rlpEncode.
inline bcos::bytesConstRef bloomRef(const bcos::Bloom& _bloom) noexcept
{
    return {_bloom.data(), _bloom.size()};
}

// A pre-Byzantium (pre-EIP-658) legacy receipt's first item is a 32-byte state root.
constexpr size_t POST_STATE_ROOT_SIZE = 32;

size_t length(const protocol::EthReceiptData& _receipt) noexcept
{
    // status is uint8_t; encode/length take it as uint64_t (identical RLP bytes, and avoids
    // the never-defined toCompactBigEndian(byte) overload in bcos-utilities, which -Werror
    // rejects — same reason Web3Transaction.cpp casts yParity before encoding).
    auto const statusAsU64 = static_cast<uint64_t>(_receipt.status);
    if (_receipt.type != 0)
    {
        // 1-byte EIP-2718 type prefix followed by the inner status list.
        return 1 + length(statusAsU64, _receipt.cumulativeGasUsed, bloomRef(_receipt.logsBloom),
                       _receipt.logs);
    }
    if (_receipt.postState.has_value())
    {
        return length(*_receipt.postState, _receipt.cumulativeGasUsed, bloomRef(_receipt.logsBloom),
            _receipt.logs);
    }
    return length(
        statusAsU64, _receipt.cumulativeGasUsed, bloomRef(_receipt.logsBloom), _receipt.logs);
}

void encode(bcos::bytes& _out, const protocol::EthReceiptData& _receipt) noexcept
{
    // See length(): cast status to uint64_t before encoding (identical RLP bytes).
    auto const statusAsU64 = static_cast<uint64_t>(_receipt.status);
    if (_receipt.type != 0)
    {
        // EIP-2718: single-byte type prefix OUTSIDE the RLP list.
        _out.push_back(_receipt.type);
        encode(_out, statusAsU64, _receipt.cumulativeGasUsed, bloomRef(_receipt.logsBloom),
            _receipt.logs);
        return;
    }
    if (_receipt.postState.has_value())
    {
        encode(_out, *_receipt.postState, _receipt.cumulativeGasUsed, bloomRef(_receipt.logsBloom),
            _receipt.logs);
        return;
    }
    encode(
        _out, statusAsU64, _receipt.cumulativeGasUsed, bloomRef(_receipt.logsBloom), _receipt.logs);
}

bcos::Error::UniquePtr decode(bcos::bytesRef& _in, protocol::EthReceiptData& _receipt) noexcept
{
    if (_in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input data is too short");
    }

    // EIP-2718 typed receipt: a single-byte type prefix (0x01..0x7f) directly followed by the
    // RLP list. A byte >= 0x80 would be the head of an RLP string/list, i.e. a legacy receipt.
    if (_in[0] > 0 && _in[0] < BYTES_HEAD_BASE)
    {
        _receipt.type = _in[0];
        _in = _in.getCroppedData(1);
        _receipt.postState.reset();
        // The multi-arg decode expects a list header here: [status, cumGas, bloom, logs].
        return decode(
            _in, _receipt.status, _receipt.cumulativeGasUsed, _receipt.logsBloom, _receipt.logs);
    }

    _receipt.type = 0;
    // Legacy receipt: a list whose first item is either the post-transaction state root
    // (a 32-byte string, pre-Byzantium) or the status byte (Byzantium+, EIP-658; RLP empty
    // string for 0, single byte for 1). Decode it as raw bytes first, then disambiguate by
    // length — the RLP header of the first item must not be peeked via decodeHeader (that
    // would consume the view and misalign the remaining items).
    auto&& [error, listHeader] = decodeHeader(_in);
    if (error)
    {
        return std::move(error);
    }
    if (!listHeader.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }
    auto payloadView = _in.getCroppedData(0, listHeader.payloadLength);

    bcos::bytes firstItem;
    if (auto e = decode(payloadView, firstItem); e != nullptr)
    {
        return e;
    }
    if (firstItem.size() == POST_STATE_ROOT_SIZE)
    {
        _receipt.postState = bcos::h256(bcos::bytesConstRef(firstItem.data(), firstItem.size()));
        _receipt.status = 0;
    }
    else if (firstItem.size() <= 1)
    {
        _receipt.postState.reset();
        _receipt.status = firstItem.empty() ? 0 : firstItem[0];
    }
    else
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedLength, "Unexpected status/postState item length");
    }

    // Remaining items are top-level members of the receipt list (not a nested list).
    if (auto e =
            decodeItems(payloadView, _receipt.cumulativeGasUsed, _receipt.logsBloom, _receipt.logs);
        e != nullptr)
    {
        return e;
    }
    if (!payloadView.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Unexpected list elements");
    }
    _in = _in.getCroppedData(listHeader.payloadLength);
    return nullptr;
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
void EthReceipt::rlpEncode(bcos::bytes& out) const
{
    codec::rlp::encode(out, m_data);
}

bcos::Error::UniquePtr EthReceipt::rlpDecode(bcos::bytesConstRef data)
{
    // The codec's decode takes a mutable bytesRef& (it advances a view cursor); the bytes
    // themselves are never written, so a single copy into a mutable buffer is enough.
    auto mutableData = data.toBytes();
    bytesRef in(mutableData.data(), mutableData.size());
    return codec::rlp::decode(in, m_data);
}

EthReceiptData toEthReceiptData(TransactionReceipt const& receipt, uint8_t txType)
{
    EthReceiptData eth;
    eth.type = txType;
    // The BCOS receipt carries the FISCO TransactionStatus convention (None = 0 = success,
    // per mapEvmcStatusToBcosStatus); the Ethereum receipt commits EIP-658 status 1 for
    // success and 0 for every failure.
    eth.status =
        (receipt.status() == static_cast<int32_t>(protocol::TransactionStatus::None)) ? 1 : 0;
    eth.cumulativeGasUsed =
        boost::lexical_cast<bcos::u256>(std::string(receipt.cumulativeGasUsed()));
    auto const bloom = receipt.logsBloom();
    if (bloom.size() == eth.logsBloom.size())
    {
        std::memcpy(eth.logsBloom.data(), bloom.data(), bloom.size());
    }
    eth.logs.reserve(receipt.logEntries().size());
    for (auto const& log : receipt.logEntries())
    {
        EthLogData ethLog;
        auto const addr = log.address();
        ethLog.address = Address(
            bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(addr.data()), addr.size()));
        for (auto const& topic : log.topics())
        {
            ethLog.topics.push_back(topic);
        }
        ethLog.data = log.data().toBytes();
        eth.logs.push_back(std::move(ethLog));
    }
    return eth;
}
}  // namespace bcos::protocol
