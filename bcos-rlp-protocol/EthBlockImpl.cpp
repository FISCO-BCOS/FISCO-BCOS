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
 * @file EthBlockImpl.cpp
 * @brief EthBlockImpl — RLP encoding, standalone block type
 * @date 2026/6/24
 */
#include "EthBlockImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{

// encode — rlp([header, [txs], [uncles], [withdrawals]])
void EthBlockImpl::encode(bcos::bytes& out) const
{
    // 1. Header RLP
    bcos::bytes headerRlp;
    m_header.encode(headerRlp);

    // 2. Transactions list (each item is a full RLP-encoded tx)
    std::vector<bcos::bytes> encodedTxs;
    encodedTxs.reserve(m_rlpTxs.size());
    for (auto const& tx : m_rlpTxs)
    {
        encodedTxs.push_back(tx);
    }
    bcos::bytes txsRlp;
    codec::rlp::encodeListWithItems(txsRlp, encodedTxs);

    // 3. Uncles list (deprecated, always empty)
    bcos::bytes unclesRlp;
    unclesRlp.push_back(LIST_HEAD_BASE);

    // 4. Withdrawals list
    bcos::bytes wdsRlp;
    {
        std::vector<bcos::bytes> wdRlps;
        wdRlps.reserve(m_withdrawals.size());
        for (auto const& wd : m_withdrawals)
        {
            bcos::bytes wRlp;
            wd.encode(wRlp);
            wdRlps.push_back(std::move(wRlp));
        }
        codec::rlp::encodeListWithItems(wdsRlp, wdRlps);
    }

    // Outer block list
    size_t blockPayload = headerRlp.size() + txsRlp.size() + unclesRlp.size() + wdsRlp.size();
    codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = blockPayload});
    out.insert(out.end(), headerRlp.begin(), headerRlp.end());
    out.insert(out.end(), txsRlp.begin(), txsRlp.end());
    out.insert(out.end(), unclesRlp.begin(), unclesRlp.end());
    out.insert(out.end(), wdsRlp.begin(), wdsRlp.end());
}

// decode — rlp([header, [txs], [uncles], [withdrawals]])
bcos::Error::UniquePtr EthBlockImpl::decode(bcos::bytesConstRef _data)
{
    clear();

    auto mutableData = _data.toBytes();
    bcos::bytesRef ref(mutableData.data(), mutableData.size());

    auto [err, head] = decodeHeader(ref);
    if (err) { return std::move(err); }
    if (!head.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedString, "EthBlock::decode: expected outer list");
    }

    // 1. Header
    {
        auto [hErr, hHead] = decodeHeader(ref);
        if (hErr) { return std::move(hErr); }
        if (!hHead.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedString, "EthBlock::decode: header must be list");
        }

        bcos::bytes hRlp;
        if (hHead.payloadLength < LENGTH_THRESHOLD)
        {
            hRlp.push_back(static_cast<bcos::byte>(LIST_HEAD_BASE + hHead.payloadLength));
        }
        else
        {
            auto lenBytes = bcos::toCompactBigEndian(hHead.payloadLength);
            hRlp.push_back(static_cast<bcos::byte>(LONG_LIST_HEAD_BASE + lenBytes.size()));
            hRlp.insert(hRlp.end(), lenBytes.begin(), lenBytes.end());
        }
        hRlp.insert(hRlp.end(), ref.data(), ref.data() + hHead.payloadLength);

        if (auto e = m_header.decode(bcos::ref(hRlp)); e != nullptr)
        {
            return e;
        }
        ref = ref.getCroppedData(hHead.payloadLength);
    }

    // 2. Transactions list
    {
        auto [tErr, tHead] = decodeHeader(ref);
        if (tErr) { return std::move(tErr); }
        if (!tHead.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedString, "EthBlock::decode: txs must be list");
        }

        auto const* end = ref.data() + tHead.payloadLength;
        while (ref.data() < end)
        {
            auto [iErr, iHead] = decodeHeader(ref);
            if (iErr) { return std::move(iErr); }

            bcos::bytes txRlp;
            if (iHead.payloadLength < LENGTH_THRESHOLD)
            {
                auto prefix = static_cast<bcos::byte>(
                    (iHead.isList ? LIST_HEAD_BASE : BYTES_HEAD_BASE) + iHead.payloadLength);
                txRlp.push_back(prefix);
            }
            else
            {
                auto lenBytes = bcos::toCompactBigEndian(iHead.payloadLength);
                auto prefix = static_cast<bcos::byte>(
                    (iHead.isList ? LONG_LIST_HEAD_BASE : LONG_BYTES_HEAD_BASE) + lenBytes.size());
                txRlp.push_back(prefix);
                txRlp.insert(txRlp.end(), lenBytes.begin(), lenBytes.end());
            }
            txRlp.insert(txRlp.end(), ref.data(), ref.data() + iHead.payloadLength);

            m_txHashes.push_back(bcos::crypto::keccak256Hash(bcos::ref(txRlp)));
            m_rlpTxs.push_back(std::move(txRlp));
            ref = ref.getCroppedData(iHead.payloadLength);
        }
    }

    // 3. Uncles list (deprecated, skip)
    {
        auto [uErr, uHead] = decodeHeader(ref);
        if (uErr) { return std::move(uErr); }
        if (!uHead.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedString, "EthBlock::decode: uncles must be list");
        }
        ref = ref.getCroppedData(uHead.payloadLength);
    }

    // 4. Withdrawals list
    {
        auto [wErr, wHead] = decodeHeader(ref);
        if (wErr) { return std::move(wErr); }
        if (!wHead.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedString, "EthBlock::decode: withdrawals must be list");
        }

        auto const* end = ref.data() + wHead.payloadLength;
        while (ref.data() < end)
        {
            auto [whErr, whHead] = decodeHeader(ref);
            if (whErr) { return std::move(whErr); }
            if (!whHead.isList)
            {
                return BCOS_ERROR_UNIQUE_PTR(
                    DecodingError::UnexpectedString, "EthBlock::decode: withdrawal must be list");
            }

            bcos::bytes wdRlp;
            if (whHead.payloadLength < LENGTH_THRESHOLD)
            {
                wdRlp.push_back(static_cast<bcos::byte>(LIST_HEAD_BASE + whHead.payloadLength));
            }
            else
            {
                auto lenBytes = bcos::toCompactBigEndian(whHead.payloadLength);
                wdRlp.push_back(static_cast<bcos::byte>(LONG_LIST_HEAD_BASE + lenBytes.size()));
                wdRlp.insert(wdRlp.end(), lenBytes.begin(), lenBytes.end());
            }
            wdRlp.insert(wdRlp.end(), ref.data(), ref.data() + whHead.payloadLength);

            EthWithdrawal wd;
            wd.decode(bcos::ref(wdRlp));
            m_withdrawals.push_back(std::move(wd));
            ref = ref.getCroppedData(whHead.payloadLength);
        }
    }

    return nullptr;
}

size_t EthBlockImpl::size() const
{
    size_t s = m_header.size();
    for (auto const& tx : m_rlpTxs)
    {
        s += tx.size();
    }
    for (auto const& wd : m_withdrawals)
    {
        s += sizeof(wd.index) + sizeof(wd.validatorIndex) + bcos::Address::SIZE + sizeof(wd.amount);
    }
    return s;
}

void EthBlockImpl::appendTransactionRlp(const bcos::bytes& _rlp)
{
    m_txHashes.push_back(bcos::crypto::keccak256Hash(bcos::ref(_rlp)));
    m_rlpTxs.push_back(_rlp);
}

void EthBlockImpl::appendTransactionRlp(bcos::bytes&& _rlp)
{
    m_txHashes.push_back(bcos::crypto::keccak256Hash(bcos::ref(_rlp)));
    m_rlpTxs.push_back(std::move(_rlp));
}

void EthBlockImpl::setTransactionRlp(size_t _index, const bcos::bytes& _rlp)
{
    if (_index >= m_rlpTxs.size())
    {
        m_rlpTxs.resize(_index + 1);
        m_txHashes.resize(_index + 1);
    }
    m_rlpTxs[_index] = _rlp;
    m_txHashes[_index] = bcos::crypto::keccak256Hash(bcos::ref(_rlp));
}

void EthBlockImpl::setTransactionHash(size_t _index, const bcos::crypto::HashType& _hash)
{
    if (_index >= m_txHashes.size())
    {
        m_txHashes.resize(_index + 1);
    }
    m_txHashes[_index] = _hash;
}

void EthBlockImpl::setWithdrawal(size_t _index, const EthWithdrawal& _wd)
{
    if (_index >= m_withdrawals.size())
    {
        m_withdrawals.resize(_index + 1);
    }
    m_withdrawals[_index] = _wd;
}

void EthBlockImpl::clear()
{
    m_header = EthBlockHeader{};
    m_txHashes.clear();
    m_rlpTxs.clear();
    m_withdrawals.clear();
}

}  // namespace bcos::protocol
