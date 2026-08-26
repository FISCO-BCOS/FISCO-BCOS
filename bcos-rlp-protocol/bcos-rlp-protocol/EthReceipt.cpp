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
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <cctype>
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
        // geth's setStatus accepts only 0x01 (success), empty (failure), or a 32-byte
        // postState; any other single byte is "invalid receipt status" (EIP-658).
        if (firstItem.size() == 1 && firstItem[0] != 1)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::InvalidFieldset, "invalid receipt status byte");
        }
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
bcos::Error::UniquePtr EthReceipt::rlpEncode(bcos::bytes& out) const
{
    // A type byte >= 0x80 would be written verbatim and then misread by the decoder
    // as a legacy RLP item head; reject it here so encode/decode accept the same set
    // (mirrors EthBlock::rlpEncode's typed-arm check).
    if (m_data.type >= BYTES_HEAD_BASE)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
            "EthReceipt::rlpEncode: invalid EIP-2718 type byte");
    }
    codec::rlp::encode(out, m_data);
    return nullptr;
}

bcos::Error::UniquePtr EthReceipt::rlpDecode(bcos::bytesConstRef data)
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

bcos::Error::UniquePtr toEthReceiptData(
    TransactionReceipt const& receipt, uint8_t txType, EthReceiptData& eth)
{
    // Reset the destination at entry so no stale field (notably postState) can
    // survive from a previous call when the caller reuses the object.
    eth = EthReceiptData{};
    // EIP-2718 confines the transaction type to 0x00..0x7f; a type byte >= 0x80 would be
    // written verbatim as the prefix and then read back as the head of a legacy RLP item,
    // so the receipt would not round-trip. Fail closed like the other inputs here.
    if (txType >= BYTES_HEAD_BASE)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
            "toEthReceiptData: invalid EIP-2718 type " + std::to_string(txType));
    }
    eth.type = txType;
    // The BCOS receipt carries the FISCO TransactionStatus convention (None = 0 = success,
    // per mapEvmcStatusToBcosStatus); the Ethereum receipt commits EIP-658 status 1 for
    // success and 0 for every failure.
    eth.status =
        (receipt.status() == static_cast<int32_t>(protocol::TransactionStatus::None)) ? 1 : 0;
    // cumulativeGasUsed must parse to a number: it feeds the receipts root, so a
    // missing/non-numeric value fails closed (returns an Error) rather than
    // substituting 0. Producers write decimal (transaction-scheduler) or
    // "0x"+minimal hex (opstack-executor), so parse both shapes explicitly
    // (mirrors jsonStringToInt) instead of relying on boost::lexical_cast.
    auto const cumStr = std::string(receipt.cumulativeGasUsed());
    if (cumStr.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::InputTooShort, "toEthReceiptData: empty cumulativeGasUsed");
    }
    // Validate the string shape before parsing: boost's implicit base rules (a leading 0
    // selects octal) and an empty 0x payload would otherwise silently produce a wrong
    // value, which feeds the receipts root.
    bool const hexForm =
        cumStr.size() >= 2 && cumStr[0] == '0' && (cumStr[1] == 'x' || cumStr[1] == 'X');
    auto const digits = hexForm ? cumStr.substr(2) : cumStr;
    auto const isDecimalDigit = [](char c) { return c >= '0' && c <= '9'; };
    auto const isHexDigit = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    // Reject an empty digit run ("0x" with no digits): all_of over an empty range
    // returns true, and fromHex("")/u256 would silently yield 0.
    if (digits.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InvalidFieldset,
            "toEthReceiptData: empty cumulativeGasUsed payload: " + cumStr);
    }
    // A u256 holds at most 256 bits: more hex digits than 64 (or decimal digits than 78)
    // would wrap modulo 2^256 under boost's unchecked policy — bound before parsing so the
    // value can never silently wrap into a wrong receipts-root input.
    if (hexForm ? digits.size() > 64 : digits.size() > 78)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedLength,
            "toEthReceiptData: cumulativeGasUsed exceeds 256 bits: " + cumStr);
    }
    bool const shapeOk = hexForm ? std::all_of(digits.begin(), digits.end(), isHexDigit) :
                                   std::all_of(digits.begin(), digits.end(), isDecimalDigit);
    if (!shapeOk)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InvalidFieldset,
            "toEthReceiptData: non-numeric cumulativeGasUsed: " + cumStr);
    }
    try
    {
        if (hexForm)
        {
            eth.cumulativeGasUsed = bcos::fromBigEndian<bcos::u256>(bcos::fromHex(digits));
        }
        else
        {
            // Normalise the decimal form so boost's implicit-base rule (a leading 0
            // selects octal) can never fire: strip leading zeros, keep at least one digit.
            auto decimalDigits = digits;
            auto const firstNonZero = decimalDigits.find_first_not_of('0');
            if (firstNonZero == std::string::npos)
            {
                decimalDigits = "0";
            }
            else if (firstNonZero > 0)
            {
                decimalDigits.erase(0, firstNonZero);
            }
            // 2^256-1 has 78 decimal digits; 78-digit strings above it still truncate
            // silently under boost's unchecked u256, so reject them lexicographically
            // (equal-length digit strings compare correctly as strings).
            if (decimalDigits.size() == 78 && decimalDigits >
                                                  "115792089237316195423570985008687907853269984665"
                                                  "640564039457584007913129639935")
            {
                return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedLength,
                    "toEthReceiptData: cumulativeGasUsed exceeds 256 bits: " + cumStr);
            }
            eth.cumulativeGasUsed = bcos::u256(decimalDigits);
        }
    }
    catch (std::exception const&)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InvalidFieldset,
            "toEthReceiptData: non-numeric cumulativeGasUsed: " + cumStr);
    }
    auto const bloom = receipt.logsBloom();
    if (bloom.size() == eth.logsBloom.size())
    {
        std::memcpy(eth.logsBloom.data(), bloom.data(), bloom.size());
    }
    else
    {
        // Fail closed: the bloom feeds the receipts root, so a substituted bloom would
        // silently corrupt the trie. Surface the fault instead of logging and continuing.
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedLength,
            "toEthReceiptData: logsBloom size mismatch: " + std::to_string(bloom.size()) +
                " != " + std::to_string(eth.logsBloom.size()));
    }
    eth.logs.reserve(receipt.logEntries().size());
    for (auto const& log : receipt.logEntries())
    {
        EthLogData ethLog;
        auto const& addr = log.address();
        if (addr.size() == 20)
        {
            // Raw 20-byte address.
            ethLog.address = Address(
                bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(addr.data()), addr.size()));
        }
        else if (addr.size() == 40)
        {
            // In-tree producers (HostContext) store the address as 40 ASCII hex
            // chars; decode as hex — FixedBytes' default AlignRight would
            // otherwise truncate the ASCII bytes to the last 20 of them. Validate the
            // charset first: FixedBytes FromHex throws on non-hex characters outside
            // the enclosing try, which would break the Error-return contract.
            auto const addrStr =
                std::string(reinterpret_cast<const char*>(addr.data()), addr.size());
            if (!std::all_of(addrStr.begin(), addrStr.end(),
                    [](unsigned char c) { return std::isxdigit(c) != 0; }))
            {
                return BCOS_ERROR_UNIQUE_PTR(
                    DecodingError::InvalidFieldset, "toEthReceiptData: non-hex log address");
            }
            ethLog.address = Address(addrStr, Address::FromHex);
        }
        else
        {
            return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedLength,
                "toEthReceiptData: log address length " + std::to_string(addr.size()) +
                    " (expected 20 raw bytes or 40 hex chars)");
        }
        for (auto const& topic : log.topics())
        {
            ethLog.topics.push_back(topic);
        }
        ethLog.data = log.data().toBytes();
        eth.logs.push_back(std::move(ethLog));
    }
    return nullptr;
}
}  // namespace bcos::protocol
