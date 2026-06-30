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
 * @file EthBlock.cpp
 * @brief EthBlock — RLP encoding, standalone block type
 * @date 2026/6/24
 */
#include "EthBlock.h"
#include "bcos-codec/rlp/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{

// encode — rlp([header, [txs], [uncles], [withdrawals]])
void EthBlock::encode(bcos::bytes& out) const
{
    // Pre-compute sizes for each part
    size_t blockHeaderSize = m_header.encodedLength();

    size_t txsPayload = 0;
    for (auto const& tx : m_rlpTxs)
    {
        txsPayload += tx.size();
    }
    size_t txsListSize = codec::rlp::lengthOfLength(txsPayload) + txsPayload;

    size_t unclesSize = 1;  // always 0xc0

    size_t wdPayload = 0;
    for (auto const& _wd : m_withdrawals)
    {
        wdPayload += _wd.encodedLength();
    }
    size_t wdListSize = codec::rlp::lengthOfLength(wdPayload) + wdPayload;

    size_t blockPayload = blockHeaderSize + txsListSize + unclesSize + wdListSize;
    out.reserve(out.size() + blockPayload + codec::rlp::lengthOfLength(blockPayload));

    // Outer block list header
    codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = blockPayload});

    // 1. Header
    m_header.encode(out);

    // 2. Transactions
    codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = txsPayload});
    for (auto const& tx : m_rlpTxs)
    {
        out.insert(out.end(), tx.begin(), tx.end());
    }

    // 3. Uncles (empty list)
    out.push_back(LIST_HEAD_BASE);

    // 4. Withdrawals
    codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = wdPayload});
    for (auto const& _wd : m_withdrawals)
    {
        _wd.encode(out);
    }
}

bcos::Error::UniquePtr EthBlock::decode(bcos::bytesConstRef _data)
{
    clear();

    auto mutableData = _data.toBytes();
    bcos::bytesRef rest(mutableData.data(), mutableData.size());

    // Outer list
    auto [outerErr, outerHead] = decodeHeader(rest);
    if (outerErr) 
    { 
        return std::move(outerErr); 
    }
    if (!outerHead.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedString, "EthBlock::decode: expected outer list");
    }

    // 1. Header
    {
        auto [itemErr, blockHeaderItem] = codec::rlp::decodeItemRlp(rest);
        if (itemErr) { return std::move(itemErr); }
        if (auto e = m_header.decode(blockHeaderItem); e != nullptr)
        {
            return e;
        }
    }

    // 2. Transactions: decode each item header, take full item bytes (header + payload)
    {
        auto [txErr, txsHead] = decodeHeader(rest);
        if (txErr) { return std::move(txErr); }
        if (!txsHead.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedString, "EthBlock::decode: txs must be list");
        }
        auto const* end = rest.data() + txsHead.payloadLength;
        while (rest.data() < end)
        {
            auto [txErr, txItem] = codec::rlp::decodeItemRlp(rest);
            if (txErr) 
            { 
                return std::move(txErr); 
            }
            appendTransaction(
                bcos::bytes(txItem.data(), txItem.data() + txItem.size()));
        }
    }

    // 3. Uncles list (deprecated, skip)
    {
        auto [uErr, uHead] = decodeHeader(rest);
        if (uErr) { return std::move(uErr); }
        if (!uHead.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedString, "EthBlock::decode: uncles must be list");
        }
        rest = rest.getCroppedData(uHead.payloadLength);
    }

    // 4. Withdrawals list
    {
        auto [wErr, wHead] = decodeHeader(rest);
        if (wErr) { return std::move(wErr); }
        if (!wHead.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedString, "EthBlock::decode: withdrawals must be list");
        }
        auto const* end = rest.data() + wHead.payloadLength;
        while (rest.data() < end)
        {
            auto [wdErr, wdItem] = codec::rlp::decodeItemRlp(rest);
            if (wdErr) 
            { 
                return std::move(wdErr); 
            }
            EthWithdrawal wd;
            wd.decode(wdItem);
            m_withdrawals.push_back(std::move(wd));
        }
    }

    return nullptr;
}

size_t EthBlock::size() const
{
    size_t size = m_header.size();
    for (auto const& tx : m_rlpTxs)
    {
        size += tx.size();
    }
    for (auto const& wd : m_withdrawals)
    {
        size += sizeof(wd.index) + sizeof(wd.validatorIndex) + bcos::Address::SIZE + sizeof(wd.amount);
    }
    return size;
}

void EthBlock::appendTransaction(const bcos::bytes& _rlp)
{
    m_txHashes.push_back(bcos::crypto::keccak256Hash(bcos::ref(_rlp)));
    m_rlpTxs.push_back(_rlp);
}

void EthBlock::appendTransaction(bcos::bytes&& _rlp)
{
    m_txHashes.push_back(bcos::crypto::keccak256Hash(bcos::ref(_rlp)));
    m_rlpTxs.push_back(std::move(_rlp));
}

void EthBlock::setTransactionRlp(size_t _index, const bcos::bytes& _rlp)
{
    if (_index >= m_rlpTxs.size())
    {
        m_rlpTxs.resize(_index + 1);
        m_txHashes.resize(_index + 1);
    }
    m_rlpTxs[_index] = _rlp;
    m_txHashes[_index] = bcos::crypto::keccak256Hash(bcos::ref(_rlp));
}

void EthBlock::setTransactionHash(size_t _index, const bcos::crypto::HashType& _hash)
{
    if (_index >= m_txHashes.size())
    {
        m_txHashes.resize(_index + 1);
    }
    m_txHashes[_index] = _hash;
}

void EthBlock::setWithdrawal(size_t _index, const EthWithdrawal& _wd)
{
    if (_index >= m_withdrawals.size())
    {
        m_withdrawals.resize(_index + 1);
    }
    m_withdrawals[_index] = _wd;
}

void EthBlock::clear()
{
    m_header = EthBlockHeader{};
    m_txHashes.clear();
    m_rlpTxs.clear();
    m_withdrawals.clear();
}

}  // namespace bcos::protocol
